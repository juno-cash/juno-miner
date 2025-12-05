cmake_minimum_required(VERSION 3.10)
project(juno-miner VERSION 1.0.0 LANGUAGES C CXX ASM)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find required packages
find_package(CURL REQUIRED)
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(JSONCPP REQUIRED jsoncpp)

# Find ZeroMQ library (optional - for instant block notifications)
pkg_check_modules(ZMQ libzmq)
if(ZMQ_FOUND)
    message(STATUS "Found libzmq: ${ZMQ_LIBRARIES}")
    add_definitions(-DHAVE_ZMQ)
else()
    message(STATUS "libzmq not found - ZMQ block notifications disabled (install libzmq3-dev)")
endif()

# Find NUMA library (optional but recommended for multi-socket systems)
find_library(NUMA_LIBRARY numa)
if(NUMA_LIBRARY)
    message(STATUS "Found libnuma: ${NUMA_LIBRARY}")
    add_definitions(-DHAVE_NUMA)
else()
    message(STATUS "libnuma not found - NUMA optimizations disabled")
endif()

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CURL_INCLUDE_DIRS}
    ${OPENSSL_INCLUDE_DIR}
    ${JSONCPP_INCLUDE_DIRS}
    ${ZMQ_INCLUDE_DIRS}
)

# Copy RandomX library from zcash project
set(RANDOMX_DIR ${CMAKE_CURRENT_SOURCE_DIR}/randomx)
include_directories(
    ${RANDOMX_DIR}
    ${RANDOMX_DIR}/blake2
)

# Collect RandomX source files
file(GLOB_RECURSE RANDOMX_SOURCES "${RANDOMX_DIR}/*.cpp" "${RANDOMX_DIR}/*.c")
file(GLOB RANDOMX_ASM "${RANDOMX_DIR}/*.S")

# Exclude tests and benchmark files
list(FILTER RANDOMX_SOURCES EXCLUDE REGEX ".*/tests/.*")
list(FILTER RANDOMX_SOURCES EXCLUDE REGEX ".*/benchmark\\.cpp$")

# Exclude ARM64 and RISC-V files if not on those architectures
list(FILTER RANDOMX_SOURCES EXCLUDE REGEX ".*jit_compiler_a64\\.cpp$")
list(FILTER RANDOMX_SOURCES EXCLUDE REGEX ".*jit_compiler_rv64\\.cpp$")
list(FILTER RANDOMX_ASM EXCLUDE REGEX ".*jit_compiler_a64_static\\.S$")
list(FILTER RANDOMX_ASM EXCLUDE REGEX ".*jit_compiler_rv64_static\\.S$")

# Source files
set(SOURCES
    src/main.cpp
    src/rpc_client.cpp
    src/config.cpp
    src/miner.cpp
    src/utils.cpp
    src/logger.cpp
    ${RANDOMX_SOURCES}
    ${RANDOMX_ASM}
    ${RANDOMX_DIR}/blake2/blake2b.c
)

# Create executable
add_executable(juno-miner ${SOURCES})

# Create test executable
add_executable(test_hash_verification
    test_hash_verification.cpp
    src/miner.cpp
    src/rpc_client.cpp
    src/utils.cpp
    src/logger.cpp
    ${RANDOMX_SOURCES}
    ${RANDOMX_ASM}
    ${RANDOMX_DIR}/blake2/blake2b.c
)

add_executable(test_simple_mine
    test_simple_mine.cpp
    src/miner.cpp
    src/rpc_client.cpp
    src/utils.cpp
    src/logger.cpp
    ${RANDOMX_SOURCES}
    ${RANDOMX_ASM}
    ${RANDOMX_DIR}/blake2/blake2b.c
)

add_executable(verify_block_1583
    verify_block_1583.cpp
    src/utils.cpp
    src/logger.cpp
    ${RANDOMX_SOURCES}
    ${RANDOMX_ASM}
    ${RANDOMX_DIR}/blake2/blake2b.c
)

add_executable(test_comparison
    test_comparison.cpp
    src/miner.cpp
    src/rpc_client.cpp
    src/utils.cpp
    src/logger.cpp
    ${RANDOMX_SOURCES}
    ${RANDOMX_ASM}
    ${RANDOMX_DIR}/blake2/blake2b.c
)

add_executable(test_mining_simple
    test_mining_simple.cpp
    src/miner.cpp
    src/rpc_client.cpp
    src/utils.cpp
    src/logger.cpp
    ${RANDOMX_SOURCES}
    ${RANDOMX_ASM}
    ${RANDOMX_DIR}/blake2/blake2b.c
)

# Link libraries
target_link_libraries(juno-miner
    ${CURL_LIBRARIES}
    Threads::Threads
    ${OPENSSL_LIBRARIES}
    ${JSONCPP_LIBRARIES}
    dl
    $<$<BOOL:${NUMA_LIBRARY}>:${NUMA_LIBRARY}>
    $<$<BOOL:${ZMQ_FOUND}>:${ZMQ_LIBRARIES}>
)

target_link_libraries(test_hash_verification
    ${CURL_LIBRARIES}
    Threads::Threads
    ${OPENSSL_LIBRARIES}
    ${JSONCPP_LIBRARIES}
    dl
    $<$<BOOL:${NUMA_LIBRARY}>:${NUMA_LIBRARY}>
)

target_link_libraries(test_simple_mine
    ${CURL_LIBRARIES}
    Threads::Threads
    ${OPENSSL_LIBRARIES}
    ${JSONCPP_LIBRARIES}
    dl
    $<$<BOOL:${NUMA_LIBRARY}>:${NUMA_LIBRARY}>
)

target_link_libraries(verify_block_1583
    Threads::Threads
    ${OPENSSL_LIBRARIES}
    dl
    $<$<BOOL:${NUMA_LIBRARY}>:${NUMA_LIBRARY}>
)

target_link_libraries(test_comparison
    ${CURL_LIBRARIES}
    Threads::Threads
    ${OPENSSL_LIBRARIES}
    ${JSONCPP_LIBRARIES}
    dl
    $<$<BOOL:${NUMA_LIBRARY}>:${NUMA_LIBRARY}>
)

target_link_libraries(test_mining_simple
    ${CURL_LIBRARIES}
    Threads::Threads
    ${OPENSSL_LIBRARIES}
    ${JSONCPP_LIBRARIES}
    dl
    $<$<BOOL:${NUMA_LIBRARY}>:${NUMA_LIBRARY}>
)

# Compiler flags for optimization
target_compile_options(juno-miner PRIVATE
    -O3
    -march=native
    -mtune=native
)

# Install target
install(TARGETS juno-miner DESTINATION bin)
