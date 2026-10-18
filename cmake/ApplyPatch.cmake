# SPDX-License-Identifier: AGPL-3.0-only
#
# Applies PATCH to the current folder once: skipped when it is already applied,
# so FetchContent can run it again on every configure.
#   cmake -DPATCH=<file> -P ApplyPatch.cmake

execute_process(COMMAND git apply --reverse --check "${PATCH}" RESULT_VARIABLE already OUTPUT_QUIET ERROR_QUIET)
if(already EQUAL 0)
    return()
endif()
execute_process(COMMAND git apply "${PATCH}" RESULT_VARIABLE failed)
if(failed)
    message(FATAL_ERROR "Could not apply ${PATCH}")
endif()
