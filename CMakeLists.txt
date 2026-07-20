# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.1.1)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.1.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_LIST_DIR}/boards)
set(PICO_BOARD pico CACHE STRING "Board type")

# set(PICO_BOARD waveshare_rp2040_pizero CACHE STRING "Board type" FORCE)

# set(PICO_BOARD pico2 CACHE STRING "Board type" FORCE)

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

# set(FREERTOS_HEAP 2)
set(FREERTOS_KERNEL_PATH ${CMAKE_CURRENT_SOURCE_DIR}/FreeRTOS-Kernel)
include(${CMAKE_CURRENT_SOURCE_DIR}/rtos/FreeRTOS_Kernel_import.cmake)


include(${CMAKE_CURRENT_SOURCE_DIR}/coreJSON/jsonFilePaths.cmake)

set(PROJECT_NAME ErgoType)
project(${PROJECT_NAME} C CXX ASM)

set(FATFS_FFCONF ${PICO_SDK_PATH}/lib/tinyusb/lib/fatfs/source/ffconf.h)
file(READ ${FATFS_FFCONF} FATFS_FFCONF_CONTENT)
if(NOT FATFS_FFCONF_CONTENT MATCHES "#define[ \t]+FF_USE_STRFUNC[ \t]+1")
    string(REGEX REPLACE "#define[ \t]+FF_USE_STRFUNC[ \t]+[0-9]+"
           "#define FF_USE_STRFUNC 1"
           FATFS_FFCONF_CONTENT
           "${FATFS_FFCONF_CONTENT}")
    file(WRITE ${FATFS_FFCONF} "${FATFS_FFCONF_CONTENT}")
endif()


# Ensure Pico SDK's TinyUSB is built with the FreeRTOS OSAL (CFG_TUSB_OS).
# This must be set before pico_sdk_init() (where tinyusb_device/tinyusb_board targets are created).
set(TINYUSB_OPT_OS OPT_OS_FREERTOS)
set(PICO_PIO_USB_PATH ${CMAKE_CURRENT_SOURCE_DIR}/Pico-PIO-USB)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Pico SDK TinyUSB commit 86ad6e56 omits tuh_mount_cb() for hub addresses, has
# no host-owner entry which starts enumeration without requeueing itself, and
# cannot retire an exact EP0 or enumeration owner after a lost HCD completion
# event. Generate one build-local source with those audited compatibility
# deltas; never mutate the installed SDK.
set(TINYUSB_USBH_SOURCE "${PICO_TINYUSB_PATH}/src/host/usbh.c")
set(TINYUSB_USBH_PORT_SOURCE
    "${CMAKE_CURRENT_BINARY_DIR}/tinyusb-usbh-port.c")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
             "${TINYUSB_USBH_SOURCE}")
file(SHA256 "${TINYUSB_USBH_SOURCE}" TINYUSB_USBH_SHA256)
if(NOT TINYUSB_USBH_SHA256 STREQUAL
   "eeb692225f34e3ea9fbd195e0b4b80a5fb5f1d36b092a42a78d6b88a37574ec0")
    message(FATAL_ERROR "Pinned TinyUSB usbh.c changed; re-audit the port")
endif()
file(READ "${TINYUSB_USBH_SOURCE}" TINYUSB_USBH_CONTENT)
set(TINYUSB_USBH_HUB_MOUNT_UPSTREAM [=[
    enum_full_complete();

    if (is_hub_addr(dev_addr)) {
      TU_LOG_USBH("HUB address = %u is mounted\r\n", dev_addr);
    }else {
      // Invoke callback if available
      if (tuh_mount_cb) tuh_mount_cb(dev_addr);
    }
]=])
set(TINYUSB_USBH_HUB_MOUNT_PORT [=[
    // Upstream TinyUSB:
    // enum_full_complete();
    // Firmware's terminal owner refuses a late successful mount after its
    // no-progress watchdog has begun retiring this enumeration epoch.
    if (!enum_full_complete(true)) return;

    // Upstream TinyUSB:
    // if (is_hub_addr(dev_addr)) {
    //   TU_LOG_USBH("HUB address = %u is mounted\r\n", dev_addr);
    // }else {
    //   // Invoke callback if available
    //   if (tuh_mount_cb) tuh_mount_cb(dev_addr);
    // }
    // Firmware also needs the exact post-enumeration fence for hub lifecycle.
    if (is_hub_addr(dev_addr)) {
      TU_LOG_USBH("HUB address = %u is mounted\r\n", dev_addr);
    }
    if (tuh_mount_cb) tuh_mount_cb(dev_addr);
]=])
string(FIND "${TINYUSB_USBH_CONTENT}"
       "${TINYUSB_USBH_HUB_MOUNT_UPSTREAM}" TINYUSB_USBH_HOOK_OFFSET)
if(TINYUSB_USBH_HOOK_OFFSET EQUAL -1)
    message(FATAL_ERROR
            "Pinned TinyUSB usbh.c hub mount anchor changed; re-audit the port")
endif()
string(LENGTH "${TINYUSB_USBH_HUB_MOUNT_UPSTREAM}"
       TINYUSB_USBH_HOOK_LENGTH)
math(EXPR TINYUSB_USBH_HOOK_TAIL_OFFSET
     "${TINYUSB_USBH_HOOK_OFFSET} + ${TINYUSB_USBH_HOOK_LENGTH}")
string(SUBSTRING "${TINYUSB_USBH_CONTENT}"
       ${TINYUSB_USBH_HOOK_TAIL_OFFSET} -1 TINYUSB_USBH_HOOK_TAIL)
string(FIND "${TINYUSB_USBH_HOOK_TAIL}"
       "${TINYUSB_USBH_HUB_MOUNT_UPSTREAM}" TINYUSB_USBH_SECOND_HOOK)
if(NOT TINYUSB_USBH_SECOND_HOOK EQUAL -1)
    message(FATAL_ERROR "TinyUSB hub mount anchor is not unique")
endif()
string(REPLACE "${TINYUSB_USBH_HUB_MOUNT_UPSTREAM}"
       "${TINYUSB_USBH_HUB_MOUNT_PORT}"
       TINYUSB_USBH_PORT_CONTENT "${TINYUSB_USBH_CONTENT}")

