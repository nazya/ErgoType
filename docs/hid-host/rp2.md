# RP2 USB HID Host Memory and Capacity

The RP2040 and RP2350 builds use the same Linux-derived HID host model,
FreeRTOS heap policy, TinyUSB host glue, and Pico-PIO-USB backend. The parser
and capacity macros documented here therefore apply to the RP2 port unless a
board configuration overrides them.

The exact object and code sizes below were measured from the current RP2040
ARM build. Recheck the ELF map for RP2350, a different architecture, or any SDK,
compiler, structure, or capacity change.

## Firmware Heap

The current [`configTOTAL_HEAP_SIZE`](../../rtos/FreeRTOSConfig.h) is 223,744
bytes (218.5 KiB). HID objects, input devices, FreeRTOS objects, descriptor
buffers, report buffers, driver state, and other firmware services all use that
same heap. Successful attachment therefore depends on the complete live graph:
every interface, report, field, virtual receiver child, and simultaneous device.

The heap itself (`ucHeap`) is a static array in `.bss`, and FreeRTOS takes task
stacks from it at run time. Every byte moved into static `.data` or `.bss`
therefore lowers the largest heap that still links. The main SRAM bank ends at
`0x20040000`; the gap between the linker symbol `__bss_end__` and that address
is the room left for a larger heap.

### Parser limits

| Limit | RP2 port | Pinned Linux | Meaning |
| --- | ---: | ---: | --- |
| `HID_MAX_FIELDS` | 64 | 256 | Maximum field entries retained in one report. |
| `HID_MAX_USAGES` | 675 | 12,288 | Maximum usage/value entries retained in one field. |
| `HID_MAX_REPORT_COUNT` | 12,288 | 12,288 | Maximum declared wire report count. |
| `HID_MAX_DESCRIPTOR_SIZE` | 4 KiB | 4 KiB | Maximum HID report descriptor size. |

`HID_MAX_USAGES` counts entries in each field. It is not a global table and is
not the maximum numeric Usage ID. A range with a nonzero minimum retains
`minimum..minimum+674` at the current limit. For a zero-based Consumer array,
the current limit retains selectors `0x000..0x2a2`; an individually declared
Usage ID can still be numerically higher than `0x2a2`.

For a field wider than the retained table:

- the parser keeps the first 675 usage/value entries;
- the complete wire width remains represented in the report size;
- a non-padding INPUT ARRAY with more than 675 physical slots is rejected,
  because dropping slots would change key-state semantics;
- an array selector outside the retained mapping produces no input event and
  reports `WARN: HID_USAGE_CAP_DROP` when that selector first appears;
- a separate Usage item encountered after the parser table is full remains a
  parse error.

Structured SET_REPORT is not safe for a capped Output or Feature field:
`hid_output_report()` would clear the complete wire buffer and serialize zeros
for the omitted tail. Such a device needs either a complete retained field or a
raw request whose caller supplies the complete buffer.

### Why wide fields cost so much

Linux materializes usage metadata for every parsed field and retains it for the
lifetime of the interface. On the current 32-bit ABI:

```text
sizeof(struct hid_field) = 100 B
ordinary field           = 100 + usage_count * 40 B
INPUT ARRAY field        = 100 + usage_count * 32 B
                              + report_count * 8 B
```

For example:

| Usages | Persistent allocation for a six-slot Consumer array |
| ---: | ---: |
| 675 | 21,748 B (21.2 KiB) |
| 1,024 | 32,916 B (32.1 KiB) |
| 12,288 | 393,364 B (384.1 KiB) |

The parser also uses three temporary arrays totalling 9 bytes per local usage.
At 675 entries those arrays require 6,075 bytes and are released after parsing.
One maximal retained field plus its live parser storage is therefore about
33 KiB before allocator metadata and the rest of the device graph.

These objects cannot simply move to flash because their contents are generated
from the attached device's report descriptor. A different sparse parser would
be a larger departure from the retained Linux implementation.

### Allocation and buffer behavior

A persistent report or field allocation failure rejects and destroys the whole
interface; the firmware does not bind a partial report graph.
`ERR: HID_FIELD_NOMEM` identifies a persistent field-allocation failure, while
`ERR: HID_PROBE_NOMEM` covers other probe allocations. Successful attachment
still requires the expected `DEVICE: added` and input behavior without a new
OOM count.

