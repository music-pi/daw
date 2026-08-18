add_library(mk3_emu STATIC
    mk3_emu.c
)

target_include_directories(mk3_emu PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../mk3
)

# No external dependencies — POSIX sockets only
