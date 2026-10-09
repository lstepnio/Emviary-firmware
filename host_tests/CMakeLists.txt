cmake_minimum_required(VERSION 3.14)
project(esp32_photoframe_tests)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Fetch Google Test
include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.14.0
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Enable testing
enable_testing()

# Cron engine tests
add_executable(
  cron_test
  test_cron.cpp
  ../main/cron.c
)

target_link_libraries(
  cron_test
  GTest::gtest_main
)

target_include_directories(
  cron_test
  PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../main
)

# Wake-schedule simulation (early-wake re-sleep loop vs. the real cron engine)
add_executable(
  wake_schedule_test
  test_wake_schedule.cpp
  ../main/cron.c
)

target_link_libraries(
  wake_schedule_test
  GTest::gtest_main
)

target_include_directories(
  wake_schedule_test
  PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../main
)

# Backoff between failed unattended network wakes (#121), against the real cron engine
add_executable(
  network_backoff_test
  test_network_backoff.cpp
  ../main/network_backoff.c
  ../main/cron.c
)

target_link_libraries(
  network_backoff_test
  GTest::gtest_main
)

target_include_directories(
  network_backoff_test
  PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../main
)

# HTTP Basic credential checking for the optional device API password (#130)
add_executable(
  http_auth_test
  test_http_auth.cpp
  ../main/http_auth.c
)

target_link_libraries(
  http_auth_test
  GTest::gtest_main
)

target_include_directories(
  http_auth_test
  PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../main
)

# Last-crash record: reason and the one-line summary
add_executable(
  crash_record_test
  test_crash_record.cpp
  ../main/crash_record.c
)

target_link_libraries(
  crash_record_test
  GTest::gtest_main
)

target_include_directories(
  crash_record_test
  PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../main
)

# Station reconnect policy: only an AP that keeps rejecting the credentials
# fails a connect; an absent one never does
add_executable(
  wifi_retry_test
  test_wifi_retry.cpp
  ../main/wifi_retry_policy.c
)

target_link_libraries(
  wifi_retry_test
  GTest::gtest_main
)

target_include_directories(
  wifi_retry_test
  PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${CMAKE_CURRENT_SOURCE_DIR}/../main
)

# JPEG frame size as tjpgd will decode it (last SOF0 before SOS)
add_executable(
  jpeg_header_test
  test_jpeg_header.cpp
  ../main/jpeg_header.c
)

target_link_libraries(
  jpeg_header_test
  GTest::gtest_main
)

target_include_directories(
  jpeg_header_test
  PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../main
)

# Discover tests
include(GoogleTest)
gtest_discover_tests(cron_test)
gtest_discover_tests(wake_schedule_test)
gtest_discover_tests(http_auth_test)
gtest_discover_tests(network_backoff_test)
gtest_discover_tests(crash_record_test)
gtest_discover_tests(wifi_retry_test)
gtest_discover_tests(jpeg_header_test)

# On-device image pipeline characterization tests (host build of
# main/image_processor.c against ESP-IDF stubs + system libpng)
find_package(PNG REQUIRED)

add_executable(
  image_pipeline_test
  test_image_pipeline.cpp
  ../main/image_processor.c
  ../main/jpeg_header.c
  stubs/esp_stubs.c
  stubs/fake_display_manager.c
  stubs/fake_config_manager.c
  stubs/fake_processing_settings.c
)

target_include_directories(
  image_pipeline_test
  PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${CMAKE_CURRENT_SOURCE_DIR}/../main
)

target_link_libraries(
  image_pipeline_test
  GTest::gtest_main
  PNG::PNG
  m
)

gtest_discover_tests(image_pipeline_test)

