include_guard(GLOBAL)

set(CMAKE_SYSTEM_NAME               Generic)
set(CMAKE_SYSTEM_PROCESSOR          arm)

set(CMAKE_C_COMPILER_ID GNU)
set(CMAKE_CXX_COMPILER_ID GNU)

# Some default GCC settings
# arm-none-eabi- must be part of path environment
set(TOOLCHAIN_PREFIX                arm-none-eabi-)

set(CMAKE_C_COMPILER                ${TOOLCHAIN_PREFIX}gcc)
set(CMAKE_ASM_COMPILER              ${CMAKE_C_COMPILER})
set(CMAKE_CXX_COMPILER              ${TOOLCHAIN_PREFIX}g++)
set(CMAKE_LINKER                    ${TOOLCHAIN_PREFIX}g++)
set(CMAKE_OBJCOPY                   ${TOOLCHAIN_PREFIX}objcopy)
set(CMAKE_SIZE                      ${TOOLCHAIN_PREFIX}size)
set(CMAKE_AR                        ${TOOLCHAIN_PREFIX}gcc-ar)
set(CMAKE_RANLIB                    ${TOOLCHAIN_PREFIX}gcc-ranlib)
set(CMAKE_NM                        ${TOOLCHAIN_PREFIX}gcc-nm)

set(CMAKE_EXECUTABLE_SUFFIX_ASM     ".elf")
set(CMAKE_EXECUTABLE_SUFFIX_C       ".elf")
set(CMAKE_EXECUTABLE_SUFFIX_CXX     ".elf")

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# MCU specific flags. Use *_INIT variables so repeated CMake configure passes do
# not append a second copy of every option to the cache.
set(TARGET_FLAGS "-mcpu=cortex-m3")
set(COMMON_COMPILE_FLAGS
    "${TARGET_FLAGS} -fdata-sections -ffunction-sections -fstack-usage")

set(CMAKE_C_FLAGS_INIT "${COMMON_COMPILE_FLAGS} -Wall")
set(CMAKE_CXX_FLAGS_INIT
    "${COMMON_COMPILE_FLAGS} -fno-rtti -fno-exceptions -fno-threadsafe-statics")
set(CMAKE_ASM_FLAGS_INIT "${TARGET_FLAGS} -x assembler-with-cpp -MMD -MP")

# The cyclomatic-complexity parameter must be defined for the Cyclomatic complexity feature in STM32CubeIDE to work.
# However, most GCC toolchains do not support this option, which causes a compilation error; for this reason, the feature is disabled by default.
# set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fcyclomatic-complexity")

set(CMAKE_C_FLAGS_DEBUG "-Os -g3 -flto" CACHE STRING "C Debug flags" FORCE)
set(CMAKE_C_FLAGS_RELEASE "-Os -g0 -flto -DNDEBUG" CACHE STRING "C Release flags" FORCE)
set(CMAKE_CXX_FLAGS_DEBUG "-Os -g3 -flto" CACHE STRING "C++ Debug flags" FORCE)
set(CMAKE_CXX_FLAGS_RELEASE "-Os -g0 -flto -DNDEBUG" CACHE STRING "C++ Release flags" FORCE)

set(CMAKE_EXE_LINKER_FLAGS_INIT
    "${TARGET_FLAGS} -T \"${CMAKE_SOURCE_DIR}/STM32F103XX_FLASH.ld\" --specs=nano.specs -Wl,-Map=${CMAKE_PROJECT_NAME}.map -Wl,--gc-sections -Wl,--print-memory-usage")
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "-flto" CACHE STRING "Release linker flags" FORCE)
set(CMAKE_EXE_LINKER_FLAGS_DEBUG "-flto" CACHE STRING "Debug linker flags" FORCE)
set(TOOLCHAIN_LINK_LIBRARIES "m")
