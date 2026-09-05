# An ALL target intentionally runs even when linking is cached: a missing or
# damaged exported file must be repaired by the next ordinary build.
set(artifact_arguments
    "-DNM=${CMAKE_NM}"
    "-DOBJCOPY=${CMAKE_OBJCOPY}"
    "-DELF=$<TARGET_FILE:${CMAKE_PROJECT_NAME}>"
    "-DDISPLAY=${GAMEBOX_OLED_INTERFACE}"
    "-DPROFILE=$<CONFIG>"
    "-DOUTPUT_ROOT=${GAMEBOX_ARTIFACT_DIR}"
    "-DSCRATCH=${CMAKE_CURRENT_BINARY_DIR}/artifact-stage"
)
set(artifact_script "${CMAKE_CURRENT_LIST_DIR}/PackageFirmware.cmake")
add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
    COMMAND "${CMAKE_COMMAND}" "-DNM=${CMAKE_NM}"
            "-DELF=$<TARGET_FILE:${CMAKE_PROJECT_NAME}>"
            -P "${CMAKE_CURRENT_LIST_DIR}/VerifyNoHeap.cmake"
    COMMENT "Auditing firmware runtime heap symbols"
    VERBATIM
)
add_custom_target(firmware_artifacts ALL
    COMMAND "${CMAKE_COMMAND}" ${artifact_arguments} -DMODE=EXPORT
            -P "${artifact_script}"
    # Preserve the original build-tree filenames for IDE/debugger integrations.
    COMMAND "${CMAKE_OBJCOPY}" -O ihex "$<TARGET_FILE:${CMAKE_PROJECT_NAME}>"
            "${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_PROJECT_NAME}.hex"
    COMMAND "${CMAKE_OBJCOPY}" -O binary "$<TARGET_FILE:${CMAKE_PROJECT_NAME}>"
            "${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_PROJECT_NAME}.bin"
    DEPENDS ${CMAKE_PROJECT_NAME}
    COMMENT "Auditing and exporting ${GAMEBOX_OLED_INTERFACE} firmware"
    VERBATIM
)
add_custom_target(verify_artifacts
    COMMAND "${CMAKE_COMMAND}" ${artifact_arguments} -DMODE=VERIFY
            -P "${artifact_script}"
    COMMENT "Verifying existing ${GAMEBOX_OLED_INTERFACE} firmware package"
    VERBATIM
)
enable_testing()
add_test(NAME firmware_artifacts
    COMMAND "${CMAKE_COMMAND}" ${artifact_arguments} -DMODE=VERIFY
            -P "${artifact_script}"
)
