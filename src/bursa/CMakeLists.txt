include(${CMAKE_SOURCE_DIR}/cmake/Util.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/CPM.cmake)

CPMAddPackage(
	NAME fmt
	GIT_TAG 12.2.0
	GIT_REPOSITORY https://github.com/fmtlib/fmt.git
)
set_target_properties(fmt PROPERTIES FOLDER thirdparty)
target_compile_features(fmt PUBLIC cxx_std_23)

CPMAddPackage(
	NAME unordered_dense
	GIT_TAG v4.8.1
	GIT_REPOSITORY https://github.com/martinus/unordered_dense.git
)
set_target_properties(unordered_dense PROPERTIES FOLDER thirdparty)
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
	NAME plf_colony
	GIT_TAG c4c1a2e7f4346faa5d16e89ba1b4fa8c3d976bd2
	GIT_REPOSITORY https://github.com/mattreecebentley/plf_colony.git
	DOWNLOAD_ONLY True
)

if (plf_colony_ADDED)
	add_library(plf_colony INTERFACE ${plf_colony_SOURCE_DIR}/plf_colony.h)
	target_include_directories(plf_colony INTERFACE ${plf_colony_SOURCE_DIR})
	target_compile_features(plf_colony INTERFACE cxx_std_23)
    set_target_properties(plf_colony PROPERTIES FOLDER thirdparty)
	target_compile_options(plf_colony INTERFACE -Wno-unused-parameter)
endif()

set(
	source_list
	"public/bursa/order_book.hpp"
	"public/bursa/tick_bitset.hpp"
	"public/bursa/price_levels.hpp"
	"public/bursa/meta.hpp"
	"private/order_book.cpp"
)

add_library(bursa STATIC ${source_list})

#warnings_as_errors(bursa)

target_compile_features(bursa PUBLIC cxx_std_23)

target_compile_options(bursa PRIVATE -fno-rtti -fno-exceptions)

target_include_directories(
	bursa
	PUBLIC "public"
)

target_link_libraries(bursa PRIVATE mimalloc fmt plf_colony unordered_dense)