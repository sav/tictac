# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Savio Sena <savio.sena@gmail.com>
#
# ctest driver: run tictac over a fixture with no plugins and require the
# emitted PGN to match the expected file byte for byte.

execute_process(
  COMMAND ${TICTAC} -f ${FIXTURE} -o ${OUT}
  RESULT_VARIABLE run_rc)
if(NOT run_rc EQUAL 0)
  message(FATAL_ERROR "tictac exited ${run_rc} on ${FIXTURE}")
endif()

execute_process(
  COMMAND ${CMAKE_COMMAND} -E compare_files ${OUT} ${EXPECTED}
  RESULT_VARIABLE diff_rc)
if(NOT diff_rc EQUAL 0)
  message(FATAL_ERROR "output ${OUT} does not match ${EXPECTED}")
endif()
