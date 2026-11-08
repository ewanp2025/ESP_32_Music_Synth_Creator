cmake_minimum_required(VERSION 3.16)
project(Esp32SynthStudio LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 COMPONENTS Core Gui Widgets Multimedia REQUIRED)

add_executable(Esp32SynthStudio
    main_studio.cpp
    midireader.cpp
    midireader.h
    synth_sdk_modules.h
    synth_studio_gui.h
    xpressive_parser.h
    effect.h
)

target_link_libraries(Esp32SynthStudio PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Multimedia
)

if(MSVC)
    target_compile_options(Esp32SynthStudio PRIVATE /utf-8)
endif()

if(UNIX AND NOT APPLE)
    target_compile_options(Esp32SynthStudio PRIVATE -O3 -ffast-math)
    target_link_libraries(Esp32SynthStudio PRIVATE asound pthread)
elseif(WIN32)
    target_link_libraries(Esp32SynthStudio PRIVATE winmm)
endif()