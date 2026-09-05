cmake_minimum_required(VERSION 3.22)
if(NOT DEFINED NM OR NOT DEFINED ELF)
    message(FATAL_ERROR "VerifyNoHeap.cmake requires NM and ELF")
endif()

execute_process(
    COMMAND "${NM}" -C "${ELF}"
    RESULT_VARIABLE nm_result
    OUTPUT_VARIABLE symbols
    ERROR_VARIABLE nm_error
)
if(NOT nm_result EQUAL 0)
    message(FATAL_ERROR "Unable to inspect ELF symbols: ${nm_error}")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/NoHeapSymbols.cmake")
firmware_verify_no_heap("${symbols}")

message(STATUS "ELF heap audit passed")
