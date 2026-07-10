-- SPDX-License-Identifier: GPL-3.0-or-later
-- Copyright (C) 2026 Savio Sena <savio.sena@gmail.com>
--
-- skel.lua — a tictac plugin skeleton
--
-- Usage:
--   tictac --file db.pgn --plugin skel.lua

local plugin = {}

plugin.meta = {
  name        = "skel",
  version     = "1.0.0",
  description = "tictac skeleton plugin",
  args        = {},
}

function plugin.init(ctx)
end

function plugin.process(input, ctx)
  return {
    action = "pass",
    game = input.game,
    data = {},
  }
end

function plugin.finish(ctx)
end

return plugin

-- -- vim:ft=lua:ts=2:sw=2:
