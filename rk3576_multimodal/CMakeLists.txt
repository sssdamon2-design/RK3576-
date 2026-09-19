cmake_minimum_required(VERSION 3.16)
project(rk3576_multimodal LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
find_package(Threads REQUIRED)

option(BUILD_WITH_RKLLM "Require a real RKLLM SDK adapter (not included yet)" OFF)
option(BUILD_WITH_VLM "Require a real VLM SDK adapter (not included yet)" OFF)
if(BUILD_WITH_RKLLM OR BUILD_WITH_VLM)
    message(FATAL_ERROR
        "RK3576 Runtime/SDK not available: this repository has no verified SDK adapter. "
        "Provide RK3576 headers/libraries/models and implement the adapter before enabling these options.")
endif()

function(project_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /utf-8)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

add_library(multimodal_core
    src/video/latest_frame_buffer.cpp
    src/ai/llm_engine.cpp
    src/ai/vlm_engine.cpp
    src/ai/rk_backends_unavailable.cpp
    src/router/intent_router.cpp
    src/app/request_processor.cpp
)
target_include_directories(multimodal_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(multimodal_core PUBLIC Threads::Threads)
project_warnings(multimodal_core)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(CAMERA_CAPTURE_SOURCE src/video/video_capture.cpp)
else()
    set(CAMERA_CAPTURE_SOURCE src/video/video_capture_unavailable.cpp)
    message(STATUS "V4L2 unavailable: building portable core/CLI with explicit camera error")
endif()
add_library(multimodal_camera src/video/camera_service.cpp ${CAMERA_CAPTURE_SOURCE})
target_link_libraries(multimodal_camera PUBLIC multimodal_core)
project_warnings(multimodal_camera)

add_executable(rk3576_multimodal src/main.cpp src/app/cli.cpp)
target_link_libraries(rk3576_multimodal PRIVATE multimodal_camera)
project_warnings(rk3576_multimodal)

include(CTest)
if(BUILD_TESTING)
    add_executable(core_tests tests/core_tests.cpp)
    target_link_libraries(core_tests PRIVATE multimodal_core)
    project_warnings(core_tests)
    add_test(NAME core_tests COMMAND core_tests)
    # 测试专用设备替身仅链接此测试，绝不进入产品目标。
    add_executable(camera_service_tests
        tests/camera_service_tests.cpp tests/camera_test_capture.cpp
        src/video/camera_service.cpp)
    target_include_directories(camera_service_tests PRIVATE tests)
    target_link_libraries(camera_service_tests PRIVATE multimodal_core)
    project_warnings(camera_service_tests)
    add_test(NAME camera_service_tests COMMAND camera_service_tests)
    add_test(NAME cli_smoke COMMAND ${CMAKE_COMMAND}
        -DAPP=$<TARGET_FILE:rk3576_multimodal>
        -DTEST_DATA=${CMAKE_CURRENT_SOURCE_DIR}/tests
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_smoke.cmake)
    set_tests_properties(core_tests camera_service_tests cli_smoke PROPERTIES TIMEOUT 20)
endif()
