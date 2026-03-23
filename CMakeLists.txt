cmake_minimum_required(VERSION 3.16)
project(CryptoHedgeLab VERSION 1.0.0 LANGUAGES CXX)

# ─── C++ Standard ───────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ─── Compiler Flags ─────────────────────────────────────────────────────────
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic")
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -fsanitize=address,undefined")

# Default to Release build
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")

# ─── Dependencies ───────────────────────────────────────────────────────────

# Boost (Beast for WebSocket, Asio for networking)
find_package(Boost 1.75 REQUIRED COMPONENTS system)
if(Boost_FOUND)
    message(STATUS "Boost version: ${Boost_VERSION}")
    message(STATUS "Boost include: ${Boost_INCLUDE_DIRS}")
endif()

# OpenSSL (required for TLS connection to Binance)
find_package(OpenSSL REQUIRED)
if(OPENSSL_FOUND)
    message(STATUS "OpenSSL version: ${OPENSSL_VERSION}")
endif()

# ─── Module 1: Feed Core ───────────────────────────────────────────────────
add_executable(cryptohedgelab_feed
    main.cpp
    core/feed_handler/binance_ws.cpp
)

target_include_directories(cryptohedgelab_feed PRIVATE
    ${CMAKE_SOURCE_DIR}                # For #include "core/..."
    ${Boost_INCLUDE_DIRS}
)

target_link_libraries(cryptohedgelab_feed PRIVATE
    Boost::system
    OpenSSL::SSL
    OpenSSL::Crypto
)

# ─── Platform-Specific ─────────────────────────────────────────────────────
if(APPLE)
    # Suppress deprecation warnings for OpenSSL on macOS
    target_compile_definitions(cryptohedgelab_feed PRIVATE
        _SILENCE_ALL_CXX17_DEPRECATION_WARNINGS
    )
endif()

# ─── Build Summary ──────────────────────────────────────────────────────────
message(STATUS "")
message(STATUS "── CryptoHedgeLab Module 1: Feed Core ──")
message(STATUS "  Target: cryptohedgelab_feed")
message(STATUS "  Standard: C++${CMAKE_CXX_STANDARD}")
message(STATUS "  Build: ${CMAKE_BUILD_TYPE}")
message(STATUS "")
