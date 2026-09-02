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

set(forbidden_symbols
    "malloc"
    "calloc"
    "realloc"
    "free"
    "_malloc_r"
    "_calloc_r"
    "_realloc_r"
    "_free_r"
    "_sbrk"
    "_sbrk_r"
    "operator new"
    "operator delete"
    "pvPortMalloc"
    "vPortFree"
    "__cxa_atexit"
    "__aeabi_atexit"
)

string(REPLACE "\r\n" "\n" symbol_lines "${symbols}")
foreach(symbol IN LISTS forbidden_symbols)
    string(REGEX MATCH "(^|\n)[^\n]*[ \t]${symbol}(\([^\n]*\))?\n" match "${symbol_lines}")
    if(match)
        message(FATAL_ERROR "Runtime heap symbol found in firmware: ${symbol}")
    endif()
endforeach()

message(STATUS "ELF heap audit passed")
