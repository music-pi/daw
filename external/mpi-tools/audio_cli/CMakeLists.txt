add_executable(audio_cli
    audio_cli.c
)

# Set C standard
set_target_properties(audio_cli PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
)

# Remove JUCE compile definitions that are added globally (for C files)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(audio_cli PRIVATE
        $<$<COMPILE_LANGUAGE:C>:-UJUCE_HEADLESS -UJUCE_USE_CURL -UJUCE_WEB_BROWSER>
    )
endif()

target_link_libraries(audio_cli
    PRIVATE audio sndfile
)