cmake_minimum_required(VERSION 3.15)

project(Racing C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
option(RACING_ENABLE_LTO "Enable link-time optimization when the toolchain supports it" ON)

find_package(PkgConfig)

add_library(racing_core
    src/game.c
)

target_include_directories(racing_core PUBLIC include)
target_compile_options(racing_core PRIVATE -Wall -Wextra -Wpedantic -O2)
target_link_libraries(racing_core PUBLIC m)

foreach(racing_define
    WIN_WIDTH
    WIN_HEIGHT
    ROAD_WIDTH
    SEG_LENGTH
    ROAD_COUNT
    VIEW_DISTANCE
    COLLECTIBLE_COUNT
    RACING_TARGET_FRAME_MS
    RACING_LVGL_FRAME_MS
    RACING_PRINT_FRAME_TIME
)
    if(DEFINED ${racing_define})
        target_compile_definitions(racing_core PUBLIC ${racing_define}=${${racing_define}})
    endif()
endforeach()

if(PkgConfig_FOUND)
    pkg_check_modules(SDL2 sdl2)
    pkg_check_modules(SDL2_IMAGE SDL2_image)
    pkg_check_modules(SDL2_TTF SDL2_ttf)
endif()

add_executable(main src/main.c)
target_link_libraries(main PRIVATE racing_core)

if(SDL2_FOUND AND SDL2_IMAGE_FOUND AND SDL2_TTF_FOUND)
    add_executable(racing_desktop
        src/desktop_main.c
        src/render_sdl.c
    )
    target_compile_definitions(racing_desktop PRIVATE ASSET_DIR="${CMAKE_SOURCE_DIR}/assets")
    target_include_directories(racing_desktop PRIVATE ${SDL2_INCLUDE_DIRS} ${SDL2_IMAGE_INCLUDE_DIRS} ${SDL2_TTF_INCLUDE_DIRS})
    target_compile_options(racing_desktop PRIVATE ${SDL2_CFLAGS_OTHER} ${SDL2_IMAGE_CFLAGS_OTHER} ${SDL2_TTF_CFLAGS_OTHER})
    target_link_libraries(racing_desktop PRIVATE racing_core ${SDL2_IMAGE_LIBRARIES} ${SDL2_TTF_LIBRARIES} ${SDL2_LIBRARIES})
endif()

if(RACING_ENABLE_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT racing_lto_supported OUTPUT racing_lto_error)
    if(racing_lto_supported)
        set_property(TARGET racing_core PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
        set_property(TARGET main PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
        if(TARGET racing_desktop)
            set_property(TARGET racing_desktop PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
        endif()
    endif()
endif()
