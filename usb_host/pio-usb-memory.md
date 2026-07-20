# PIO USB Host Memory Notes

This note tracks the RAM cost of adding TinyUSB host through Pico-PIO-USB on
RP2040.

The FreeRTOS heap size is critical for this firmware. Every byte moved into
static `.data` or `.bss` reduces the maximum possible `configTOTAL_HEAP_SIZE`.
Task stack tuning can improve free heap at runtime, but it does not fix a link
failure caused by static RAM layout.

## Current Link Picture

With the tested host branch and `configTOTAL_HEAP_SIZE = 232 * 1024`, the link
failed with:

```text
region `RAM' overflowed by 13868 bytes
```

The failed map still shows the useful layout:

```text
.data total                 20600 B
.bss starts                 0x20005138
ucHeap                      237568 B = 232 KiB
RAM overflow                13868 B
```

Meaning:

```text
232 KiB heap does not fit with the current host static RAM.
218 KiB is the approximate upper heap size that should link with the same code.
```

This is not caused by FreeRTOS task stack depth alone. FreeRTOS task stacks are
allocated from `ucHeap` at runtime. The link failure happens earlier because
`ucHeap` itself is a static `.bss` array and there is not enough RAM left for it.

## Layers

TinyUSB host state and Pico-PIO-USB state are separate layers.

```text
TinyUSB HID host state
-> TinyUSB USB host core state
-> Pico-PIO-USB host controller state
-> Pico-PIO-USB time-critical RAM code
-> pio_usb_ep_pool[]
-> PIO/DMA/D+/D-
```

TinyUSB stores logical USB state:

- device slots
- hub slots
- HID interface slots
- host event queues
- descriptor/control buffers
- HID IN/OUT transfer buffers

Pico-PIO-USB stores low-level transport state needed to drive D+/D- from PIO:

- root ports
- low-level USB device records
- low-level endpoint slots
- pre-encoded USB packet buffers
- PIO/transaction state
- RAM-resident timing-critical code

## Why Host Costs So Much RAM

The biggest surprise is not `pio_usb_ep_pool[]`. That pool is visible and
configurable, but the larger cost is Pico-PIO-USB code placed in RAM.

Pico-PIO-USB is effectively a software USB host controller/PHY on top of PIO.
The hot transaction path must hit tight USB timing windows. RP2040 flash runs
through QSPI XIP cache, so cache misses can stall unpredictably. Pico-PIO-USB
therefore marks many functions with `__not_in_flash_func`,
`__no_inline_not_in_flash_func`, or `.time_critical.*`; the linker puts those
functions into `.data` so they execute from SRAM.

Current map shows about this much RAM-resident Pico-PIO-USB / PIO-HCD code:

```text
~12.2 KiB Pico-PIO-USB / hcd_pio_usb time-critical code in .data
```

Large entries from the current map:

```text
pio_usb_ll_encode_tx_data             4180 B
initialize_host_programs              1444 B
pio_usb_bus_receive_packet_and_handshake 704 B
handle_endpoint_irq                    692 B
pio_usb_host_frame                     692 B
crc16_tbl                              512 B
connection_check                       536 B
configure_fullspeed_host               264 B
configure_lowspeed_host                264 B
```

This memory is expensive but not safely removable by configuration. Moving any
of these functions/tables to flash is a timing experiment, not a safe cleanup.

## Current Static RAM Consumers

Approximate current host-related RAM costs after `PIO_USB_EP_POOL_CNT` was
reduced to `8`:

```text
~12.2 KiB  Pico-PIO-USB / hcd_pio_usb time-critical code in .data
~1.5 KiB   pio_usb_ep_pool[8]
~0.3 KiB   pio_usb_device[4]
~0.2 KiB   pio_port[1]
~0.1 KiB   pio_usb_root_port[1]
~2.0 KiB   TinyUSB host/HID/hub static state
~0.3 KiB   usb_host/task debug event ring
```

Important current map entries:

```text
pio_usb_ep_pool[8]       1504 B
_hidh_epbuf              512 B
_usbh_epbuf              520 B
_usbh_devices            430 B
_usbh_qdef_buf           192 B
hid_host_events          288 B
```

Non-host RAM that is also visible in the map:

```text
stdio_tusb_cdc pending_buf       2048 B
TinyUSB device queue buffer       768 B
MSC device endpoint buffer        512 B
CDC device state/buffers         ~1.2 KiB
log.c static format buffer       2048 B
```

Those are not the host regression root cause, but they are real RAM reserves if
heap pressure becomes more important than logging/CDC/MSC burst tolerance.

## `PIO_USB_EP_POOL_CNT`

`PIO_USB_EP_POOL_CNT` controls how many low-level Pico-PIO-USB endpoint slots
can be open at once.

Default upstream Pico-PIO-USB value:

```c
#define PIO_USB_EP_POOL_CNT 32
```

Current port value:

```c
#define PIO_USB_EP_POOL_CNT 8
```

One `endpoint_t` is about 188 bytes. Most of it is:

```c
uint8_t buffer[(64 + 4) * 2 * 7 / 6 + 2];
```

That buffer holds a pre-encoded USB packet stream for PIO TX:

- SYNC
- PID
- payload up to 64 bytes
- CRC16
- bit-stuffing worst case
- EOP

Cost:

```text
32 endpoint slots * ~188 B = ~6016 B
 8 endpoint slots * ~188 B = ~1504 B