# Upstream TinyUSB: no equivalent. Recovery is already in the host owner when
# hub reset completes, so enter the same enum_new_device() path directly. A
# blocking queue send here could deadlock TinyUSB's only host consumer.
set(TINYUSB_USBH_REATTACH_UPSTREAM [=[
  return true;
}

static uint8_t get_new_address(bool is_hub) {
]=])
set(TINYUSB_USBH_REATTACH_PORT [=[
  return true;
}

// Upstream TinyUSB: no equivalent; host-owner attach without queue recursion.
bool usbh_port_attach_on_host(uint8_t rhport, uint8_t hub_addr, uint8_t hub_port) {
  TU_VERIFY(!_dev0.enumerating && _ctrl_xfer.stage == CONTROL_STAGE_IDLE);

  hcd_event_t event = {
    .rhport = rhport,
    .event_id = HCD_EVENT_DEVICE_ATTACH,
    .connection = {
      .hub_addr = hub_addr,
      .hub_port = hub_port
    }
  };

  _dev0.enumerating = 1;
  usbh_port_enum_start_on_host();
  return enum_new_device(&event);
}

// Upstream TinyUSB: no equivalent; atomic raced-epoch close and direct attach.
bool usbh_port_reenumerate_on_host(uint8_t rhport, uint8_t hub_addr, uint8_t hub_port) {
  TU_VERIFY(!_dev0.enumerating && _ctrl_xfer.stage == CONTROL_STAGE_IDLE);
  TU_VERIFY(tuh_reenumerate_begin_cb(rhport, hub_addr, hub_port));

  process_removing_device(rhport, hub_addr, hub_port);
  return usbh_port_attach_on_host(rhport, hub_addr, hub_port);
}

static uint8_t get_new_address(bool is_hub) {
]=])
string(FIND "${TINYUSB_USBH_PORT_CONTENT}"
       "${TINYUSB_USBH_REATTACH_UPSTREAM}" TINYUSB_USBH_REATTACH_OFFSET)
if(TINYUSB_USBH_REATTACH_OFFSET EQUAL -1)
    message(FATAL_ERROR
            "Pinned TinyUSB enum helper anchor changed; re-audit the port")
endif()
string(LENGTH "${TINYUSB_USBH_REATTACH_UPSTREAM}"
       TINYUSB_USBH_REATTACH_LENGTH)
math(EXPR TINYUSB_USBH_REATTACH_TAIL_OFFSET
     "${TINYUSB_USBH_REATTACH_OFFSET} + ${TINYUSB_USBH_REATTACH_LENGTH}")
string(SUBSTRING "${TINYUSB_USBH_PORT_CONTENT}"
       ${TINYUSB_USBH_REATTACH_TAIL_OFFSET} -1 TINYUSB_USBH_REATTACH_TAIL)
string(FIND "${TINYUSB_USBH_REATTACH_TAIL}"
       "${TINYUSB_USBH_REATTACH_UPSTREAM}" TINYUSB_USBH_SECOND_REATTACH)
if(NOT TINYUSB_USBH_SECOND_REATTACH EQUAL -1)
    message(FATAL_ERROR "TinyUSB enum helper anchor is not unique")
endif()
string(REPLACE "${TINYUSB_USBH_REATTACH_UPSTREAM}"
       "${TINYUSB_USBH_REATTACH_PORT}"
       TINYUSB_USBH_PORT_CONTENT "${TINYUSB_USBH_PORT_CONTENT}")

# Upstream TinyUSB: no equivalent. This weak hook authorizes the direct
# re-enumeration helper above; the transport provides the strong callback.
set(TINYUSB_USBH_REATTACH_HOOK_UPSTREAM [=[
TU_ATTR_WEAK void tuh_event_hook_cb(uint8_t rhport, uint32_t eventid, bool in_isr) {
  (void) rhport;
  (void) eventid;
  (void) in_isr;
}
]=])
set(TINYUSB_USBH_REATTACH_HOOK_PORT [=[
TU_ATTR_WEAK void tuh_event_hook_cb(uint8_t rhport, uint32_t eventid, bool in_isr) {
  (void) rhport;
  (void) eventid;
  (void) in_isr;
}

// Upstream TinyUSB: no equivalent; firmware's direct reattach generation fence.
TU_ATTR_WEAK bool tuh_reenumerate_begin_cb(uint8_t rhport, uint8_t hub_addr, uint8_t hub_port) {
  (void) rhport;
  (void) hub_addr;
  (void) hub_port;
  return false;
}
]=])
string(FIND "${TINYUSB_USBH_PORT_CONTENT}"
       "${TINYUSB_USBH_REATTACH_HOOK_UPSTREAM}"
       TINYUSB_USBH_REATTACH_HOOK_OFFSET)
if(TINYUSB_USBH_REATTACH_HOOK_OFFSET EQUAL -1)
    message(FATAL_ERROR
            "Pinned TinyUSB usbh.c weak-hook anchor changed; re-audit the port")
endif()
string(LENGTH "${TINYUSB_USBH_REATTACH_HOOK_UPSTREAM}"
       TINYUSB_USBH_REATTACH_HOOK_LENGTH)
math(EXPR TINYUSB_USBH_REATTACH_HOOK_TAIL_OFFSET
     "${TINYUSB_USBH_REATTACH_HOOK_OFFSET} + ${TINYUSB_USBH_REATTACH_HOOK_LENGTH}")
string(SUBSTRING "${TINYUSB_USBH_PORT_CONTENT}"
       ${TINYUSB_USBH_REATTACH_HOOK_TAIL_OFFSET} -1
       TINYUSB_USBH_REATTACH_HOOK_TAIL)
string(FIND "${TINYUSB_USBH_REATTACH_HOOK_TAIL}"
       "${TINYUSB_USBH_REATTACH_HOOK_UPSTREAM}"
       TINYUSB_USBH_SECOND_REATTACH_HOOK)
if(NOT TINYUSB_USBH_SECOND_REATTACH_HOOK EQUAL -1)
    message(FATAL_ERROR "TinyUSB weak-hook anchor is not unique")
endif()
string(REPLACE "${TINYUSB_USBH_REATTACH_HOOK_UPSTREAM}"
       "${TINYUSB_USBH_REATTACH_HOOK_PORT}"
       TINYUSB_USBH_PORT_CONTENT "${TINYUSB_USBH_PORT_CONTENT}")

