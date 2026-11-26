# PlayerBots

The server discovers this module through AzerothCore's module loader. Reconfigure and build `worldserver` with modules
enabled, and keep `AddonChannel = 1` in the worldserver configuration.

Copy the `addon/ACPlayerBots` folder to `Interface/AddOns` in the WoW 3.3.5a client and enable **AC PlayerBots**.
Enter `/pb add class Warrior`, `/pb add class Death Knight`, or `/pb add class 战士` in the chat box.
The server sends the requesting player a system message acknowledging the class. This command does not create a bot.
