cmake_minimum_required(VERSION 3.20)
project(Ghostband LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# The engine is deliberately dependency-free and host-agnostic: no JUCE, no
# third-party libraries. The same source files become the VST3's engine later,
# so nothing in here may assume a plugin host, an audio thread, or a GUI.
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
