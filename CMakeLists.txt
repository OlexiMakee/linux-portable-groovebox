cmake_minimum_required(VERSION 3.20)
project(xu10_groovebox VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(xu10_groovebox
    src/main.cpp
    src/arpeggiator.cpp
    src/audio_engine.cpp
    src/pattern_manager.cpp
    src/sampler_engine.cpp
    src/device_profile.cpp
    src/hw_monitor.cpp
    src/input_manager.cpp
    src/midi_engine.cpp
    src/net_audio_streamer.cpp
    src/ui/canvas.cpp
    src/ui/params.cpp
    src/ui/widgets.cpp
    src/ui/views.cpp
    src/ui/actions.cpp
    src/ui/keymap.cpp
    src/ui/layout.cpp
    src/ui/ui_shell.cpp
)

target_include_directories(xu10_groovebox PRIVATE src)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(SYSROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/sysroot")
    target_include_directories(xu10_groovebox SYSTEM PRIVATE
        "${SYSROOT_DIR}/usr/include"
        "${SYSROOT_DIR}/usr/include/SDL2"
    )
    target_link_directories(xu10_groovebox PRIVATE
        "${SYSROOT_DIR}/usr/lib"
    )
    target_link_options(xu10_groovebox PRIVATE
        "-Wl,--allow-shlib-undefined"
        "-Wl,-rpath,/usr/lib"
    )
    target_link_libraries(xu10_groovebox PRIVATE
        SDL2
        asound
        pthread
        m
        dl
    )
else()
    find_package(SDL2 REQUIRED)
    target_include_directories(xu10_groovebox SYSTEM PRIVATE
        ${SDL2_INCLUDE_DIRS}
        "/opt/homebrew/include"
        "/opt/homebrew/include/SDL2"
    )
    target_link_directories(xu10_groovebox PRIVATE "/opt/homebrew/lib")
    target_link_libraries(xu10_groovebox PRIVATE SDL2)

    add_executable(render_tour
        tools/render_tour.cpp
        src/arpeggiator.cpp
        src/audio_engine.cpp
        src/pattern_manager.cpp
        src/sampler_engine.cpp
        src/device_profile.cpp
        src/hw_monitor.cpp
        src/input_manager.cpp
        src/midi_engine.cpp
        src/net_audio_streamer.cpp
        src/ui/canvas.cpp
        src/ui/params.cpp
        src/ui/widgets.cpp
        src/ui/views.cpp
        src/ui/actions.cpp
        src/ui/keymap.cpp
        src/ui/layout.cpp
        src/ui/ui_shell.cpp
    )
    target_include_directories(render_tour PRIVATE src)
    target_include_directories(render_tour SYSTEM PRIVATE
        ${SDL2_INCLUDE_DIRS}
        "/opt/homebrew/include"
        "/opt/homebrew/include/SDL2"
    )
    target_link_directories(render_tour PRIVATE "/opt/homebrew/lib")
    target_link_libraries(render_tour PRIVATE SDL2)
endif()

target_compile_options(xu10_groovebox PRIVATE
    -O2
    -Wall
    -Wextra
)
