cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(apogee_flight LANGUAGES C CXX)

target_sources(app PRIVATE src/main.cpp)
target_include_directories(
    app
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../common/include"
)
target_compile_features(app PRIVATE cxx_std_20)
