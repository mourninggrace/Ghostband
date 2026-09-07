cmake_minimum_required(VERSION 3.20)
# C is enabled because JUCE requires a C compiler for the targets that depend
# on it, even though nothing Ghostband writes is C.
project(Ghostband VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Link the C++ runtime statically. Without this the plugin depends on
# MSVCP140.dll and VCRUNTIME140.dll, so on any machine that has never had a
# Visual C++ redistributable installed it does not fail gracefully - the host
# simply cannot load it, usually with no useful message. A plugin someone copies
# onto a strange machine has to be self-contained.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# ==============================================================================
# Engine
#
# Deliberately dependency-free and host-agnostic: no JUCE, no third-party
# libraries. These same source files are what the VST3 runs, so nothing in here
# may assume a plugin host, an audio thread, or a GUI.
# ==============================================================================
add_library(ghostband_engine STATIC
    engine/src/Json.cpp
    engine/src/MidiFile.cpp
    engine/src/Music.cpp
    engine/src/SongPlan.cpp
    engine/src/Profile.cpp
    engine/src/Groove.cpp
    engine/src/Render.cpp
)

target_include_directories(ghostband_engine PUBLIC engine/include)

if (MSVC)
    target_compile_options(ghostband_engine PRIVATE /W4 /utf-8 /permissive-)
else()
    target_compile_options(ghostband_engine PRIVATE -Wall -Wextra)
endif()

# ==============================================================================
# CLI - the test harness that lets the engine be judged by ear without a host
# ==============================================================================
add_executable(ghostband cli/main.cpp)
target_link_libraries(ghostband PRIVATE ghostband_engine)

if (MSVC)
    target_compile_options(ghostband PRIVATE /W4 /utf-8 /permissive-)
endif()

set_target_properties(ghostband PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY         "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG   "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
)

# ==============================================================================
# VST3
#
# JUCE is not vendored here yet. Point GHOSTBAND_JUCE_PATH at any 8.x checkout;
# it defaults to the one Polygraph already uses so this builds with no network
# access. Swapping to a submodule later is a one-line change.
# ==============================================================================
set(GHOSTBAND_JUCE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/external/JUCE"
    CACHE PATH "Path to a JUCE 8.x checkout")

# JUCE is a pinned submodule. It used to fall back to a hardcoded path into an
# unrelated project on one particular machine, which meant this repository built
# nowhere else - and failed with a confusing error when it did.
if (NOT EXISTS "${GHOSTBAND_JUCE_PATH}/CMakeLists.txt")
    message(FATAL_ERROR
        "JUCE is missing from ${GHOSTBAND_JUCE_PATH}.\n"
        "Run:  git submodule update --init --recursive\n"
        "Or point -DGHOSTBAND_JUCE_PATH=<path> at an existing JUCE 8.x checkout.")
endif()

if (EXISTS "${GHOSTBAND_JUCE_PATH}/CMakeLists.txt")
    message(STATUS "Ghostband: building the VST3 against JUCE at ${GHOSTBAND_JUCE_PATH}")
    add_subdirectory("${GHOSTBAND_JUCE_PATH}" "${CMAKE_BINARY_DIR}/juce" EXCLUDE_FROM_ALL)

    juce_add_plugin(GhostbandPlugin
        PRODUCT_NAME                "Ghostband"
        COMPANY_NAME                "Ghostband"
        PLUGIN_MANUFACTURER_CODE    Gbnd
        PLUGIN_CODE                 Gbd1
        FORMATS                     VST3
        VST3_CATEGORIES             Tools
        IS_SYNTH                    FALSE
        IS_MIDI_EFFECT              FALSE
        NEEDS_MIDI_INPUT            TRUE
        NEEDS_MIDI_OUTPUT           TRUE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD     FALSE
    )

    target_sources(GhostbandPlugin PRIVATE
        plugin/PluginProcessor.cpp
        plugin/PluginEditor.cpp
        plugin/GhostbandLookAndFeel.cpp
    )

    target_compile_definitions(GhostbandPlugin PUBLIC
        GHOSTBAND_VERSION="${PROJECT_VERSION}"
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0

        # Dragging the editor onto a second monitor with different scaling left
        # every control dead: the window drew in the right place but hit-testing
        # was computed against the wrong scale factor, so clicks landed nowhere.
        # That is a plugin declaring itself per-monitor DPI aware inside a host
        # that is not. The host owns the window, so the host should own the
        # scaling - turning this off makes JUCE defer to it instead of applying
        # a second, conflicting transform of its own.
        JUCE_WIN_PER_MONITOR_DPI_AWARE=0
    )

    target_link_libraries(GhostbandPlugin PRIVATE
        ghostband_engine
        juce::juce_audio_basics
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_core
        juce::juce_data_structures
        juce::juce_events
        juce::juce_graphics
        juce::juce_gui_basics
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
    )
    # ==========================================================================
    # Plugin harness
    #
    # Instantiates the processor with a fake playhead and checks what actually
    # comes out of processBlock. Links the same plugin sources but not the
    # plugin client, so it runs as a plain console app with no host involved.
    # ==========================================================================
    juce_add_console_app(ghostband_plugin_test PRODUCT_NAME "ghostband_plugin_test")

    target_sources(ghostband_plugin_test PRIVATE
        test/PluginHarness.cpp
        plugin/PluginProcessor.cpp
        plugin/PluginEditor.cpp
        plugin/GhostbandLookAndFeel.cpp
    )

    target_include_directories(ghostband_plugin_test PRIVATE plugin)

    target_compile_definitions(ghostband_plugin_test PRIVATE
        GHOSTBAND_VERSION="${PROJECT_VERSION}"
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_STANDALONE_APPLICATION=1
        JUCE_DISPLAY_SPLASH_SCREEN=0
    )

    target_link_libraries(ghostband_plugin_test PRIVATE
        ghostband_engine
        juce::juce_audio_basics
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_core
        juce::juce_data_structures
        juce::juce_events
        juce::juce_graphics
        juce::juce_gui_basics
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
    )
    # ==========================================================================
    # Plugin probe
    #
    # Loads a target VST3 headlessly and measures what each note actually
    # produces. Driver profiles were being written from documentation and guess
    # work and then handed to the user to verify by ear, which is not a
    # reasonable thing to ask of anyone. This works the map out by measurement
    # instead.
    # ==========================================================================
    juce_add_console_app(ghostband_probe PRODUCT_NAME "ghostband_probe")

    target_sources(ghostband_probe PRIVATE tools/ProbePlugin.cpp)

    target_compile_definitions(ghostband_probe PRIVATE
        GHOSTBAND_VERSION="${PROJECT_VERSION}"
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_STANDALONE_APPLICATION=1
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_PLUGINHOST_VST3=1
        JUCE_PLUGINHOST_VST=0
        JUCE_PLUGINHOST_AU=0

        # Needed for runDispatchLoopUntil. Sampled instruments load their
        # content on the message thread, and a console app that never runs that
        # loop measures every note as silence.
        JUCE_MODAL_LOOPS_PERMITTED=1
    )

    target_link_libraries(ghostband_probe PRIVATE
        juce::juce_audio_basics
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_core
        juce::juce_data_structures
        juce::juce_events
        juce::juce_graphics
        juce::juce_gui_basics
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
    )
else()
    message(WARNING "Ghostband: JUCE not found, building the CLI only. "
                    "Set -DGHOSTBAND_JUCE_PATH=<path to JUCE 8.x> to build the VST3.")
endif()
