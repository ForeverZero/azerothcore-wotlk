/*
 * This file is part of the AzerothCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "CharacterCache.h"
#include "Chat.h"
#include "CombatManager.h"
#include "CommandScript.h"
#include "Common.h"
#include "Creature.h"
#include "DBCStores.h"
#include "GameTime.h"
#include "Group.h"
#include "GroupMgr.h"
#include "GroupReference.h"
#include "GroupScript.h"
#include "Item.h"
#include "LFGMgr.h"
#include "Map.h"
#include "MotionMaster.h"
#include "MoveSpline.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "PlayerBotTalents.h"
#include "PlayerScript.h"
#include "QueryPackets.h"
#include "QuestDef.h"
#include "Random.h"
#include "SharedDefines.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "TC9Sidecar.h"
#include "Trainer.h"
#include "WorldScript.h"
#include "WorldSession.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{
using namespace Acore::ChatCommands;
using PlayerBots::BotRole;
using PlayerBots::BotTalentBuild;

struct ClassName
{
    Classes Id;
    std::string_view English;
    std::string_view SimplifiedChinese;
    std::string_view TraditionalChinese;
};

constexpr ClassName ClassNames[] =
{
    { CLASS_WARRIOR, "Warrior", "战士", "戰士" },
    { CLASS_PALADIN, "Paladin", "圣骑士", "聖騎士" },
    { CLASS_HUNTER, "Hunter", "猎人", "獵人" },
    { CLASS_ROGUE, "Rogue", "潜行者", "盜賊" },
    { CLASS_PRIEST, "Priest", "牧师", "牧師" },
    { CLASS_DEATH_KNIGHT, "Death Knight", "死亡骑士", "死亡騎士" },
    { CLASS_SHAMAN, "Shaman", "萨满祭司", "薩滿" },
    { CLASS_MAGE, "Mage", "法师", "法師" },
    { CLASS_WARLOCK, "Warlock", "术士", "術士" },
    { CLASS_DRUID, "Druid", "德鲁伊", "德魯伊" }
};

enum class BotState
{
    Follow,
    Combat
};

constexpr float MinFollowTriggerDistance = 8.0f;
constexpr float MaxFollowTriggerDistance = 12.0f;
constexpr float FollowArrivalDistance = 5.0f;
constexpr float MinBotOffsetDistance = 1.5f;

struct PlayerBot
{
    PlayerBot(std::unique_ptr<WorldSession> session, BotTalentBuild const& build)
        : Session(std::move(session)), Build(&build) { }

    std::unique_ptr<WorldSession> Session;
    BotTalentBuild const* Build;
    BotState State = BotState::Follow;
    float FollowTriggerDistance = frand(MinFollowTriggerDistance, MaxFollowTriggerDistance);
    std::optional<TimePoint> OutOfRangeSince;
};

struct PartyBots
{
    ObjectGuid GroupGuid;
    uint8 OwnerLevel = 0;
    std::vector<PlayerBot> Members;
};

std::unordered_map<ObjectGuid, PartyBots> BotsByOwner;
std::vector<std::unique_ptr<WorldSession>> PendingLogout;

PlayerBot* FindOwnedBot(ObjectGuid ownerGuid, ObjectGuid botGuid)
{
    auto party = BotsByOwner.find(ownerGuid);
    if (party == BotsByOwner.end())
        return nullptr;

    auto& members = party->second.Members;
    auto member = std::ranges::find_if(members, [botGuid](PlayerBot const& bot)
    {
        Player const* player = bot.Session->GetPlayer();
        return player && player->GetGUID() == botGuid;
    });
    return member == members.end() ? nullptr : &*member;
}

bool CanAddBot(ChatHandler& handler, Player const& owner, Group const* group)
{
    if (!group || !group->IsFull())
        return true;

    if (group->isRaidGroup())
        handler.SendErrorMessage("PlayerBots: your raid is full.");
    else if (!group->IsLeader(owner.GetGUID()))
        handler.SendErrorMessage("PlayerBots: only the party leader can convert a full party to a raid.");
    else
        return true;

    return false;
}

uint8 FindRace(Player const& owner, Classes playerClass)
{
    uint8 const ownerRace = owner.getRace(true);
    if (sObjectMgr->GetPlayerInfo(ownerRace, playerClass))
        return ownerRace;

    constexpr uint8 races[] =
    {
        RACE_HUMAN, RACE_ORC, RACE_DWARF, RACE_NIGHTELF, RACE_UNDEAD_PLAYER,
        RACE_TAUREN, RACE_GNOME, RACE_TROLL, RACE_BLOODELF, RACE_DRAENEI
    };

    for (uint8 race : races)
        if (Player::TeamIdForRace(race) == owner.GetTeamId(true) && sObjectMgr->GetPlayerInfo(race, playerClass))
            return race;

    return 0;
}

BotTalentBuild const* SelectRandomBuild(Classes playerClass, BotRole role)
{
    std::vector<BotTalentBuild const*> choices;
    for (BotTalentBuild const& build : PlayerBots::TalentBuilds)
        if (build.Class == playerClass && build.Role == role)
            choices.push_back(&build);

    if (choices.empty())
        return nullptr;

    return choices[urand(0, static_cast<uint32>(choices.size() - 1))];
}

std::string_view GetRoleAbbreviation(BotRole role)
{
    switch (role)
    {
        case BotRole::Dps:
            return "Dps";
        case BotRole::Healer:
            return "Heal";
        case BotRole::Tank:
            return "Tank";
    }

    return {};
}

std::string GenerateBotName(BotTalentBuild const& build)
{
    std::string prefix(GetRoleAbbreviation(build.Role));
    prefix += build.Abbreviation;
    for (uint8 attempt = 0; attempt < 32; ++attempt)
    {
        std::string name = prefix;
        while (name.size() < MAX_PLAYER_NAME)
            name += static_cast<char>('a' + urand(0, 25));

        if (!sCharacterCache->GetCharacterGuidByName(name) && !ObjectAccessor::FindPlayerByName(name, false) &&
            !sObjectMgr->IsReservedName(name))
            return name;
    }

    return {};
}

std::optional<Position> FindNearbyBotPosition(Player const& owner, float radius)
{
    constexpr uint8 maxAttempts = 12;
    Position const center = owner.GetPosition();
    Map const* map = owner.GetMap();
    for (uint8 attempt = 0; attempt < maxAttempts; ++attempt)
    {
        float const angle = frand(0.0f, 2.0f * float(M_PI));
        float const distance = frand(MinBotOffsetDistance, radius);
        float x = center.GetPositionX() + distance * std::cos(angle);
        float y = center.GetPositionY() + distance * std::sin(angle);
        float z = center.GetPositionZ();
        Acore::NormalizeMapCoord(x);
        Acore::NormalizeMapCoord(y);
        if (!map->CheckCollisionAndGetValidCoords(&owner, center.GetPositionX(), center.GetPositionY(),
                                                  center.GetPositionZ(), x, y, z, false))
            continue;

        Position candidate(x, y, z, center.GetOrientation());
        if (center.GetExactDist(candidate) <= radius && center.GetExactDist2d(candidate) >= MinBotOffsetDistance)
            return candidate;
    }

    return std::nullopt;
}

bool InitBot(Player& bot, uint8 level, BotTalentBuild const& build);

bool AddBot(ChatHandler& handler, ClassName const& className)
{
    Player* owner = handler.GetPlayer();
    if (!owner)
        return false;

    ObjectGuid const ownerGuid = owner->GetGUID();
    Map* map = owner->FindMap();
    Group* group = owner->GetGroup();
    if (sToCloud9Sidecar->ClusterModeEnabled() || !map || !owner->IsInWorld() || map->IsBattlegroundOrArena() ||
        owner->GetTransport() || owner->GetVehicle() || owner->IsInFlight() || owner->IsFlying())
    {
        handler.SendErrorMessage("PlayerBots: cannot create a bot at your current location.");
        return false;
    }

    if (!CanAddBot(handler, *owner, group))
        return false;

    if ((group && (group->isBGGroup() || group->isBFGroup() || group->isLFGGroup() ||
                   sLFGMgr->GetState(group->GetGUID()) != lfg::LFG_STATE_NONE)) ||
        sLFGMgr->GetState(ownerGuid) != lfg::LFG_STATE_NONE)
    {
        handler.SendErrorMessage("PlayerBots: your group is unavailable for temporary players.");
        return false;
    }

    uint8 const race = FindRace(*owner, className.Id);
    BotTalentBuild const* build = SelectRandomBuild(className.Id, BotRole::Dps);
    std::string const name = build ? GenerateBotName(*build) : std::string{};
    if (!race || name.empty())
    {
        handler.SendErrorMessage("PlayerBots: could not initialize the requested character.");
        return false;
    }

    auto session = std::make_unique<WorldSession>(0, std::string{}, 0, nullptr, SEC_PLAYER,
        EXPANSION_WRATH_OF_THE_LICH_KING, 0, owner->GetSession()->GetSessionDbcLocale(), 0, false, false, 0);
    auto bot = std::make_unique<Player>(session.get());
    CharacterCreateInfo createInfo(name, race, className.Id, urand(0, 1) ? GENDER_MALE : GENDER_FEMALE);
    ObjectGuid::LowType const lowGuid = sObjectMgr->GetGenerator<HighGuid::Player>().Generate();
    Position position = FindNearbyBotPosition(*owner, 10.0f).value_or(owner->GetPosition());
    position.SetOrientation(frand(0.0f, 2.0f * float(M_PI)));
    // The character is linked into the map's player list by Create, as in normal login.
    if (!bot->Create(lowGuid, &createInfo, map, &position) || map->CannotEnter(bot.get(), true) != Map::CAN_ENTER)
    {
        handler.SendErrorMessage("PlayerBots: could not create the character on this map.");
        return false;
    }

    bot->SetPhaseMask(owner->GetPhaseMask(), false);
    bot->GetMotionMaster()->Initialize();
    session->SetPlayer(bot.get());
    ObjectAccessor::AddObject(bot.get());
    if (!map->AddPlayerToMap(bot.get()))
    {
        ObjectAccessor::RemoveObject(bot.get());
        session->SetPlayer(nullptr);
        handler.SendErrorMessage("PlayerBots: could not enter the map.");
        return false;
    }

    Player* botPlayer = bot.release();
    ObjectGuid const botGuid = botPlayer->GetGUID();
    owner = ObjectAccessor::FindPlayer(ownerGuid);
    if (!owner)
    {
        session->LogoutPlayer(false);
        return false;
    }

    uint8 const ownerLevel = owner->GetLevel();
    if (!InitBot(*botPlayer, ownerLevel, *build))
    {
        session->LogoutPlayer(false);
        handler.SendErrorMessage("PlayerBots: could not initialize the summoned bot.");
        return false;
    }

    owner = ObjectAccessor::FindPlayer(ownerGuid);
    botPlayer = ObjectAccessor::FindPlayer(botGuid);
    if (!owner || !botPlayer)
    {
        session->LogoutPlayer(false);
        return false;
    }

    group = owner->GetGroup();
    if (!CanAddBot(handler, *owner, group))
    {
        session->LogoutPlayer(false);
        return false;
    }

    bool const createGroup = !group;
    if (createGroup)
    {
        group = owner->GetGroupInvite();
        if (group && !group->IsLeader(ownerGuid))
        {
            owner->UninviteFromGroup();
            group = nullptr;
        }

        if (!group)
            group = new Group();

        group->Create(owner);
        sGroupMgr->AddGroup(group);
    }

    if (group->IsFull())
        group->ConvertToRaid();

    if (group->IsFull() || !group->AddMember(botPlayer))
    {
        if (createGroup)
            group->Disband();

        session->LogoutPlayer(false);
        handler.SendErrorMessage("PlayerBots: could not join your group.");
        return false;
    }

    PartyBots& party = BotsByOwner[ownerGuid];
    party.GroupGuid = group->GetGUID();
    if (party.Members.empty())
        party.OwnerLevel = ownerLevel;

    party.Members.emplace_back(std::move(session), *build);
    group->BroadcastGroupUpdate();
    handler.PSendSysMessage("PlayerBots: {} ({} {}) joined your group.", name, build->Name, className.English);
    return true;
}

void ResetBotTalents(Player& bot)
{
    uint8 const activeSpec = bot.GetActiveSpec();
    for (uint8 spec = 0; spec < bot.GetSpecsCount(); ++spec)
    {
        if (spec == activeSpec)
            continue;

        bot.ActivateSpec(spec);
        bot.resetTalents(true);
    }

    bot.ActivateSpec(activeSpec);
    bot.resetTalents(true);
    bot.InitTalentForLevel();
}

void SpendBotTalentPoints(Player& bot, BotTalentBuild const& build)
{
    if (!bot.GetFreeTalentPoints())
        return;

    uint32 const* tabs = GetTalentTabPages(bot.getClass());
    std::array<uint8, MAX_TALENT_TABS> order{ build.PrimaryTree };
    std::size_t next = 1;
    for (uint8 page = 0; page < MAX_TALENT_TABS; ++page)
        if (page != build.PrimaryTree)
            order[next++] = page;

    for (uint8 page : order)
    {
        std::string_view const ranks = build.TreeRanks[page];
        if (ranks.empty())
            continue;

        std::vector<TalentEntry const*> talents;
        for (TalentEntry const* talent : sTalentStore)
            if (talent->TalentTab == tabs[page])
                talents.push_back(talent);

        std::ranges::sort(talents, [](TalentEntry const* left, TalentEntry const* right)
        {
            return std::tie(left->Row, left->Col, left->TalentID) < std::tie(right->Row, right->Col, right->TalentID);
        });

        std::vector<uint8> learned(std::min(ranks.size(), talents.size()), 0);
        uint32 before;
        // A prerequisite can appear after its dependent talent in the same row.
        do
        {
            before = bot.GetFreeTalentPoints();
            for (std::size_t index = 0; index < learned.size(); ++index)
            {
                TalentEntry const& talent = *talents[index];
                uint8 const targetRanks = ranks[index] - '0';
                for (uint8& rank = learned[index]; rank < targetRanks; ++rank)
                {
                    if (!bot.GetFreeTalentPoints())
                        return;

                    bot.LearnTalent(talent.TalentID, rank);
                    if (!bot.HasTalent(talent.RankID[rank], bot.GetActiveSpec()))
                        break;
                }
            }
        } while (bot.GetFreeTalentPoints() != before);
    }
}

using BotSpellLevels = std::unordered_map<uint32, uint32>;

void RecordTaughtSpellLevels(BotSpellLevels& levels, uint32 spellId, uint32 level)
{
    auto record = [&levels, level](uint32 taughtSpell)
    {
        auto [entry, inserted] = levels.try_emplace(taughtSpell, level);
        if (!inserted)
            entry->second = std::min(entry->second, level);
    };

    record(spellId);
    for (SpellEffectInfo const& effect : sSpellMgr->AssertSpellInfo(spellId)->GetEffects())
        if (effect.IsEffect(SPELL_EFFECT_LEARN_SPELL) && effect.TriggerSpell)
            record(effect.TriggerSpell);
}

void ForgetOverlevelSpells(Player& bot, BotSpellLevels const& levels)
{
    auto fitsLevel = [&bot, &levels](uint32 spellId)
    {
        auto const entry = levels.find(spellId);
        if (entry != levels.end())
            return entry->second <= bot.GetLevel();

        return sSpellMgr->AssertSpellInfo(spellId)->SpellLevel <= bot.GetLevel();
    };

    std::vector<uint32> spellsToForget;
    std::set<uint32> ranksToRestore;
    for (auto const& [spellId, spell] : bot.GetSpellMap())
    {
        if (spell->State == PLAYERSPELL_REMOVED || fitsLevel(spellId))
            continue;

        spellsToForget.push_back(spellId);
        for (uint32 rank = sSpellMgr->GetPrevSpellInChain(spellId); rank; rank = sSpellMgr->GetPrevSpellInChain(rank))
        {
            if (bot.HasSpell(rank) && fitsLevel(rank))
            {
                ranksToRestore.insert(rank);
                break;
            }
        }
    }

    for (uint32 spellId : spellsToForget)
    {
        if (bot.HasSpell(spellId) && sSpellMgr->AssertSpellInfo(spellId)->HasEffect(SPELL_EFFECT_DUAL_WIELD))
            bot.SetCanDualWield(false);

        bot.removeSpell(spellId, SPEC_MASK_ALL, false, true, true);
    }

    // Lower ranks stay in the spell map when superseded, but must be reactivated after losing the higher rank.
    for (uint32 spellId : ranksToRestore)
    {
        if (bot.HasActiveSpell(spellId))
            continue;

        bot.removeSpell(spellId, SPEC_MASK_ALL, false);
        bot.learnSpell(spellId);
    }
}

void TrainBotSpells(Player& bot, std::vector<Trainer::Trainer const*> const& trainers)
{
    // A spell from one trainer may unlock another trainer's spell or the next rank.
    bool learned;
    do
    {
        learned = false;
        for (Trainer::Trainer const* trainer : trainers)
        {
            for (Trainer::Spell const& spell : trainer->GetSpells())
            {
                if (spell.ReqLevel > bot.GetLevel() || !trainer->CanTeachSpell(&bot, &spell))
                    continue;

                if (spell.IsCastable())
                    bot.CastSpell(&bot, spell.SpellId, true);
                else
                    bot.learnSpell(spell.SpellId);

                learned |= !trainer->CanTeachSpell(&bot, &spell);
            }
        }
    } while (learned);
}

void RestoreBotSpells(Player& bot, BotTalentBuild const& build)
{
    BotSpellLevels levels;
    std::vector<Trainer::Trainer const*> trainers;
    for (Trainer::Trainer const* trainer : sObjectMgr->GetClassTrainers(bot.getClass()))
    {
        if (!trainer->IsTrainerValidForPlayer(&bot))
            continue;

        trainers.push_back(trainer);
        for (Trainer::Spell const& spell : trainer->GetSpells())
            if (bot.IsSpellFitByClassAndRace(spell.SpellId))
                RecordTaughtSpellLevels(levels, spell.SpellId, spell.ReqLevel);
    }

    std::vector<Quest const*> classQuests;
    for (auto const& [questId, quest] : sObjectMgr->GetQuestTemplates())
    {
        if (!quest->GetRequiredClasses() || !bot.SatisfyQuestClass(quest, false) || !bot.SatisfyQuestRace(quest, false))
            continue;

        SpellInfo const* reward = sSpellMgr->GetSpellInfo(quest->GetRewSpellCast());
        if (!reward || !reward->HasEffect(SPELL_EFFECT_LEARN_SPELL))
            continue;

        RecordTaughtSpellLevels(levels, reward->Id, quest->GetMinLevel());
        if (quest->GetMinLevel() <= bot.GetLevel())
            classQuests.push_back(quest);
    }

    bot.LearnDefaultSkills();
    bot.LearnCustomSpells();
    ForgetOverlevelSpells(bot, levels);
    for (Quest const* quest : classQuests)
        bot.learnQuestRewardedSpells(quest);

    TrainBotSpells(bot, trainers);
    SpendBotTalentPoints(bot, build);
    // The talents can unlock spells and further ranks that were unavailable on the first trainer pass.
    TrainBotSpells(bot, trainers);
}

bool InitBot(Player& bot, uint8 level, BotTalentBuild const& build)
{
    // Resetting talents can move an offhand item from its equipped slot into inventory.
    std::array<ObjectGuid, EQUIPMENT_SLOT_END> equipment{};
    for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
        if (Item const* item = bot.GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
            equipment[slot] = item->GetGUID();

    if (bot.GetLevel() != level)
    {
        bot.GiveLevel(level);
        if (bot.GetLevel() != level)
            return false;

        bot.SetUInt32Value(PLAYER_XP, 0);
    }

    ResetBotTalents(bot);
    RestoreBotSpells(bot, build);
    for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
    {
        if (!equipment[slot])
            continue;

        Item* item = bot.GetItemByGuid(equipment[slot]);
        if (!item)
            continue;

        InventoryResult const useResult = bot.CanUseItem(item, false);
        if (item->GetTemplate()->RequiredLevel > level ||
            useResult == EQUIP_ERR_NO_REQUIRED_PROFICIENCY || useResult == EQUIP_ERR_CANT_EQUIP_SKILL)
        {
            bot.DestroyItem(item->GetBagSlot(), item->GetSlot(), true);
            continue;
        }

        // Losing Dual Wield or Titan's Grip can make FindEquipSlot reject the offhand slot entirely.
        uint32 const inventoryType = item->GetTemplate()->InventoryType;
        if (slot == EQUIPMENT_SLOT_OFFHAND &&
            ((!bot.CanDualWield() && (inventoryType == INVTYPE_WEAPON || inventoryType == INVTYPE_WEAPONOFFHAND)) ||
             (!bot.CanTitanGrip() && inventoryType == INVTYPE_2HWEAPON)))
        {
            bot.DestroyItem(item->GetBagSlot(), item->GetSlot(), true);
            continue;
        }

        uint16 dest = 0;
        if (!bot.GetItemByPos(INVENTORY_SLOT_BAG_0, slot) &&
            bot.CanEquipItem(slot, dest, item, false, false) == EQUIP_ERR_OK)
        {
            bot.RemoveItem(item->GetBagSlot(), item->GetSlot(), true);
            bot.EquipItem(dest, item, true);
        }
    }

    bot.AutoUnequipOffhandIfNeed();
    return true;
}

void RenameBot(Player& bot, std::string const& name)
{
    std::string const oldName = bot.GetName();
    bot.SetName(name);
    ObjectAccessor::UpdatePlayerNameMapReference(oldName, &bot);

    WorldPackets::Query::NameQueryResponse response;
    response.Guid = bot.GetGUID().WriteAsPacked();
    response.Name = bot.GetName();
    response.Race = bot.getRace();
    response.Sex = bot.getGender();
    response.Class = bot.getClass();
    bot.SendMessageToSet(response.Write(), false);

    if (Group* group = bot.GetGroup())
        group->SetTransientMemberName(bot.GetGUID(), name);
}

void StopBotMovement(Player& player)
{
    MotionMaster* motion = player.GetMotionMaster();
    MovementGeneratorType const movement = motion->GetMotionSlotType(MOTION_SLOT_ACTIVE);
    if (movement == FOLLOW_MOTION_TYPE || movement == CHASE_MOTION_TYPE)
        motion->MovementExpiredOnSlot(MOTION_SLOT_ACTIVE);

    player.StopMoving();
}

void UpdateFollow(PlayerBot& bot, Player& player, Player& owner, TimePoint now)
{
    constexpr float teleportDistance = 200.0f;
    constexpr Seconds teleportDelay{30};

    Map& ownerMap = *owner.GetMap();
    if (player.GetPhaseMask() != owner.GetPhaseMask())
        player.SetPhaseMask(owner.GetPhaseMask(), true);

    if (player.GetMap() != &ownerMap)
    {
        bot.OutOfRangeSince.reset();
        StopBotMovement(player);
        Position const position = FindNearbyBotPosition(owner, FollowArrivalDistance).value_or(owner.GetPosition());
        player.TeleportTo(ownerMap, position);
        return;
    }

    float const distance = player.GetExactDist(owner.GetPosition());
    if (distance <= teleportDistance)
        bot.OutOfRangeSince.reset();
    else if (!bot.OutOfRangeSince)
        bot.OutOfRangeSince = now;
    else if (now - *bot.OutOfRangeSince >= teleportDelay)
    {
        bot.OutOfRangeSince.reset();
        Position const position = FindNearbyBotPosition(owner, FollowArrivalDistance).value_or(owner.GetPosition());
        player.TeleportTo(ownerMap, position);
        return;
    }

    MotionMaster* motion = player.GetMotionMaster();
    bool const following = motion->GetMotionSlotType(MOTION_SLOT_ACTIVE) == FOLLOW_MOTION_TYPE;
    if (following)
    {
        if (!player.movespline->Finalized() || player.HasUnitState(UNIT_STATE_NOT_MOVE))
            return;

        if (distance <= FollowArrivalDistance)
        {
            bot.FollowTriggerDistance = frand(MinFollowTriggerDistance, MaxFollowTriggerDistance);
            StopBotMovement(player);
            return;
        }
    }
    else if (distance <= bot.FollowTriggerDistance)
        return;

    std::optional<Position> const destination = FindNearbyBotPosition(owner, FollowArrivalDistance);
    float const radius = destination ? owner.GetExactDist2d(*destination)
                                     : frand(MinBotOffsetDistance, FollowArrivalDistance);
    float const angle = destination ? owner.GetRelativeAngle(&*destination) : frand(0.0f, 2.0f * float(M_PI));
    // The follow generator adds the follower's combat reach to this offset.
    motion->MoveFollow(&owner, radius - player.GetCombatReach(), angle, MOTION_SLOT_ACTIVE, false);
}

Unit* FindCombatOpponent(Player const& bot, Player const& fighter)
{
    if (!fighter.IsAlive() || !fighter.IsInCombat())
        return nullptr;

    // Keep a valid attack going until it ends so several enemies do not cause repeated target changes.
    Unit const* current = bot.GetVictim();
    Unit* nearest = nullptr;
    float nearestDistance = 0.0f;
    auto consider = [&](CombatReference const* reference)
    {
        Unit* target = reference->GetOther(&fighter);
        if (reference->IsSuppressedFor(&fighter) || !CombatManager::CanBeginCombat(&bot, target) ||
            !bot.IsValidAttackTarget(target))
            return;

        if (Creature const* creature = target->ToCreature(); creature && creature->IsEvadingAttacks())
            return;

        float const distance = bot.GetExactDist(target->GetPosition());
        if (target == current || (!nearest || (nearest != current && distance < nearestDistance)))
        {
            nearest = target;
            nearestDistance = distance;
        }
    };

    CombatManager const& combat = fighter.GetCombatManager();
    for (auto const& [guid, reference] : combat.GetPvECombatRefs())
        consider(reference);

    for (auto const& [guid, reference] : combat.GetPvPCombatRefs())
        consider(reference);

    return nearest;
}

Unit* SelectBotCombatTarget(Player const& bot, Player const& owner)
{
    constexpr float assistRange = 100.0f;
    if (bot.IsInCombat())
        return FindCombatOpponent(bot, bot);

    if (bot.IsWithinDistInMap(&owner, assistRange))
        if (Unit* target = FindCombatOpponent(bot, owner))
            return target;

    if (Group const* group = bot.GetGroup())
    {
        for (GroupReference const* member = group->GetFirstMember(); member; member = member->next())
        {
            Player const* teammate = member->GetSource();
            if (!teammate || teammate == &bot || teammate == &owner || !bot.IsWithinDistInMap(teammate, assistRange))
                continue;

            if (Unit* target = FindCombatOpponent(bot, *teammate))
                return target;
        }
    }

    return nullptr;
}

void LeaveBotCombat(PlayerBot& bot, Player& player)
{
    bot.State = BotState::Follow;
    bot.OutOfRangeSince.reset();
    StopBotMovement(player);
    player.AttackStop();
}

void UpdateBotState(PlayerBot& bot, Player& player, Player* owner, TimePoint now)
{
    if (!owner || !player.IsAlive())
    {
        LeaveBotCombat(bot, player);
        return;
    }

    Unit* target = nullptr;
    if (player.IsInMap(owner) && player.InSamePhase(owner) && !player.IsMounted() && !player.GetVehicle())
        target = SelectBotCombatTarget(player, *owner);

    if (!target)
    {
        if (bot.State == BotState::Combat)
            LeaveBotCombat(bot, player);
        else
            UpdateFollow(bot, player, *owner, now);

        return;
    }

    bot.State = BotState::Combat;
    bot.OutOfRangeSince.reset();
    if (player.GetVictim() == target && player.HasUnitState(UNIT_STATE_MELEE_ATTACKING) &&
        player.GetMotionMaster()->GetMotionSlotType(MOTION_SLOT_ACTIVE) == CHASE_MOTION_TYPE)
        return;

    ObjectGuid const botGuid = player.GetGUID();
    ObjectGuid const targetGuid = target->GetGUID();
    if (player.GetVictim() != target || !player.HasUnitState(UNIT_STATE_MELEE_ATTACKING))
        if (!player.Attack(target, true))
            return;

    if (Player* attacker = ObjectAccessor::FindPlayer(botGuid))
        if (Unit* victim = attacker->GetVictim(); victim && victim->GetGUID() == targetGuid)
            attacker->GetMotionMaster()->MoveChase(victim);
}

void QueueOwnerLogout(ObjectGuid owner)
{
    auto party = BotsByOwner.extract(owner);
    if (party.empty())
        return;

    for (PlayerBot& bot : party.mapped().Members)
        PendingLogout.push_back(std::move(bot.Session));
}

void LogOutPendingBots()
{
    while (!PendingLogout.empty())
    {
        auto session = std::move(PendingLogout.back());
        PendingLogout.pop_back();
        session->LogoutPlayer(false);
    }
}

class PlayerBotsOwnerEvents final : public PlayerScript
{
public:
    PlayerBotsOwnerEvents() : PlayerScript("PlayerBotsOwnerEvents",
        { PLAYERHOOK_ON_BEFORE_LOGOUT, PLAYERHOOK_CAN_JOIN_LFG }) { }

    void OnPlayerBeforeLogout(Player* player) override
    {
        QueueOwnerLogout(player->GetGUID());
    }

    bool OnPlayerCanJoinLfg(Player* player, uint8, std::set<uint32>&, std::string const&) override
    {
        Group* group = player->GetGroup();
        if (!group || !std::ranges::any_of(group->GetMemberSlots(), &Group::MemberSlot::Transient))
            return true;

        ChatHandler(player->GetSession()).SendSysMessage("PlayerBots: remove temporary players before using Dungeon Finder.");
        return false;
    }
};

class PlayerBotsGroupEvents final : public GroupScript
{
public:
    PlayerBotsGroupEvents() : GroupScript("PlayerBotsGroupEvents",
        { GROUPHOOK_ON_REMOVE_MEMBER, GROUPHOOK_ON_DISBAND }) { }

    void OnRemoveMember(Group* group, ObjectGuid guid, RemoveMethod, ObjectGuid, char const*) override
    {
        for (auto party = BotsByOwner.begin(); party != BotsByOwner.end(); ++party)
        {
            if (party->second.GroupGuid != group->GetGUID())
                continue;

            if (party->first == guid)
            {
                QueueOwnerLogout(guid);
                return;
            }

            auto& members = party->second.Members;
            auto removed = std::ranges::find_if(members, [guid](PlayerBot const& bot)
            {
                Player const* player = bot.Session->GetPlayer();
                return player && player->GetGUID() == guid;
            });
            if (removed == members.end())
                continue;

            PendingLogout.push_back(std::move(removed->Session));
            members.erase(removed);
            if (members.empty())
                BotsByOwner.erase(party);

            return;
        }
    }

    void OnDisband(Group* group) override
    {
        for (auto party = BotsByOwner.begin(); party != BotsByOwner.end();)
        {
            auto current = party++;
            if (current->second.GroupGuid == group->GetGUID())
                QueueOwnerLogout(current->first);
        }
    }
};

class PlayerBotsWorldEvents final : public WorldScript
{
public:
    PlayerBotsWorldEvents() : WorldScript("PlayerBotsWorldEvents", { WORLDHOOK_ON_UPDATE, WORLDHOOK_ON_SHUTDOWN }) { }

    // Group hooks can run while the group is unlinking members or being deleted.
    void OnUpdate(uint32) override
    {
        LogOutPendingBots();
        TimePoint const now = GameTime::Now();
        if (BotsByOwner.empty() || now < _nextBotUpdate)
            return;

        _nextBotUpdate = now + Milliseconds{250};
        // Level changes from map updates are applied here, where all of the owner's bots can be updated safely.
        // Keep GUIDs across actions because level or combat hooks can remove a bot from the group.
        struct BotUpdate
        {
            ObjectGuid OwnerGuid;
            ObjectGuid BotGuid;
            std::optional<uint8> InitLevel;
        };
        std::vector<BotUpdate> updates;
        for (auto& [ownerGuid, party] : BotsByOwner)
        {
            std::optional<uint8> initLevel;
            if (Player const* owner = ObjectAccessor::FindPlayer(ownerGuid))
            {
                uint8 const currentLevel = owner->GetLevel();
                if (std::exchange(party.OwnerLevel, currentLevel) != currentLevel)
                    initLevel = currentLevel;
            }

            for (PlayerBot const& bot : party.Members)
                if (Player const* player = bot.Session->GetPlayer(); player && player->IsInWorld())
                    updates.push_back({ ownerGuid, player->GetGUID(), initLevel });
        }

        for (BotUpdate const& update : updates)
        {
            PlayerBot* bot = FindOwnedBot(update.OwnerGuid, update.BotGuid);
            if (!bot)
                continue;

            if (update.InitLevel)
            {
                if (Player* player = ObjectAccessor::FindPlayer(update.BotGuid))
                    InitBot(*player, *update.InitLevel, *bot->Build);

                bot = FindOwnedBot(update.OwnerGuid, update.BotGuid);
                if (!bot)
                    continue;
            }

            if (Player* player = ObjectAccessor::FindPlayer(update.BotGuid))
                UpdateBotState(*bot, *player, ObjectAccessor::FindPlayer(update.OwnerGuid), now);
        }
    }

    void OnShutdown() override
    {
        while (!BotsByOwner.empty())
            QueueOwnerLogout(BotsByOwner.begin()->first);

        LogOutPendingBots();
    }

private:
    TimePoint _nextBotUpdate{};
};

class PlayerBotsCommands final : public CommandScript
{
public:
    PlayerBotsCommands() : CommandScript("PlayerBotsCommands") { }

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable const addCommands =
        {
            { "class", HandleAddClass, SEC_PLAYER, Console::No }
        };

        static ChatCommandTable const roleCommands =
        {
            { "set", HandleRoleSet, SEC_PLAYER, Console::No }
        };

        static ChatCommandTable const playerBotsCommands =
        {
            { "add", addCommands },
            { "init", HandleInit, SEC_PLAYER, Console::No },
            { "role", roleCommands }
        };

        return { { "pb", playerBotsCommands } };
    }

private:
    static std::string_view TrimArgument(std::string_view argument)
    {
        constexpr std::string_view whitespace = " \t\r\n";
        std::size_t const first = argument.find_first_not_of(whitespace);
        if (first == std::string_view::npos)
            return {};

        return argument.substr(first, argument.find_last_not_of(whitespace) - first + 1);
    }

    static bool HandleRoleSet(ChatHandler* handler, Tail args)
    {
        std::string_view const requestedRole = TrimArgument(args);
        BotRole role;
        if (requestedRole == "dps")
            role = BotRole::Dps;
        else if (requestedRole == "tank")
            role = BotRole::Tank;
        else if (requestedRole == "healer")
            role = BotRole::Healer;
        else
        {
            handler->SendErrorMessage("PlayerBots: usage: .pb role set <dps/tank/healer>");
            return false;
        }

        Player* owner = handler->GetPlayer();
        if (!owner)
            return false;

        ObjectGuid const ownerGuid = owner->GetGUID();
        ObjectGuid const botGuid = owner->GetTarget();
        PlayerBot* bot = FindOwnedBot(ownerGuid, botGuid);
        Player* player = bot ? ObjectAccessor::FindPlayer(botGuid) : nullptr;
        if (!player)
        {
            handler->SendErrorMessage("PlayerBots: invalid target. Select one of your bots.");
            return false;
        }

        BotTalentBuild const* build = SelectRandomBuild(bot->Build->Class, role);
        if (!build)
        {
            handler->SendErrorMessage("PlayerBots: {} cannot take the {} role.", player->GetName(), requestedRole);
            return false;
        }

        std::string const name = GenerateBotName(*build);
        if (name.empty() || !InitBot(*player, owner->GetLevel(), *build))
        {
            handler->SendErrorMessage("PlayerBots: could not initialize the bot for the requested role.");
            return false;
        }

        bot = FindOwnedBot(ownerGuid, botGuid);
        player = bot ? ObjectAccessor::FindPlayer(botGuid) : nullptr;
        if (!player)
        {
            handler->SendErrorMessage("PlayerBots: the bot is no longer available.");
            return false;
        }

        bot->Build = build;
        RenameBot(*player, name);
        handler->PSendSysMessage("PlayerBots: {} is now {} ({}).", name, requestedRole, build->Name);
        return true;
    }

    static bool HandleInit(ChatHandler* handler)
    {
        Player* owner = handler->GetPlayer();
        if (!owner)
            return false;

        ObjectGuid const selected = owner->GetTarget();
        ObjectGuid const ownerGuid = owner->GetGUID();
        auto party = BotsByOwner.find(ownerGuid);
        std::vector<ObjectGuid> targets;
        if (party != BotsByOwner.end())
        {
            for (PlayerBot const& bot : party->second.Members)
            {
                Player const* player = bot.Session->GetPlayer();
                if (player && (!selected || player->GetGUID() == selected))
                    targets.push_back(player->GetGUID());
            }
        }

        if (targets.empty())
        {
            handler->SendErrorMessage(selected ? "PlayerBots: invalid target. Select one of your bots."
                                               : "PlayerBots: you have no summoned bots.");
            return false;
        }

        uint8 const level = owner->GetLevel();
        std::size_t initialized = 0;
        for (ObjectGuid guid : targets)
        {
            PlayerBot const* bot = FindOwnedBot(ownerGuid, guid);
            Player* player = ObjectAccessor::FindPlayer(guid);
            if (bot && player && InitBot(*player, level, *bot->Build))
                ++initialized;
        }

        if (initialized != targets.size())
        {
            handler->SendErrorMessage("PlayerBots: initialized {} of {} bots at level {}.",
                initialized, targets.size(), uint32(level));
            return false;
        }

        handler->PSendSysMessage("PlayerBots: initialized {} bot(s) at level {}.", initialized, uint32(level));
        return true;
    }

    static bool HandleAddClass(ChatHandler* handler, Tail args)
    {
        std::string_view const requestedClass = TrimArgument(args);
        if (requestedClass.empty())
        {
            handler->SendErrorMessage("Usage: .pb add class <class>");
            return false;
        }

        for (ClassName const& className : ClassNames)
        {
            if (requestedClass == className.SimplifiedChinese || requestedClass == className.TraditionalChinese ||
                std::ranges::equal(requestedClass, className.English, [](unsigned char left, unsigned char right)
                {
                    return std::tolower(left) == std::tolower(right);
                }))
            {
                return AddBot(*handler, className);
            }
        }

        handler->SendErrorMessage("PlayerBots: unknown class. Usage: .pb add class <class>");
        return false;
    }
};
}

void Addmod_playerbotsScripts()
{
    new PlayerBotsCommands();
    new PlayerBotsOwnerEvents();
    new PlayerBotsGroupEvents();
    new PlayerBotsWorldEvents();
}
