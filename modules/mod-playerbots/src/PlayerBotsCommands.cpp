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

#include "Chat.h"
#include "Player.h"
#include "PlayerScript.h"
#include "SharedDefines.h"
#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>
#include <string_view>

namespace
{
constexpr std::string_view AddonPrefix = "ACPlayerBots\t";

struct ClassName
{
    std::string_view English;
    std::string_view SimplifiedChinese;
    std::string_view TraditionalChinese;
};

constexpr ClassName ClassNames[] =
{
    { "Warrior", "战士", "戰士" },
    { "Paladin", "圣骑士", "聖騎士" },
    { "Hunter", "猎人", "獵人" },
    { "Rogue", "潜行者", "盜賊" },
    { "Priest", "牧师", "牧師" },
    { "Death Knight", "死亡骑士", "死亡騎士" },
    { "Shaman", "萨满祭司", "薩滿" },
    { "Mage", "法师", "法師" },
    { "Warlock", "术士", "術士" },
    { "Druid", "德鲁伊", "德魯伊" }
};

void HandlePlayerBotsCommand(ChatHandler& handler, std::string_view command)
{
    std::istringstream input{ std::string(command) };
    std::string action;
    std::string argument;
    std::string requestedClass;
    if (!(input >> action >> argument) || action != "add" || argument != "class" ||
        !std::getline(input >> std::ws, requestedClass))
    {
        handler.SendSysMessage("Usage: /pb add class <class>");
        return;
    }

    requestedClass.erase(requestedClass.find_last_not_of(" \t\r\n") + 1);
    for (ClassName const& className : ClassNames)
    {
        if (requestedClass == className.SimplifiedChinese || requestedClass == className.TraditionalChinese ||
            std::ranges::equal(requestedClass, className.English, [](unsigned char left, unsigned char right)
            {
                return std::tolower(left) == std::tolower(right);
            }))
        {
            handler.PSendSysMessage("PlayerBots: received add class request for {}.", className.English);
            return;
        }
    }

    handler.SendSysMessage("PlayerBots: unknown class. Usage: /pb add class <class>");
}

class PlayerBotsCommands final : public PlayerScript
{
public:
    PlayerBotsCommands() : PlayerScript("PlayerBotsCommands", { PLAYERHOOK_CAN_PLAYER_USE_PRIVATE_CHAT }) { }

    bool OnPlayerCanUseChat(Player* player, uint32 type, uint32 language, std::string& message,
        Player* receiver) override
    {
        if (type != CHAT_MSG_WHISPER || language != LANG_ADDON || player != receiver ||
            !message.starts_with(AddonPrefix))
            return true;

        ChatHandler handler(player->GetSession());
        HandlePlayerBotsCommand(handler, std::string_view(message).substr(AddonPrefix.size()));
        return false;
    }
};
}

void Addmod_playerbotsScripts()
{
    new PlayerBotsCommands();
}
