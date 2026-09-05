# Parse nm's symbol column before comparing names. Matching the complete nm
# output with an unanchored prefix wrongly rejects LTO's freertos_hooks.cpp
# debug symbols and can miss the final line or C++ array allocation operators.
function(firmware_verify_no_heap symbols)
    set(forbidden_symbols malloc calloc realloc free _malloc_r _calloc_r
        _realloc_r _free_r _sbrk _sbrk_r pvPortMalloc vPortFree __cxa_atexit
        __aeabi_atexit)
    string(REPLACE "\r" "" lines "${symbols}")
    string(REPLACE "\n" ";" lines "${lines}")
    foreach(line IN LISTS lines)
        if(NOT line MATCHES "^[ \t]*([0-9a-fA-F]+[ \t]+)?[a-zA-Z?][ \t]+")
            continue()
        endif()
        string(REGEX REPLACE "^[ \t]*([0-9a-fA-F]+[ \t]+)?[a-zA-Z?][ \t]+" "" name "${line}")
        if(name IN_LIST forbidden_symbols)
            message(FATAL_ERROR "Runtime heap symbol found in firmware: ${name}")
        endif()
        foreach(allocation IN ITEMS "operator new(" "operator new[](" "operator delete(" "operator delete[](")
            string(FIND "${name}" "${allocation}" position)
            if(position EQUAL 0)
                message(FATAL_ERROR "Runtime heap symbol found in firmware: ${name}")
            endif()
        endforeach()
    endforeach()
endfunction()
