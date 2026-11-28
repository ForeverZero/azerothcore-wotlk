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

#ifndef PLAYER_BOT_TALENTS_H
#define PLAYER_BOT_TALENTS_H

#include "DBCStructure.h"
#include "SharedDefines.h"
#include <array>
#include <string_view>

namespace PlayerBots
{
enum class BotRole
{
    Dps,
    Healer,
    Tank
};

struct BotTalentBuild
{
    Classes Class;
    BotRole Role;
    std::string_view Name;
    std::string_view Abbreviation;
    uint8 PrimaryTree;
    std::array<std::string_view, MAX_TALENT_TABS> TreeRanks;
};

// Level 80 PvE builds from https://azerothprime.com/en/wotlk/talent-calculator.
// Each string follows Talent.dbc row and column order in its tree. Omitted trailing ranks are zero.
inline constexpr BotTalentBuild TalentBuilds[] =
{
    { CLASS_DRUID, BotRole::Dps, "Balance", "bal", 0, // 58/0/13
        { "5102203115331303213305311231", "", "205003012" } },
    { CLASS_DRUID, BotRole::Dps, "Feral Cat", "cat", 1, // 0/53/18
        { "", "503202132322010053120230310511", "205503012" } },
    { CLASS_DRUID, BotRole::Tank, "Feral Bear", "bear", 1, // 0/60/11
        { "", "503232132322010353120300313511", "20350001" } },
    { CLASS_DRUID, BotRole::Healer, "Restoration", "rest", 2, // 11/0/60
        { "05320001", "", "230023312131502531253313051" } },

    { CLASS_HUNTER, BotRole::Dps, "Beast Mastery", "bm", 0, // 53/13/5
        { "51200201515012233110531351", "005305", "5" } },
    { CLASS_HUNTER, BotRole::Dps, "Marksmanship", "mm", 1, // 7/57/7
        { "502", "035335131030013233035031051", "5000002" } },
    { CLASS_HUNTER, BotRole::Dps, "Survival", "surv", 2, // 0/15/56
        { "", "005305101", "5000032500033330531135301331" } },

    { CLASS_MAGE, BotRole::Dps, "Arcane", "arc", 0, // 57/3/11
        { "033023030100330150323132505321", "03", "023023001" } },
    { CLASS_MAGE, BotRole::Dps, "Fire", "fire", 1, // 18/53/0
        { "23002303110003", "0055032012303330053120300351", "" } },
    { CLASS_MAGE, BotRole::Dps, "Frost", "frst", 2, // 18/0/53
        { "23000503110003", "", "0533030311133100030152231351" } },

    { CLASS_PALADIN, BotRole::Healer, "Holy", "holy", 0, // 51/20/0
        { "50350150021013053100515221", "50023131203", "" } },
    { CLASS_PALADIN, BotRole::Tank, "Protection", "prot", 1, // 0/53/18
        { "", "05005135200132311333312321", "521022012003" } },
    { CLASS_PALADIN, BotRole::Dps, "Retribution", "ret", 2, // 2/17/52
        { "02", "453201002", "05232051203331302133201331" } },

    { CLASS_PRIEST, BotRole::Healer, "Discipline", "disc", 0, // 56/15/0
        { "0503203130300512331313231251", "20450103", "" } },
    { CLASS_PRIEST, BotRole::Healer, "Holy", "holy", 1, // 18/53/0
        { "05032031103", "234501032002151530000331351", "" } },
    { CLASS_PRIEST, BotRole::Dps, "Shadow", "shad", 2, // 14/0/57
        { "05032031", "", "325023051223010223152301351" } },

    { CLASS_ROGUE, BotRole::Dps, "Assassination", "assa", 0, // 51/13/7
        { "005303104352100520103331051", "005005003", "502" } },
    { CLASS_ROGUE, BotRole::Dps, "Combat", "cmbt", 1, // 20/51/0
        { "00532000514", "0252051000035015223100501251", "" } },
    { CLASS_ROGUE, BotRole::Dps, "Subtlety", "sub", 2, // 19/0/52
        { "30532010113", "", "5022032030301121350115031251" } },

    { CLASS_SHAMAN, BotRole::Dps, "Elemental", "ele", 0, // 57/14/0
        { "0533001523213351322301351", "005050031", "" } },
    { CLASS_SHAMAN, BotRole::Dps, "Enhancement", "enh", 1, // 19/52/0
        { "053030152", "30305003105021333031131131051", "" } },
    { CLASS_SHAMAN, BotRole::Healer, "Restoration", "rest", 2, // 0/13/58
        { "", "0050503", "50005331335310501122331251" } },

    { CLASS_WARLOCK, BotRole::Dps, "Affliction", "aff", 0, // 55/0/16
        { "2350002030023510253500331151", "", "550000051" } },
    { CLASS_WARLOCK, BotRole::Dps, "Demonology", "demo", 1, // 0/56/15
        { "", "003203301135202530135221351", "55000005" } },
    { CLASS_WARLOCK, BotRole::Dps, "Destruction", "dest", 2, // 0/19/52
        { "", "033100301134", "05203205210231051035030351" } },

    { CLASS_WARRIOR, BotRole::Dps, "Arms", "arms", 0, // 55/8/8
        { "3022302023335100102012213231251", "305", "2033" } },
    { CLASS_WARRIOR, BotRole::Dps, "Fury", "fury", 1, // 18/53/0
        { "30202300233", "325003005500310053120501351", "" } },
    { CLASS_WARRIOR, BotRole::Tank, "Protection", "prot", 2, // 15/5/51
        { "3500200023", "302", "053351225000012521030113321" } },

    { CLASS_DEATH_KNIGHT, BotRole::Tank, "Blood", "bld", 0, // 43/23/5
        { "005512153330030320102013", "3050505000023", "005" } },
    { CLASS_DEATH_KNIGHT, BotRole::Dps, "Frost", "frst", 1, // 15/56/0
        { "23050005", "32005350352203012300033101351", "" } },
    { CLASS_DEATH_KNIGHT, BotRole::Dps, "Unholy", "uh", 2, // 0/19/52
        { "", "320043500002", "2300303050032152000150013133051" } },
};
}

#endif // PLAYER_BOT_TALENTS_H
