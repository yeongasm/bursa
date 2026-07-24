include(${CMAKE_SOURCE_DIR}/cmake/Util.cmake)

set(
	source_list
	"private/order_book.cpp"
)

add_library(bursa STATIC ${source_list})

warnings_as_errors(bursa)

target_compile_features(bursa PUBLIC cxx_std_23)

target_compile_options(bursa PRIVATE -fno-rtti -fno-exceptions)

target_include_directories(
	bursa
	PUBLIC "public"
)

target_link_libraries(bursa PRIVATE mimalloc fmt plf_colony unordered_dense)