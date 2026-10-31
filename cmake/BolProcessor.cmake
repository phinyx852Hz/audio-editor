# SPDX-License-Identifier: AGPL-3.0-only
#
# The Bol Processor BP3 console (third_party/bolprocessor), BSD-licensed, by Bernard Bel, Jim
# Kippen, Srikumar Karaikudi Subramanian and Anthony Kozar: the grammar engine Bel and Kippen
# built to model tablā improvisation. PHI runs it as a program of its own, phi-bp3, from the
# Compose menu (Source/ui/BolProcessorDialog.cpp), and reads back the items it produces or its
# MIDI file. Built unchanged; libcurl, which BP3 uses only to send notes to its PHP interface,
# is replaced by the stubs in third_party/bolprocessor/curl-shim.

find_package(ALSA REQUIRED)
file(GLOB bp3_sources CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/third_party/bolprocessor/source/*.c")
add_executable(phi-bp3 ${bp3_sources})
target_include_directories(phi-bp3 PRIVATE
    "${CMAKE_SOURCE_DIR}/third_party/bolprocessor/curl-shim"
    "${CMAKE_SOURCE_DIR}/third_party/bolprocessor/source")
# As BP3's own Makefile builds it; its warnings are upstream's.
target_compile_options(phi-bp3 PRIVATE -fno-common -w)
target_link_libraries(phi-bp3 PRIVATE ALSA::ALSA m)
install(TARGETS phi-bp3 RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
# BP3 reads its message strings from console_strings.json (from its PHP interface, GPL-3.0, by
# Bernard Bel) in the folder it runs in; PHI copies it there.
install(FILES third_party/bolprocessor/console_strings.json DESTINATION "${CMAKE_INSTALL_DATADIR}/${APP_ID}/bolprocessor")
# The examples: Kippen and Bel's qāʿida grammar (share/bolprocessor/README.md).
install(DIRECTORY share/bolprocessor/ DESTINATION "${CMAKE_INSTALL_DATADIR}/${APP_ID}/bolprocessor/examples")
install(FILES third_party/bolprocessor/License.txt DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/${APP_ID}" RENAME BolProcessor-License.txt)
install(FILES third_party/bolprocessor/Credits.txt DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/${APP_ID}" RENAME BolProcessor-Credits.txt)
# PHI tabla (Source/sampler/PhiTabla.h): the owner's own recorded strokes, a Slicer kit.
install(DIRECTORY share/phi-tabla/ DESTINATION "${CMAKE_INSTALL_DATADIR}/${APP_ID}/phi-tabla")
