-- SPDX-License-Identifier: GPL-3.0-or-later
-- Copyright (C) 2026 Savio Sena <savio.sena@gmail.com>
--
-- board_check.lua -- assert on, or hand along, the board cursor of the pipeline
-- value, so a chain of these pins down where the cursor starts and that it
-- survives every hop between plugins unchanged.

local plugin = {}

function plugin.process(input, ctx)
  local mode = ctx.args:get("mode", "start")

  -- The cursor handed to the first plugin is the game's first position.
  if mode == "start" then
    local want = input.game:startBoard():fen()
    assert(input.board:fen() == want, "cursor: " .. input.board:fen() .. " want " .. want)
    return input
  end

  -- Move the cursor somewhere the default could never be, for the next plugin.
  if mode == "set" then
    return { game = input.game, board = input.game:board(2) }
  end

  -- Returning a bare Game must forward the cursor untouched.
  if mode == "forward_game" then return input.game end

  if mode == "expect" then
    local ply = ctx.args:number("ply")
    assert(ply ~= nil, "expect mode requires ply=")
    local want = input.game:board(ply):fen()
    assert(input.board:fen() == want, "cursor: " .. input.board:fen() .. " want " .. want)
    return input
  end

  error("board_check: unknown mode '" .. tostring(mode) .. "'")
end

return plugin
