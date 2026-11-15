# Source lists for DREAM.
#
# Mirrors the SOURCES/HEADERS of dream.pro for the current tree. Kept as an
# explicit list on purpose: src/ contains dead/legacy files (GUI-QT/qt2,
# datadecoding/epg, GPSData, MultimediaDlg, PacketSocketNull, windows/, android/)
# that must NOT be compiled, so a GLOB would pick up the wrong things.

set(DREAM_SRC "${CMAKE_SOURCE_DIR}/src")

# Core DSP / receiver / transmitter: built for every variant (console and GUI).
set(DREAM_CORE_SOURCES
    AMDemodulation.cpp
    AMSSDemodulation.cpp
    chanest/ChanEstTime.cpp
    chanest/ChannelEstimation.cpp
    chanest/IdealChannelEstimation.cpp
    chanest/TimeLinear.cpp
    chanest/TimeWiener.cpp
    creceivedata.cpp
    ctransmitdata.cpp
    datadecoding/DABMOT.cpp
    datadecoding/DataDecoder.cpp
    datadecoding/DataEncoder.cpp
    datadecoding/epgutil.cpp
    datadecoding/Experiment.cpp
    datadecoding/Journaline.cpp
    datadecoding/journaline/crc_8_16.c
    datadecoding/journaline/dabdgdec_impl.c
    datadecoding/journaline/log.c
    datadecoding/journaline/newsobject.cpp
    datadecoding/journaline/newssvcdec_impl.cpp
    datadecoding/journaline/NML.cpp
    datadecoding/journaline/Splitter.cpp
    datadecoding/MOTSlideShow.cpp
    DataIO.cpp
    drmchannel/ChannelSimulation.cpp
    DrmReceiver.cpp
    DrmSimulation.cpp
    DrmTransceiver.cpp
    DrmTransmitter.cpp
    FAC/FAC.cpp
    InputResample.cpp
    interleaver/BlockInterleaver.cpp
    interleaver/SymbolInterleaver.cpp
    IQInputFilter.cpp
    matlib/MatlibSigProToolbox.cpp
    matlib/MatlibStdToolbox.cpp
    MDI/AFPacketGenerator.cpp
    MDI/MDIDecode.cpp
    MDI/MDIInBuffer.cpp
    MDI/MDIRSCI.cpp
    MDI/MDITagItemDecoders.cpp
    MDI/MDITagItems.cpp
    MDI/PacketSinkFile.cpp
    MDI/PacketSocket.cpp
    MDI/PacketSourceFile.cpp
    MDI/Pft.cpp
    MDI/RCITagItems.cpp
    MDI/RSCITagItemDecoders.cpp
    MDI/RSISubscriber.cpp
    MDI/TagPacketDecoder.cpp
    MDI/TagPacketDecoderMDI.cpp
    MDI/TagPacketDecoderRSCIControl.cpp
    MDI/TagPacketGenerator.cpp
    mlc/BitInterleaver.cpp
    mlc/ChannelCode.cpp
    mlc/ConvEncoder.cpp
    mlc/EnergyDispersal.cpp
    mlc/Metric.cpp
    mlc/MLC.cpp
    mlc/QAMMapping.cpp
    mlc/ViterbiDecoder.cpp
    MSCMultiplexer.cpp
    ofdmcellmapping/CellMappingTable.cpp
    ofdmcellmapping/OFDMCellMapping.cpp
    OFDM.cpp
    Parameter.cpp
    PlotManager.cpp
    ReceptLog.cpp
    resample/Resample.cpp
    resample/ResampleFilter.cpp
    Scheduler.cpp
    SDC/SDCReceive.cpp
    SDC/SDCTransmit.cpp
    SDC/audioparam.cpp
    ServiceInformation.cpp
    SimulationParameters.cpp
    sound/audiofilein.cpp
    sourcedecoders/aac_codec.cpp
    sourcedecoders/AudioCodec.cpp
    sourcedecoders/AudioSourceDecoder.cpp
    sourcedecoders/AudioSourceEncoder.cpp
    sourcedecoders/null_codec.cpp
    sourcedecoders/opus_codec.cpp
    spectrumanalyser.cpp
    sync/FreqSyncAcq.cpp
    sync/SyncUsingPil.cpp
    sync/TimeSync.cpp
    sync/TimeSyncFilter.cpp
    sync/TimeSyncTrack.cpp
    tables/TableCarMap.cpp
    tables/TableFAC.cpp
    tables/TableStations.cpp
    TextMessage.cpp
    util/CRC.cpp
    util/FileTyper.cpp
    util/LogPrint.cpp
    util/Reassemble.cpp
    util/Settings.cpp
    util/Utilities.cpp
    Version.cpp
    sound/soundnull.cpp
    MSC/logicalframe.cpp
    MSC/audiosuperframe.cpp
    MSC/aacsuperframe.cpp
    MSC/xheaacsuperframe.cpp
    MSC/frameborderdescription.cpp
    resample/speexresampler.cpp
    resample/cspectrumresample.cpp
    resample/caudioresample.cpp
    sourcedecoders/reverb.cpp
    sourcedecoders/caudioreverb.cpp
    tuner.cpp
    sound/soundfactory.cpp
)

