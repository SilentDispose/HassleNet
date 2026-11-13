cmake_minimum_required(VERSION 3.20)
project(HassleNet)

set(HASSLENET_BUILD_EXAMPLE ON)

include(FetchContent)
FetchContent_Declare(
    minhook
    GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
    GIT_TAG v1.3.4
)
FetchContent_MakeAvailable(minhook)

add_library(${PROJECT_NAME} STATIC
    Source/BitStream.cpp
    Source/DS_HuffmanEncodingTree.cpp
    Source/HassleNet.cpp
    Source/Hooks.cpp
    Source/PatternScan.cpp
    Source/SLikeNetTypes.cpp
    Source/StringCompressor.cpp
)

target_include_directories(${PROJECT_NAME} PUBLIC Include)
target_link_libraries(${PROJECT_NAME} PRIVATE minhook ws2_32)

target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_17)

if (MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4 /EHsc /permissive-)
    target_compile_definitions(${PROJECT_NAME} PRIVATE _CRT_SECURE_NO_WARNINGS)
endif ()

if (HASSLENET_BUILD_EXAMPLE)
    add_subdirectory(Example)
endif ()
