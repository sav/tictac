-- SPDX-License-Identifier: GPL-3.0-or-later
-- Copyright (C) 2026 Savio Sena <savio.sena@gmail.com>
--
-- scope_count.lua -- accumulate a per-game counter in ctx.scope and assert it
-- equals `games=` at finish. ctx.scope is private to each plugin instance, so
-- running this file twice in one pipeline must yield the same count in both,
-- proving scope persists across games and does not leak between plugins.

local plugin = {}

function plugin.init(ctx)
  ctx.scope.n = 0
end

function plugin.process(input, ctx)
  ctx.scope.n = ctx.scope.n + 1
  return input
end

function plugin.finish(ctx)
  local games = ctx.args:number("games")
  assert(games ~= nil, "scope_count.lua requires games=")
  assert(ctx.scope.n == games,
    string.format("scope: expected %d, counted %d", games, ctx.scope.n))
end

return plugin