# x86-only SIMD trellis kernels. Both are fully #ifdef'd out (USE_SIMD is
# #undef'd in mlc/ViterbiDecoder.h) so ViterbiDecoder.cpp's scalar path is used
# everywhere; we only compile these on x86 to stay faithful to dream.pro.
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|i[3-6]86)$" AND NOT CMAKE_OSX_ARCHITECTURES MATCHES "arm64")
    list(APPEND DREAM_CORE_SOURCES mlc/TrellisUpdateMMX.cpp mlc/TrellisUpdateSSE2.cpp)
endif()

if(UNIX)
    list(APPEND DREAM_CORE_SOURCES linux/Pacer.cpp)
endif()

# Console (NO_QT) entry point + terminal UI.
set(DREAM_CONSOLE_SOURCES
    main.cpp
    linux/ConsoleIO.cpp
)

# Qt (core) helpers, used by the GUI build (contains(QT,core) in dream.pro).
set(DREAM_QTCORE_SOURCES
    GUI-QT/Logging.cpp
    util-QT/EPG.cpp
    util-QT/epgdec.cpp
    util-QT/Util.cpp
    main-Qt/ctrx.cpp
    main-Qt/crx.cpp
    main-Qt/ctx.cpp
    MDI/PacketSocketHTTP.cpp
)

set(DREAM_GUI_SOURCES
    GUI-QT/AnalogDemDlg.cpp
    GUI-QT/CWindow.cpp
    GUI-QT/DialogUtil.cpp
    GUI-QT/DRMPlot.cpp
    GUI-QT/EPGDlg.cpp
    GUI-QT/EvaluationDlg.cpp
    GUI-QT/fdrmdialog.cpp
    GUI-QT/fmdialog.cpp
    GUI-QT/GeneralSettingsDlg.cpp
    GUI-QT/jlbrowser.cpp
    GUI-QT/JLViewer.cpp
    GUI-QT/LiveScheduleDlg.cpp
    GUI-QT/MultColorLED.cpp
    GUI-QT/MultSettingsDlg.cpp
    GUI-QT/Schedule.cpp
    GUI-QT/SlideShowViewer.cpp
    GUI-QT/SoundCardSelMenu.cpp
    GUI-QT/StationsDlg.cpp
    GUI-QT/OpusCodecParams.cpp
    GUI-QT/AACCodecParams.cpp
    GUI-QT/TransmDlg.cpp
    GUI-QT/main.cpp
)

set(DREAM_GUI_FORMS
    GUI-QT/AboutDlgbase.ui
    GUI-QT/AMMainWindow.ui
    GUI-QT/AMSSDlgbase.ui
    GUI-QT/AACCodecParams.ui
    GUI-QT/OpusCodecParams.ui
    GUI-QT/DRMMainWindow.ui
    GUI-QT/EPGDlgbase.ui
    GUI-QT/FMMainWindow.ui
    GUI-QT/GeneralSettingsDlgbase.ui
    GUI-QT/JLViewer.ui
    GUI-QT/LiveScheduleWindow.ui
    GUI-QT/MultSettingsDlgbase.ui
    GUI-QT/SlideShowViewer.ui
    GUI-QT/StationsDlgbase.ui
    GUI-QT/systemevalDlgbase.ui
    GUI-QT/TransmDlgbase.ui
)

# Prefix a list of src/-relative paths with the absolute src dir.
function(dream_prefix_sources out_var)
    set(result "")
    foreach(f IN LISTS ARGN)
        list(APPEND result "${DREAM_SRC}/${f}")
    endforeach()
    set(${out_var} "${result}" PARENT_SCOPE)
endfunction()

# Return the headers that sit next to the given sources (same basename), so
# AUTOMOC sees every Q_OBJECT header that belongs to a compiled .cpp, and
# nothing else (e.g. BWSViewer.h must not be moc'ed when BWS is off).
function(dream_companion_headers out_var)
    set(result "")
    foreach(f IN LISTS ARGN)
        get_filename_component(dir "${f}" DIRECTORY)
        get_filename_component(base "${f}" NAME_WE)
        if(EXISTS "${dir}/${base}.h")
            list(APPEND result "${dir}/${base}.h")
        endif()
    endforeach()
    set(${out_var} "${result}" PARENT_SCOPE)
endfunction()
