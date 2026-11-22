cmake_minimum_required(VERSION 3.22)

# Sample Forge · MBStudio91 — da un brano a un pacchetto di campioni (Standalone / VST3 / AU), nativo C++
project(SampleForge VERSION 2.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(APPLE)
    # Binario universale: Mac Intel (es. MacBook Pro 2019, Mac mini) e Apple Silicon
    if(NOT CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64" CACHE STRING "" FORCE)
    endif()
    if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
        set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "" FORCE)
    endif()
endif()

# Motore DSP senza dipendenze e suo banco di prova a riga di comando
add_library(sf_engine STATIC Source/Engine/SampleForgeEngine.cpp)
target_include_directories(sf_engine PUBLIC Source/Engine)
set_target_properties(sf_engine PROPERTIES POSITION_INDEPENDENT_CODE ON)

option(SF_ENGINE_ONLY "Compila solo il motore e il suo test (senza JUCE)" OFF)
add_executable(sf_engine_cli tests/engine_cli.cpp)
target_link_libraries(sf_engine_cli PRIVATE sf_engine)
if(SF_ENGINE_ONLY)
    return()
endif()

# JUCE: cartella locale (-DJUCE_DIR=...) oppure download automatico della versione fissata
set(JUCE_DIR "" CACHE PATH "Percorso di una copia locale di JUCE")
if(JUCE_DIR AND EXISTS "${JUCE_DIR}/CMakeLists.txt")
    add_subdirectory("${JUCE_DIR}" JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 9.0.5
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

set(SF_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND SF_FORMATS AU)
endif()

juce_add_plugin(SampleForge
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/icons/sample-forge-1024.png"
    COMPANY_NAME "MBStudio91"
    COMPANY_WEBSITE "https://mbstudio91.fun"
    BUNDLE_ID "fun.mbstudio91.sampleforge"
    PRODUCT_NAME "Sample Forge"
    VERSION ${PROJECT_VERSION}
    PLUGIN_MANUFACTURER_CODE Mbst
    PLUGIN_CODE Mbsf
    FORMATS ${SF_FORMATS}
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Instrument Sampler
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    HARDENED_RUNTIME_ENABLED TRUE)

include(MBStudioUI/MBStudioUI.cmake)
mbstudio_ui_add(SampleForge)

target_sources(SampleForge PRIVATE
    Source/ControllerLeds.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(SampleForge PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_USE_FLAC=1
    JUCE_USE_OGGVORBIS=1
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(SampleForge
    PRIVATE
        sf_engine
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Prova del processore senza interfaccia (caricamento, pad, MIDI, PANIC, export)
juce_add_console_app(SampleForgePluginTest PRODUCT_NAME "sf_plugin_test")
target_sources(SampleForgePluginTest PRIVATE tests/plugin_test.cpp Source/ControllerLeds.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
mbstudio_ui_add(SampleForgePluginTest)
target_compile_definitions(SampleForgePluginTest PRIVATE
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_MP3AUDIOFORMAT=1 JUCE_USE_FLAC=1 JUCE_USE_OGGVORBIS=1
    JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1 JucePlugin_Name="Sample Forge")
target_link_libraries(SampleForgePluginTest PRIVATE
    sf_engine juce::juce_audio_utils juce::juce_audio_formats juce::juce_audio_processors juce::juce_gui_extra
    juce::juce_recommended_config_flags)

# Test della mappatura dei controller (nessun hardware richiesto)
juce_add_console_app(sf_controller_test PRODUCT_NAME "sf_controller_test")
target_sources(sf_controller_test PRIVATE tests/controller_test.cpp Source/ControllerLeds.cpp)
target_link_libraries(sf_controller_test PRIVATE
    juce::juce_audio_devices juce::juce_events juce::juce_core juce::juce_recommended_config_flags)
