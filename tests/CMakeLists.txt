# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Savio Sena <savio.sena@gmail.com>
#
# Integration tests: drive the tictac binary with small Lua plugins and PGN
# fixtures. Each plugin asserts its own invariants; how a failed assert surfaces
# (a non-zero exit from process(), or a logged error from finish()) is handled by
# tt_case below.

# Run the tictac binary from this directory so plugin/fixture paths stay relative.
# A failed assert in a process() hook aborts the run (non-zero exit, which ctest
# treats as failure). A failed assert in a finish() hook is only logged and the
# run still exits 0, so also fail the test whenever any finish hook reports an
# error -- otherwise the finish-based checks below would pass vacuously.
function(tt_case name)
  add_test(NAME ${name}
    COMMAND $<TARGET_FILE:tictac> ${ARGN}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  set_tests_properties(${name} PROPERTIES FAIL_REGULAR_EXPRESSION "error: finish of")
endfunction()

# An invalid return must abort the run; the test passes when the reported error
# matches `regex` (PASS_REGULAR_EXPRESSION makes the match, not the exit code,
# decide the result).
function(tt_error name regex)
  tt_case(${name} ${ARGN})
  set_tests_properties(${name} PROPERTIES PASS_REGULAR_EXPRESSION "${regex}")
endfunction()

# --- Return contract: valid returns and their effect on the stream --------------
# three_games.pgn holds 3 games; count.lua asserts how many values reach it.

tt_case(return_pass        -f fixtures/three_games.pgn -p "plugins/return_modes.lua mode=pass"        -p "plugins/count.lua expect=3" --no-output)
tt_case(return_input       -f fixtures/three_games.pgn -p "plugins/return_modes.lua mode=input"       -p "plugins/count.lua expect=3" --no-output)
tt_case(return_true        -f fixtures/three_games.pgn -p "plugins/return_modes.lua mode=true"        -p "plugins/count.lua expect=3" --no-output)
tt_case(return_board       -f fixtures/three_games.pgn -p "plugins/return_modes.lua mode=board"       -p "plugins/count.lua expect=3" --no-output)
tt_case(return_table_pass  -f fixtures/three_games.pgn -p "plugins/return_modes.lua mode=table_pass"  -p "plugins/count.lua expect=3" --no-output)
tt_case(return_nil         -f fixtures/three_games.pgn -p "plugins/return_modes.lua mode=nil"         -p "plugins/count.lua expect=0" --no-output)
tt_case(return_false       -f fixtures/three_games.pgn -p "plugins/return_modes.lua mode=false"       -p "plugins/count.lua expect=0" --no-output)
tt_case(return_table_drop  -f fixtures/three_games.pgn -p "plugins/return_modes.lua mode=table_drop"  -p "plugins/count.lua expect=0" --no-output)
tt_case(return_table_stop  -f fixtures/three_games.pgn -p "plugins/return_modes.lua mode=table_stop"  -p "plugins/count.lua expect=1" --no-output)
tt_case(return_table_abort -f fixtures/three_games.pgn -p "plugins/return_modes.lua mode=table_abort" -p "plugins/count.lua expect=0" --no-output)
tt_case(return_fanout      -f fixtures/three_games.pgn -p "plugins/return_modes.lua mode=fanout2"     -p "plugins/count.lua expect=6" --no-output)
tt_case(return_fanout_stop -f fixtures/three_games.pgn -p "plugins/return_modes.lua mode=fanout_stop" -p "plugins/count.lua expect=2" --no-output)

# --- Return contract: invalid returns are rejected ------------------------------

tt_error(return_invalid_string               "bad return value"                                   -f fixtures/three_games.pgn -p "plugins/return_modes.lua mode=badstring" --no-output)
tt_error(return_invalid_multi                "bad return value"                                   -f fixtures/three_games.pgn -p "plugins/return_modes.lua mode=multi" --no-output)
tt_error(return_invalid_fanout_key           "fan-out element with an unsupported key 'foo'"      -f fixtures/three_games.pgn -p "plugins/return_modes.lua mode=fanout_badkey" --no-output)
tt_error(return_invalid_action               "unknown action 'bogus'"                             -f fixtures/three_games.pgn -p "plugins/return_modes.lua mode=bad_action" --no-output)
tt_error(return_invalid_fanout_abort         "fan-out only accepts action"                        -f fixtures/three_games.pgn -p "plugins/return_modes.lua mode=fanout_abort" --no-output)
tt_error(return_invalid_element_action       "fan-out element with an unsupported key 'action'"   -f fixtures/three_games.pgn -p "plugins/return_modes.lua mode=element_action" --no-output)
tt_error(return_invalid_empty_table          "returned an empty table"                            -f fixtures/three_games.pgn -p "plugins/return_modes.lua mode=empty_table" --no-output)
tt_error(return_invalid_fanout_empty_element "fan-out whose element #1 is empty"                  -f fixtures/three_games.pgn -p "plugins/return_modes.lua mode=fanout_empty_element" --no-output)
tt_error(return_invalid_fanout_nested_array  "unsupported key 'positional entry'"                 -f fixtures/three_games.pgn -p "plugins/return_modes.lua mode=fanout_nested_array" --no-output)

# A wrong-type action/game/board must not crash the process (sol2's unchecked get<T>()
# would otherwise abort); each must surface as an ordinary plugin error instead.
tt_error(return_invalid_action_table         "non-string 'action'"                                -f fixtures/three_games.pgn -p "plugins/return_modes.lua mode=action_table" --no-output)
tt_error(return_invalid_action_number        "non-string 'action'"                                -f fixtures/three_games.pgn -p "plugins/return_modes.lua mode=action_number" --no-output)
tt_error(return_invalid_game_string          "field 'game' is not a Game"                         -f fixtures/three_games.pgn -p "plugins/return_modes.lua mode=game_string" --no-output)
tt_error(return_invalid_board_number         "field 'board' is not a Board"                       -f fixtures/three_games.pgn -p "plugins/return_modes.lua mode=board_number" --no-output)
tt_error(return_invalid_fanout_action_table  "fan-out with a non-string 'action'"                 -f fixtures/three_games.pgn -p "plugins/return_modes.lua mode=fanout_action_table" --no-output)
tt_error(return_invalid_fanout_element_game  "fan-out element whose field 'game' is not a Game"   -f fixtures/three_games.pgn -p "plugins/return_modes.lua mode=fanout_element_game_string" --no-output)
tt_error(return_invalid_fanout_element_board "fan-out element whose field 'board' is not a Board" -f fixtures/three_games.pgn -p "plugins/return_modes.lua mode=fanout_element_board_number" --no-output)

# --- --on-error policy applied to an invalid return -----------------------------

tt_case(onerror_pass -f fixtures/three_games.pgn -p "plugins/return_modes.lua mode=badstring" -p "plugins/count.lua expect=3" --on-error pass --no-output)
tt_case(onerror_drop -f fixtures/three_games.pgn -p "plugins/return_modes.lua mode=badstring" -p "plugins/count.lua expect=0" --on-error drop --no-output)

# --- Pipeline value & context contract ------------------------------------------

# input.data: nil at the head of each game, flows to the next plugin.
tt_case(data_flow -f fixtures/three_games.pgn -p plugins/data_set.lua -p plugins/data_check.lua --no-output)

# ctx.scope: per-plugin, persists across games. Two instances must each count 3.
tt_case(scope_per_plugin -f fixtures/three_games.pgn -p "plugins/scope_count.lua games=3" -p "plugins/scope_count.lua games=3" --no-output)

# ctx.shared: global across plugins and games. Two instances over 3 games -> 6.
tt_case(shared_global -f fixtures/three_games.pgn -p plugins/shared_inc.lua -p "plugins/shared_inc.lua expect=6" --no-output)

# --- API smoke test -------------------------------------------------------------

tt_case(api_smoke -f fixtures/scholars.pgn -p plugins/api_smoke.lua --no-output)
