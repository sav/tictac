# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Savio Sena <savio.sena@gmail.com>
#
# ctest driver: run tictac at --jobs N and compare the output against the
# expected file as a multiset of lines. Above -j1 games come out in completion
# order, so a byte comparison would only be testing the scheduler; sorting both
# sides still catches a lost, duplicated or torn record.

execute_process(
  COMMAND ${TICTAC} -j ${JOBS} -f ${FIXTURE} -p ${PLUGIN} -o ${OUT}
  RESULT_VARIABLE run_rc)
if(NOT run_rc EQUAL 0)
  message(FATAL_ERROR "tictac exited ${run_rc} on ${FIXTURE} at -j${JOBS}")
endif()

file(STRINGS ${OUT} got)
file(STRINGS ${EXPECTED} want)
list(SORT got)
list(SORT want)
if(NOT got STREQUAL want)
  list(LENGTH got got_n)
  list(LENGTH want want_n)
  message(FATAL_ERROR
    "output ${OUT} does not match ${EXPECTED} as a set of lines "
    "(${got_n} lines vs ${want_n})")
endif()
