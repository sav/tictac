-- SPDX-License-Identifier: GPL-3.0-or-later
-- Copyright (C) 2026 Savio Sena <savio.sena@gmail.com>
--
-- collect.lua -- emit whatever puzzle position an upstream plugin selected,
-- reading the handoff left in input.board / input.data.
--   --plugin "puzzle.lua ..." --plugin "collect.lua out=puzzles.epd"

local plugin = {
  meta = {
    name = "collect",
    args = {
      out = { type = "string", help = "Where to write the collected positions (default: ctx.out)." },
    },
  },
}

function plugin.init(ctx)
  local out = ctx.args:get("out")
  ctx.scope.w = out and ctx.open(out) or nil
end

function plugin.process(input, ctx)
  if input.data and input.data.puzzle then
    local w = ctx.scope.w or ctx.out
    if w then w:writef("%s ; mate in %d\n", input.board:fen(), input.data.mate) end
  end
  return input
end

return plugin
