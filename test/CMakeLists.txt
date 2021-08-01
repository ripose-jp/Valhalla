set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
find_package(CURL REQUIRED)

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

# Context Tests

add_executable(test_context context.c)
target_link_libraries(
    test_context
    libunity
    ${PROJECT_NAME}
)
add_test(test_context ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/test_context)

# Request Tests

add_executable(test_request request.c)
target_link_libraries(
    test_request
    libunity
    ${PROJECT_NAME}
    ${CURL_LIBRARY}
    ${TALLOC_LIBRARY}
    Threads::Threads
)
add_test(
    NAME test_request
    COMMAND spawn-fcgi -p 9001 -n ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/test_request
)

# Response Tests

add_executable(test_response response.c)
target_link_libraries(
    test_response
    libunity
    ${PROJECT_NAME}
    ${CURL_LIBRARY}
    ${TALLOC_LIBRARY}
    Threads::Threads
)
add_test(
    NAME test_response
    COMMAND spawn-fcgi -p 9002 -n ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/test_response
)
