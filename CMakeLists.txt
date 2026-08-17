cmake_minimum_required(VERSION 3.19)

if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
    # Manifest is embedded via app.manifest.rc; mt.exe fails on some SDK installs.
    set(CMAKE_MT "${CMAKE_CURRENT_SOURCE_DIR}/tools/mt_ok.bat" CACHE FILEPATH "" FORCE)
endif()

project(CurvioEQ LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets Network)

qt_standard_project_setup()

qt_add_executable(CurvioEQ
    WIN32 MACOSX_BUNDLE
    app.manifest.rc
    resources/app.qrc
    main.cpp
    mainwindow.cpp
    mainwindow.h
    mainwindow.ui
    audio/audiosessionenumerator.cpp
    audio/audiosessionenumerator.h
    audio/processloopbackcapture.cpp
    audio/processloopbackcapture.h
    audio/wasapirenderer.cpp
    audio/wasapirenderer.h
    audio/eqprocessor.cpp
    audio/eqprocessor.h
    audio/surroundprocessor.cpp
    audio/surroundprocessor.h
    audio/eqaudiosession.cpp
    audio/eqaudiosession.h
    audio/audioprocessor.h
    audio/audiopipeline.cpp
    audio/audiopipeline.h
    audio/resampler.cpp
    audio/resampler.h
    audio/spscringbuffer.h
    audio/clocksync.h
    audio/mixlimiter.cpp
    audio/mixlimiter.h
    audio/audiothreadutils.h
    audio/dspstatus.cpp
    audio/dspstatus.h
    audio/audioengine.cpp
    audio/audioengine.h
    audio/audiopolicyrouter.cpp
    audio/audiopolicyrouter.h
    audio/audioendpointvolume.cpp
    audio/audioendpointvolume.h
    audio/audiosessionvolume.cpp
    audio/audiosessionvolume.h
    audio/log.h
    ui/appiconprovider.cpp
    ui/appiconprovider.h
    ui/appsessiondelegate.cpp
    ui/appsessiondelegate.h
    ui/presetstore.cpp
    ui/presetstore.h
    ui/settingsstore.cpp
    ui/settingsstore.h
    ui/settingsdialog.cpp
    ui/settingsdialog.h
    ui/keybindsdialog.cpp
    ui/keybindsdialog.h
    ui/globalhotkeymanager.cpp
    ui/globalhotkeymanager.h
    ui/appconstants.h
    ui/apppaths.cpp
    ui/apppaths.h
    ui/audiodeviceresolver.cpp
    ui/audiodeviceresolver.h
    ui/singleinstanceserver.cpp
    ui/singleinstanceserver.h
    ui/presetpanelcontroller.cpp
    ui/presetpanelcontroller.h
    ui/sessionlistcontroller.cpp
    ui/sessionlistcontroller.h
    ui/eqsessionmanager.cpp
    ui/eqsessionmanager.h
    ui/eqcolorpalette.cpp
    ui/eqcolorpalette.h
    ui/traycontroller.cpp
    ui/traycontroller.h
    ui/spectrumanalyzer.cpp
    ui/spectrumanalyzer.h
    ui/spectrumwidget.cpp
    ui/spectrumwidget.h
)

target_compile_definitions(CurvioEQ PRIVATE
    UNICODE
    _UNICODE
    WINVER=0x0A00
    _WIN32_WINNT=0x0A00
    NOMINMAX
)

target_include_directories(CurvioEQ PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(CurvioEQ
    PRIVATE
        Qt::Core
        Qt::Widgets
        Qt::Network
        ole32
        mmdevapi
        uuid
        avrt
        runtimeobject
)

install(TARGETS CurvioEQ
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

qt_generate_deploy_app_script(
    TARGET CurvioEQ
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})

enable_testing()

add_executable(CurvioEQ_DspVerify tools/dsp_verify.cpp
    audio/dspstatus.cpp
    audio/eqprocessor.cpp
    audio/audiopipeline.cpp
    audio/resampler.cpp
)
target_include_directories(CurvioEQ_DspVerify PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(CurvioEQ_DspVerify PRIVATE NOMINMAX)

add_dependencies(CurvioEQ CurvioEQ_DspVerify)

add_custom_command(TARGET CurvioEQ POST_BUILD
    COMMAND "$<TARGET_FILE:CurvioEQ_DspVerify>"
    COMMENT "Verifying DSP engine (EQ, resampler, ring buffer)..."
    VERBATIM)

add_test(NAME DspVerify COMMAND CurvioEQ_DspVerify)

install(TARGETS CurvioEQ_DspVerify
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
