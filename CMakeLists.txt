cmake_minimum_required(VERSION 3.29)
project(intel_jpeg_decoder_plugin)

set(CMAKE_CXX_STANDARD 20)
find_package(VPL REQUIRED)

message("${VPL_INCLUDE_DIR}")
message("${VPL_LIB_DIR}")
message("${VPL_BIN_DIR}")

find_package(OpenMP)
if (OpenMP_FOUND)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${OpenMP_C_FLAGS}")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${OpenMP_CXX_FLAGS}")
endif ()

if (CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 19.12.25835)
    set(CMAKE_CXX20_STANDARD_COMPILE_OPTION "/std:c++latest")
    set(CMAKE_CXX20_EXTENSION_COMPILE_OPTION "/std:c++latest")
endif ()

if (CMAKE_VERSION VERSION_LESS 3.18)
    set(DEV_MODULE Development)
else()
    set(DEV_MODULE Development.Module)
endif()

find_package(Python 3.11 COMPONENTS Interpreter ${DEV_MODULE} REQUIRED)
execute_process(
        COMMAND "${Python_EXECUTABLE}" -m nanobind --cmake_dir
        OUTPUT_STRIP_TRAILING_WHITESPACE OUTPUT_VARIABLE nanobind_ROOT)
find_package(nanobind CONFIG REQUIRED)

add_executable(intel_jpeg_decoder_plugin intel_jpeg_decoder_plugin.cpp)
target_link_directories(intel_jpeg_decoder_plugin PUBLIC ${VPL_LIB_DIR})
target_link_libraries(intel_jpeg_decoder_plugin PUBLIC ${VPL_LIB_DIR}/vpl.lib)
target_include_directories(intel_jpeg_decoder_plugin PUBLIC ${VPL_INCLUDE_DIR})

nanobind_add_module(test_ext test_ext.cpp)
target_link_directories(test_ext PUBLIC ${VPL_LIB_DIR})
target_link_libraries(test_ext PUBLIC ${VPL_LIB_DIR}/vpl.lib)
target_include_directories(test_ext PUBLIC ${VPL_INCLUDE_DIR})