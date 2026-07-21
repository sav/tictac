-- SPDX-License-Identifier: GPL-3.0-or-later
-- Copyright (C) 2026 Savio Sena <savio.sena@gmail.com>
--
-- split.lua -- write each game to its own file, grouped by a header value.
-- Reopening a path would truncate it, so writers are cached per key.
--   --plugin "split.lua by=Result dir=out/"

local plugin = {
  meta = {
    name = "split",
    args = {
      by  = { type = "string", default = "Result", help = "Header to group games by." },
      dir = { type = "string", default = "",       help = "Directory prefix for the output files." },
    },
  },
}

function plugin.init(ctx)
  ctx.scope.writers = {}
end

function plugin.process(input, ctx)
  local by = ctx.args:get("by", "Result")
  local dir = ctx.args:get("dir", "")
  local key = (input.game:header(by) or "NA"):gsub("[^%w]", "_")
  local w = ctx.scope.writers[key]
  if not w then
    w = ctx.open(dir .. key .. ".pgn")
    ctx.scope.writers[key] = w
  end
  w:writeGame(input.game)
  return input
end

return plugin
