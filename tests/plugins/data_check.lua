-- SPDX-License-Identifier: GPL-3.0-or-later
-- Copyright (C) 2026 Savio Sena <savio.sena@gmail.com>
--
-- data_check.lua -- second stage of the input.data flow test. Asserts the value
-- stamped by data_set.lua arrived intact, proving data flows down the pipeline
-- per game.

local plugin = {}

function plugin.process(input, ctx)
  assert(input.data == ctx.index,
    string.format("data_check: expected %s, got %s", tostring(ctx.index), tostring(input.data)))
  return input
end

return plugin
