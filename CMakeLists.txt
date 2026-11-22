cmake_minimum_required(VERSION 3.20)
project(HassleNet)

option(HASSLENET_BUILD_EXAMPLE "Build the example plugin" OFF)

add_library(${PROJECT_NAME} STATIC
    Source/HassleNet/Config.cpp
    Source/HassleNet/HassleNet.cpp
    Source/HassleNet/Hooks.cpp
    Source/HassleNet/Metadata.cpp
    Source/SLikeNet/BitStream.cpp
    Source/SLikeNet/DS_HuffmanEncodingTree.cpp
    Source/SLikeNet/StringCompressor.cpp
    Source/SLikeNet/Types.cpp
)

target_include_directories(${PROJECT_NAME} PUBLIC Include)
target_link_libraries(${PROJECT_NAME} PRIVATE ws2_32)

target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_17)

if (MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4 /EHsc /permissive-)
    target_compile_definitions(${PROJECT_NAME} PRIVATE _CRT_SECURE_NO_WARNINGS)
endif ()

if (HASSLENET_BUILD_EXAMPLE)
    add_subdirectory(Example)
endif ()
