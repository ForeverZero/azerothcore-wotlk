# PlayerBots

The server discovers this module through AzerothCore's module loader. Build `worldserver` with the module enabled.

Enter `.pb add class Warrior`, `.pb add class Death Knight`, or `.pb add class 战士` in the chat box.
The server creates an idle character with a random name and a race on your faction that can play the requested class.
It appears at your position and joins your party or raid. If you are solo, a party is created automatically.
The command fails when the group is full.

All bots you created go offline when you log out, leave your group, the group disbands, or any of your bots is removed
from the group. The characters and their group memberships are held in memory.

Spawn bots on the ground in the open world or a regular dungeon or raid. This feature uses local groups and is
unavailable in clustered worlds, PvP instances, or Dungeon Finder groups. Groups with bots cannot queue together
for Dungeon Finder, battlegrounds, or arenas.
