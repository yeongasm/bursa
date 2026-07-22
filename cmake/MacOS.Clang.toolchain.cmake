#----------------------------------------------------------------------------------------------------------------------
# MacOS.Clang.toolchain.cmake
#
# This CMake toolchain file configures a native macOS build to use the Clang compilers.
#
# It is package-manager agnostic: it prefers the newest LLVM/Clang it can find (typically installed via Homebrew or
# MacPorts) and falls back to the Apple Clang that ships with Xcode / the Command Line Tools. To always prioritise the
# latest toolchain, Homebrew's rolling 'llvm' keg is preferred, followed by any versioned 'llvm@<n>' kegs in descending
# order, then MacPorts, then whatever 'clang' is on the PATH.
#
# The following variables can be used to configure the behavior of this toolchain file:
#
# | CMake Variable              | Description                                                                                   |
# |-----------------------------|-----------------------------------------------------------------------------------------------|
# | MACOS_LLVM_ROOT             | The root of a specific LLVM installation to use (its 'bin' directory contains clang/clang++).  |
# | LLVM_ROOT                   | Respected as a fallback if MACOS_LLVM_ROOT isn't set.                                          |
# | CMAKE_OSX_ARCHITECTURES     | The architecture(s) to build for. Defaults to 'arm64'. See MacOS.SDK.cmake.                    |
# | CMAKE_OSX_DEPLOYMENT_TARGET | The minimum macOS version to target. Defaults to '11.0'. See MacOS.SDK.cmake.                  |
# | CMAKE_OSX_SYSROOT           | The path to the macOS SDK. Defaults to the value reported by 'xcrun'. See MacOS.SDK.cmake.     |
# | CLANG_TIDY_CHECKS           | List of rules clang-tidy should check. Not set by default.                                    |
#
# The toolchain file will set the following variables:
#
# | CMake Variable        | Description                                    |
# |-----------------------|------------------------------------------------|
# | CMAKE_C_COMPILER      | The path to the C compiler to use.             |
# | CMAKE_CXX_COMPILER    | The path to the C++ compiler to use.           |
# | CMAKE_CXX_CLANG_TIDY  | The clang-tidy command line, if enabled.       |
#
# Resources:
#   <https://cmake.org/cmake/help/latest/manual/cmake-toolchains.7.html>
#
cmake_minimum_required(VERSION 3.20)

include_guard()

if(NOT (CMAKE_HOST_SYSTEM_NAME STREQUAL Darwin))
    return()
endif()

set(UNUSED ${CMAKE_TOOLCHAIN_FILE}) # Note: only to prevent cmake unused variable warning

# Builds an ordered list of candidate LLVM 'bin' directories, newest first, for use as find_program HINTS.
function(_macos_llvm_bin_hints OUTPUT_VARIABLE)
    set(HINTS)

    # 1. Explicit override.
    if(MACOS_LLVM_ROOT)
        list(APPEND HINTS "${MACOS_LLVM_ROOT}/bin")
    elseif(LLVM_ROOT)
        list(APPEND HINTS "${LLVM_ROOT}/bin")
    endif()

    # 2. Homebrew (Apple Silicon prefix and Intel prefix) and MacPorts.
    #    The rolling 'llvm' keg is always the newest release, so it comes first; versioned 'llvm@<n>' kegs follow in
    #    descending order.
    foreach(PREFIX "/opt/homebrew" "/usr/local")
        list(APPEND HINTS "${PREFIX}/opt/llvm/bin")

        file(GLOB VERSIONED_LLVM_DIRS "${PREFIX}/opt/llvm@*")
        list(SORT VERSIONED_LLVM_DIRS COMPARE NATURAL ORDER DESCENDING)
        foreach(DIR ${VERSIONED_LLVM_DIRS})
            list(APPEND HINTS "${DIR}/bin")
        endforeach()
    endforeach()

    # MacPorts.
    file(GLOB MACPORTS_LLVM_DIRS "/opt/local/libexec/llvm-*/bin")
    list(SORT MACPORTS_LLVM_DIRS COMPARE NATURAL ORDER DESCENDING)
    list(APPEND HINTS ${MACPORTS_LLVM_DIRS})
    list(APPEND HINTS "/opt/local/bin")

    set(${OUTPUT_VARIABLE} "${HINTS}" PARENT_SCOPE)
endfunction()

_macos_llvm_bin_hints(LLVM_BIN_HINTS)

message(VERBOSE "MacOS.Clang: LLVM bin hints = ${LLVM_BIN_HINTS}")

find_program(CMAKE_C_COMPILER
    NAMES clang
    HINTS ${LLVM_BIN_HINTS}
    REQUIRED
)

find_program(CMAKE_CXX_COMPILER
    NAMES clang++
    HINTS ${LLVM_BIN_HINTS}
    REQUIRED
)

# Wire up clang-tidy, resolved from the same directory as the selected compiler.
if(CLANG_TIDY_CHECKS)
    get_filename_component(CLANG_PATH ${CMAKE_CXX_COMPILER} DIRECTORY)
    find_program(CLANG_TIDY_PROGRAM
        NAMES clang-tidy
        HINTS "${CLANG_PATH}" ${LLVM_BIN_HINTS}
    )
    if(CLANG_TIDY_PROGRAM)
        set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_PROGRAM};-checks=${CLANG_TIDY_CHECKS}")
    else()
        message(WARNING "MacOS.Clang: CLANG_TIDY_CHECKS was set but 'clang-tidy' could not be found.")
    endif()
endif()

# macOS SDK / sysroot / deployment target.
include("${CMAKE_CURRENT_LIST_DIR}/MacOS.SDK.cmake")
