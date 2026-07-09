cmake_minimum_required(VERSION 3.27)
project(NisojeLivePanelV3 VERSION 0.2.17 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Fix: /Ob2 (inline agresivo) causa STATUS_STACK_BUFFER_OVERRUN (0xC0000409)
# en combinacion con /GS. Cambiamos a /Ob1 que solo inlinea funciones marcadas
# con __inline/__forceinline. Mantiene /O2 para velocidad.
if(MSVC)
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /Ob1")
    string(REPLACE "/Ob2" "/Ob1" CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")
endif()

option(NLP3_BUILD_TESTS "Build tests" ON)

add_subdirectory(src)

if(NLP3_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
