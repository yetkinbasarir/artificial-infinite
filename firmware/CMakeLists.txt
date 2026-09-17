cmake_minimum_required(VERSION 3.18)

if(NOT PICO_SDK_PATH)
	# Search order: env, ../pico-sdk (next to the repo), firmware/pico-sdk, <repo root>/pico-sdk
	if(DEFINED ENV{PICO_SDK_PATH})
		set(PICO_SDK_PATH "$ENV{PICO_SDK_PATH}")
	elseif(EXISTS "${CMAKE_CURRENT_LIST_DIR}/../../pico-sdk/pico_sdk_init.cmake")
		set(PICO_SDK_PATH "${CMAKE_CURRENT_LIST_DIR}/../../pico-sdk")
	elseif(EXISTS "${CMAKE_CURRENT_LIST_DIR}/pico-sdk/pico_sdk_init.cmake")
		set(PICO_SDK_PATH "${CMAKE_CURRENT_LIST_DIR}/pico-sdk")
	elseif(EXISTS "${CMAKE_CURRENT_LIST_DIR}/../pico-sdk/pico_sdk_init.cmake")
		set(PICO_SDK_PATH "${CMAKE_CURRENT_LIST_DIR}/../pico-sdk")
	endif()
endif()

include(pico_sdk_import.cmake)

project(artificial_infinite C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

set(PIKOCODE_SOURCES
	${CMAKE_CURRENT_LIST_DIR}/src/main.cpp
	${CMAKE_CURRENT_LIST_DIR}/src/ClockSync.cpp
	${CMAKE_CURRENT_LIST_DIR}/src/PikoAudioBank.cpp
	${CMAKE_CURRENT_LIST_DIR}/src/PikoRuntime.cpp
	${CMAKE_CURRENT_LIST_DIR}/src/PikoSampleManager.cpp
	${CMAKE_CURRENT_LIST_DIR}/doth/WS2812.cpp 
	${CMAKE_CURRENT_LIST_DIR}/doth/usb_descriptors.c
)

# 248 MHz overclock: slow the boot2 flash SPI clock so XIP reads stay reliable.
pico_define_boot_stage2(slower_boot2 ${PICO_DEFAULT_BOOT_STAGE2_FILE})
target_compile_definitions(slower_boot2 PRIVATE PICO_FLASH_SPI_CLKDIV=4)

function(add_pikocore_firmware TARGET_NAME OUTPUT_BASENAME FLASH_SIZE_BYTES)
	add_executable(${TARGET_NAME} ${PIKOCODE_SOURCES})
	pico_set_boot_stage2(${TARGET_NAME} slower_boot2)
	set_target_properties(${TARGET_NAME} PROPERTIES OUTPUT_NAME "${OUTPUT_BASENAME}")
	pico_generate_pio_header(${TARGET_NAME} ${CMAKE_CURRENT_LIST_DIR}/doth/WS2812.pio)
	pico_generate_pio_header(${TARGET_NAME} ${CMAKE_CURRENT_LIST_DIR}/doth/onewiremidi.pio)

	target_link_libraries(${TARGET_NAME}
		pico_stdlib
		pico_multicore
		hardware_adc
		hardware_dma
		hardware_pio
		hardware_timer
		hardware_clocks
		hardware_pwm
		hardware_flash
		hardware_sync
		tinyusb_device
		tinyusb_board
	)

	set_property(TARGET ${TARGET_NAME} APPEND_STRING PROPERTY LINK_FLAGS "-Wl,--print-memory-usage")

	pico_set_binary_type(${TARGET_NAME} copy_to_ram)
	pico_enable_stdio_usb(${TARGET_NAME} 0)
	pico_enable_stdio_uart(${TARGET_NAME} 0)

	target_include_directories(${TARGET_NAME} PRIVATE
		${CMAKE_CURRENT_LIST_DIR}
		${CMAKE_CURRENT_LIST_DIR}/src
	)

	set(PIKOCODE_TARGET ${TARGET_NAME})
	include("${CMAKE_CURRENT_LIST_DIR}/target_compile_definitions.cmake")
	unset(PIKOCODE_TARGET)

	target_compile_definitions(${TARGET_NAME} PRIVATE
		PICO_FLASH_SIZE_BYTES=${FLASH_SIZE_BYTES}u
		PIKO_FIRMWARE_RESERVE=524288u
		PICO_STDIO_USB_CONNECTION_WITHOUT_DTR=1
		USBD_MANUFACTURER="infinitedigits"
		USBD_PRODUCT="pikocore"
	)

	pico_add_extra_outputs(${TARGET_NAME})

	add_custom_command(
		TARGET ${TARGET_NAME}
		POST_BUILD
		COMMAND ${CMAKE_COMMAND}
			-DBIN_FILE="$<TARGET_FILE_DIR:${TARGET_NAME}>/${OUTPUT_BASENAME}.bin"
			-DFIRMWARE_RESERVE=524288
			-P "${CMAKE_CURRENT_LIST_DIR}/cmake/check_firmware_reserve.cmake"
	)
endfunction()

add_pikocore_firmware(artificial_infinite "artificial-infinite-2mb" 2097152)
