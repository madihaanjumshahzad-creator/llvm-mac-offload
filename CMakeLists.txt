cmake_minimum_required(VERSION 3.20)
project(MacOffload CXX)

find_package(LLVM REQUIRED CONFIG)
message(STATUS "Using LLVM ${LLVM_PACKAGE_VERSION} from ${LLVM_DIR}")

set(CMAKE_CXX_STANDARD 17)
add_definitions(${LLVM_DEFINITIONS})
include_directories(${LLVM_INCLUDE_DIRS})

add_library(MacOffload MODULE pass/MacOffload.cpp)
set_target_properties(MacOffload PROPERTIES PREFIX "" COMPILE_FLAGS "-fno-rtti")
