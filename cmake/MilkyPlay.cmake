# SPDX-License-Identifier: AGPL-3.0-only
#
# MilkyPlay, the module player library of MilkyTracker (third_party/milkyplay),
# New-BSD licensed, by Peter "pailes" Barth and the MilkyTracker contributors.
# It plays and loads the Tracker's modules (XM, MOD, IT, S3M and others).

add_library(phinyx_milkyplay STATIC
    third_party/milkyplay/AudioDriverBase.cpp
    third_party/milkyplay/AudioDriverManager_PHINYX.cpp
    third_party/milkyplay/AudioDriver_NULL.cpp
    third_party/milkyplay/AudioDriver_WAVWriter.cpp
    third_party/milkyplay/ChannelMixer.cpp
    third_party/milkyplay/ExporterXM.cpp
    third_party/milkyplay/LittleEndian.cpp
    third_party/milkyplay/Loader669.cpp
    third_party/milkyplay/LoaderAMF.cpp
    third_party/milkyplay/LoaderAMS.cpp
    third_party/milkyplay/LoaderCBA.cpp
    third_party/milkyplay/LoaderDBM.cpp
    third_party/milkyplay/LoaderDIGI.cpp
    third_party/milkyplay/LoaderDSM.cpp
    third_party/milkyplay/LoaderDTM.cpp
    third_party/milkyplay/LoaderFAR.cpp
    third_party/milkyplay/LoaderGDM.cpp
    third_party/milkyplay/LoaderIMF.cpp
    third_party/milkyplay/LoaderIT.cpp
    third_party/milkyplay/LoaderMDL.cpp
    third_party/milkyplay/LoaderMOD.cpp
    third_party/milkyplay/LoaderMTM.cpp
    third_party/milkyplay/LoaderMXM.cpp
    third_party/milkyplay/LoaderOKT.cpp
    third_party/milkyplay/LoaderPLM.cpp
    third_party/milkyplay/LoaderPSM.cpp
    third_party/milkyplay/LoaderPTM.cpp
    third_party/milkyplay/LoaderS3M.cpp
    third_party/milkyplay/LoaderSTM.cpp
    third_party/milkyplay/LoaderULT.cpp
    third_party/milkyplay/LoaderUNI.cpp
    third_party/milkyplay/LoaderXM.cpp
    third_party/milkyplay/MasterMixer.cpp
    third_party/milkyplay/PlayerBase.cpp
    third_party/milkyplay/PlayerFAR.cpp
    third_party/milkyplay/PlayerGeneric.cpp
    third_party/milkyplay/PlayerIT.cpp
    third_party/milkyplay/PlayerSTD.cpp
    third_party/milkyplay/ResamplerFactory.cpp
    third_party/milkyplay/SampleLoaderAIFF.cpp
    third_party/milkyplay/SampleLoaderALL.cpp
    third_party/milkyplay/SampleLoaderAbstract.cpp
    third_party/milkyplay/SampleLoaderGeneric.cpp
    third_party/milkyplay/SampleLoaderIFF.cpp
    third_party/milkyplay/SampleLoaderSF2.cpp
    third_party/milkyplay/SampleLoaderWAV.cpp
    third_party/milkyplay/XIInstrument.cpp
    third_party/milkyplay/XMFile.cpp
    third_party/milkyplay/XModule.cpp
)
target_include_directories(phinyx_milkyplay PUBLIC third_party/milkyplay)
# Built as for MilkyTracker (more samples per instrument, the editor's module calls), with PHINYX's driver manager.
target_compile_definitions(phinyx_milkyplay PUBLIC MILKYTRACKER)
set_target_properties(phinyx_milkyplay PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_options(phinyx_milkyplay PRIVATE -w)   # third-party code: its warnings are not ours to fix
