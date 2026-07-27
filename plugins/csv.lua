-- SPDX-License-Identifier: GPL-3.0-or-later
-- Copyright (C) 2026 Savio Sena <savio.sena@gmail.com>
--
-- csv.lua -- export one row per game (white, black, result, eco) to a CSV file.
--   --plugin "csv.lua out=games.csv"

local plugin = {
  meta = {
    name = "csv",
    args = {
      out = { type = "string", default = "games.csv", help = "Output CSV path." },
    },
  },
}

-- No header row: under -j the workers share one writer and nothing orders a
-- worker's init before another's first row, so a header could land mid-file.
function plugin.init(ctx)
  ctx.scope.w = ctx.open(ctx.args:get("out", "games.csv"))
end

function plugin.process(input, ctx)
  local g = input.game
  ctx.scope.w:writef("%s,%s,%s,%s\n",
    g:header("White") or "", g:header("Black") or "", g:result(), g:header("ECO") or "")
  return input
end

return plugin
