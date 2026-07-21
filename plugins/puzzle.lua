-- SPDX-License-Identifier: GPL-3.0-or-later
-- Copyright (C) 2026 Savio Sena <savio.sena@gmail.com>
--
-- puzzle.lua -- scan a game for a forced mate and hand that position downstream
-- via board + data, for a collector plugin to emit. Needs a UCI engine.
--   --plugin "puzzle.lua engine=stockfish depth=20 mate=3"

local plugin = {
  meta = {
    name = "puzzle",
    args = {
      engine = { type = "string", default = "stockfish", help = "UCI engine path." },
      depth  = { type = "number", default = 20,          help = "Search depth per position." },
      mate   = { type = "number", default = 3,           help = "Longest mate (in moves) to accept." },
    },
  },
}

function plugin.init(ctx)
  ctx.scope.sf = ctx.engine(ctx.args:get("engine", "stockfish"))
end

function plugin.process(input, ctx)
  local depth = ctx.args:number("depth", 20)
  local limit = ctx.args:number("mate", 3)
  for node in input.game:positions() do
    local r = ctx.scope.sf:analyse(node.board, { depth = depth })
    if r.mate and r.mate > 0 and r.mate <= limit then
      return { game = input.game, board = node.board,
               data = { puzzle = true, mate = r.mate, pv = r.pv } }
    end
  end
  return false
end

return plugin
