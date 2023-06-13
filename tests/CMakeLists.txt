cmake_minimum_required(VERSION 3.10)
project(smartec)
set(CMAKE_CXX_STANDARD 17)

add_library(smartec SHARED "smartec.cc")
target_link_libraries(smartec PUBLIC ibverbs)
target_link_libraries(smartec PUBLIC Jerasure)

add_executable(main main.cc)
target_link_libraries(main smartec)
