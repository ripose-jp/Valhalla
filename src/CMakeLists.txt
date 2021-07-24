add_subdirectory(containers)
add_subdirectory(buffer)

find_package(FCGI)
find_package(talloc)

set(
    HDR_FILES
    ./include/valhalla.h
)
set(
    SRC_FILES
    context.c
    request.c
    route.c
    strutil.c
)
set(
    LIBS
    libsds
    libcont
    ${FCGI_LIBRARY}
    ${TALLOC_LIBRARY}
)

add_library(${PROJECT_NAME} SHARED ${SRC_FILES})
add_library(${PROJECT_NAME}_static STATIC ${SRC_FILES})

set_target_properties(
    ${PROJECT_NAME} PROPERTIES
    PUBLIC_HEADER ${HDR_FILES}
    OUTPUT_NAME ${PROJECT_NAME}
    LINKER_LANGUAGE C
)
set_target_properties(
    ${PROJECT_NAME}_static PROPERTIES
    OUTPUT_NAME ${PROJECT_NAME}
    PUBLIC_HEADER ${HDR_FILES}
    LINKER_LANGUAGE C
)

target_link_libraries(${PROJECT_NAME} ${LIBS})

install(
    TARGETS ${PROJECT_NAME}
    LIBRARY DESTINATION lib
    PUBLIC_HEADER DESTINATION include
)