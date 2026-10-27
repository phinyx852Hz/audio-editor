# SPDX-License-Identifier: AGPL-3.0-only
#
# Puts free fonts in place of the four of Vaporizer2's eight fonts that may not be
# redistributed, under the same file names, so its code (BinaryData::SFUIDisplayRegular_ttf
# and so on) loads them unchanged. Each pair is one of its skins' fonts:
#
#   Tech      SF UI Display (Apple, mock-ups only)   -> Inter (OFL-1.1)
#   Dark      Trade Gothic LT (Linotype, commercial) -> Archivo Narrow (OFL-1.1)
#   IceCubes  Alte Haas Grotesk (freeware, no terms) -> Schibsted Grotesk (OFL-1.1)
#   Classic   Open Sans (Apache-2.0)                 -> kept
#
# The replacements are static Regular and Bold cuts of the Google Fonts releases, made
# with fontTools' varLib.instancer (Inter at opsz 14). Their licences and PHI-FONTS.md go
# into the source folder's root, from where the licence step copies them.
#   cmake -DSRC=<Vaporizer2 source folder> -P ReplaceFonts.cmake

set(here ${CMAKE_CURRENT_LIST_DIR})
set(fonts "${SRC}/VASTvaporizer/Resources/Fonts")
foreach(pair
        "Inter-Regular.ttf|SFUIDisplay-Regular.ttf"
        "Inter-Bold.ttf|SFUIDisplay-Bold.ttf"
        "ArchivoNarrow-Regular.ttf|Trade Gothic LT.ttf"
        "ArchivoNarrow-Bold.ttf|Trade Gothic LT Bold.ttf"
        "SchibstedGrotesk-Regular.ttf|AlteHaasGroteskRegular.ttf"
        "SchibstedGrotesk-Bold.ttf|AlteHaasGroteskBold.ttf")
    string(REPLACE "|" ";" parts "${pair}")
    list(GET parts 0 from)
    list(GET parts 1 to)
    if(NOT EXISTS "${fonts}/${to}")
        message(FATAL_ERROR "Vaporizer2 has no ${to} any more: check cmake/instruments/vaporizer2-fonts")
    endif()
    file(COPY_FILE "${here}/${from}" "${fonts}/${to}")
endforeach()
foreach(f PHI-FONTS.md OFL-Inter.txt OFL-ArchivoNarrow.txt OFL-SchibstedGrotesk.txt LICENSE-OpenSans.txt)
    file(COPY_FILE "${here}/${f}" "${SRC}/${f}")
endforeach()
