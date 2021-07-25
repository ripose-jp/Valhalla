# Add the unit testing library

add_library(
    libunity
    STATIC
    unity/unity.c
    unity/unity_internals.h
)

# StrUtil Tests

add_executable(test_strutil strutil.c)
target_link_libraries(
    test_strutil
    libunity
    ${PROJECT_NAME}
    ${TALLOC_LIBRARY}
)
add_test(test_strutil ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/test_strutil)

# Route Tree Tests

add_executable(test_routes route.c)
target_link_libraries(
    test_routes
    libunity
    ${PROJECT_NAME}
    ${TALLOC_LIBRARY}
)
add_test(test_routes ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/test_routes)