cmake_minimum_required(VERSION 3.22)
project(pa02)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_COMPILER g++)

set(EXECUTABLES pa02_server pa02_client)

add_executable(pa02_server server.cpp blockchain.cpp)
add_executable(pa02_client client.cpp peer_connection.cpp)

if (${CMAKE_HOST_APPLE})
    execute_process(
        COMMAND brew --prefix openssl
        OUTPUT_VARIABLE OPENSSL_HOME
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    set(ENV{PKG_CONFIG_PATH} 
            "${OPENSSL_HOME}/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}"
    )
endif()

if ("${CMAKE_BUILD_TYPE}" STREQUAL "Debug")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g3 -O0")# -fsanitize=thread,undefined")
else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DNDEBUG -O2")
endif()

find_package(OpenSSL REQUIRED)
target_link_libraries(pa02_server OpenSSL::SSL)

foreach(EXE IN ITEMS ${EXECUTABLES})
    target_compile_definitions(${EXE} PUBLIC FMT_HEADER_ONLY=1)
endforeach()
