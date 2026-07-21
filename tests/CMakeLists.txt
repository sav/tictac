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

tt_error(return_invalid_string               "invalid action 'foo'"                               -f fixtures/three_games.pgn -p "plugins/return_modes.lua mode=badstring" --no-output)
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

# --- Lua runtime errors inside process() ----------------------------------------
# Distinct from an invalid return value: these take the !res.valid() path, which
# depends on the sol2 call staying protected (SOL_ALL_SAFETIES_ON).

tt_error(process_raises          "boom"            -f fixtures/three_games.pgn -p "plugins/return_modes.lua mode=raise"    --no-output)
tt_error(process_nil_index       "attempt to"      -f fixtures/three_games.pgn -p "plugins/return_modes.lua mode=nilindex" --no-output)

# --- --on-error policy ----------------------------------------------------------
# Applied both to an invalid return and to a raised Lua error.

tt_case(onerror_pass -f fixtures/three_games.pgn -p "plugins/return_modes.lua mode=badstring" -p "plugins/count.lua expect=3" --on-error pass --no-output)
tt_case(onerror_drop -f fixtures/three_games.pgn -p "plugins/return_modes.lua mode=badstring" -p "plugins/count.lua expect=0" --on-error drop --no-output)
tt_case(onerror_pass_raise -f fixtures/three_games.pgn -p "plugins/return_modes.lua mode=raise" -p "plugins/count.lua expect=3" --on-error pass --no-output)
tt_case(onerror_drop_raise -f fixtures/three_games.pgn -p "plugins/return_modes.lua mode=raise" -p "plugins/count.lua expect=0" --on-error drop --no-output)

# --- Output path: PGN round-trip ------------------------------------------------
# Everything else runs --no-output, which leaves Writer::writeGame and Game::pgn()
# unexercised: move numbering, the black-to-move "N..." form, and the result token.

function(tt_roundtrip name fixture)
  add_test(NAME ${name}
    COMMAND ${CMAKE_COMMAND}
      -DTICTAC=$<TARGET_FILE:tictac>
      -DFIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/${fixture}
      -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/expected/${fixture}
      -DOUT=${CMAKE_CURRENT_BINARY_DIR}/${name}.pgn
      -P ${CMAKE_CURRENT_SOURCE_DIR}/roundtrip.cmake)
endfunction()

tt_roundtrip(roundtrip_startpos       scholars.pgn)
tt_roundtrip(roundtrip_black_to_move  black_to_move.pgn)

# --- Pipeline value & context contract ------------------------------------------

# input.data: nil at the head of each game, flows to the next plugin.
tt_case(data_flow -f fixtures/three_games.pgn -p plugins/data_set.lua -p plugins/data_check.lua --no-output)

# ctx.scope: per-plugin, persists across games. Two instances must each count 3.
tt_case(scope_per_plugin -f fixtures/three_games.pgn -p "plugins/scope_count.lua games=3" -p "plugins/scope_count.lua games=3" --no-output)

# ctx.shared: global across plugins and games. Two instances over 3 games -> 6.
tt_case(shared_global -f fixtures/three_games.pgn -p plugins/shared_inc.lua -p "plugins/shared_inc.lua expect=6" --no-output)

# --- API smoke test -------------------------------------------------------------

tt_case(api_smoke -f fixtures/scholars.pgn -p plugins/api_smoke.lua --no-output)

# --- ctx.args accessors ---------------------------------------------------------
# The argument spec is fixed here and args_check.lua asserts every accessor
# against it, malformed values included. A failed assert in init() aborts the
# run, so a non-zero exit is what ctest sees.

tt_case(args_accessors -f fixtures/scholars.pgn --no-output
  -p "plugins/args_check.lua one=1 many=a many=b nums=1 nums=2 empty= flag yes=yes off=off tags=a,b,c junk=20abc word=abc maybe=perhaps neg=-3 frac=1.5")

# --- PGN parse errors -----------------------------------------------------------
# Two distinct paths, and neither may take the run down: an unparseable SAN token
# (caught per game, that game abandoned) and a StreamParserError from the parser
# itself (everything parsed before it is kept). games_are.lua pins down *which*
# games survived, since a count alone cannot tell the right game was dropped.
#
# The exit-code cases and the warning-text cases are kept separate on purpose:
# PASS_REGULAR_EXPRESSION makes the match decide the result and ignores the exit
# code, so a plugin assertion paired with one would pass vacuously.

