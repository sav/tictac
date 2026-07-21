-- SPDX-License-Identifier: GPL-3.0-or-later
-- Copyright (C) 2026 Savio Sena <savio.sena@gmail.com>
--
-- position.lua -- forward the first mainline position whose FEN starts with a
-- given prefix, handing that position to the next plugin as the board cursor.
--   --plugin "position.lua fen=rnbqkbnr"

local plugin = {
  meta = {
    name = "position",
    args = {
      fen = { type = "string", help = "FEN prefix to match against each position." },
    },
  },
}

function plugin.process(input, ctx)
  local target = ctx.args:require("fen")
  for node in input.game:positions() do
    local fen = node.board:fen()
    if fen:sub(1, #target) == target then
      return { game = input.game, board = node.board }
    end
  end
  return false
end

return plugin
