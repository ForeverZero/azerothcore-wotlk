# PlayerBots

The server discovers this module through AzerothCore's module loader. Build `worldserver` with the module enabled.

Enter `.pb add class Warrior`, `.pb add class Death Knight`, or `.pb add class 战士` in the chat box.
The server creates a character on your faction and assigns a random DPS specialization for the requested class.
Its name starts with the role and specialization abbreviations, such as `Dpsarc` for an Arcane Mage,
followed by random letters.
It spawns at a random valid position within 10 yards, faces a random direction, and runs the same initialization
as `.pb init` before joining your party or raid. If no nearby position is suitable, it spawns at your position.
If you are solo, a party is created automatically.
When the party is full, the leader's next add command converts it to a raid and adds the bot.
The command fails when the raid reaches 40 members.

Enter `.pb init` with one of your bots selected to initialize that bot. Clear your target to initialize all bots
you summoned. Initialization synchronizes their levels to yours, resets talents, and spends the active spec's points
using the bot's assigned WotLK PvE build, starting with its primary tree. It learns class quest and trainer spells
available at that level, including trainer ranks unlocked by newly learned talents. It forgets spells above
the new level and destroys previously equipped items whose level or proficiency requirements are no longer met.
When a summoner's level changes, the server runs the same initialization on all of their bots in the group.

Select one of your bots and enter `.pb role set <dps/tank/healer>` to assign a role. The server chooses a random
specialization for that class and role, reruns initialization, and renames the bot to match. The command fails
when the target is unavailable or the bot's class has no build for the requested role.

Bots start in Follow state. Each bot rolls its own trigger distance between 8 and 12 yards and starts following
when you move beyond it. Each bot chooses a separate random destination within 5 yards of you. It keeps that
offset while following and rolls a new trigger distance when it settles.
They teleport to their owner after a map change or after more than 200 yards of separation for 30 seconds.
Cross-map transfers respect instance entry checks and do not enter battlegrounds or arenas.

Bots enter Combat state when they find an eligible opponent already fighting them or a teammate within 100 yards.
They prioritize their own opponents and keep the current opponent while it remains eligible. When assisting,
they check their summoner first. They chase into melee range and auto-attack, then resume following once they have
no target.

A bot goes offline when it is removed from the group. All bots you created go offline when you log out, leave your
group, or the group disbands. The characters and their group memberships are held in memory.

Spawn bots on the ground in the open world or a regular dungeon or raid. This feature uses local groups and is
unavailable in clustered worlds, PvP instances, or Dungeon Finder groups. Groups with bots cannot queue together
for Dungeon Finder, battlegrounds, or arenas.
