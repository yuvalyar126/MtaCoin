cmake_minimum_required(VERSION 3.22)
project(MTA_Coin C)

set(CMAKE_C_STANDARD 17)

find_package(ZLIB REQUIRED)

add_executable(MTA_Coin main.c)

target_link_libraries(MTA_Coin pthread z ${ZLIB_LIBRARIES})