# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Savio Sena <savio.sena@gmail.com>
#
# ctest driver: a plugin returning "stop" on the first game must halt the run
# cleanly at --jobs N. How many games come out is timing-dependent -- the games
# already handed to the other workers still finish -- but it is bounded: at
# least the stopping one, never more than one per worker.

execute_process(
  COMMAND ${TICTAC} -j ${JOBS} -f ${FIXTURE} -p ${PLUGIN} -o ${OUT}
  RESULT_VARIABLE run_rc)
if(NOT run_rc EQUAL 0)
  message(FATAL_ERROR "tictac exited ${run_rc} on ${FIXTURE} at -j${JOBS}")
endif()

file(STRINGS ${OUT} lines REGEX "^\\[Event ")
list(LENGTH lines emitted)
if(emitted LESS 1 OR emitted GREATER ${JOBS})
  message(FATAL_ERROR
    "emitted ${emitted} games at -j${JOBS} after a stop; expected between 1 and ${JOBS}")
endif()
