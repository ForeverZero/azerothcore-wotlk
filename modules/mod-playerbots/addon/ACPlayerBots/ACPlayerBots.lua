SLASH_ACPLAYERBOTS1 = "/pb"

SlashCmdList.ACPLAYERBOTS = function(message)
  local playerName = UnitName("player")
  SendAddonMessage("ACPlayerBots", message, "WHISPER", playerName)
end
