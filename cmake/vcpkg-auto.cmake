set(_nlp3_vcpkg_candidates "")

if(DEFINED ENV{VCPKG_ROOT} AND NOT "$ENV{VCPKG_ROOT}" STREQUAL "")
    list(APPEND _nlp3_vcpkg_candidates "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
endif()

if(DEFINED ENV{CMAKE_TOOLCHAIN_FILE} AND NOT "$ENV{CMAKE_TOOLCHAIN_FILE}" STREQUAL "")
    list(APPEND _nlp3_vcpkg_candidates "$ENV{CMAKE_TOOLCHAIN_FILE}")
endif()

if(WIN32)
    list(APPEND _nlp3_vcpkg_candidates
        "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/vcpkg/scripts/buildsystems/vcpkg.cmake"
        "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/vcpkg/scripts/buildsystems/vcpkg.cmake"
        "C:/Program Files/Microsoft Visual Studio/2022/Professional/VC/vcpkg/scripts/buildsystems/vcpkg.cmake"
        "C:/Program Files/Microsoft Visual Studio/2022/Enterprise/VC/vcpkg/scripts/buildsystems/vcpkg.cmake"
    )
endif()

foreach(_nlp3_vcpkg_toolchain IN LISTS _nlp3_vcpkg_candidates)
    if(EXISTS "${_nlp3_vcpkg_toolchain}")
        include("${_nlp3_vcpkg_toolchain}")
        return()
    endif()
endforeach()

message(FATAL_ERROR
    "Could not locate vcpkg.cmake. Set VCPKG_ROOT or CMAKE_TOOLCHAIN_FILE "
    "to a vcpkg installation before configuring Nisoje LivePanel."
)
