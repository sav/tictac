# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Savio Sena <savio.sena@gmail.com>
#
# ctest driver: run the engine option test with a fresh log. The mock engine
# appends every setoption it receives to TT_MOCK_OPTIONS_LOG, so the file has to
# start empty or a stale run could satisfy the assertions.

file(REMOVE ${LOG})

set(ENV{TT_MOCK_OPTIONS_LOG} ${LOG})
execute_process(
  COMMAND ${TICTAC} -f fixtures/scholars.pgn
          -p "plugins/engine_check.lua mode=options log=${LOG}" --no-output
  WORKING_DIRECTORY ${WORKDIR}
  RESULT_VARIABLE run_rc)
if(NOT run_rc EQUAL 0)
  message(FATAL_ERROR "tictac exited ${run_rc}")
endif()
