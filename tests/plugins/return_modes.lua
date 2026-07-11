-- SPDX-License-Identifier: GPL-3.0-or-later
-- Copyright (C) 2026 Savio Sena <savio.sena@gmail.com>
--
-- return_modes.lua -- return a value chosen by `mode=` to exercise every branch
-- of the process() return contract (see LUA.md section 4), valid and invalid.

local plugin = {}

function plugin.process(input, ctx)
  local mode = ctx.args:get("mode", "pass")

  -- Valid returns.
  if mode == "pass" then return input end
  if mode == "input" then return input end
  if mode == "true" then return true end
  if mode == "false" then return false end
  if mode == "nil" then return end
  if mode == "board" then return input.board end
  if mode == "table_pass" then return { action = "pass" } end
  if mode == "table_drop" then return { action = "drop" } end
  if mode == "table_stop" then return { action = "stop" } end
  if mode == "table_abort" then return { action = "abort" } end
  if mode == "fanout2" then
    return { { game = input.game }, { game = input.game } }
  end
  if mode == "fanout_stop" then
    return { action = "stop", { game = input.game }, { game = input.game } }
  end

  -- Invalid returns: each must be rejected as a plugin error.
  if mode == "badstring" then return "foo" end
  if mode == "multi" then return 1, 2, 3 end
  if mode == "fanout_badkey" then return { { foo = 1 } } end
  if mode == "bad_action" then return { action = "bogus" } end
  if mode == "fanout_abort" then return { action = "abort", { game = input.game } } end
  if mode == "element_action" then return { { action = "pass", game = input.game } } end
  if mode == "empty_table" then return {} end
  if mode == "fanout_empty_element" then return { {} } end
  if mode == "fanout_nested_array" then return { { {} } } end
  if mode == "action_table" then return { action = {} } end
  if mode == "action_number" then return { action = 1 } end
  if mode == "game_string" then return { game = "not a game" } end
  if mode == "board_number" then return { board = 42 } end
  if mode == "fanout_action_table" then return { action = {}, { game = input.game } } end
  if mode == "fanout_element_game_string" then return { { game = "not a game" } } end
  if mode == "fanout_element_board_number" then return { { board = 42 } } end

  error("return_modes: unknown mode '" .. tostring(mode) .. "'")
end

return plugin