tt_case(pgn_bad_move_drops_game  -f fixtures/bad_move.pgn         -p "plugins/games_are.lua expect=Alice,Erin" --no-output)
tt_case(pgn_bad_move_first       -f fixtures/bad_move_first.pgn   -p "plugins/games_are.lua expect=Carol"      --no-output)
tt_case(pgn_ambiguous_move       -f fixtures/ambiguous_move.pgn   -p "plugins/games_are.lua expect=Erin"       --no-output)
tt_case(pgn_two_bad_moves        -f fixtures/two_bad_moves.pgn    -p "plugins/games_are.lua expect=Erin"       --no-output)
tt_case(pgn_partial_database     -f fixtures/partial_database.pgn -p "plugins/games_are.lua expect=Alice"      --no-output)
tt_case(pgn_bad_header_no_games  -f fixtures/bad_header.pgn       -p "plugins/games_are.lua expect="           --no-output)

# The warning has to name the offending move and the players, or a skipped game
# is invisible in a large run.
tt_error(pgn_bad_move_warns    "skipping game \\(Carol vs Dave\\): cannot parse SAN move 'Qz9'" -f fixtures/bad_move.pgn       -p "plugins/games_are.lua expect=Alice,Erin" --no-output)
tt_error(pgn_ambiguous_warns   "ambiguous move 'Ne4'"                                          -f fixtures/ambiguous_move.pgn -p "plugins/games_are.lua expect=Erin"       --no-output)
tt_error(pgn_parse_error_warns "PGN parse error: Invalid header"                              -f fixtures/bad_header.pgn     -p "plugins/games_are.lua expect="           --no-output)

# --- UCI engine driver ----------------------------------------------------------
# Driven against the mock engines under mock/ rather than a real engine, so the
# expected values are fixed and the failure modes (a binary that is not an
# engine, one that dies mid-search) are reproducible. Every case gets a TIMEOUT:
# the failure modes are exactly the ones that would otherwise hang on a blocking
# read of an engine that is never going to answer.

# ARGN is a CMake list: join it so the plugin spec stays one argument rather
# than splitting on the separator into extra -p values.
function(tt_engine name)
  string(JOIN " " spec plugins/engine_check.lua ${ARGN})
  tt_case(${name} -f fixtures/scholars.pgn -p "${spec}" --no-output)
  set_tests_properties(${name} PROPERTIES TIMEOUT 30)
endfunction()

function(tt_engine_error name regex)
  string(JOIN " " spec plugins/engine_check.lua ${ARGN})
  tt_error(${name} "${regex}" -f fixtures/scholars.pgn -p "${spec}" --no-output)
  set_tests_properties(${name} PROPERTIES TIMEOUT 30)
endfunction()

tt_engine(engine_analyse  mode=analyse)
tt_engine(engine_bestmove mode=bestmove)
tt_engine(engine_multipv  mode=multipv)

# A mate score must arrive as `mate` with `score` unset.
tt_engine(engine_mate mode=mate)
set_tests_properties(engine_mate PROPERTIES ENVIRONMENT "TT_MOCK_SCORE=mate 3")

# Option values must reach the wire in UCI spelling: 16, not 16.000000.
add_test(NAME engine_options
  COMMAND ${CMAKE_COMMAND}
    -DTICTAC=$<TARGET_FILE:tictac>
    -DWORKDIR=${CMAKE_CURRENT_SOURCE_DIR}
    -DLOG=${CMAKE_CURRENT_BINARY_DIR}/engine_options.log
    -P ${CMAKE_CURRENT_SOURCE_DIR}/engine_options.cmake)
set_tests_properties(engine_options PROPERTIES TIMEOUT 30)

# Failure modes. A dead child can surface either as a failed write (EPIPE, once
# the child is gone) or as EOF on the next read, depending on how far the child
# got before exiting -- both are correct, so accept either.
tt_engine_error(engine_missing_binary "engine: (write failed|process closed unexpectedly)" mode=missing)
tt_engine_error(engine_not_uci        "engine: (write failed|process closed unexpectedly)" mode=not_uci)
tt_engine_error(engine_dies_mid_search "engine: (write failed|process closed unexpectedly)" mode=dies)

# A throwing constructor must not leak its pipes. The plugin counts open
# descriptors either side of a batch of failed spawns, so the leak is measured
# directly instead of waiting to hit an fd limit that may be enormous.
tt_engine(engine_no_leak_on_failure mode=leak times=50)

tt_engine_error(engine_no_limits    "requires one of depth, movetime or nodes" mode=no_limits)
tt_engine_error(engine_multipv_zero "multipv must be between 1 and 64"         mode=multipv_zero)
tt_engine_error(engine_multipv_huge "multipv must be between 1 and 64"         mode=multipv_huge)
