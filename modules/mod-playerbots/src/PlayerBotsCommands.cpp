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
#include "CommandScript.h"
#include "Common.h"
#include "Group.h"
#include "GroupMgr.h"
#include "GroupScript.h"
#include "LFGMgr.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "PlayerScript.h"
#include "Random.h"
#include "SharedDefines.h"
#include "TC9Sidecar.h"
#include "WorldScript.h"
#include "WorldSession.h"
#include <algorithm>
#include <cctype>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace
{
using namespace Acore::ChatCommands;

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

struct PartyBots
{
    ObjectGuid GroupGuid;
    std::vector<std::unique_ptr<WorldSession>> Sessions;
};

std::unordered_map<ObjectGuid, PartyBots> BotsByOwner;
std::unordered_set<ObjectGuid> PendingLogout;

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

std::string GenerateBotName()
{
    for (uint8 attempt = 0; attempt < 32; ++attempt)
    {
        std::string name(12, 'a');
        for (char& letter : name)
            letter = static_cast<char>('a' + urand(0, 25));

        name.front() = static_cast<char>(std::toupper(static_cast<unsigned char>(name.front())));
        if (!sCharacterCache->GetCharacterGuidByName(name) && !ObjectAccessor::FindPlayerByName(name, false) &&
            !sObjectMgr->IsReservedName(name))
            return name;
    }

    return {};
}

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

    if (group && group->IsFull())
    {
        handler.SendErrorMessage("PlayerBots: your group is full.");
        return false;
    }

    if ((group && (group->isBGGroup() || group->isBFGroup() || group->isLFGGroup() ||
                   sLFGMgr->GetState(group->GetGUID()) != lfg::LFG_STATE_NONE)) ||
        sLFGMgr->GetState(ownerGuid) != lfg::LFG_STATE_NONE || PendingLogout.contains(ownerGuid))
    {
        handler.SendErrorMessage("PlayerBots: your group is unavailable for temporary players.");
        return false;
    }

    uint8 const race = FindRace(*owner, className.Id);
    std::string const name = GenerateBotName();
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
    Position const position = owner->GetPosition();
    // The character is linked into the map's player list by Create, as in normal login.
    if (!bot->Create(lowGuid, &createInfo, map, &position) || map->CannotEnter(bot.get(), true) != Map::CAN_ENTER)
    {
        handler.SendErrorMessage("PlayerBots: could not create the character on this map.");
        return false;
    }

    bot->SetPhaseMask(owner->GetPhaseMask(), false);
    bot->SetImmuneToAll(true);
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
    owner = ObjectAccessor::FindPlayer(ownerGuid);
    if (!owner)
    {
        session->LogoutPlayer(false);
        return false;
    }

    group = owner->GetGroup();
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
    party.Sessions.push_back(std::move(session));
    group->BroadcastGroupUpdate();
    handler.PSendSysMessage("PlayerBots: {} ({}) joined your group.", name, className.English);
    return true;
}

void LogOutPendingBots()
{
    while (!PendingLogout.empty())
    {
        ObjectGuid const owner = PendingLogout.extract(PendingLogout.begin()).value();
        auto bots = BotsByOwner.extract(owner);
        if (bots.empty())
            continue;

        for (auto const& session : bots.mapped().Sessions)
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
        if (BotsByOwner.contains(player->GetGUID()))
            PendingLogout.insert(player->GetGUID());
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
        for (auto const& [owner, party] : BotsByOwner)
        {
            if (party.GroupGuid != group->GetGUID())
                continue;

            if (owner == guid || std::ranges::any_of(party.Sessions, [guid](auto const& session)
                {
                    Player const* bot = session->GetPlayer();
                    return bot && bot->GetGUID() == guid;
                }))
                PendingLogout.insert(owner);
        }
    }

    void OnDisband(Group* group) override
    {
        for (auto const& [owner, party] : BotsByOwner)
            if (party.GroupGuid == group->GetGUID())
                PendingLogout.insert(owner);
    }
};

class PlayerBotsWorldEvents final : public WorldScript
{
public:
    PlayerBotsWorldEvents() : WorldScript("PlayerBotsWorldEvents", { WORLDHOOK_ON_UPDATE, WORLDHOOK_ON_SHUTDOWN }) { }

    // Group hooks can run while the group is unlinking members or being deleted.
    void OnUpdate(uint32) override { LogOutPendingBots(); }

    void OnShutdown() override
    {
        for (auto const& [owner, party] : BotsByOwner)
            PendingLogout.insert(owner);

        LogOutPendingBots();
    }
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

        static ChatCommandTable const playerBotsCommands =
        {
            { "add", addCommands }
        };

        return { { "pb", playerBotsCommands } };
    }

private:
    static bool HandleAddClass(ChatHandler* handler, Tail args)
    {
        constexpr std::string_view whitespace = " \t\r\n";
        std::size_t const first = args.find_first_not_of(whitespace);
        if (first == std::string_view::npos)
        {
            handler->SendErrorMessage("Usage: .pb add class <class>");
            return false;
        }

        std::string_view const requestedClass = args.substr(first, args.find_last_not_of(whitespace) - first + 1);
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