FreeRTOS `heap_4` must also provide one sufficiently large contiguous block.
Total free heap can therefore look adequate while a large field allocation
still fails; inspect the reported largest free block as well as total free
memory. If total free heap returns to its earlier level after a device is
removed but the largest block stays smaller, that is fragmentation, not by
itself a leak.

Other relevant allocations are bounded as follows:

- report descriptors use one exact transient allocation up to 4 KiB;
- configuration descriptors use TinyUSB's 512-byte buffer when possible and
  one exact transient allocation for validated lengths through 4 KiB;
- every active interface owns an interrupt-IN buffer sized for its largest
  INPUT report plus the PIO backend's packet-safe tail;
- logical control/output request nodes share a 4 KiB firmware budget;
- the optional HID Haptics path retains at most five application effects and
  five report snapshots.

Excluding an optional driver from CMake also excludes its mutable builtin
runtime record. Static `.bss` still consumes SRAM, even though it does not
consume or fragment the FreeRTOS heap.

## Pico-PIO-USB Transport

Pico-PIO-USB owns static arrays and timing-critical SRAM-resident code in
addition to the dynamic firmware heap described above.

### Current static footprint

The current RP2040 ELF map gives these Pico-PIO-USB objects:

| Object | Configuration | Element size | Total |
| --- | ---: | ---: | ---: |
| `pio_port[1]` | fixed | 220 B | 220 B |
| `pio_usb_device[4]` | `PIO_USB_DEVICE_CNT=4` | 76 B | 304 B |
| `pio_usb_ep_pool[8]` | `PIO_USB_EP_POOL_CNT=8` | 188 B | 1,504 B |
| `pio_usb_root_port[1]` | `PIO_USB_ROOT_PORT_CNT=1` | 48 B | 48 B |

The encoded ACK/NAK/STALL/PRE and SOF/keepalive packets plus the SOF length
add 33 bytes. The repeating-timer record, alarm pointer, SOF counter, and timer
flag add another 33 bytes. The table does not include linker padding or the
shared Pico SDK alarm-pool storage.

These sizes describe the current ARM32 ABI and pinned Pico-PIO-USB source.
Recheck the ELF map after changing the SDK, structures, compiler, or capacity
macros.

### TinyUSB host buffers

TinyUSB host state adds about 2.5 KiB of static RAM:

| Object | Size |
| --- | ---: |
| `_usbh_devices` | 1,720 B |
| `_usbh_epbuf` | 520 B |
| `usbhid_report_rx_slots` (4 × 52 B) | 208 B |
| `_hidh_itf` | 56 B |
| `_hidh_epbuf` | 32 B |

The DMA-visible transfer buffers among them are placed in the scratch X bank
(`CFG_TUH_MEM_SECTION` in [`tusb_config.h`](../../tusb/tusb_config.h)). They
use 788 bytes (`0x20040000..0x20040314`) and leave 1,260 bytes below the
core-1 stack bottom at `0x20040800`.

### SRAM-resident code

Pico-PIO-USB implements a software USB host controller/PHY on top of PIO.
Timing-sensitive functions marked with `__not_in_flash_func`,
`__no_inline_not_in_flash_func`, or `.time_critical.*` are linked into SRAM to
avoid unpredictable XIP-cache stalls.

The recorded RP2040 map contains about 12.2 KiB of Pico-PIO-USB and PIO-HCD
time-critical code in `.data`. Its largest entries include:

```text
pio_usb_ll_encode_tx_data                 4180 B
initialize_host_programs                  1444 B
pio_usb_bus_receive_packet_and_handshake   704 B
handle_endpoint_irq                        692 B
crc16_tbl                                  512 B
connection_check                           508 B
pio_usb_host_frame                         468 B
configure_fullspeed_host                   268 B
configure_lowspeed_host                    268 B
```

These are map measurements rather than ABI constants. Moving any of this code
to flash is a USB-timing experiment and requires hardware verification.

### Active capacity values

```c
#define PIO_USB_EP_POOL_CNT      8
#define PIO_USB_DEV_EP_CNT      16
#define PIO_USB_DEVICE_CNT       4
#define PIO_USB_HUB_PORT_CNT     8
#define PIO_USB_ROOT_PORT_CNT    1
#define PIO_USB_EP_SIZE         64
```

The endpoint pool was imported already reduced from Pico-PIO-USB's default 32
to eight. The root-port count was later reduced from two to one. The other
values retain the configuration initially imported by this port.

### Endpoint pool: 32 to 8

