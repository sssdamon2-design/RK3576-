cmake_minimum_required(VERSION 3.10)

project(rk3576_multimodal)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)

add_executable(
    rk3576_multimodal

    src/main.cpp

    src/video/video_capture.cpp
    src/video/latest_frame_buffer.cpp
)

target_include_directories(
    rk3576_multimodal
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(
    rk3576_multimodal
    PRIVATE
    Threads::Threads
)