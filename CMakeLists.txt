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
option(GAMEBOX_ENABLE_ANALYZER "Run GCC's path-sensitive static analyzer" OFF)
set(GAMEBOX_OLED_INTERFACE "SPI" CACHE STRING "OLED transport: SPI or I2C")
set_property(CACHE GAMEBOX_OLED_INTERFACE PROPERTY STRINGS SPI I2C)
string(TOUPPER "${GAMEBOX_OLED_INTERFACE}" GAMEBOX_OLED_INTERFACE)
if(GAMEBOX_OLED_INTERFACE STREQUAL "SPI")
    set(GAMEBOX_OLED_SPI 1)
elseif(GAMEBOX_OLED_INTERFACE STREQUAL "I2C")
    set(GAMEBOX_OLED_SPI 0)
else()
    message(FATAL_ERROR "GAMEBOX_OLED_INTERFACE must be SPI or I2C")
endif()
# Directory scope deliberately covers HAL, CubeMX, RTOS, and application code.
add_compile_definitions(GAMEBOX_OLED_SPI=${GAMEBOX_OLED_SPI})
message(STATUS "OLED interface: ${GAMEBOX_OLED_INTERFACE}")
set(GAMEBOX_ARTIFACT_DIR "${CMAKE_SOURCE_DIR}/artifacts/firmware" CACHE PATH
    "Destination for validated firmware packages")

if(GAMEBOX_ENABLE_ANALYZER AND NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    message(FATAL_ERROR "GAMEBOX_ENABLE_ANALYZER requires a GNU C++ compiler")
endif()

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
    App/Src/platform/oled_transport.cpp
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
    # GCC currently mis-models ETL's pointer-pair string_view and intentionally
    # non-returning firmware halt loops. Keep the remaining analyzer families
    # as errors while the ordinary compiler still checks uninitialized values.
    $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<BOOL:${GAMEBOX_ENABLE_ANALYZER}>>:-fanalyzer;-Wno-analyzer-use-of-uninitialized-value;-Wno-analyzer-infinite-loop>
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

include(cmake/FirmwareArtifacts.cmake)
