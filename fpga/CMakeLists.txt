cmake_minimum_required(VERSION 3.28)
project(hft_fpga LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

find_package(verilator REQUIRED HINTS $ENV{VERILATOR_ROOT})

add_executable(itch_rtl_test itch_rtl_test.cpp)
target_include_directories(itch_rtl_test PRIVATE ${PROJECT_SOURCE_DIR}/../src)
verilate(itch_rtl_test SOURCES itch_parser.sv VERILATOR_ARGS -Wall)

enable_testing()
add_test(NAME itch_rtl COMMAND itch_rtl_test)
