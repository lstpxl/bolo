if(NOT DEFINED BUILD_NUMBER_FILE OR NOT DEFINED OUTPUT_HEADER)
    message(FATAL_ERROR "BUILD_NUMBER_FILE and OUTPUT_HEADER must be provided")
endif()

set(current_build_number 0)
if(EXISTS "${BUILD_NUMBER_FILE}")
    file(READ "${BUILD_NUMBER_FILE}" current_build_number_raw)
    string(STRIP "${current_build_number_raw}" current_build_number_raw)
    if(NOT current_build_number_raw STREQUAL "")
        set(current_build_number "${current_build_number_raw}")
    endif()
endif()

math(EXPR next_build_number "${current_build_number} + 1")

get_filename_component(output_header_dir "${OUTPUT_HEADER}" DIRECTORY)
file(MAKE_DIRECTORY "${output_header_dir}")
file(WRITE "${BUILD_NUMBER_FILE}" "${next_build_number}\n")
file(WRITE "${OUTPUT_HEADER}" "#pragma once\n#define BOLO_BUILD_NUMBER ${next_build_number}\n")
