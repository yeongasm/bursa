include(${CMAKE_SOURCE_DIR}/cmake/Util.cmake)

set(
  source_list
  "main.cpp"
  "nanobench.cpp"
)

add_executable(sandbox ${source_list})
warnings_as_errors(sandbox)
target_compile_features(sandbox PUBLIC cxx_std_23)
target_compile_options(sandbox PRIVATE -fno-rtti -fno-exceptions)
target_link_libraries(sandbox PUBLIC mimalloc bursa nanobench fmt unordered_dense)