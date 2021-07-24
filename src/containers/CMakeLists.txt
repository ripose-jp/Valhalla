add_library(
    libcont
    STATIC
    khash.h
    strcasemap.h
    strmap.h
)
set_target_properties(
    libcont PROPERTIES
    LINKER_LANGUAGE C
)