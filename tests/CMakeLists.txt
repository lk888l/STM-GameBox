cmake_minimum_required(VERSION 3.22)
project(gamebox_host_tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

enable_testing()

if(MSVC)
    add_compile_options(/W4 /WX /permissive-)
else()
    add_compile_options(
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror
    )
endif()

add_library(gamebox_pure STATIC
    ../App/Src/input/button_engine.cpp
    ../App/Src/games/snake_game.cpp
    ../App/Src/games/dino_game.cpp
    ../App/Src/games/air_raid_game.cpp
    ../App/Src/games/tetris_game.cpp
    ../App/Src/games/pong_game.cpp
    ../App/Src/display/canvas.cpp
    ../App/Src/display/font6x8.cpp
    ../App/Src/storage/settings_codec.cpp
    ../App/Src/storage/calendar_codec.cpp
    ../App/Src/ui/menu_model.cpp
    ../App/Src/app/app_module.cpp
    ../App/Src/app/app_manager.cpp
)

target_include_directories(gamebox_pure PUBLIC
    ../App/Inc
    ../ThirdParty/etl/include
)
target_compile_definitions(gamebox_pure PUBLIC
    ETL_NO_STL
    ETL_MINIMAL_ERRORS
    ETL_FORCE_STD_INITIALIZER_LIST
)

add_executable(gamebox_host_tests test_main.cpp)
target_link_libraries(gamebox_host_tests PRIVATE gamebox_pure)
add_test(NAME gamebox_host_tests COMMAND gamebox_host_tests)

add_executable(oled_preview oled_preview.cpp)
target_link_libraries(oled_preview PRIVATE gamebox_pure)
