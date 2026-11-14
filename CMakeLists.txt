cmake_minimum_required(VERSION 3.20)
project(HassleNet)

option(HASSLENET_BUILD_EXAMPLE "Build the example plugin" OFF)

include(FetchContent)
FetchContent_Declare(
    kthook
    GIT_REPOSITORY https://github.com/SilentDispose/kthook.git
    GIT_TAG 4331456543f408c42122ced22144cfb698031266
)
FetchContent_MakeAvailable(kthook)

add_library(${PROJECT_NAME} STATIC
    Source/BitStream.cpp
    Source/DS_HuffmanEncodingTree.cpp
    Source/HassleNet.cpp
    Source/Hooks.cpp
    Source/SLikeNetTypes.cpp
    Source/StringCompressor.cpp
)

target_include_directories(${PROJECT_NAME} PUBLIC Include)
target_link_libraries(${PROJECT_NAME}
    PUBLIC kthook
    PRIVATE ws2_32
)

target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_17)

if (MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4 /EHsc /permissive-)
    target_compile_definitions(${PROJECT_NAME} PRIVATE _CRT_SECURE_NO_WARNINGS)
endif ()

if (HASSLENET_BUILD_EXAMPLE)
    add_subdirectory(Example)
endif ()
