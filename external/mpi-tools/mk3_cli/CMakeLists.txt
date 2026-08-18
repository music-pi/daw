find_package(PkgConfig REQUIRED)
pkg_check_modules(FREETYPE2 REQUIRED freetype2)

add_executable(mk3_cli mk3_cli.c)

# Set C standard
set_target_properties(mk3_cli PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
)

# Remove JUCE compile definitions that are added globally (for C files)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(mk3_cli PRIVATE
        $<$<COMPILE_LANGUAGE:C>:-UJUCE_HEADLESS -UJUCE_USE_CURL -UJUCE_WEB_BROWSER>
    )
endif()

target_include_directories(mk3_cli PRIVATE
    ${CMAKE_SOURCE_DIR}/external/mk3
    ${FREETYPE2_INCLUDE_DIRS}
)

target_link_libraries(mk3_cli PRIVATE
    mk3
    ${FREETYPE2_LIBRARIES}
)