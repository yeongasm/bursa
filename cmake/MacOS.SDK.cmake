#----------------------------------------------------------------------------------------------------------------------
# MacOS.SDK.cmake
#
# Resolves the macOS SDK (sysroot), target architecture and deployment target for a native macOS build.
#
# This file is the macOS analogue of 'Windows.Kits.cmake': it is included by the macOS toolchain files and is
# responsible for locating the platform SDK. Unlike Windows, macOS exposes the SDK through 'xcrun', which works with
# either a full Xcode installation or the standalone Command Line Tools, so no registry / vswhere style search is
# required.
#
# The following variables can be used to configure the behavior of this file. Each one is only defaulted when the
# caller has not already provided a value, so they can be overridden from a preset, the command line or an including
# toolchain file:
#
# | CMake Variable                | Description                                                                        |
# |-------------------------------|------------------------------------------------------------------------------------|
# | CMAKE_OSX_ARCHITECTURES       | The architecture(s) to build for. Defaults to 'arm64' (Apple Silicon).             |
# | CMAKE_OSX_DEPLOYMENT_TARGET   | The minimum macOS version to target. Defaults to '11.0' (the macOS the M1 shipped).|
# | CMAKE_OSX_SYSROOT             | The path to the macOS SDK. Defaults to the value reported by 'xcrun'.              |
#
include_guard()

if(NOT (CMAKE_HOST_SYSTEM_NAME STREQUAL Darwin))
    return()
endif()

# Default to Apple Silicon. The project currently targets the M1 series and newer.
if(NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "macOS build architecture(s)" FORCE)
endif()

# The M1 series shipped with macOS 11.0 (Big Sur); use that as the minimum deployment target.
if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS deployment target" FORCE)
endif()

# Locate the macOS SDK via 'xcrun'. This works with both a full Xcode install and the Command Line Tools.
if(NOT CMAKE_OSX_SYSROOT)
    find_program(XCRUN_PROGRAM xcrun)
    if(XCRUN_PROGRAM STREQUAL "XCRUN_PROGRAM-NOTFOUND")
        message(FATAL_ERROR
            "MacOS.SDK: 'xcrun' could not be found. Install Xcode or the Command Line Tools "
            "('xcode-select --install'), or set CMAKE_OSX_SYSROOT manually.")
    endif()

    execute_process(
        COMMAND ${XCRUN_PROGRAM} --sdk macosx --show-sdk-path
        OUTPUT_VARIABLE MACOS_SDK_PATH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE MACOS_SDK_RESULT
        ERROR_VARIABLE MACOS_SDK_ERROR
    )

    if(NOT MACOS_SDK_RESULT EQUAL 0 OR MACOS_SDK_PATH STREQUAL "")
        message(FATAL_ERROR
            "MacOS.SDK: Unable to resolve the macOS SDK path using 'xcrun --sdk macosx --show-sdk-path'.\n"
            "${MACOS_SDK_ERROR}")
    endif()

    set(CMAKE_OSX_SYSROOT "${MACOS_SDK_PATH}" CACHE PATH "The path to the macOS SDK." FORCE)
endif()

if(NOT EXISTS "${CMAKE_OSX_SYSROOT}")
    message(FATAL_ERROR "MacOS.SDK: The macOS SDK path '${CMAKE_OSX_SYSROOT}' does not exist.")
endif()

message(VERBOSE "MacOS.SDK: CMAKE_OSX_ARCHITECTURES = ${CMAKE_OSX_ARCHITECTURES}")
message(VERBOSE "MacOS.SDK: CMAKE_OSX_DEPLOYMENT_TARGET = ${CMAKE_OSX_DEPLOYMENT_TARGET}")
message(VERBOSE "MacOS.SDK: CMAKE_OSX_SYSROOT = ${CMAKE_OSX_SYSROOT}")
