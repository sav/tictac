-- SPDX-License-Identifier: GPL-3.0-or-later
-- Copyright (C) 2026 Savio Sena <savio.sena@gmail.com>
--
-- histogram.lua -- count games per header value and emit a sorted table in
-- finish. Passes games through untouched, so it can sit anywhere in a pipeline.
--   --plugin "histogram.lua by=ECO out=eco_histogram.txt"

local plugin = {
  meta = {
    name = "histogram",
    args = {
      by  = { type = "string", default = "ECO",             help = "Header to tally." },
      out = { type = "string", default = "histogram.txt",   help = "Output path." },
    },
  },
}

function plugin.init(ctx)
  ctx.scope.count = {}
end

function plugin.process(input, ctx)
  local key = input.game:header(ctx.args:get("by", "ECO")) or "?"
  ctx.scope.count[key] = (ctx.scope.count[key] or 0) + 1
  return input
end

function plugin.finish(ctx)
  local keys = {}
  for key in pairs(ctx.scope.count) do keys[#keys + 1] = key end
  table.sort(keys)
  local w = ctx.open(ctx.args:get("out", "histogram.txt"))
  for _, key in ipairs(keys) do w:writef("%s\t%d\n", key, ctx.scope.count[key]) end
end

return plugin
