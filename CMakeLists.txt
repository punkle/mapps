cmake_minimum_required(VERSION 3.15)
project(mapps LANGUAGES C CXX)

include(FetchContent)

# Berry scripting language
FetchContent_Declare(
    berry
    GIT_REPOSITORY "https://github.com/berry-lang/berry"
    GIT_TAG "v0.1.10"
)
FetchContent_MakeAvailable(berry)

# mapps library
file(GLOB MAPPS_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c
)

add_library(mapps ${MAPPS_SOURCES})
target_include_directories(mapps PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(mapps PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(mapps PUBLIC libberry)
