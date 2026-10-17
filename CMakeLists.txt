cmake_minimum_required(VERSION 3.21)

set(PICO_PLATFORM rp2350 CACHE STRING "Target platform")
set(PICO_BOARD tufty2350 CACHE STRING "Board")
set(PICO_BOARD_HEADER_DIRS "${CMAKE_CURRENT_LIST_DIR}/board")
set(PICO_PSRAM_SIZE_BYTES 8388608)
set(PICO_SDK_PATH "${CMAKE_CURRENT_LIST_DIR}/lib/pico-sdk" CACHE PATH "Pinned Pico SDK" FORCE)
if(NOT EXISTS "${PICO_SDK_PATH}/pico_sdk_init.cmake")
  message(FATAL_ERROR "Dependencies missing. Run ./tools/build.sh or python3 tools/deps.py")
endif()
include("${PICO_SDK_PATH}/external/pico_sdk_import.cmake")

project(tufty_nes_ble VERSION 0.1.0 LANGUAGES C CXX ASM)
file(GLOB dependency_patches CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/patch/*/*.patch")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/deps.lock.json" "${CMAKE_CURRENT_SOURCE_DIR}/tools/deps.py" ${dependency_patches})
find_package(Python3 REQUIRED COMPONENTS Interpreter)
execute_process(COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/tools/deps.py" --radio --check
  RESULT_VARIABLE deps_result OUTPUT_VARIABLE deps_output ERROR_VARIABLE deps_error)
if(NOT deps_result EQUAL 0)
  message(FATAL_ERROR "Dependency validation failed:\n${deps_output}${deps_error}")
endif()
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
pico_sdk_init()
# Use the RAM pairing database. The SDK also lists the mutually exclusive TLV
# implementation unconditionally; do not build its Flash-backed database.
get_target_property(btstack_ble_sources pico_btstack_ble INTERFACE_SOURCES)
list(FILTER btstack_ble_sources EXCLUDE REGEX "/le_device_db_tlv\\.c$")
set_property(TARGET pico_btstack_ble PROPERTY INTERFACE_SOURCES "${btstack_ble_sources}")
# BLE-only polling architecture; the SDK's named poll target requires lwIP.
add_library(tufty_radio_poll INTERFACE)
target_link_libraries(tufty_radio_poll INTERFACE pico_cyw43_arch pico_async_context_poll)
target_compile_definitions(tufty_radio_poll INTERFACE PICO_CYW43_ARCH_POLL=1 CYW43_LWIP=0)
if(NOT PICO_SDK_VERSION_STRING VERSION_EQUAL "2.3.0")
  message(FATAL_ERROR "Restore the locked Pico SDK 2.3.0 checkout")
endif()

set(upstream "${CMAKE_CURRENT_SOURCE_DIR}/lib/tufty-nes/pico-nes")
set(fatfs "${upstream}/drivers/fatfs")
add_executable(nes
  src/main.cpp src/display.cpp src/menu.cpp src/storage.cpp src/fat_disk.cpp
  src/rom_store.cpp src/usb.cpp src/descriptors.cpp src/protocol.cpp
  src/power.cpp src/power_board.cpp src/power_reset.cpp
  src/disk_ui.cpp src/ble.cpp src/ble_stack.c src/hid_gamepad.cpp src/radio_wait.c
  "${upstream}/infones/InfoNES.cpp" "${upstream}/infones/InfoNES_Mapper.cpp"
  "${upstream}/infones/InfoNES_pAPU.cpp" "${upstream}/infones/K6502.cpp"
  "${fatfs}/ff.c" "${fatfs}/ffunicode.c"
  lib/littlefs/lfs.c lib/littlefs/lfs_util.c lib/cjson/cJSON.c)
target_include_directories(nes PRIVATE src config "${upstream}/infones" "${fatfs}"
  lib/littlefs lib/cjson)
target_compile_definitions(nes PRIVATE TUFTY2350
  INFONES_PLATFORM_POLL=tufty_service TUFTY_FATFS_CONFIG="tufty_ffconf.h"
  PICO_STACK_SIZE=0x1000 PICO_CORE1_STACK_SIZE=0x1000
  LFS_NO_DEBUG LFS_NO_WARN LFS_NO_ERROR LFS_NO_MALLOC cJSON_NESTING_LIMIT=8
  PICO_STDIO_USB_ENABLE_RESET_VIA_BAUD_RATE=0
  PICO_BTSTACK_CYW43_MAX_HCI_PROCESS_LOOP_COUNT=4)
target_link_libraries(nes PRIVATE pico_stdlib pico_multicore pico_flash pico_unique_id
  hardware_psram hardware_dma hardware_pio hardware_pwm hardware_clocks
  hardware_flash hardware_powman tinyusb_device
  tufty_radio_poll pico_btstack_ble pico_btstack_cyw43)
target_compile_options(nes PRIVATE -Wall -Wextra -Wno-unused-parameter)
target_link_options(nes PRIVATE "LINKER:-T,${CMAKE_CURRENT_SOURCE_DIR}/cmake/firmware_limit.ld"
  "LINKER:--print-memory-usage" "LINKER:--wrap=btstack_cyw43_init"
  "LINKER:--wrap=cyw43_await_background_or_timeout_us"
  "LINKER:--wrap=cyw43_delay_ms" "LINKER:--wrap=cyw43_delay_us")
set_property(TARGET nes APPEND PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/cmake/firmware_limit.ld")
pico_generate_pio_header(nes "${CMAKE_CURRENT_SOURCE_DIR}/src/parallel.pio")
pico_enable_stdio_uart(nes 0)
pico_enable_stdio_usb(nes 0)
pico_set_program_name(nes "Tufty NES Flash")
pico_set_program_version(nes "${PROJECT_VERSION}")
set_target_properties(nes PROPERTIES OUTPUT_NAME tufty-nes-flash)
# Preserve the SDK/native tagged compatibility block behavior; check separately.
pico_add_extra_outputs(nes)
