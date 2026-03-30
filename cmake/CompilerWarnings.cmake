add_library(tictac_warnings INTERFACE)

target_compile_options(tictac_warnings INTERFACE
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow
    -Wnon-virtual-dtor
    -Wold-style-cast
    -Wcast-align
    -Woverloaded-virtual
    -Wconversion
    -Wsign-conversion
)

if(TICTAC_ENABLE_SANITIZERS)
    target_compile_options(tictac_warnings INTERFACE -fsanitize=address,undefined)
    target_link_options(tictac_warnings INTERFACE -fsanitize=address,undefined)
endif()