# Upstream TinyUSB has no exact-owner recovery after an HCD completion event is
# lost. Preserve its ordinary completion helper verbatim, then add a build-local
# host-owner entry which matches daddr + callback + user_data before synthesizing
# the same terminal TIMEOUT giveback.
set(TINYUSB_USBH_CONTROL_RECOVER_UPSTREAM [=[
static bool usbh_control_xfer_cb (uint8_t daddr, uint8_t ep_addr, xfer_result_t result, uint32_t xferred_bytes) {
]=])
set(TINYUSB_USBH_CONTROL_RECOVER_PORT [=[
// Upstream TinyUSB: no equivalent; firmware must terminate an exact
// callback/user_data owner after bounded PIO completion-drain failure.
int usbh_port_control_recover_on_host(uint8_t daddr,
                                      tuh_xfer_cb_t complete_cb,
                                      uintptr_t user_data) {
  if (_ctrl_xfer.stage == CONTROL_STAGE_IDLE) {
    return 0;
  }

  if (_ctrl_xfer.daddr != daddr ||
      _ctrl_xfer.complete_cb != complete_cb ||
      _ctrl_xfer.user_data != user_data) {
    return -1;
  }

  (void) hcd_edpt_abort_xfer(usbh_get_rhport(daddr), daddr, 0);
  _control_xfer_complete(daddr, XFER_RESULT_TIMEOUT);
  return 1;
}

static bool usbh_control_xfer_cb (uint8_t daddr, uint8_t ep_addr, xfer_result_t result, uint32_t xferred_bytes) {
]=])
string(FIND "${TINYUSB_USBH_PORT_CONTENT}"
       "${TINYUSB_USBH_CONTROL_RECOVER_UPSTREAM}"
       TINYUSB_USBH_CONTROL_RECOVER_OFFSET)
if(TINYUSB_USBH_CONTROL_RECOVER_OFFSET EQUAL -1)
    message(FATAL_ERROR
            "Pinned TinyUSB control-complete anchor changed; re-audit the port")
endif()
string(LENGTH "${TINYUSB_USBH_CONTROL_RECOVER_UPSTREAM}"
       TINYUSB_USBH_CONTROL_RECOVER_LENGTH)
math(EXPR TINYUSB_USBH_CONTROL_RECOVER_TAIL_OFFSET
     "${TINYUSB_USBH_CONTROL_RECOVER_OFFSET} + ${TINYUSB_USBH_CONTROL_RECOVER_LENGTH}")
string(SUBSTRING "${TINYUSB_USBH_PORT_CONTENT}"
       ${TINYUSB_USBH_CONTROL_RECOVER_TAIL_OFFSET} -1
       TINYUSB_USBH_CONTROL_RECOVER_TAIL)
string(FIND "${TINYUSB_USBH_CONTROL_RECOVER_TAIL}"
       "${TINYUSB_USBH_CONTROL_RECOVER_UPSTREAM}"
       TINYUSB_USBH_SECOND_CONTROL_RECOVER)
if(NOT TINYUSB_USBH_SECOND_CONTROL_RECOVER EQUAL -1)
    message(FATAL_ERROR "TinyUSB control-complete anchor is not unique")
endif()
string(REPLACE "${TINYUSB_USBH_CONTROL_RECOVER_UPSTREAM}"
       "${TINYUSB_USBH_CONTROL_RECOVER_PORT}"
       TINYUSB_USBH_PORT_CONTENT "${TINYUSB_USBH_PORT_CONTENT}")

include("${CMAKE_CURRENT_SOURCE_DIR}/usb_host/tinyusb-usbh-enum-port.cmake")
file(WRITE "${TINYUSB_USBH_PORT_SOURCE}" "${TINYUSB_USBH_PORT_CONTENT}")

get_target_property(TINYUSB_HOST_BASE_SOURCES
                    tinyusb_host_base INTERFACE_SOURCES)
set(TINYUSB_USBH_SOURCE_COUNT 0)
foreach(TINYUSB_HOST_BASE_SOURCE IN LISTS TINYUSB_HOST_BASE_SOURCES)
    if(TINYUSB_HOST_BASE_SOURCE STREQUAL "${TINYUSB_USBH_SOURCE}")
        math(EXPR TINYUSB_USBH_SOURCE_COUNT
             "${TINYUSB_USBH_SOURCE_COUNT} + 1")
    endif()
endforeach()
if(NOT TINYUSB_USBH_SOURCE_COUNT EQUAL 1)
    message(FATAL_ERROR
            "TinyUSB host source must occur exactly once; re-audit the port")
endif()
list(FIND TINYUSB_HOST_BASE_SOURCES "${TINYUSB_USBH_SOURCE}"
     TINYUSB_USBH_SOURCE_INDEX)
list(REMOVE_AT TINYUSB_HOST_BASE_SOURCES ${TINYUSB_USBH_SOURCE_INDEX})
set_property(TARGET tinyusb_host_base PROPERTY INTERFACE_SOURCES
             ${TINYUSB_HOST_BASE_SOURCES})
target_sources(tinyusb_host_base INTERFACE "${TINYUSB_USBH_PORT_SOURCE}")
set_source_files_properties("${TINYUSB_USBH_PORT_SOURCE}" PROPERTIES
                            INCLUDE_DIRECTORIES
                            "${PICO_TINYUSB_PATH}/src/host")

# TinyUSB's HID class assumes interface -> HID -> endpoint layout and prefetches
# the report descriptor into its 512-byte enumeration buffer before task-side
# Linux `usbhid_parse()` can request it. Generate a second build-local source
# which scans the bounded interface extras like Linux, leaves only endpoint and
# class-slot enumeration in TinyUSB, and hands SET_IDLE plus report-descriptor
# ownership/retry to that parser. Linux relies on the reset-default Report
# protocol and does not emit a generic SET_PROTOCOL request.
# The installed SDK remains untouched.
set(TINYUSB_HIDH_SOURCE
    "${PICO_TINYUSB_PATH}/src/class/hid/hid_host.c")
set(TINYUSB_HIDH_PORT_SOURCE
    "${CMAKE_CURRENT_BINARY_DIR}/tinyusb-hid-host-port.c")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
             "${TINYUSB_HIDH_SOURCE}")
file(SHA256 "${TINYUSB_HIDH_SOURCE}" TINYUSB_HIDH_SHA256)
if(NOT TINYUSB_HIDH_SHA256 STREQUAL
   "c4f1125c2235dc870b280343df5d4c088824c6d40430ea65cd21cf56f84ddea9")
    message(FATAL_ERROR "Pinned TinyUSB hid_host.c changed; re-audit the port")
endif()
file(READ "${TINYUSB_HIDH_SOURCE}" TINYUSB_HIDH_CONTENT)
set(TINYUSB_HIDH_OPEN_UPSTREAM [=[
bool hidh_open(uint8_t rhport, uint8_t daddr, tusb_desc_interface_t const* desc_itf, uint16_t max_len) {
  (void) rhport;
  (void) max_len;

  TU_VERIFY(TUSB_CLASS_HID == desc_itf->bInterfaceClass);
  TU_LOG_DRV("[%u] HID opening Interface %u\r\n", daddr, desc_itf->bInterfaceNumber);

  // len = interface + hid + n*endpoints
  uint16_t const drv_len = (uint16_t) (sizeof(tusb_desc_interface_t) + sizeof(tusb_hid_descriptor_hid_t) +
                                       desc_itf->bNumEndpoints * sizeof(tusb_desc_endpoint_t));
  TU_ASSERT(max_len >= drv_len);
  uint8_t const* p_desc = (uint8_t const*) desc_itf;

  //------------- HID descriptor -------------//
  p_desc = tu_desc_next(p_desc);
  tusb_hid_descriptor_hid_t const* desc_hid = (tusb_hid_descriptor_hid_t const*) p_desc;
  TU_ASSERT(HID_DESC_TYPE_HID == desc_hid->bDescriptorType);

  hidh_interface_t* p_hid = find_new_itf();
  TU_ASSERT(p_hid); // not enough interface, try to increase CFG_TUH_HID
  p_hid->daddr = daddr;

  //------------- Endpoint Descriptors -------------//
  p_desc = tu_desc_next(p_desc);
  tusb_desc_endpoint_t const* desc_ep = (tusb_desc_endpoint_t const*) p_desc;

  for (int i = 0; i < desc_itf->bNumEndpoints; i++) {
    TU_ASSERT(TUSB_DESC_ENDPOINT == desc_ep->bDescriptorType);
    TU_ASSERT(tuh_edpt_open(daddr, desc_ep));

    if (tu_edpt_dir(desc_ep->bEndpointAddress) == TUSB_DIR_IN) {
      p_hid->ep_in = desc_ep->bEndpointAddress;
      p_hid->epin_size = tu_edpt_packet_size(desc_ep);
    } else {
      p_hid->ep_out = desc_ep->bEndpointAddress;
      p_hid->epout_size = tu_edpt_packet_size(desc_ep);
    }

    p_desc = tu_desc_next(p_desc);
    desc_ep = (tusb_desc_endpoint_t const*) p_desc;
  }

  p_hid->itf_num = desc_itf->bInterfaceNumber;

  // Assume bNumDescriptors = 1
  p_hid->report_desc_type = desc_hid->bReportType;
  p_hid->report_desc_len = tu_unaligned_read16(&desc_hid->wReportLength);

  // Per HID Specs: default is Report protocol, though we will force Boot protocol when set_config
  p_hid->protocol_mode = _hidh_default_protocol;
  if (HID_SUBCLASS_BOOT == desc_itf->bInterfaceSubClass) {
    p_hid->itf_protocol = desc_itf->bInterfaceProtocol;
  }

  return true;
}

]=])
set(TINYUSB_HIDH_OPEN_PORT [=[
// Upstream TinyUSB hidh_open() assumes interface -> HID -> endpoint(s):
// bool hidh_open(uint8_t rhport, uint8_t daddr, tusb_desc_interface_t const* desc_itf, uint16_t max_len) {
//   (void) rhport;
//   (void) max_len;
//
//   TU_VERIFY(TUSB_CLASS_HID == desc_itf->bInterfaceClass);
//   TU_LOG_DRV("[%u] HID opening Interface %u\r\n", daddr, desc_itf->bInterfaceNumber);
//
//   // len = interface + hid + n*endpoints
//   uint16_t const drv_len = (uint16_t) (sizeof(tusb_desc_interface_t) + sizeof(tusb_hid_descriptor_hid_t) +
//                                        desc_itf->bNumEndpoints * sizeof(tusb_desc_endpoint_t));
//   TU_ASSERT(max_len >= drv_len);
//   uint8_t const* p_desc = (uint8_t const*) desc_itf;
//
//   //------------- HID descriptor -------------//
//   p_desc = tu_desc_next(p_desc);
//   tusb_hid_descriptor_hid_t const* desc_hid = (tusb_hid_descriptor_hid_t const*) p_desc;
//   TU_ASSERT(HID_DESC_TYPE_HID == desc_hid->bDescriptorType);
//
//   hidh_interface_t* p_hid = find_new_itf();
//   TU_ASSERT(p_hid); // not enough interface, try to increase CFG_TUH_HID
//   p_hid->daddr = daddr;
//
//   //------------- Endpoint Descriptors -------------//
//   p_desc = tu_desc_next(p_desc);
//   tusb_desc_endpoint_t const* desc_ep = (tusb_desc_endpoint_t const*) p_desc;
//
//   for (int i = 0; i < desc_itf->bNumEndpoints; i++) {
//     TU_ASSERT(TUSB_DESC_ENDPOINT == desc_ep->bDescriptorType);
//     TU_ASSERT(tuh_edpt_open(daddr, desc_ep));
//
//     if (tu_edpt_dir(desc_ep->bEndpointAddress) == TUSB_DIR_IN) {
//       p_hid->ep_in = desc_ep->bEndpointAddress;
//       p_hid->epin_size = tu_edpt_packet_size(desc_ep);
//     } else {
//       p_hid->ep_out = desc_ep->bEndpointAddress;
//       p_hid->epout_size = tu_edpt_packet_size(desc_ep);
//     }
//
//     p_desc = tu_desc_next(p_desc);
//     desc_ep = (tusb_desc_endpoint_t const*) p_desc;
//   }
//
//   p_hid->itf_num = desc_itf->bInterfaceNumber;
//
//   // Assume bNumDescriptors = 1
//   p_hid->report_desc_type = desc_hid->bReportType;
//   p_hid->report_desc_len = tu_unaligned_read16(&desc_hid->wReportLength);
//
//   // Per HID Specs: default is Report protocol, though we will force Boot protocol when set_config
//   p_hid->protocol_mode = _hidh_default_protocol;
//   if (HID_SUBCLASS_BOOT == desc_itf->bInterfaceSubClass) {
//     p_hid->itf_protocol = desc_itf->bInterfaceProtocol;
//   }
//
//   return true;
// }
// Linux USB core accepts the HID descriptor in interface extra data or after
// endpoint[0]. Scan the bounded current-interface stream before opening the
// same TinyUSB endpoints, without consuming descriptors from the next interface.
bool hidh_open(uint8_t rhport, uint8_t daddr, tusb_desc_interface_t const* desc_itf, uint16_t max_len) {
  (void) rhport;

  TU_VERIFY(max_len >= sizeof(tusb_desc_interface_t));
  TU_VERIFY(desc_itf->bLength >= sizeof(tusb_desc_interface_t) && desc_itf->bLength <= max_len);
  TU_VERIFY(TUSB_CLASS_HID == desc_itf->bInterfaceClass);
  TU_LOG_DRV("[%u] HID opening Interface %u\r\n", daddr, desc_itf->bInterfaceNumber);

  uint8_t const* const desc_end = (uint8_t const*) desc_itf + max_len;
  uint8_t const* p_desc = (uint8_t const*) desc_itf + desc_itf->bLength;
  tusb_hid_descriptor_hid_t const* desc_hid = NULL;

  // Validate the whole current-interface stream before endpoint-open side effects.
  while (p_desc < desc_end) {
    uint16_t const remaining = (uint16_t) (desc_end - p_desc);
    TU_VERIFY(remaining >= 2);
    uint8_t const desc_len = tu_desc_len(p_desc);
    TU_VERIFY(desc_len >= 2 && desc_len <= remaining);
    if (TUSB_DESC_INTERFACE == tu_desc_type(p_desc)) break;

    if (HID_DESC_TYPE_HID == tu_desc_type(p_desc) && !desc_hid) {
      TU_VERIFY(desc_len >= sizeof(tusb_hid_descriptor_hid_t));
      desc_hid = (tusb_hid_descriptor_hid_t const*) p_desc;
    } else if (TUSB_DESC_ENDPOINT == tu_desc_type(p_desc)) {
      TU_VERIFY(desc_len >= sizeof(tusb_desc_endpoint_t));
    }
    p_desc += desc_len;
  }
  TU_VERIFY(desc_hid);

  hidh_interface_t* p_hid = find_new_itf();
  TU_ASSERT(p_hid); // not enough interface, try to increase CFG_TUH_HID
  tu_memclr(p_hid, sizeof(*p_hid));

  // Open up to the number of endpoints advertised by this interface. Linux
  // likewise keeps the descriptors it actually finds when fewer are present.
  uint8_t opened_endpoints = 0;
  uint8_t ep_in = 0;
  uint8_t ep_out = 0;
  uint16_t epin_size = 0;
  uint16_t epout_size = 0;
  p_desc = (uint8_t const*) desc_itf + desc_itf->bLength;
  while (p_desc < desc_end && opened_endpoints < desc_itf->bNumEndpoints) {
    uint8_t const desc_len = tu_desc_len(p_desc);
    if (TUSB_DESC_INTERFACE == tu_desc_type(p_desc)) break;

    if (TUSB_DESC_ENDPOINT == tu_desc_type(p_desc)) {
      tusb_desc_endpoint_t const* desc_ep = (tusb_desc_endpoint_t const*) p_desc;
      TU_ASSERT(tuh_edpt_open(daddr, desc_ep));

      if (tu_edpt_dir(desc_ep->bEndpointAddress) == TUSB_DIR_IN) {
        ep_in = desc_ep->bEndpointAddress;
        epin_size = tu_edpt_packet_size(desc_ep);
      } else {
        ep_out = desc_ep->bEndpointAddress;
        epout_size = tu_edpt_packet_size(desc_ep);
      }
      opened_endpoints++;
    }
    p_desc += desc_len;
  }

  // Publish the class slot only after every descriptor and endpoint-open step
  // succeeded, so a failed open cannot consume a CFG_TUH_HID instance.
  p_hid->itf_num = desc_itf->bInterfaceNumber;
  p_hid->ep_in = ep_in;
  p_hid->ep_out = ep_out;
  p_hid->epin_size = epin_size;
  p_hid->epout_size = epout_size;

  // Assume bNumDescriptors = 1
  p_hid->report_desc_type = desc_hid->bReportType;
  p_hid->report_desc_len = tu_unaligned_read16(&desc_hid->wReportLength);

  // USB reset selects Report protocol. Keep TinyUSB's metadata aligned without
  // emitting the SET_PROTOCOL request which Linux usbhid does not send.
  p_hid->protocol_mode = _hidh_default_protocol;
  if (HID_SUBCLASS_BOOT == desc_itf->bInterfaceSubClass) {
    p_hid->itf_protocol = desc_itf->bInterfaceProtocol;
  }
  p_hid->daddr = daddr;

  return true;
}

]=])
string(FIND "${TINYUSB_HIDH_CONTENT}" "${TINYUSB_HIDH_OPEN_UPSTREAM}"
       TINYUSB_HIDH_OPEN_OFFSET)
if(TINYUSB_HIDH_OPEN_OFFSET EQUAL -1)
    message(FATAL_ERROR
            "Pinned TinyUSB HID open anchor changed; re-audit the port")
endif()
string(LENGTH "${TINYUSB_HIDH_OPEN_UPSTREAM}" TINYUSB_HIDH_OPEN_LENGTH)
math(EXPR TINYUSB_HIDH_OPEN_TAIL_OFFSET
     "${TINYUSB_HIDH_OPEN_OFFSET} + ${TINYUSB_HIDH_OPEN_LENGTH}")
string(SUBSTRING "${TINYUSB_HIDH_CONTENT}"
       ${TINYUSB_HIDH_OPEN_TAIL_OFFSET} -1 TINYUSB_HIDH_OPEN_TAIL)
string(FIND "${TINYUSB_HIDH_OPEN_TAIL}" "${TINYUSB_HIDH_OPEN_UPSTREAM}"
       TINYUSB_HIDH_SECOND_OPEN)
if(NOT TINYUSB_HIDH_SECOND_OPEN EQUAL -1)
    message(FATAL_ERROR "TinyUSB HID open anchor is not unique")
endif()
string(REPLACE "${TINYUSB_HIDH_OPEN_UPSTREAM}" "${TINYUSB_HIDH_OPEN_PORT}"
       TINYUSB_HIDH_PORT_CONTENT "${TINYUSB_HIDH_CONTENT}")
set(TINYUSB_HIDH_SET_CONFIG_UPSTREAM [=[
bool hidh_set_config(uint8_t daddr, uint8_t itf_num) {
  tusb_control_request_t request;
  request.wIndex = tu_htole16((uint16_t) itf_num);

  tuh_xfer_t xfer;
  xfer.daddr = daddr;
  xfer.result = XFER_RESULT_SUCCESS;
  xfer.setup = &request;
  xfer.user_data = CONFG_SET_IDLE;

  // fake request to kick-off the set config process
  process_set_config(&xfer);

  return true;
}

]=])
set(TINYUSB_HIDH_SET_CONFIG_PORT [=[
// Upstream TinyUSB performs HID class policy during enumeration:
// bool hidh_set_config(uint8_t daddr, uint8_t itf_num) {
//   tusb_control_request_t request;
//   request.wIndex = tu_htole16((uint16_t) itf_num);
//
//   tuh_xfer_t xfer;
//   xfer.daddr = daddr;
//   xfer.result = XFER_RESULT_SUCCESS;
//   xfer.setup = &request;
//   xfer.user_data = CONFG_SET_IDLE;
//
//   // fake request to kick-off the set config process
//   process_set_config(&xfer);
//
//   return true;
// }
// Linux usbhid owns SET_IDLE in task-side parse and assumes the USB
// reset-default Report protocol, so begin at the already ported descriptor/
// mount state without a wire SET_IDLE or SET_PROTOCOL transaction here.
bool hidh_set_config(uint8_t daddr, uint8_t itf_num) {
  tusb_control_request_t request = { 0 };
  request.wIndex = tu_htole16((uint16_t) itf_num);

  tuh_xfer_t xfer = { 0 };
  xfer.daddr = daddr;
  xfer.result = XFER_RESULT_SUCCESS;
  xfer.setup = &request;
  xfer.user_data = CONFIG_GET_REPORT_DESC;

  // fake request to kick-off the set config process
  process_set_config(&xfer);

  return true;
}

]=])
string(FIND "${TINYUSB_HIDH_PORT_CONTENT}"
       "${TINYUSB_HIDH_SET_CONFIG_UPSTREAM}"
       TINYUSB_HIDH_SET_CONFIG_OFFSET)
if(TINYUSB_HIDH_SET_CONFIG_OFFSET EQUAL -1)
    message(FATAL_ERROR
            "Pinned TinyUSB HID set-config anchor changed; re-audit the port")
endif()
string(LENGTH "${TINYUSB_HIDH_SET_CONFIG_UPSTREAM}"
       TINYUSB_HIDH_SET_CONFIG_LENGTH)
math(EXPR TINYUSB_HIDH_SET_CONFIG_TAIL_OFFSET
     "${TINYUSB_HIDH_SET_CONFIG_OFFSET} + ${TINYUSB_HIDH_SET_CONFIG_LENGTH}")
string(SUBSTRING "${TINYUSB_HIDH_PORT_CONTENT}"
       ${TINYUSB_HIDH_SET_CONFIG_TAIL_OFFSET} -1
       TINYUSB_HIDH_SET_CONFIG_TAIL)
string(FIND "${TINYUSB_HIDH_SET_CONFIG_TAIL}"
       "${TINYUSB_HIDH_SET_CONFIG_UPSTREAM}"
       TINYUSB_HIDH_SECOND_SET_CONFIG)
if(NOT TINYUSB_HIDH_SECOND_SET_CONFIG EQUAL -1)
    message(FATAL_ERROR "TinyUSB HID set-config anchor is not unique")
endif()
string(REPLACE "${TINYUSB_HIDH_SET_CONFIG_UPSTREAM}"
       "${TINYUSB_HIDH_SET_CONFIG_PORT}"
       TINYUSB_HIDH_PORT_CONTENT "${TINYUSB_HIDH_PORT_CONTENT}")
set(TINYUSB_HIDH_REPORT_DESC_UPSTREAM [=[
    case CONFIG_GET_REPORT_DESC:
      // Get Report Descriptor if possible
      // using usbh enumeration buffer since report descriptor can be very long
      if (p_hid->report_desc_len > CFG_TUH_ENUMERATION_BUFSIZE) {
        TU_LOG_DRV("HID Skip Report Descriptor since it is too large %u bytes\r\n", p_hid->report_desc_len);

        // Driver is mounted without report descriptor
        config_driver_mount_complete(daddr, idx, NULL, 0);
      } else {
        tuh_descriptor_get_hid_report(daddr, itf_num, p_hid->report_desc_type, 0,
                                      usbh_get_enum_buf(), p_hid->report_desc_len,
                                      process_set_config, CONFIG_COMPLETE);
      }
      break;

]=])
set(TINYUSB_HIDH_REPORT_DESC_PORT [=[
    case CONFIG_GET_REPORT_DESC:
      // Upstream TinyUSB:
      // // Get Report Descriptor if possible
      // // using usbh enumeration buffer since report descriptor can be very long
      // if (p_hid->report_desc_len > CFG_TUH_ENUMERATION_BUFSIZE) {
      //   TU_LOG_DRV("HID Skip Report Descriptor since it is too large %u bytes\r\n", p_hid->report_desc_len);
      //
      //   // Driver is mounted without report descriptor
      //   config_driver_mount_complete(daddr, idx, NULL, 0);
      // } else {
      //   tuh_descriptor_get_hid_report(daddr, itf_num, p_hid->report_desc_type, 0,
      //                                 usbh_get_enum_buf(), p_hid->report_desc_len,
      //                                 process_set_config, CONFIG_COMPLETE);
      // }
      // Firmware usbhid_parse() owns one exact-size, four-attempt Linux fetch.
      config_driver_mount_complete(daddr, idx, NULL, 0);
      break;

]=])
string(FIND "${TINYUSB_HIDH_PORT_CONTENT}"
       "${TINYUSB_HIDH_REPORT_DESC_UPSTREAM}"
       TINYUSB_HIDH_REPORT_DESC_OFFSET)
if(TINYUSB_HIDH_REPORT_DESC_OFFSET EQUAL -1)
    message(FATAL_ERROR
            "Pinned TinyUSB HID report-fetch anchor changed; re-audit the port")
endif()
string(LENGTH "${TINYUSB_HIDH_REPORT_DESC_UPSTREAM}"
       TINYUSB_HIDH_REPORT_DESC_LENGTH)
math(EXPR TINYUSB_HIDH_REPORT_DESC_TAIL_OFFSET
     "${TINYUSB_HIDH_REPORT_DESC_OFFSET} + ${TINYUSB_HIDH_REPORT_DESC_LENGTH}")
string(SUBSTRING "${TINYUSB_HIDH_PORT_CONTENT}"
       ${TINYUSB_HIDH_REPORT_DESC_TAIL_OFFSET} -1
       TINYUSB_HIDH_REPORT_DESC_TAIL)
string(FIND "${TINYUSB_HIDH_REPORT_DESC_TAIL}"
       "${TINYUSB_HIDH_REPORT_DESC_UPSTREAM}"
       TINYUSB_HIDH_SECOND_REPORT_DESC)
if(NOT TINYUSB_HIDH_SECOND_REPORT_DESC EQUAL -1)
    message(FATAL_ERROR "TinyUSB HID report-fetch anchor is not unique")
endif()
string(REPLACE "${TINYUSB_HIDH_REPORT_DESC_UPSTREAM}"
       "${TINYUSB_HIDH_REPORT_DESC_PORT}"
       TINYUSB_HIDH_PORT_CONTENT "${TINYUSB_HIDH_PORT_CONTENT}")
file(WRITE "${TINYUSB_HIDH_PORT_SOURCE}" "${TINYUSB_HIDH_PORT_CONTENT}")

get_target_property(TINYUSB_HOST_BASE_SOURCES
                    tinyusb_host_base INTERFACE_SOURCES)
set(TINYUSB_HIDH_SOURCE_COUNT 0)
foreach(TINYUSB_HOST_BASE_SOURCE IN LISTS TINYUSB_HOST_BASE_SOURCES)
    if(TINYUSB_HOST_BASE_SOURCE STREQUAL "${TINYUSB_HIDH_SOURCE}")
        math(EXPR TINYUSB_HIDH_SOURCE_COUNT
             "${TINYUSB_HIDH_SOURCE_COUNT} + 1")
    endif()
endforeach()
if(NOT TINYUSB_HIDH_SOURCE_COUNT EQUAL 1)
    message(FATAL_ERROR
            "TinyUSB HID host source must occur exactly once; re-audit the port")
endif()
list(FIND TINYUSB_HOST_BASE_SOURCES "${TINYUSB_HIDH_SOURCE}"
     TINYUSB_HIDH_SOURCE_INDEX)
list(REMOVE_AT TINYUSB_HOST_BASE_SOURCES ${TINYUSB_HIDH_SOURCE_INDEX})
set_property(TARGET tinyusb_host_base PROPERTY INTERFACE_SOURCES
             ${TINYUSB_HOST_BASE_SOURCES})
target_sources(tinyusb_host_base INTERFACE "${TINYUSB_HIDH_PORT_SOURCE}")
set_source_files_properties("${TINYUSB_HIDH_PORT_SOURCE}" PROPERTIES
                            INCLUDE_DIRECTORIES
                            "${PICO_TINYUSB_PATH}/src/class/hid")

# Add executable. Default name is the project name, version 0.1
add_executable(${PROJECT_NAME})

pico_generate_pio_header(${PROJECT_NAME}
        ${CMAKE_CURRENT_LIST_DIR}/led/ws2812.pio
)

add_subdirectory(tusb)
add_subdirectory(pico-usb-flash-drive)
add_subdirectory(keyd)
add_subdirectory(pointing/pmw3360)
add_subdirectory(pointing/pmw3389)

target_sources(${PROJECT_NAME} PUBLIC
        ${JSON_SOURCES}
        # ${CMAKE_CURRENT_LIST_DIR}/test.c
        ${CMAKE_CURRENT_LIST_DIR}/main.c
        ${CMAKE_CURRENT_LIST_DIR}/devmon.c
        ${CMAKE_CURRENT_LIST_DIR}/tusb_device_task.c
        ${CMAKE_CURRENT_LIST_DIR}/usb_host/task.c
        ${CMAKE_CURRENT_LIST_DIR}/usb_host/tinyusb_hid.c
        ${CMAKE_CURRENT_LIST_DIR}/usb_host/usbhid.c
        ${CMAKE_CURRENT_LIST_DIR}/usb_host/usbhid_report.c
        ${CMAKE_CURRENT_LIST_DIR}/usb_host/hid_async.c
        ${CMAKE_CURRENT_LIST_DIR}/usb_host/hid_transport_sync.c
        ${CMAKE_CURRENT_LIST_DIR}/usb_host/hid_workqueue.c
        ${CMAKE_CURRENT_LIST_DIR}/usb_host/hid_timer.c
        # CONFIG_USB_HIDDEV is disabled until the firmware has a hiddev consumer.
        # ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/usbhid/hiddev.c
        ${CMAKE_CURRENT_LIST_DIR}/usb_host/evdev_client.c
        # Deferred hidraw calls are documented inline in hid-core.c.
        ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-core.c
        ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-input.c
        ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-generic.c
        ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-drivers.c
        ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-quirks.c
        ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-haptic.c
        ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-multitouch.c
        # ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-accutouch.c
        ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-a4tech.c
        # ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-aureal.c
        # Deferred: HIDDEV-forced driver; keep out until hiddev proxy coverage is
        # hardware/emulator tested for this path.
        # ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-belkin.c
        ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-chicony.c
        # ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-cherry.c
        # Deferred: mixed driver file; CM6533 raw/hiddev path is disabled in
        # source, and HS100B-only enablement should be tested explicitly.
        # ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-cmedia.c
        ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-creative-sb0540.c
        ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-cypress.c
        # ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-elecom.c
        # ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-evision.c
        # ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-ezkey.c
        # ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-gembird.c
        # ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-gyration.c
        # ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-glorious.c
        # ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-huawei.c
        ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-holtek-kbd.c
        # ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-holtek-mouse.c
        # ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-icade.c
        ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-ite.c
        # Deferred: keep Jabra out until this driver has explicit hardware/emulator
        # coverage; the generic pre-probe descriptor path is now available.
        # ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-jabra.c
        # ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-kensington.c
        # ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-keytouch.c
        ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-kye.c
        # ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-lcpower.c
        # ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-macally.c
        # ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-maltron.c
        # ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-monterey.c
        # ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-nti.c
        # ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-ortek.c
        # ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-penmount.c
        # ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-petalynx.c
        # Deferred: HIDDEV-forced audio/headset path; keep out until hiddev proxy
        # behavior is tested for this driver.
        # ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-plantronics.c
        ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-primax.c
        ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-pxrc.c
        ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-razer.c
        ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-rapoo.c
        # ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-redragon.c
        # ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-retrode.c
        ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-saitek.c
        # Deferred: Samsung IrDA 184-byte path forces HIDDEV instead of normal
        # hidinput; keep out until firmware has a useful hiddev consumer/proxy.
        # ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-samsung.c
        # ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-semitek.c
        # ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-sigmamicro.c
        # ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-speedlink.c
        # ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-sunplus.c
        # ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-tivo.c
        # ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-topre.c
        # ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-topseed.c
        # ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-twinhan.c
        # ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-viewsonic.c
        # Deferred: Vivaldi keyboard stack needs the matching fmap/input support
        # path enabled and tested as a unit.
        # ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-vivaldi-common.c
        # ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-vivaldi.c
        # ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-vrc2.c
        # ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-waltop.c
        # ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-xiaomi.c
        # ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-xinmo.c
        ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-zydacron.c
        ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/input/input.c
        ${CMAKE_CURRENT_LIST_DIR}/usb_host/evdev.c
        ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/input/input-mt.c
        # Deferred with hid-vivaldi*.c above.
        # ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/input/vivaldi-fmap.c
        ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/input/ff-core.c
        # Deferred: LED/backlight class proxies are not needed by the current HID
        # allowlist.
        # ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/leds/led-class.c
        # ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/video/backlight/backlight.c
        # Deferred: CONFIG_HID_BATTERY_STRENGTH is disabled until the
        # power_supply proxy has matching driver/hardware coverage.
        # ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/power/supply/power_supply_core.c
        ${CMAKE_CURRENT_LIST_DIR}/rtos/freertos_hook.c
        ${CMAKE_CURRENT_LIST_DIR}/led/led.c
        ${CMAKE_CURRENT_LIST_DIR}/led/ws2812.c
        ${CMAKE_CURRENT_LIST_DIR}/keyscan.c
        ${CMAKE_CURRENT_LIST_DIR}/jconfig.c
        ${CMAKE_CURRENT_LIST_DIR}/log.c
        ${CMAKE_CURRENT_LIST_DIR}/stdio_tusb_cdc.c
        ${CMAKE_CURRENT_LIST_DIR}/uart_stdio.c
        ${CMAKE_CURRENT_LIST_DIR}/display/ssd1306.c
        ${CMAKE_CURRENT_LIST_DIR}/ui/ui.c
        ${CMAKE_CURRENT_LIST_DIR}/accel/filter.c
        ${CMAKE_CURRENT_LIST_DIR}/accel/filter-adaptive.c
        ${CMAKE_CURRENT_LIST_DIR}/accel/filter-adaptive-high-dpi.c
        ${CMAKE_CURRENT_LIST_DIR}/accel/filter-adaptive-low-dpi.c
        ${CMAKE_CURRENT_LIST_DIR}/accel/filter-custom.c
        ${CMAKE_CURRENT_LIST_DIR}/accel/filter-flat.c
        ${CMAKE_CURRENT_LIST_DIR}/pointing/pointer.c
        ${CMAKE_CURRENT_LIST_DIR}/tusb/usb_cdc_callbacks.c
        ${CMAKE_CURRENT_LIST_DIR}/tusb/usb_device_callbacks.c
        ${CMAKE_CURRENT_LIST_DIR}/tusb/usb_hid_callbacks.c
        ${CMAKE_CURRENT_LIST_DIR}/tusb/usb_webhid.c
        ${CMAKE_CURRENT_LIST_DIR}/tusb/usb_msc_callbacks.c
)

# Make sure TinyUSB can find tusb_config.h
target_include_directories(${PROJECT_NAME} PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_CURRENT_LIST_DIR}/rtos
        ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/include
        ${JSON_INCLUDE_PUBLIC_DIRS}
)

