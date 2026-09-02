set(FREERTOS_KERNEL_DIR "${CMAKE_SOURCE_DIR}/ThirdParty/FreeRTOS-Kernel")

add_library(freertos_kernel STATIC
    ${FREERTOS_KERNEL_DIR}/tasks.c
    ${FREERTOS_KERNEL_DIR}/queue.c
    ${FREERTOS_KERNEL_DIR}/list.c
    ${FREERTOS_KERNEL_DIR}/portable/GCC/ARM_CM3/port.c
)

target_include_directories(freertos_kernel
    PUBLIC
        ${FREERTOS_KERNEL_DIR}/include
        ${FREERTOS_KERNEL_DIR}/portable/GCC/ARM_CM3
        ${CMAKE_SOURCE_DIR}/App/Config
)

target_link_libraries(freertos_kernel PUBLIC stm32cubemx)
target_compile_definitions(freertos_kernel PRIVATE projCOVERAGE_TEST=0)

add_library(etl INTERFACE)
add_library(etl::etl ALIAS etl)
target_include_directories(etl SYSTEM INTERFACE
    ${CMAKE_SOURCE_DIR}/ThirdParty/etl/include
)
target_compile_definitions(etl INTERFACE
    ETL_NO_STL
    ETL_MINIMAL_ERRORS
    ETL_FORCE_STD_INITIALIZER_LIST
)
