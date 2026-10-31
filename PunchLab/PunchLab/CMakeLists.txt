cmake_minimum_required(VERSION 3.22)

project(PunchLab VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Universal binary on macOS (Apple Silicon + Intel)
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "" FORCE)
endif()

# ---------------------------------------------------------------------------
# JUCE: берётся из ./JUCE (git submodule / ручная копия) или скачивается сам
# ---------------------------------------------------------------------------
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
    add_subdirectory(JUCE)
else()
    include(FetchContent)
    # Скачивается архивом — Git не нужен
    FetchContent_Declare(JUCE
        URL https://github.com/juce-framework/JUCE/archive/refs/tags/8.0.9.tar.gz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

set(PUNCHLAB_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND PUNCHLAB_FORMATS AU)
endif()

juce_add_plugin(PunchLab
    COMPANY_NAME                "PunchLab Audio"
    COMPANY_WEBSITE             ""
    BUNDLE_ID                   "com.punchlab.punchlab"
    PLUGIN_MANUFACTURER_CODE    Pnch
    PLUGIN_CODE                 PnLb
    FORMATS                     ${PUNCHLAB_FORMATS}
    PRODUCT_NAME                "PunchLab"
    DESCRIPTION                 "Multi-module drum and percussion processor"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES             Fx Dynamics Distortion Reverb
    AU_MAIN_TYPE                kAudioUnitType_Effect
    COPY_PLUGIN_AFTER_BUILD     FALSE)

target_sources(PunchLab PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Params.cpp
    Source/PresetManager.cpp
    Source/FactoryPresets.cpp
    Source/gui/Widgets.cpp)

target_include_directories(PunchLab PRIVATE Source)

target_compile_definitions(PunchLab PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(PunchLab
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Необязательный офлайн-тест DSP (рендер синтетического лупа через все пресеты)
# cmake -DPUNCHLAB_BUILD_TESTS=ON ..
# ---------------------------------------------------------------------------
option(PUNCHLAB_BUILD_TESTS "Build offline DSP smoke test" OFF)
if(PUNCHLAB_BUILD_TESTS)
    juce_add_console_app(PunchLabTest PRODUCT_NAME "PunchLabTest")
    target_sources(PunchLabTest PRIVATE
        Tests/SmokeTest.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/Params.cpp
        Source/PresetManager.cpp
        Source/FactoryPresets.cpp
        Source/gui/Widgets.cpp)
    target_include_directories(PunchLabTest PRIVATE Source)
    target_compile_definitions(PunchLabTest PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JucePlugin_Build_Standalone=0
        PUNCHLAB_TEST_BUILD=1 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(PunchLabTest PRIVATE
        juce::juce_audio_utils juce::juce_dsp
        juce::juce_recommended_config_flags)
endif()
