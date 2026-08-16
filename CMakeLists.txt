cmake_minimum_required(VERSION 3.20)

project(ApogeeFC VERSION 0.1.0 LANGUAGES CXX)

add_library(apogee_common INTERFACE)
target_include_directories(
    apogee_common
    INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/common/include"
)
target_compile_features(apogee_common INTERFACE cxx_std_20)
target_compile_options(
    apogee_common
    INTERFACE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall;-Wextra;-Wpedantic;-Wconversion;-Wsign-conversion;-Werror>
)

add_executable(
    apogee_ground_client
    host/apogee_ground_client.cpp
)
target_include_directories(
    apogee_ground_client
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/host/include"
)
target_link_libraries(apogee_ground_client PRIVATE apogee_common)

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests/host)
endif()