`PIO_USB_EP_POOL_CNT` is the number of low-level endpoint records shared by
all devices on the PIO root port. Control endpoints, hub interrupt endpoints,
and device interrupt/bulk endpoints all consume this common pool when opened.

One `endpoint_t` is 188 bytes. Its largest member is a 160-byte buffer holding
a pre-encoded maximum-size USB packet with room for PID, CRC16, bit stuffing,
and EOP.

```text
upstream/default: 32 * 188 B = 6,016 B
current port:      8 * 188 B = 1,504 B
saved:                              4,512 B
```

This reduction does not limit protocol-level report contents. It limits the
number of low-level USB endpoints that can be open simultaneously across the
entire topology. If all eight records are occupied, the next endpoint open
fails. A composite device or a topology behind a hub may then fail enumeration
or expose only the interfaces whose endpoints fitted.

Eight is the current firmware policy. It covers the tested direct-device
topologies, but it is not a general capacity guarantee for arbitrary hubs or
several simultaneous composite devices.

### Root ports: 2 to 1

`PIO_USB_ROOT_PORT_CNT` is the number of physical PIO USB root ports, meaning
independent D+/D- pairs driven directly by Pico-PIO-USB. One `root_port_t` is
48 bytes in the current RP2040 build.

```text
previous port value: 2 * 48 B = 96 B
current port value:  1 * 48 B = 48 B
saved:                         48 B
```

This port uses one physical PIO USB connector, so the second root-port record
was removed. A USB hub does not require another PIO root port; the hub and its
children share the same physical root connection.

### Device records

`PIO_USB_DEVICE_CNT=4` reserves four 76-byte `usb_device_t` records, for
304 bytes total in the current RP2040 build. This value was not reduced by the
port.

It must not be confused with TinyUSB's logical device limit. In the active
TinyUSB PIO HCD adapter, endpoint records carry the USB address directly and
the adapter's strong IRQ handler bypasses Pico-PIO-USB's weak handler that
searches `pio_usb_device[]`. `pio_usb_host_init()` returns record zero, but
the active HCD does not use the remaining records to represent hub children.

Reducing this array from four records to one would save 228 bytes on the
current RP2040 ABI. That is only a candidate cleanup: it changes shared
Pico-PIO-USB host/device storage and needs a separate source audit, build, and
hardware regression before changing the configuration.

### Embedded endpoint and hub arrays

Each `usb_device_t` contains:

```c
uint8_t endpoint_id[PIO_USB_DEV_EP_CNT];       // 16 bytes
uint8_t child_devices[PIO_USB_HUB_PORT_CNT];   // 8 bytes
```

`endpoint_id[]` is used by Pico-PIO-USB's device-mode implementation. The
active TinyUSB host HCD instead uses the global `pio_usb_ep_pool[]`.

`child_devices[]` has no reader or writer anywhere in the current tree.
Consequently `PIO_USB_HUB_PORT_CNT=8` currently reserves structure space but
does not set the number of hub ports supported by TinyUSB. Reducing it would
not by itself narrow or expand real hub support. Any memory saving also depends
on structure padding, so it must be confirmed in the target ELF map.

### Endpoint packet size

`PIO_USB_EP_SIZE=64` is used for a temporary packet buffer in `pio_usb.c`. The
persistent `endpoint_t::buffer` is separately hard-coded for a 64-byte payload
in `usb_definitions.h`.

Therefore lowering `PIO_USB_EP_SIZE` alone does not shrink
`pio_usb_ep_pool[]`. It only reduces temporary storage while making ordinary
full-speed 64-byte control or data packets unsafe. It is not a useful memory
reduction for this host.

## Capacity Summary

The port has made two Pico-PIO-USB capacity reductions:

| Setting | Change | RP2040 static saving | Functional effect |
| --- | ---: | ---: | --- |
| `PIO_USB_EP_POOL_CNT` | 32 → 8 | 4,512 B | At most eight low-level endpoints can be open across the root topology |
| `PIO_USB_ROOT_PORT_CNT` | 2 → 1 | 48 B | One physical PIO D+/D- root port |

The endpoint-pool reduction provides almost all configuration-based
Pico-PIO-USB savings. The remaining large measured cost is approximately
12.2 KiB of timing-critical code in SRAM. Moving that code to flash can break
USB timing and is not a safe configuration change.

Devices limited by RAM are marked `Hardware limits: RAM` in the
[device tables](README.md#device-tables). Exact emulator
configurations and measured heap plateaus remain in
[`hid-emulator-coverage.md`](../../usb_host/hid-emulator-coverage.md).
