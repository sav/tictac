-- SPDX-License-Identifier: GPL-3.0-or-later
-- Copyright (C) 2026 Savio Sena <savio.sena@gmail.com>
--
-- dedup.lua -- drop games already seen this run, keyed by their serialized PGN.
--   --plugin dedup.lua

local plugin = { meta = { name = "dedup" } }

function plugin.init(ctx)
  ctx.scope.seen = {}
end

function plugin.process(input, ctx)
  local key = input.game:pgn()
  if ctx.scope.seen[key] then
    ctx.log.info("dropped duplicate game %d", ctx.index)
    return false
  end
  ctx.scope.seen[key] = true
  return input
end

return plugin
