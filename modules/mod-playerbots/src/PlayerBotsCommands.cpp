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
#include "CommandScript.h"
#include "Common.h"
#include <algorithm>
#include <cctype>
#include <string_view>

namespace
{
using namespace Acore::ChatCommands;

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
                handler->PSendSysMessage("PlayerBots: received add class request for {}.", className.English);
                return true;
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
}
