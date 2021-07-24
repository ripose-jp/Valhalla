add_library(
    libsds
    STATIC
    sdsalloc.h
    sds.c
)
set_target_properties(
    libsds PROPERTIES
    LINKER_LANGUAGE C
)
if ("${CMAKE_C_COMPILER_ID}" STREQUAL "GNU")
    target_compile_options(libsds PRIVATE -fPIC)
endif()
