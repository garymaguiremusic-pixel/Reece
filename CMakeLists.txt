cmake_minimum_required(VERSION 3.22)

project(Reese VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# ------------------------------------------------------------------------------
# Options
option(REESE_BUILD_STANDALONE "Also build a standalone app for testing"     OFF)
option(REESE_BUILD_TESTS      "Build the unit test console app"            ON)
option(REESE_COPY_AFTER_BUILD "Install the plugin into the user plugin folder after building" OFF)
set(JUCE_DIR "" CACHE PATH "Path to a local JUCE checkout (leave empty to download JUCE 9.0.4)")

# ------------------------------------------------------------------------------
# JUCE
if(JUCE_DIR)
    add_subdirectory(${JUCE_DIR} ${CMAKE_BINARY_DIR}/JUCE EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        9.0.4
        GIT_SHALLOW    ON)
    FetchContent_MakeAvailable(JUCE)
endif()

# ------------------------------------------------------------------------------
# Sources shared by the plugin and the unit tests
set(REESE_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/DSP/SynthEngine.cpp
    Source/DSP/Saturation.cpp
    Source/UI/CustomLookAndFeel.cpp
    Source/UI/VirusComponents.cpp)

set(REESE_DEFINITIONS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

set(REESE_MODULES
    juce::juce_audio_utils
    juce::juce_dsp)

# ------------------------------------------------------------------------------
# Plugin (VST3 instrument; Windows is the target for now)
set(REESE_FORMATS VST3)
if(REESE_BUILD_STANDALONE)
    list(APPEND REESE_FORMATS Standalone)
endif()

# The VST3 manifest helper has to run on the build machine, which isn't possible
# when cross-compiling.
if(CMAKE_CROSSCOMPILING)
    set(REESE_VST3_MANIFEST FALSE)
else()
    set(REESE_VST3_MANIFEST TRUE)
endif()

# Same manufacturer as Virus Delay and KickDrum, so hosts list them under one vendor.
juce_add_plugin(Reese
    PRODUCT_NAME                "Reese"
    COMPANY_NAME                "VirusDelay Project"
    BUNDLE_ID                   com.virusdelayproject.reese
    PLUGIN_MANUFACTURER_CODE    Vdpj
    PLUGIN_CODE                 Rees
    FORMATS                     ${REESE_FORMATS}
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES             Instrument Synth
    VST3_AUTO_MANIFEST          ${REESE_VST3_MANIFEST}
    COPY_PLUGIN_AFTER_BUILD     ${REESE_COPY_AFTER_BUILD})

target_sources(Reese PRIVATE ${REESE_SOURCES})
target_include_directories(Reese PRIVATE Source)
target_compile_definitions(Reese PUBLIC ${REESE_DEFINITIONS})
target_link_libraries(Reese
    PRIVATE
        ${REESE_MODULES}
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Windows release builds also write debug symbols to a separate .pdb (never shipped
# inside the plugin), so crash dumps from hosts can be turned into readable stacks.
if(MSVC)
    foreach(target Reese Reese_VST3 Reese_Standalone)
        if(TARGET ${target})
            target_compile_options(${target} PRIVATE "$<$<CONFIG:Release>:/Zi>")
            target_link_options(${target} PRIVATE "$<$<CONFIG:Release>:/DEBUG;/OPT:REF;/OPT:ICF>")
        endif()
    endforeach()
endif()

# ------------------------------------------------------------------------------
# Unit tests: a console app that compiles the same sources plus Tests/*.cpp
if(REESE_BUILD_TESTS)
    enable_testing()

    juce_add_console_app(ReeseTests PRODUCT_NAME "ReeseTests")

    file(GLOB REESE_TEST_SOURCES CONFIGURE_DEPENDS Tests/*.cpp)
    target_sources(ReeseTests PRIVATE ${REESE_SOURCES} ${REESE_TEST_SOURCES})
    target_include_directories(ReeseTests PRIVATE Source)
    target_compile_definitions(ReeseTests PRIVATE ${REESE_DEFINITIONS} REESE_TESTS=1 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(ReeseTests
        PRIVATE
            ${REESE_MODULES}
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

    add_test(NAME ReeseTests COMMAND ReeseTests)
endif()
