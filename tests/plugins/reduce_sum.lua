-- SPDX-License-Identifier: GPL-3.0-or-later
-- Copyright (C) 2026 Savio Sena <savio.sena@gmail.com>
--
-- reduce_sum.lua -- a cross-worker aggregator: tally games, total plies and a
-- per-result count, then emit one line in reduce(). The per-result table (a
-- string-keyed nested table) exercises the partial()/reduce() value transport.
-- Because the output folds every worker's games, it must be identical at -j1
-- and -j N however the games were split.

local plugin = {}

function plugin.init(ctx)
  ctx.scope.games = 0
  ctx.scope.plies = 0
  ctx.scope.results = {}
end

function plugin.process(input, ctx)
  local s = ctx.scope
  s.games = s.games + 1
  s.plies = s.plies + input.game:moveCount()
  local r = input.game:result()
  s.results[r] = (s.results[r] or 0) + 1
  return input
end

function plugin.partial(ctx)
  local s = ctx.scope
  return { games = s.games, plies = s.plies, results = s.results }
end

function plugin.reduce(partials, ctx)
  local games, plies, results = 0, 0, {}
  for _, part in ipairs(partials) do
    games = games + (part.games or 0)
    plies = plies + (part.plies or 0)
    for r, n in pairs(part.results or {}) do results[r] = (results[r] or 0) + n end
  end

  local keys = {}
  for r in pairs(results) do keys[#keys + 1] = r end
  table.sort(keys)

  local w = ctx.open(ctx.args:get("out"))
  w:writef("games=%d plies=%d\n", games, plies)
  for _, r in ipairs(keys) do w:writef("result %s = %d\n", r, results[r]) end
end

return plugin
