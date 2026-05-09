cmake_minimum_required(VERSION 3.20)
project(byze-miner LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BYZE_MINER_BUILD_RANDOMX "Build bundled RandomX (vendored in third_party/)" ON)

find_package(Boost REQUIRED COMPONENTS system)
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

if(BYZE_MINER_BUILD_RANDOMX)
  add_subdirectory(third_party/randomx "${CMAKE_BINARY_DIR}/randomx-build")
endif()

add_executable(byze-miner
  src/main.cpp
)

target_include_directories(byze-miner PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/third_party/randomx/src"
)

target_link_libraries(byze-miner PRIVATE
  Boost::system
  OpenSSL::Crypto
  Threads::Threads
  randomx
)
