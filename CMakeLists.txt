cmake_minimum_required(VERSION 3.22)
project(cs171_final)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_COMPILER g++-13)

set(EXECUTABLES cs171_final)

add_executable(cs171_final main.cpp paxos_node.cpp blockchain.cpp paxos_msg.cpp)
include_directories(SYSTEM "${CMAKE_SOURCE_DIR}/cereal/include")

if (${CMAKE_HOST_APPLE})
    execute_process(
        COMMAND brew --prefix openssl@3.0
        OUTPUT_VARIABLE OPENSSL_HOME
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    set(ENV{PKG_CONFIG_PATH} 
            "${OPENSSL_HOME}/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}"
    )
endif()

if ("${CMAKE_BUILD_TYPE}" STREQUAL "Debug")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g3 -O0")
    if (${CMAKE_HOST_LINUX})
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=thread,undefined")
    endif()
else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DNDEBUG -O2")
endif()

find_package(OpenSSL REQUIRED)
target_link_libraries(cs171_final OpenSSL::SSL)

find_package(fmt REQUIRED)

foreach(EXE IN ITEMS ${EXECUTABLES})
    target_link_libraries(${EXE} fmt::fmt-header-only)
endforeach()
