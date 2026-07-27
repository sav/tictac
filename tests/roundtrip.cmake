# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Savio Sena <savio.sena@gmail.com>
#
# ctest driver: run tictac over a fixture through a pass-through plugin and
# require the emitted PGN to match the expected file byte for byte. JOBS, when
# set, pins --jobs -- only -j1 keeps input order, so only -j1 can be compared
# this way; the parallel cases go through jobs.cmake instead.

set(run_args -f ${FIXTURE} -p ${PLUGIN} -o ${OUT})
if(JOBS)
  list(APPEND run_args -j ${JOBS})
endif()

execute_process(
  COMMAND ${TICTAC} ${run_args}
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
