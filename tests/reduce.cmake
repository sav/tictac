# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Savio Sena <savio.sena@gmail.com>
#
# ctest driver: run an aggregating plugin (partial()/reduce()) at -j1 and at
# -j N and require the emitted aggregate to be byte-identical and to match the
# expected file. A reduce that lost, double-counted or misordered a worker's
# partial would diverge; this pins that the merged report is independent of how
# the games were split across workers.

file(MAKE_DIRECTORY ${WORKDIR}/j1 ${WORKDIR}/jn)

execute_process(
  COMMAND ${TICTAC} -j1 -f ${FIXTURE} -p ${PLUGIN} --no-output
  WORKING_DIRECTORY ${WORKDIR}/j1
  RESULT_VARIABLE rc1)
if(NOT rc1 EQUAL 0)
  message(FATAL_ERROR "tictac exited ${rc1} at -j1")
endif()

execute_process(
  COMMAND ${TICTAC} -j ${JOBS} --engines 2 -f ${FIXTURE} -p ${PLUGIN} --no-output
  WORKING_DIRECTORY ${WORKDIR}/jn
  RESULT_VARIABLE rcn)
if(NOT rcn EQUAL 0)
  message(FATAL_ERROR "tictac exited ${rcn} at -j${JOBS}")
endif()

execute_process(
  COMMAND ${CMAKE_COMMAND} -E compare_files ${WORKDIR}/j1/${PRODUCED} ${WORKDIR}/jn/${PRODUCED}
  RESULT_VARIABLE same_rc)
if(NOT same_rc EQUAL 0)
  message(FATAL_ERROR "reduce output differs between -j1 and -j${JOBS}")
endif()

execute_process(
  COMMAND ${CMAKE_COMMAND} -E compare_files ${WORKDIR}/j1/${PRODUCED} ${EXPECTED}
  RESULT_VARIABLE exp_rc)
if(NOT exp_rc EQUAL 0)
  message(FATAL_ERROR "reduce output ${WORKDIR}/j1/${PRODUCED} does not match ${EXPECTED}")
endif()
