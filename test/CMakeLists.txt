cmake_minimum_required(VERSION 3.18)

# ---- base64 test ----
add_executable(test_base64
    test_base64.cpp
    ../base64.cpp
)
target_include_directories(test_base64 PRIVATE ..)
target_compile_options(test_base64 PRIVATE -Wall -Wextra -std=c++11)

# ---- base64 extra tests ----
add_executable(test_base64_extra
    test_base64_extra.cpp
    ../base64.cpp
)
target_include_directories(test_base64_extra PRIVATE ..)
target_compile_options(test_base64_extra PRIVATE -Wall -Wextra -std=c++11)

# ---- ws_client test ----
find_package(OpenSSL)
if(OpenSSL_FOUND)
    add_executable(test_ws_client
        test_ws_client.cpp
        ../ws_client.cpp
    )
    target_include_directories(test_ws_client PRIVATE ..)
    target_compile_definitions(test_ws_client PRIVATE USE_TLS WS_CLIENT_TEST)
    target_compile_options(test_ws_client PRIVATE -Wall -Wextra -std=c++11)
    target_link_libraries(test_ws_client PRIVATE OpenSSL::SSL OpenSSL::Crypto pthread)
endif()

# ---- ws_uri test ----
add_executable(test_ws_uri
    test_ws_uri.cpp
    ../stream_utils.cpp
)
target_include_directories(test_ws_uri PRIVATE ..)
target_compile_options(test_ws_uri PRIVATE -Wall -Wextra -std=c++11)

# ---- utf8 test ----
add_executable(test_utf8
    test_utf8.cpp
    ../stream_utils.cpp
)
target_include_directories(test_utf8 PRIVATE ..)
target_compile_options(test_utf8 PRIVATE -Wall -Wextra -std=c++11)

# ---- CTest ----
enable_testing()
add_test(NAME base64       COMMAND test_base64)
add_test(NAME base64_extra COMMAND test_base64_extra)
add_test(NAME ws_client    COMMAND test_ws_client)
add_test(NAME ws_uri       COMMAND test_ws_uri)
add_test(NAME utf8         COMMAND test_utf8)
