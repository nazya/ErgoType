add_library(tusb_glue STATIC
        usb_descriptors.c
        usb_descriptors.h
        tusb_config.h
)

if(PICO_PLATFORM STREQUAL "rp2040")
        set(FREERTOS_PORT_INCLUDE_DIR ${FREERTOS_KERNEL_PATH}/portable/ThirdParty/GCC/RP2040/include)
        set(FREERTOS_ADAPTER_HEADER ${FREERTOS_PORT_INCLUDE_DIR}/freertos_sdk_config.h)
        set(PICO_CHIP_DIR rp2040)
elseif(PICO_PLATFORM STREQUAL "rp2350-riscv")
        set(FREERTOS_PORT_INCLUDE_DIR ${FREERTOS_KERNEL_PATH}/portable/ThirdParty/Community-Supported-Ports/GCC/RP2350_RISC-V/include)
        set(FREERTOS_ADAPTER_HEADER ${FREERTOS_PORT_INCLUDE_DIR}/freertos_sdk_config.h)
        set(PICO_CHIP_DIR rp2350)
else()
        set(FREERTOS_PORT_INCLUDE_DIR ${FREERTOS_KERNEL_PATH}/portable/ThirdParty/Community-Supported-Ports/GCC/RP2350_ARM_NTZ/non_secure)
        set(FREERTOS_ADAPTER_HEADER ${FREERTOS_PORT_INCLUDE_DIR}/freertos_sdk_config.h)
        set(PICO_CHIP_DIR rp2350)
endif()

get_target_property(TINYUSB_COMMON_DEFINES tinyusb_common_base INTERFACE_COMPILE_DEFINITIONS)

target_include_directories(tusb_glue PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_CURRENT_LIST_DIR}/../rtos
        ${PICO_SDK_PATH}/lib/tinyusb/src
        ${PICO_SDK_PATH}/src/rp2_common/tinyusb/include
        ${PICO_SDK_PATH}/lib/tinyusb/hw
        ${FREERTOS_KERNEL_PATH}/include
        ${FREERTOS_PORT_INCLUDE_DIR}
        ${PICO_SDK_PATH}/src/common/pico_base_headers/include
        ${CMAKE_BINARY_DIR}/generated/pico_base
        ${PICO_SDK_PATH}/src/boards/include
        ${PICO_SDK_PATH}/src/${PICO_CHIP_DIR}/pico_platform/include
        ${PICO_SDK_PATH}/src/${PICO_CHIP_DIR}/hardware_regs/include
        ${PICO_SDK_PATH}/src/${PICO_CHIP_DIR}/hardware_structs/include
        ${PICO_SDK_PATH}/src/rp2_common/hardware_base/include
        ${PICO_SDK_PATH}/src/rp2_common/hardware_sync/include
        ${PICO_SDK_PATH}/src/rp2_common/hardware_sync_spin_lock/include
        ${PICO_SDK_PATH}/src/rp2_common/pico_platform_compiler/include
        ${PICO_SDK_PATH}/src/rp2_common/pico_platform_panic/include
        ${PICO_SDK_PATH}/src/rp2_common/pico_platform_sections/include
)

target_compile_definitions(tusb_glue PUBLIC
        ${TINYUSB_COMMON_DEFINES}
        PICO_CONFIG_RTOS_ADAPTER_HEADER=${FREERTOS_ADAPTER_HEADER}
)

target_link_libraries(tusb_glue PUBLIC
        pico_base_headers
)
