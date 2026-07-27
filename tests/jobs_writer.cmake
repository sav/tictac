# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Savio Sena <savio.sena@gmail.com>
#
# ctest driver: two plugin stages share one ctx.open file at --jobs N. The
# interleaving is timing-dependent, so the produced file is compared as a
# multiset of lines -- which still catches a dropped, duplicated or torn write,
# the things a missing writer mutex or a per-worker reopen would cause.

file(MAKE_DIRECTORY ${WORKDIR})

execute_process(
  COMMAND ${TICTAC} -j ${JOBS} -f ${FIXTURE} -p ${PLUGIN} -p ${PLUGIN2} --no-output
  WORKING_DIRECTORY ${WORKDIR}
  RESULT_VARIABLE run_rc)
if(NOT run_rc EQUAL 0)
  message(FATAL_ERROR "tictac exited ${run_rc} at -j${JOBS}")
endif()

file(STRINGS ${WORKDIR}/${PRODUCED} got)
file(STRINGS ${EXPECTED} want)
list(SORT got)
list(SORT want)
if(NOT got STREQUAL want)
  list(LENGTH got got_n)
  list(LENGTH want want_n)
  message(FATAL_ERROR
    "produced ${WORKDIR}/${PRODUCED} does not match ${EXPECTED} as a set of lines "
    "(${got_n} lines vs ${want_n})")
endif()
