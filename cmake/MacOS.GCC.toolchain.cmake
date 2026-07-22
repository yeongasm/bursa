#----------------------------------------------------------------------------------------------------------------------
# MacOS.GCC.toolchain.cmake
#
# This CMake toolchain file configures a native macOS build to use the GCC compilers.
#
# It is package-manager agnostic for common macOS GCC distributions that expose versioned compiler names. Homebrew
# installs GCC under versioned names (e.g. 'gcc-15', 'g++-15'), while MacPorts commonly uses names such as 'gcc-mp-15'
# and 'g++-mp-15'. This file searches those versioned names and picks the highest version found across the search
# locations (Homebrew/MacPorts prefixes plus PATH and the standard system prefixes). The general search intentionally
# does not fall back to plain 'gcc'/'g++' because those names usually resolve to Apple Clang on macOS. Explicit
# MACOS_GCC_ROOT / GCC_ROOT overrides are allowed to use plain 'gcc'/'g++' because they are restricted to a user-chosen
# installation directory.
#
# NOTE: Homebrew GCC's AddressSanitizer / UndefinedBehaviorSanitizer runtimes are unreliable on macOS. Prefer the Clang
# toolchain (MacOS.Clang.toolchain.cmake) for sanitizer builds.
#
# The following variables can be used to configure the behavior of this toolchain file:
#
# | CMake Variable              | Description                                                                                |
# |-----------------------------|--------------------------------------------------------------------------------------------|
# | MACOS_GCC_ROOT              | The root of a specific GCC installation to use (its 'bin' directory contains gcc/g++).      |
# | GCC_ROOT                    | Respected as a fallback if MACOS_GCC_ROOT isn't set.                                        |
# | CMAKE_OSX_ARCHITECTURES     | The architecture(s) to build for. Defaults to 'arm64'. See MacOS.SDK.cmake.                 |
# | CMAKE_OSX_DEPLOYMENT_TARGET | The minimum macOS version to target. Defaults to '11.0'. See MacOS.SDK.cmake.               |
# | CMAKE_OSX_SYSROOT           | The path to the macOS SDK. Defaults to the value reported by 'xcrun'. See MacOS.SDK.cmake.  |
#
# The toolchain file will set the following variables:
#
# | CMake Variable        | Description                          |
# |-----------------------|--------------------------------------|
# | CMAKE_C_COMPILER      | The path to the C compiler to use.   |
# | CMAKE_CXX_COMPILER    | The path to the C++ compiler to use. |
#
# Resources:
#   <https://cmake.org/cmake/help/latest/manual/cmake-toolchains.7.html>
#
cmake_minimum_required(VERSION 3.20)

include_guard()

if(NOT (CMAKE_HOST_SYSTEM_NAME STREQUAL Darwin))
    return()
endif()

set(UNUSED "${CMAKE_TOOLCHAIN_FILE}") # Note: only to prevent cmake unused variable warning

# The range of Homebrew/MacPorts versioned GCC major versions to consider, newest first. This is intentionally wide so
# the toolchain keeps selecting the latest GCC as new major versions are released. Homebrew's 'gcc-<n>' naming uses the
# major version only.
set(MACOS_GCC_VERSION_RANGE 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10)

# Builds the list of candidate GCC 'bin' directories to search, in addition to PATH and the standard system prefixes.
# The ordering of these directories does not affect which version is chosen (see below); it only breaks ties between
# identical version numbers found in more than one place.
function(_macos_gcc_bin_hints OUTPUT_VARIABLE)
    set(HINTS)

    # Homebrew (Apple Silicon prefix and Intel prefix) versioned kegs and generic bin directories, plus MacPorts.
    foreach(PREFIX "/opt/homebrew" "/usr/local")
        file(GLOB VERSIONED_GCC_DIRS "${PREFIX}/opt/gcc*")
        foreach(DIR ${VERSIONED_GCC_DIRS})
            list(APPEND HINTS "${DIR}/bin")
        endforeach()

        list(APPEND HINTS "${PREFIX}/bin")
    endforeach()

    # MacPorts.
    list(APPEND HINTS "/opt/local/bin")

    set(${OUTPUT_VARIABLE} "${HINTS}" PARENT_SCOPE)
endfunction()

# Builds the ordered list of candidate compiler executable names (versioned newest first). Plain 'gcc'/'g++' are
# intentionally excluded because they usually resolve to Apple Clang on macOS.
function(_macos_gcc_names BASE OUTPUT_VARIABLE)
    set(NAMES)
    foreach(VERSION ${MACOS_GCC_VERSION_RANGE})
        list(APPEND NAMES "${BASE}-${VERSION}")

        if(BASE STREQUAL "gcc")
            list(APPEND NAMES "gcc-mp-${VERSION}")
        elseif(BASE STREQUAL "g++")
            list(APPEND NAMES "g++-mp-${VERSION}")
        endif()
    endforeach()
    set(${OUTPUT_VARIABLE} "${NAMES}" PARENT_SCOPE)
endfunction()

_macos_gcc_bin_hints(GCC_BIN_HINTS)
_macos_gcc_names(gcc C_COMPILER_NAMES)
_macos_gcc_names(g++ CXX_COMPILER_NAMES)

set(C_COMPILER_OVERRIDE_NAMES ${C_COMPILER_NAMES} gcc)
set(CXX_COMPILER_OVERRIDE_NAMES ${CXX_COMPILER_NAMES} g++)

message(VERBOSE "MacOS.GCC: GCC bin hints = ${GCC_BIN_HINTS}")

# 1. Explicit override: if MACOS_GCC_ROOT / GCC_ROOT is set, use the compilers from that installation unconditionally,
#    even if a newer GCC exists elsewhere on the machine. NO_DEFAULT_PATH restricts the search to just that directory,
#    so plain 'gcc'/'g++' are safe here and support custom/manual GCC installations.
set(GCC_ROOT_OVERRIDE)
if(MACOS_GCC_ROOT)
    set(GCC_ROOT_OVERRIDE "${MACOS_GCC_ROOT}")
elseif(GCC_ROOT)
    set(GCC_ROOT_OVERRIDE "${GCC_ROOT}")
endif()

if(GCC_ROOT_OVERRIDE)
    find_program(CMAKE_C_COMPILER
        NAMES ${C_COMPILER_OVERRIDE_NAMES}
        HINTS "${GCC_ROOT_OVERRIDE}/bin"
        NO_DEFAULT_PATH
        REQUIRED
    )
    find_program(CMAKE_CXX_COMPILER
        NAMES ${CXX_COMPILER_OVERRIDE_NAMES}
        HINTS "${GCC_ROOT_OVERRIDE}/bin"
        NO_DEFAULT_PATH
        REQUIRED
    )
endif()

# 2. Newest-version search. Without NAMES_PER_DIR, find_program considers one name at a time and searches every
#    directory for it. Because the NAMES list is ordered newest-first, this selects the highest GCC version available
#    across all searched locations (the hints above plus PATH and the standard system prefixes), regardless of which
#    package manager installed it or which directory it lives in. Directory order only breaks ties between equal
#    versions. Calls are no-ops if the override above already resolved the compilers.
find_program(CMAKE_C_COMPILER
    NAMES ${C_COMPILER_NAMES}
    HINTS ${GCC_BIN_HINTS}
    REQUIRED
)

find_program(CMAKE_CXX_COMPILER
    NAMES ${CXX_COMPILER_NAMES}
    HINTS ${GCC_BIN_HINTS}
    REQUIRED
)

# macOS SDK / sysroot / deployment target.
include("${CMAKE_CURRENT_LIST_DIR}/MacOS.SDK.cmake")
