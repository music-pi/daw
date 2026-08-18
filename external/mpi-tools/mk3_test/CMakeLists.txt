# tools/mk3_test/CMakeLists.txt

add_executable(mk3_test
    mk3_test.c
    partial_test.c
    input_test.c
)

# Set C standard
set_target_properties(mk3_test PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
)

# Remove JUCE compile definitions that are added globally (for C files)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(mk3_test PRIVATE
        $<$<COMPILE_LANGUAGE:C>:-UJUCE_HEADLESS -UJUCE_USE_CURL -UJUCE_WEB_BROWSER>
    )
endif()

# Include libusb + libmk3
target_link_libraries(mk3_test PRIVATE mk3)

# Include paths
target_include_directories(mk3_test PRIVATE
    ${CMAKE_SOURCE_DIR}/external/mk3
)
