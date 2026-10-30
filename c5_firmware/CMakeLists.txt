cmake_minimum_required(VERSION 3.16)

set(PROJECT_VER "1.0.0")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(cerberus_c5_sentinel)

# Scope the compatibility adapter to this build and this exact SDK blob.
get_target_property(c5_wifi_original esp_wifi_net80211 IMPORTED_LOCATION)
idf_build_get_property(c5_python PYTHON)
get_filename_component(c5_build_name "${CMAKE_BINARY_DIR}" NAME)
# PlatformIO classifies archives below BUILD_DIR as internally built components.
# Keep this prebuilt sibling outside that directory and first in library search.
# Preserve its basename because ESP-IDF's linker fragments refer to it by name.
set(c5_wifi_dir "${CMAKE_BINARY_DIR}/../cerberus-wifi-${c5_build_name}")
set(c5_wifi_local "${c5_wifi_dir}/libnet80211.a")
execute_process(COMMAND "${c5_python}" "${CMAKE_CURRENT_LIST_DIR}/../tools/prepare_c5_raw_library.py"
    --source "${c5_wifi_original}" --output "${c5_wifi_local}"
    --ar "${CMAKE_AR}" --objcopy "${CMAKE_OBJCOPY}" --nm "${CMAKE_NM}"
    RESULT_VARIABLE c5_wifi_result)
if(NOT c5_wifi_result EQUAL 0)
    message(FATAL_ERROR "C5 raw TX compatibility preparation failed")
endif()
set_property(TARGET esp_wifi_net80211 PROPERTY IMPORTED_LOCATION "${c5_wifi_local}")
target_link_directories(${CMAKE_PROJECT_NAME}.elf BEFORE PRIVATE "${c5_wifi_dir}")
# Pull our strong definition before the blob's weak definition is considered.
target_link_options(${CMAKE_PROJECT_NAME}.elf PRIVATE "-Wl,-u,ieee80211_raw_frame_sanity_check")
