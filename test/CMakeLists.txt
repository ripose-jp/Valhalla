# Add the unit testing library

add_library(
    libunity
    STATIC
    unity/unity.c
    unity/unity_internals.h
)

# Route Tree Tests

add_executable(test_routes route.c)
target_link_libraries(
    test_routes
    libunity
    ${PROJECT_NAME}
    ${TALLOC_LIBRARY}
)
add_test(test_routes ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/test_routes)