# .current.* / thumbnail lifecycle tests for the shared display flow.
# FS_MOUNT_POINT is redirected to a local directory so the tests exercise
# the real path scheme against the host filesystem.
add_executable(
  display_flow_test
  test_display_flow.cpp
  ../main/display_flow.c
  ../main/image_processor.c
  ../main/jpeg_header.c
  stubs/esp_stubs.c
  stubs/fake_display_manager.c
  stubs/fake_config_manager.c
  stubs/fake_processing_settings.c
  stubs/fake_storage.c
)

target_compile_definitions(
  display_flow_test
  PRIVATE
  FS_MOUNT_POINT="pf_storage"
)

target_include_directories(
  display_flow_test
  PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${CMAKE_CURRENT_SOURCE_DIR}/../main
)

target_link_libraries(
  display_flow_test
  GTest::gtest_main
  PNG::PNG
  m
)

gtest_discover_tests(display_flow_test)

add_executable(wifi_network_validation_test test_wifi_network_validation.cpp ../main/wifi_network_validation.c)
target_include_directories(wifi_network_validation_test PRIVATE ../main)
target_link_libraries(wifi_network_validation_test GTest::gtest_main)
gtest_discover_tests(wifi_network_validation_test)

add_executable(button_debounce_test test_button_debounce.cpp)
target_include_directories(button_debounce_test PRIVATE ../main)
target_link_libraries(button_debounce_test GTest::gtest_main)
gtest_discover_tests(button_debounce_test)

add_executable(config_validation_test test_config_validation.cpp ../main/config_validation.c)
target_include_directories(config_validation_test PRIVATE ../main)
target_link_libraries(config_validation_test GTest::gtest_main)
gtest_discover_tests(config_validation_test)

find_package(ZLIB REQUIRED)
add_executable(epdgz_test test_epdgz.cpp ../components/epaper_src/GUI_EPDGZfile.c)
target_include_directories(epdgz_test PRIVATE stubs ../components/epaper_src ../components/epaper_src/Fonts)
target_link_libraries(epdgz_test GTest::gtest_main ZLIB::ZLIB)
gtest_discover_tests(epdgz_test)

add_executable(download_policy_test test_download_policy.cpp)
target_include_directories(download_policy_test PRIVATE ../main)
target_link_libraries(download_policy_test GTest::gtest_main)
gtest_discover_tests(download_policy_test)

# Periodic checks must leave the shared ESP timer dispatch task promptly.
add_executable(periodic_tasks_test test_periodic_tasks.c ../main/periodic_tasks.c)
target_include_directories(periodic_tasks_test PRIVATE periodic_stubs ../main)
add_test(NAME periodic_tasks_dispatch COMMAND periodic_tasks_test)

# Fault injection must preserve LittleFS bytes during automatic recovery.
add_executable(storage_mount_test test_storage_mount.c ../main/storage.c)
target_include_directories(storage_mount_test PRIVATE storage_stubs ../main)
target_compile_options(storage_mount_test PRIVATE -include ${CMAKE_CURRENT_SOURCE_DIR}/storage_stubs/config_override.h)
add_test(NAME storage_mount_preservation COMMAND storage_mount_test)

add_executable(epaper_driver_test test_epaper_driver.cpp ../components/epaper_driver_ed2208_gca/src/driver_ed2208_gca.c)
target_include_directories(epaper_driver_test PRIVATE epaper_stubs stubs ../components/epaper/include)
target_compile_definitions(epaper_driver_test PRIVATE CONFIG_EP_DRIVER_ED2208_GCA=1)
target_link_libraries(epaper_driver_test GTest::gtest_main)
gtest_discover_tests(epaper_driver_test)

# Real navigation module with a one-slot queue and contended recursive mutex.
add_executable(navigation_state_test test_navigation_state.cpp ../main/navigation_state.c)
target_include_directories(navigation_state_test PRIVATE navigation_stubs stubs ../main)
target_compile_definitions(navigation_state_test PRIVATE EMVIARY_CLOUD_NAVIGATION=1)
target_link_libraries(navigation_state_test GTest::gtest_main)
gtest_discover_tests(navigation_state_test)
