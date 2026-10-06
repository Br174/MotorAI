cmake_minimum_required(VERSION 3.22.1)
project(motorai_native LANGUAGES CXX)

add_library(motorai_native SHARED
    native_bridge.cpp
    motorai_core.cpp
)

target_compile_features(motorai_native PRIVATE cxx_std_20)
target_compile_options(motorai_native PRIVATE -O3)
find_library(log-lib log)
target_link_libraries(motorai_native ${log-lib})
