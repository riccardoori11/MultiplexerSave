cmake_minimum_required(VERSION 3.20)
project(work VERSION 0.1.0 LANGUAGES CXX)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
  message(FATAL_ERROR "work currently supports Linux.")
endif()
add_executable(work src/main.cpp src/process.cpp src/project.cpp)
target_include_directories(work PRIVATE include)
target_compile_features(work PRIVATE cxx_std_20)
target_compile_options(work PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
include(GNUInstallDirs)
install(TARGETS work RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

include(CTest)
if(BUILD_TESTING)
  find_package(Python3 COMPONENTS Interpreter REQUIRED)
  add_test(NAME integration COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration.py $<TARGET_FILE:work>)
endif()
