# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Savio Sena <savio.sena@gmail.com>
#
# ctest driver: run tictac with a plugin that writes a file, then require the
# produced file to match an expected one byte for byte. PLUGIN (and the optional
# second stage PLUGIN2) is passed as one argument each -- they carry specs with
# spaces -- so their paths must be absolute: the run happens in a fresh WORKDIR
# where the produced file lands.

file(MAKE_DIRECTORY ${WORKDIR})

set(run_args -f ${FIXTURE} -p ${PLUGIN})
if(PLUGIN2)
  list(APPEND run_args -p ${PLUGIN2})
endif()
list(APPEND run_args --no-output)

execute_process(
  COMMAND ${TICTAC} ${run_args}
  WORKING_DIRECTORY ${WORKDIR}
  RESULT_VARIABLE run_rc)
if(NOT run_rc EQUAL 0)
  message(FATAL_ERROR "tictac exited ${run_rc} for plugin ${PLUGIN}")
endif()

execute_process(
  COMMAND ${CMAKE_COMMAND} -E compare_files ${WORKDIR}/${PRODUCED} ${EXPECTED}
  RESULT_VARIABLE diff_rc)
if(NOT diff_rc EQUAL 0)
  message(FATAL_ERROR "produced ${WORKDIR}/${PRODUCED} does not match ${EXPECTED}")
endif()
