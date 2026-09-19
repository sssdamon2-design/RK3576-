execute_process(COMMAND "${APP}" --no-camera
    INPUT_FILE "${TEST_DATA}/cli_input.txt"
    OUTPUT_VARIABLE output ERROR_VARIABLE errors RESULT_VARIABLE status TIMEOUT 10)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "CLI failed: ${status}: ${errors}")
endif()
foreach(expected "Request Type: TEXT" "Request Type: VISION"
        "RK3576 Runtime/SDK not available" "No latest camera frame available"
        "frame_count=0" "Exiting.")
    string(FIND "${output}${errors}" "${expected}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "Missing CLI output: ${expected}\n${output}\n${errors}")
    endif()
endforeach()
string(FIND "${output}" "Assistant>" answer)
if(NOT answer EQUAL -1)
    message(FATAL_ERROR "Unavailable backend must not produce an answer")
endif()
execute_process(COMMAND "${APP}" --llm-model
    OUTPUT_VARIABLE output ERROR_VARIABLE errors RESULT_VARIABLE status TIMEOUT 5)
if(NOT status EQUAL 2)
    message(FATAL_ERROR "Missing CLI argument should exit 2: ${status}")
endif()
execute_process(COMMAND "${APP}" --help
    OUTPUT_VARIABLE output ERROR_VARIABLE errors RESULT_VARIABLE status TIMEOUT 5)
if(NOT status EQUAL 0 OR NOT output MATCHES "llm-model")
    message(FATAL_ERROR "CLI help failed")
endif()

foreach(input "cli_exit.txt" "cli_eof.txt")
    execute_process(COMMAND "${APP}" --no-camera
        INPUT_FILE "${TEST_DATA}/${input}"
        OUTPUT_VARIABLE output ERROR_VARIABLE errors RESULT_VARIABLE status TIMEOUT 5)
    if(NOT status EQUAL 0 OR NOT output MATCHES "Exiting.")
        message(FATAL_ERROR "CLI exit/EOF failed: ${status}")
    endif()
endforeach()
execute_process(COMMAND "${APP}" --no-camera --llm-model "${TEST_DATA}/not-present/model.rkllm"
    INPUT_FILE "${TEST_DATA}/cli_exit.txt"
    OUTPUT_VARIABLE output ERROR_VARIABLE errors RESULT_VARIABLE status TIMEOUT 5)
if(NOT status EQUAL 0 OR NOT errors MATCHES "Model file does not exist")
    message(FATAL_ERROR "CLI missing model diagnostic failed")
endif()
if(WIN32)
    execute_process(COMMAND "${APP}"
        INPUT_FILE "${TEST_DATA}/cli_exit.txt"
        OUTPUT_VARIABLE output ERROR_VARIABLE errors RESULT_VARIABLE status TIMEOUT 5)
    if(NOT status EQUAL 0 OR NOT errors MATCHES "V4L2 camera unavailable")
        message(FATAL_ERROR "Windows camera-unavailable path failed")
    endif()
endif()
