-- SPDX-License-Identifier: GPL-3.0-or-later
-- Copyright (C) 2026 Savio Sena <savio.sena@gmail.com>
--
-- data_set.lua -- first stage of the input.data flow test. Confirms data starts
-- fresh (nil) for each game at the head of the pipeline, then stamps the game
-- index into data for the next plugin to read.

local plugin = {}

function plugin.process(input, ctx)
  assert(input.data == nil, "first plugin must see nil data at the start of each game")
  return { data = ctx.index }
end

return plugin
