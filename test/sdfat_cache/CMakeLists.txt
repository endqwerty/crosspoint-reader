# Exercise the firmware's exact dependency and patches through its block-device API.
find_package(Python3 COMPONENTS Interpreter REQUIRED)
FetchContent_Declare(sdfat
  URL https://codeload.github.com/greiman/SdFat/tar.gz/refs/tags/2.3.1
  URL_HASH SHA256=a86ac8c6551f8294689569cf748f6aa81ceddd2317ef4f0815a0bb39b365c769
  SOURCE_SUBDIR crosspoint-host-no-cmake)
FetchContent_MakeAvailable(sdfat)
set(storage_project "${CMAKE_CURRENT_BINARY_DIR}/dependency")
set(storage_source "${storage_project}/.pio/libdeps/host/SdFat/src")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  ${REPO_ROOT}/platformio.ini
  ${REPO_ROOT}/scripts/patch_sdfat.py
  ${REPO_ROOT}/scripts/sdfat_patches/0001-invalidate-failed-cache-fill.patch
  ${REPO_ROOT}/scripts/sdfat_patches/0002-allow-separate-fat-cache-override.patch
  ${REPO_ROOT}/scripts/sdfat_patches/0003-guard-directory-output-pointer.patch
  ${CMAKE_CURRENT_SOURCE_DIR}/prepare_dependency.py)
execute_process(COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/prepare_dependency.py
  ${sdfat_SOURCE_DIR} ${storage_project} ${REPO_ROOT}
  RESULT_VARIABLE patch_result)
if(NOT patch_result EQUAL 0)
  message(FATAL_ERROR "Preparing the reviewed SdFat test dependency failed")
endif()
file(GLOB storage_sources
  ${storage_source}/common/*.cpp ${storage_source}/FatLib/*.cpp
  ${storage_source}/ExFatLib/*.cpp ${storage_source}/FsLib/*.cpp)
foreach(mode 0 1)
  set(target SdFatCache${mode})
  add_executable(${target} SdFatCacheTest.cpp ${storage_sources})
  target_include_directories(${target} PRIVATE ${storage_source})
  target_compile_definitions(${target} PRIVATE
    ENABLE_ARDUINO_FEATURES=0 ENABLE_ARDUINO_SERIAL=0 ENABLE_ARDUINO_STRING=0
    USE_BLOCK_DEVICE_INTERFACE=1 SPI_DRIVER_SELECT=3 USE_UTF8_LONG_NAMES=1
    DESTRUCTOR_CLOSES_FILE=1 USE_SEPARATE_FAT_CACHE=${mode})
  # Select the same byte-decoding helpers as ESP32 on ARM64 host machines.
  target_compile_options(${target} PRIVATE -U__ARM_FEATURE_UNALIGNED
    -include ${CMAKE_CURRENT_SOURCE_DIR}/HostSystem.h)
  add_test(NAME ${target} COMMAND ${target})
endforeach()
add_test(NAME SdFatPatchHook COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/test_patch_hook.py ${sdfat_SOURCE_DIR})