saved                         ~4512 B
```

User-visible meaning: this is not the number of keys, reports, or HID usages.
It is the number of low-level USB transport endpoints that can be opened for
devices plugged into the PIO host port.

For a direct ErgoType-to-ErgoType host test without a hub, `8` should be enough:

```text
EP0 control
keyboard HID IN
mouse HID IN
WebHID IN
spare slots
```

If this is too low, enumeration or endpoint opening fails when
`pio_usb_host_endpoint_open()` cannot find a free slot. User-visible failures:

- keyboard works, but mouse does not
- keyboard/mouse works, but WebHID/config channel does not
- the whole device stays absent if the missing endpoint is required during enum

## TinyUSB Host Config

For one peer ErgoType in the current NKRO profile:

```text
one physical USB device
three HID interfaces:
  keyboard/consumer
  mouse
  WebHID
```

Consumer Control is a report ID inside the keyboard HID interface. It is not a
separate HID interface.

Current wider smoke-test config:

```c
#define CFG_TUH_HUB                 1
#define CFG_TUH_DEVICE_MAX          4
#define CFG_TUH_API_EDPT_XFER       1
#define CFG_TUH_HID                 4
#define CFG_TUH_ENUMERATION_BUFSIZE 512
#define CFG_TUH_HID_EPIN_BUFSIZE    1
#define CFG_TUH_HID_EPOUT_BUFSIZE   1
```

Direct single-peer config:

```c
#define CFG_TUH_HUB                 0
#define CFG_TUH_DEVICE_MAX          1
#define CFG_TUH_HID                 3
#define CFG_TUH_ENUMERATION_BUFSIZE 256
#define CFG_TUH_HID_EPIN_BUFSIZE    1
#define CFG_TUH_HID_EPOUT_BUFSIZE   1
```

Tradeoffs:

- `CFG_TUH_HUB=0`: saves roughly 100-200 B, but external USB hubs do not work.
- `CFG_TUH_DEVICE_MAX=1`: saves roughly 250-350 B versus 4, but only one downstream physical USB device is supported.
- `CFG_TUH_HID=3`: saves roughly 100-150 B versus 4 with current buffers. Enough for ErgoType NKRO. Use `2` only for boot keyboard+mouse. Use more for composite devices with more HID interfaces.
- `CFG_TUH_ENUMERATION_BUFSIZE=256`: saves 256 B versus 512. Larger
  configuration descriptors can still fail enumeration. TinyUSB may skip a
  larger HID report descriptor, but lifecycle now refetches its class-declared
  size through async EP0 up to Linux's 4 KiB limit.
- `CFG_TUH_HID_EPIN_BUFSIZE=1`: direct interrupt IN uses upstream's
  per-interface `inbuf` through the endpoint API, so TinyUSB's class buffer is
  an unused placeholder. Task context sizes that backing for the parsed INPUT
  report, with a 64-byte minimum and PIO-safe final-packet rounding.
- `CFG_TUH_API_EDPT_XFER=1`: stores an exact callback and request serial per
  endpoint. With five host slots and 16 endpoint numbers this costs 1,280 B,
  but exposes the real interrupt-transfer result and actual length.
- `CFG_TUH_HID_EPOUT_BUFSIZE=1`: saves 240 B versus 64 when
  `CFG_TUH_HID=4`. Interrupt OUT remains supported because this port submits
  the request-owned wire buffer directly instead of using TinyUSB's class
  staging buffer. Control SET_REPORT is separate.

`CFG_TUH_MEM_SECTION` places TinyUSB's DMA-visible host transfer metadata and
the port-owned interrupt-IN lifecycle slots in scratch X. In the current
RP2040 link they occupy 660 B and end 1,388 B below the real core-1 stack. The
endpoint callback table remains in main SRAM. Per-interface interrupt-IN
payload backing is ordinary PIO-visible SRAM from heap_4: normally a 72-byte
block for 64 bytes, allocated once at start and freed after the detach fence.

The callback transport pool uses five 20-byte report-descriptor metadata slots
instead of four inline 512-byte descriptor buffers. Its payload shrinks by
2,012 B and the aligned heap_4 allocation by 2,008 B. Lifecycle holds at most
one exact descriptor buffer across all devices while EP0 fetch/probe is active;
a maximum-size descriptor consumes about 4 KiB transiently and is released
after probe or fenced cancellation.

These are reasonable low-risk reductions for direct one-device testing, but
they do not recover the full 13-14 KiB needed to keep a 232 KiB heap.

## Pico-PIO-USB Config

Current host-oriented config:

```c
#define PIO_USB_EP_POOL_CNT      8
#define PIO_USB_DEV_EP_CNT       16
#define PIO_USB_DEVICE_CNT       4
#define PIO_USB_HUB_PORT_CNT     8
#define PIO_USB_ROOT_PORT_CNT    1
#define PIO_USB_EP_SIZE          64
```

Direct one-port config:

```c
#define PIO_USB_EP_POOL_CNT      8
#define PIO_USB_DEVICE_CNT       1
#define PIO_USB_ROOT_PORT_CNT    1
```

Tradeoffs:

- `PIO_USB_DEVICE_CNT=1`: saves roughly 200-300 B versus 4. Only one low-level USB device record exists, so hub children/multiple devices do not fit.
- `PIO_USB_ROOT_PORT_CNT=1`: saves roughly 50 B versus 2. Only one D+/D- PIO root port exists.
- `PIO_USB_DEV_EP_CNT`: endpoint-id entries inside each `pio_usb_device`; one byte per entry per device. Low memory impact.
- `PIO_USB_HUB_PORT_CNT`: child-device slots inside each `pio_usb_device`; only useful with hub support. Low direct memory impact.
- `PIO_USB_EP_SIZE=64`: do not reduce for normal HID host. Full-speed control and HID paths can legitimately use 64-byte packets. Reducing it saves per endpoint but risks breaking ordinary devices.

## Safe Versus Risky Reductions

Relatively safe under the narrow test scope "one direct ErgoType-like USB
device, no hub":

```text
CFG_TUH_HUB              1 -> 0       ~100-200 B
CFG_TUH_DEVICE_MAX       4 -> 1       ~250-350 B
CFG_TUH_HID              4 -> 3       ~100-150 B
CFG_TUH_ENUMERATION_BUFSIZE 512 -> 256 256 B
CFG_TUH_HID_EPIN_BUFSIZE  64 -> 1    ~250 B, only with direct-IN transport
CFG_TUH_HID_EPOUT_BUFSIZE 64 -> 1    ~250 B
PIO_USB_DEVICE_CNT       4 -> 1       ~200-300 B
PIO_USB_ROOT_PORT_CNT    2 -> 1       ~50 B
```

Already done:

```text
PIO_USB_EP_POOL_CNT      32 -> 8      ~4512 B
```

Do not reduce blindly:

```text
PIO_USB_EP_SIZE          64
HID_MAX_BUFFER_SIZE      16384
port IN backing          max(64, round_up(input size, endpoint packet))
TUD_STACK_SIZE           4096
```

Risky experiments that may save much more RAM:

```text
Move selected Pico-PIO-USB .time_critical functions/tables from RAM to flash.
```

This is the only obvious place with enough bytes to matter for a 232 KiB heap,
but it is not a safe config change. It can break USB timing. It must be tested
on hardware with attach/enumeration/report traffic.

Potential candidates to investigate, not change blindly:

```text
pio_usb_ll_encode_tx_data  ~4.1 KiB
initialize_host_programs   ~1.4 KiB
crc16_tbl                  512 B
configuration/init helpers  several hundred bytes
```

Keep in RAM unless proven otherwise:

```text
IRQ handlers
usb_in_transaction()
usb_out_transaction()
usb_setup_transaction()
pio_usb_host_frame()
PIO/FIFO tight polling paths
handshake/receive paths
```

## Practical Conclusion

The host side eats RAM for three reasons:

1. TinyUSB host adds logical host state and descriptor buffers.
2. Pico-PIO-USB adds low-level endpoint/device/root-port transport state.
3. Pico-PIO-USB puts timing-critical host code into SRAM.

The endpoint pool was the biggest simple static buffer and is already reduced
from 32 to 8. That saved about 4.5 KiB.

The remaining large cost is the RAM-resident time-critical code. Keeping a
232 KiB FreeRTOS heap while enabling this host stack requires either:

- more aggressive, risky Pico-PIO-USB flash/RAM placement experiments, or
- lowering `configTOTAL_HEAP_SIZE` to around 218 KiB, or
- removing/reducing unrelated device-side buffers/features such as CDC log
  buffering, MSC buffering, or TinyUSB device queue depth.

For bring-up, the least risky path is:

```text
keep Pico-PIO-USB timing code unchanged
use narrow one-device/no-hub host config
build with heap around 218 KiB
measure runtime heap and stack watermarks on hardware
then decide whether risky RAM-code experiments are worth it
```
