cmake_minimum_required(VERSION 3.15)
project(SlangPortInspector)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(slang REQUIRED)

add_executable(inspector src/main.cpp)

target_link_libraries(inspector PRIVATE slang::slang)