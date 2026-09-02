cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME stm32103c8t6_game-box)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME} LANGUAGES C CXX ASM)
message("Build type: " ${CMAKE_BUILD_TYPE})

option(GAMEBOX_WARNINGS_AS_ERRORS "Treat handwritten C++ warnings as errors" ON)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)
include(cmake/FreeRTOS.cmake)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    App/Src/app_bridge.cpp
    App/Src/app/app_module.cpp
    App/Src/app/app_manager.cpp
    App/Src/app/app_task.cpp
    App/Src/app/application.cpp
    App/Src/app/freertos_hooks.cpp
    App/Src/input/button_engine.cpp
    App/Src/input/input_service.cpp
    App/Src/display/canvas.cpp
    App/Src/display/font6x8.cpp
    App/Src/display/ssd1306.cpp
    App/Src/ui/menu_model.cpp
    App/Src/ui/ui_service.cpp
    App/Src/games/snake_game.cpp
    App/Src/games/dino_game.cpp
    App/Src/games/air_raid_game.cpp
    App/Src/games/tetris_game.cpp
    App/Src/games/pong_game.cpp
    App/Src/audio/audio_service.cpp
    App/Src/diagnostics/uart_dma_service.cpp
    App/Src/storage/settings_store.cpp
    App/Src/storage/settings_codec.cpp
    App/Src/storage/calendar_codec.cpp
    App/Src/platform/entropy_source.cpp
    App/Src/platform/rtc_calendar.cpp
    App/Src/platform/buzzer_timer.cpp
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/App/Inc
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    GAMEBOX_FREERTOS=1
)

target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-Wall;-Wextra;-Wpedantic;-Wconversion;-Wsign-conversion;-Wshadow;-Wundef>
    $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<BOOL:${GAMEBOX_WARNINGS_AS_ERRORS}>>:-Werror>
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx
    freertos_kernel
    etl::etl
)

# CubeMX emits placeholder Cortex exception handlers when its bundled (older)
# FreeRTOS middleware is disabled. Rename only those three generated symbols;
# FreeRTOSConfig.h routes the vector table to the current kernel port directly.
set_property(SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/stm32f1xx_it.c
    APPEND PROPERTY COMPILE_DEFINITIONS
        SVC_Handler=CubeMX_SVC_Handler
        PendSV_Handler=CubeMX_PendSV_Handler
        SysTick_Handler=CubeMX_SysTick_Handler
)

add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND}
            -DNM=${CMAKE_NM}
            -DELF=$<TARGET_FILE:${CMAKE_PROJECT_NAME}>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyNoHeap.cmake
    COMMAND ${CMAKE_OBJCOPY} -O ihex
            $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
            ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_PROJECT_NAME}.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary
            $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
            ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_PROJECT_NAME}.bin
    COMMENT "Generating HEX and BIN images"
)
