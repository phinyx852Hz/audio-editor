# SPDX-License-Identifier: AGPL-3.0-only
#
# Copies the files and folders named in LIST from SRC to DEST, keeping their paths.
# One path per line; a path ending in / is a whole folder; # starts a comment.
#   cmake -DSRC=<folder> -DDEST=<folder> -DLIST=<file> -P CopyListed.cmake

file(STRINGS "${LIST}" entries)
foreach(entry IN LISTS entries)
    if(entry STREQUAL "" OR entry MATCHES "^#")
        continue()
    endif()
    if(entry MATCHES "/$")
        file(COPY "${SRC}/${entry}" DESTINATION "${DEST}/${entry}")   # the folder's contents
    else()
        get_filename_component(folder "${entry}" DIRECTORY)
        file(COPY "${SRC}/${entry}" DESTINATION "${DEST}/${folder}")
    endif()
endforeach()
