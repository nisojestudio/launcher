cmake_minimum_required(VERSION 3.27)
project(NisojeLivePanelV3 VERSION 0.2.13 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(NLP3_BUILD_TESTS "Build tests" ON)

add_subdirectory(src)

if(NLP3_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
