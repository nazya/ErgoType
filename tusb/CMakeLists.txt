add_library(usbd STATIC
        usb_descriptors.c
        usb_descriptors.h
        tusb_config.h
)

target_include_directories(usbd PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_CURRENT_LIST_DIR}/../rtos
        ${PICO_SDK_PATH}/lib/tinyusb/src
        ${PICO_SDK_PATH}/src/rp2_common/tinyusb/include
        ${PICO_SDK_PATH}/lib/tinyusb/hw
        ${FREERTOS_KERNEL_PATH}/include
        ${FREERTOS_KERNEL_PATH}/portable/ThirdParty/GCC/RP2040/include
        ${PICO_SDK_PATH}/src/common/pico_base_headers/include
        ${CMAKE_BINARY_DIR}/generated/pico_base
        ${PICO_SDK_PATH}/src/boards/include
        ${PICO_SDK_PATH}/src/rp2040/pico_platform/include
        ${PICO_SDK_PATH}/src/rp2040/hardware_regs/include
        ${PICO_SDK_PATH}/src/rp2040/hardware_structs/include
        ${PICO_SDK_PATH}/src/rp2_common/hardware_base/include
        ${PICO_SDK_PATH}/src/rp2_common/hardware_sync/include
        ${PICO_SDK_PATH}/src/rp2_common/hardware_sync_spin_lock/include
        ${PICO_SDK_PATH}/src/rp2_common/pico_platform_compiler/include
        ${PICO_SDK_PATH}/src/rp2_common/pico_platform_panic/include
        ${PICO_SDK_PATH}/src/rp2_common/pico_platform_sections/include
)

target_compile_definitions(usbd PUBLIC
        CFG_TUSB_MCU=OPT_MCU_RP2040
        CFG_TUSB_OS=${TINYUSB_OPT_OS}
        FREE_RTOS_KERNEL_SMP=1
        PICO_32BIT=1
        PICO_BOARD="pico"
        PICO_BUILD=1
        PICO_CONFIG_RTOS_ADAPTER_HEADER=${FREERTOS_KERNEL_PATH}/portable/ThirdParty/GCC/RP2040/include/freertos_sdk_config.h
        PICO_NO_HARDWARE=0
        PICO_ON_DEVICE=1
        PICO_RP2040=1
        PICO_USE_BLOCKED_RAM=0
)
