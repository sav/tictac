-- SPDX-License-Identifier: GPL-3.0-or-later
-- Copyright (C) 2026 Savio Sena <savio.sena@gmail.com>
--
-- blunder.lua -- annotate moves whose evaluation swings by at least `drop`
-- centipawns against the side that moved, comparing each position to the one
-- before it. Needs a UCI engine.
--   --plugin "blunder.lua engine=stockfish depth=16 drop=200"

local plugin = {
  meta = {
    name = "blunder",
    args = {
      engine = { type = "string", default = "stockfish", help = "UCI engine path." },
      depth  = { type = "number", default = 16,          help = "Search depth per position." },
      drop   = { type = "number", default = 200,         help = "Centipawn swing that counts as a blunder." },
    },
  },
}

function plugin.init(ctx)
  ctx.scope.sf = ctx.engine(ctx.args:get("engine", "stockfish"))
end

function plugin.process(input, ctx)
  local depth = ctx.args:number("depth", 16)
  local drop = ctx.args:number("drop", 200)
  local prev
  for node in input.game:positions() do
    local cp = ctx.scope.sf:cp(node.board, { depth = depth })   -- white-relative
    if prev and math.abs(cp - prev) >= drop then
      node.move:setComment(string.format("blunder (%.2f)", cp / 100))
      node.move:addNag(4)                                        -- $4 = "??"
    end
    prev = cp
  end
  return input
end

return plugin
