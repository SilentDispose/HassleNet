project(HassleNetExample)

add_library(${PROJECT_NAME} SHARED Main.cpp)
target_link_libraries(${PROJECT_NAME} PRIVATE HassleNet)
set_target_properties(${PROJECT_NAME} PROPERTIES SUFFIX ".asi")
