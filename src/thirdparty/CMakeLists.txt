include(${CMAKE_SOURCE_DIR}/cmake/Util.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/CPM.cmake)

CPMAddPackage(
	NAME fmt
	GIT_TAG 12.2.0
	GIT_REPOSITORY https://github.com/fmtlib/fmt.git
)
target_compile_features(fmt PUBLIC cxx_std_23)
target_compile_options(fmt PRIVATE -Wno-deprecated-declarations)

CPMAddPackage(
	NAME unordered_dense
	GIT_TAG v4.8.1
	GIT_REPOSITORY https://github.com/martinus/unordered_dense.git
)
target_compile_features(unordered_dense INTERFACE cxx_std_23)

CPMAddPackage(
  NAME mimalloc
  GIT_TAG v3.3.2
  GIT_REPOSITORY https://github.com/microsoft/mimalloc.git
  OPTIONS "MI_BUILD_SHARED ON" "MI_OVERRIDE ON" "MI_USE_CXX ON" "MI_DEBUG OFF"
)

if (mimalloc_ADDED)
	target_compile_options(mimalloc PRIVATE -Wno-incompatible-pointer-types -Wno-int-conversion -Wno-pointer-sign -Wno-strict-prototypes)
	set_target_properties(mimalloc PROPERTIES FOLDER thirdparty)
endif()

CPMAddPackage(
  NAME nanobench
  GIT_TAG v4.3.11
  GIT_REPOSITORY https://github.com/martinus/nanobench.git
)
target_compile_features(nanobench PUBLIC cxx_std_23)