# Expose keyd headers to this target via quote-only search path so system
# headers like <string.h> are never shadowed by keyd/src/string.h.
target_compile_options(${PROJECT_NAME} PRIVATE
        -iquote${CMAKE_CURRENT_LIST_DIR}/keyd/port
        -iquote${CMAKE_CURRENT_LIST_DIR}/keyd/src
)

# In addition to pico_stdlib required for common PicoSDK functionality, add dependency on tinyusb_device
# for TinyUSB device support and tinyusb_board for the additional board support library used by the example
target_link_libraries(${PROJECT_NAME} PUBLIC
        tusb_glue
        flash_fatfs
        keyd
        pmw3360
        pmw3389

        pico_async_context_freertos
        FreeRTOS-Kernel
        FreeRTOS-Kernel-Heap4

        pico_stdlib
        pico_flash

        pico_unique_id
        pico_rand
        tinyusb_additions
        tinyusb_device
        tinyusb_host
        tinyusb_pico_pio_usb
        tinyusb_board

        hardware_sync
        hardware_flash
        hardware_i2c
        hardware_pio
        hardware_clocks

        # pico_cyw43_arch_none
)



# Uncomment this line to enable fix for Errata RP2040-E5 (the fix requires use of GPIO 15)
#target_compile_definitions(${PROJECT_NAME} PUBLIC PICO_RP2040_USB_DEVICE_ENUMERATION_FIX=1)

pico_add_extra_outputs(${PROJECT_NAME})

target_compile_definitions(${PROJECT_NAME} PRIVATE
        __KERNEL__ # use kernel UAPI layouts; firmware has no userspace ioctl headers
        PICO_STDIO_STACK_BUFFER_SIZE=256 # use a small printf on stack buffer
        PICO_STDIO_USB_STDOUT_TIMEOUT_US=5000000 # avoid discarding long CDC bursts under load
        PICO_DEFAULT_PIO_USB_DP_PIN=21
)

# add url via pico_set_program_url
pico_enable_stdio_usb(${PROJECT_NAME} 0)
pico_enable_stdio_uart(${PROJECT_NAME} 1)
