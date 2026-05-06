cmake_minimum_required(VERSION 3.22)

project(OrderMatch
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(BUILD_GMOCK OFF CACHE BOOL "Disable GoogleMock for OrderMatch" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "Disable GoogleTest install rules" FORCE)

add_subdirectory(third_party/googletest EXCLUDE_FROM_ALL)
find_package(Threads REQUIRED)

add_library(ordermatch_core STATIC
    src/core/order_book.cpp
    src/core/matching_engine.cpp
)

target_include_directories(ordermatch_core
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

add_library(ordermatch_engine STATIC
    src/engine/engine_runner.cpp
    src/engine/engine_runtime.cpp
)

target_include_directories(ordermatch_engine
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(ordermatch_engine
    PUBLIC
        ordermatch_core
        Threads::Threads
)

add_library(ordermatch_http STATIC
    src/http/codec.cpp
)

target_include_directories(ordermatch_http
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(ordermatch_http
    PUBLIC
        ordermatch_engine
)

add_executable(ordermatch_server
    src/server/main.cpp
)

target_link_libraries(ordermatch_server
    PRIVATE
        ordermatch_http
)

add_executable(ordermatch_client
    src/client/main.cpp
)

add_executable(ordermatch_tests
    tests/order_book_tests.cpp
    tests/engine_runtime_tests.cpp
    tests/http_codec_tests.cpp
    tests/http_pipeline_tests.cpp
)

target_link_libraries(ordermatch_tests
    PRIVATE
        ordermatch_core
        ordermatch_engine
        ordermatch_http
        GTest::gtest_main
)
