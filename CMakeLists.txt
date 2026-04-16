cmake_minimum_required(VERSION 3.15)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON) 

project(Racing)

find_package(SFML 2.5 COMPONENTS system window graphics network audio REQUIRED)

add_executable(main
    src/main.cpp
    src/game.cpp
    src/background.cpp
    src/finish_flag.cpp
    src/projection.cpp
    src/track.cpp
    src/collectible.cpp
    src/draw.cpp
)

target_include_directories(main PRIVATE include)
target_compile_definitions(main PRIVATE ASSET_DIR="${CMAKE_SOURCE_DIR}/assets")
target_link_libraries(main sfml-graphics sfml-audio sfml-window sfml-system)
