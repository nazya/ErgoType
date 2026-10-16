# USB HID Host Diagnostics

This page describes messages from the RP2040 wired USB HID-host path. General
CDC logging and board-level debugging are documented in
[`logging.md`](../logging.md) and [`debugging.md`](../debugging.md).

Start with the first host error, not a later missing input event:

1. If no HID-host message appears, inspect power, wiring, D+/D-, the connector,
   and TinyUSB enumeration before changing the HID parser.
2. `HID_IGNORED` concerns one interface. Other interfaces of the same composite
   device can remain usable.
3. Probe and allocation errors occur before normal input. Fix those before
   investigating mappings.
4. Report and transfer errors occur after a device has bound. Check the armed
   endpoint, transfer route, and detach ordering.
5. Repeated timeout, reset, or teardown messages while the device remains
   attached are failures. One aborted transfer immediately next to physical
   removal can be normal abort-before-remove ordering.

Messages sent from callbacks and other short paths go through a single
pending slot. If a new message arrives before the previous one has been
written, the new text is dropped and the UI warning counter increases, so a
marker can be missing from the log even though its event happened.

## `HID_REPORT_SKIP`

`ERR: HID_REPORT_SKIP` formerly covered several late receive-path symptoms. In
the current implementation it has one transport meaning: a nominally
successful PIO/TinyUSB giveback violated the armed buffer contract, for example
because `actual_len > bufsize`. The payload is rejected before
`hid_safe_input_report()`.

A late aborted completion misses its cleared serial silently, and an ordinary
Linux parser rejection is not reported as a transport failure. If
`HID_REPORT_SKIP` appears, inspect the endpoint giveback and armed length rather
than key mapping.

## Message reference

| Message | Meaning |
| --- | --- |
| `WARN: HID_IGNORED` | No linked driver accepted this HID interface; other composite interfaces are unaffected. |
| `WARN: HUION_MODE_INIT` | The native Huion switcher's public USB string initialization failed; any firmware property already read remains available to the upstream program. |
| `WARN: HUION_MODE_UNCONFIRMED` | The string sequence returned without confirming Huion mode selection. This is not a successful mode-change verdict. |
| `ERR: HID_ASYNC_INIT_FAIL` / `HID_WORKQUEUE_INIT_FAIL` / `HID_TIMER_INIT_FAIL` / `HID_LIFECYCLE_INIT_FAIL` / `HID_REPORT_INIT_FAIL` | Startup could not initialize that host runtime owner. The TinyUSB host task is then not started and `ERR: TUH_RUNTIME_OWNER_MISSING` follows. |
| `ERR: HID_ASYNC_TASK_FAIL` / `HID_WORKQUEUE_TASK_FAIL` / `HID_TIMER_TASK_FAIL` / `HID_LIFECYCLE_TASK_FAIL` / `HID_REPORT_TASK_FAIL` | The FreeRTOS task for that host runtime owner could not be created; the TinyUSB host task is not started. |
| `ERR: TUH_RUNTIME_OWNER_MISSING` | At least one of the five host runtime owners is missing, so startup did not create the TinyUSB host task. |
| `ERR: HID_CORE_FAIL` / `TUH_CONFIG_FAIL` / `TUSB_HOST_FAIL` | The host task could not initialize the HID core, configure the PIO root port, or start the TinyUSB host stack. The host task suspends itself. |
| `ERR: HID_TIMER_INIT_TWICE` | Timer initialization was invoked after its mutex had already been created. |
| `ERR: HID_ENUM_FAIL` / `HID_ENUM_TIMEOUT` | TinyUSB enumeration of an attached device failed or ran past its deadline; no HID interface of that device was probed. |
| `ERR: HID_ENUM_GONE` | The interface disappeared before probe could take its snapshot or register it; the half-built HID is released. |
| `ERR: HID_TABLE_FULL` | The bounded HID interface table had no free entry for a new interface. |
| `ERR: HID_ALLOC_FAIL` | Allocating the `hid_device` or its usbhid state failed before probe. |
| `ERR: HID_INT_IN_MISSING` | The interface has no interrupt IN endpoint, which Linux usbhid requires; the interface is not bound. |
| `ERR: HID_RAW_INTERFACE_FULL` | The bounded raw-interface snapshot table was full when a new HID interface arrived, so it could not be recorded. |
| `WARN: HID_DESC_COUNT` | The HID class descriptor declared a wrong optional-descriptor count; the count was corrected as Linux does. |
| `WARN: HID_OPT_DESC` | The HID class descriptor lists optional class descriptors that usbhid does not use. |
| `WARN: HID_STRING_Q_FAIL` | A pre-probe USB string request could not be queued; probe continues without that string. |
| `ERR: HID_ASYNC_REQ_FAIL` | An asynchronous HID request failed for a reason other than device removal. |
| `ERR: HID_REPORT_NOMEM` | A logical report request could not allocate its node and was dropped. |
| `ERR: HID_CTRL_HOLD_FAIL` / `HID_CTRL_PARSE_FAIL` | A completed control GET could not be held for, or was rejected by, its report parser. |
| `ERR: HID_REPORT_OUT_FAIL` / `HID_REPORT_SET_FAIL` | The OUTPUT report over interrupt OUT, or the SET_REPORT over EP0, completed with an error. |
| `ERR: HID_IO_ACCOUNTING` | A transport I/O lease was released more often than it was taken; release stops before underflow. |
| `ERR: EVDEV_INPUT_DROP` | One evdev client queue was full, so the incoming input event was dropped. Queued releases and lifecycle markers are kept; Linux `SYN_DROPPED` resynchronization is not implemented. |
| `ERR: POWER_SUPPLY_LIMIT` | A ninth power supply was registered while the fixed eight-member power QueueSet was full; registration returns `-ENOSPC`. |
| `ERR: HID_ADDR_RANGE` / `HID_ADMIT_LINK` / `HID_CTRL_OWNER` / `HID_FIFO_BALANCE` / `HID_FIFO_CHAIN` / `HID_FIFO_HEAD` / `HID_FIFO_LANE` / `HID_FIFO_LINK` / `HID_FIFO_RELEASE` / `HID_FIFO_TAIL` / `HID_FINISH_STATE` / `HID_LIFE_OWNER` / `HID_RESET_OWNER` / `HID_RX_BUF_MISSING` / `HID_SUBMIT_STATE` / `HID_USB_IO_PUT` | An internal transport invariant failed: device-address range, admission list, control-lane owner, async FIFO links or lane, slot state, lifecycle or reset owner, receive buffer, or I/O lease. Debug builds then stop at `configASSERT()`; keep the log for analysis. |
| `ERR: HID_ADD_FAIL` | `hid_add_device()` failed for an error other than `-ENODEV`, such as parse, registration, or start failure. |
| `ERR: HID_EVDEV_FAIL` | The evdev input handler itself could not register. TinyUSB and the HID core continue for other input consumers. |
| `ERR: HID_INITCALL_FAIL` | At least one linked Linux module initcall failed; every remaining initcall still ran and host startup continued. |
| `ERR: HID_DRIVER_FAIL` | At least one linked HID driver failed to register; unrelated drivers and host startup continued. |
| `ERR: EVDEV_CONNECT_NOMEM` | The evdev handler could not allocate its private handle; as in Linux, the HID and other input consumers remain registered. |
| `ERR: EVDEV_CLIENT_NOMEM` | The automatic firmware evdev client or its queue could not be allocated; that client is skipped without unbinding the HID. |
| `ERR: EVDEV_OPEN_FAIL` | Opening one automatic evdev client failed; its private state is released and the HID remains bound. |
| `ERR: EVDEV_PUBLISH_FAIL` | One evdev client could not join devmon; it is closed and released without unbinding the HID. |
| `ERR: HID_USB_DEV_ALLOC_FAIL` | Device publication reached the bounded cache without the required admission slot. Ordinary cache pressure is parked event-first, so this indicates an admission/lifetime invariant failure rather than expected OOM. |
| `ERR: HID_PROBE_DEFER_FAIL` | Mount/pre-probe identity could not be retained, the bounded probe slots were occupied, or device-descriptor pre-probe exhausted its attempts. |
| `ERR: HID_PROBE_NOMEM` | A parser/probe allocation failed, including the transient exact-size report-descriptor buffer. |
| `ERR: HID_USB_PARENT_MISSING` | A child was observed after its hub cache epoch disappeared; it was rejected instead of attached to the root hub. |
| `ERR: HID_REPORT_SKIP` | A successful endpoint giveback exceeded or otherwise violated its armed buffer bounds; payload was rejected before parsing. |
| `ERR: HID_RX_REARM_FAIL` | Receive could not be armed again after a report callback. |
| `ERR: HID_RX_STALL` | Interrupt IN stalled. Payload was discarded and asynchronous endpoint clear-halt recovery started. |
| `DBG: HID_CLEAR_HALT_OK` | Remote endpoint halt was cleared; the host owner may now reset the local PIO toggle to DATA0 and rearm. |
| `ERR: HID_CLEAR_HALT_FAIL` | Remote clear-halt transfer failed; terminal recovery queues coordinated device teardown/re-enumeration. |
| `ERR: HID_RX_XFER_FAIL` | Interrupt IN failed or timed out. Payload was discarded and upstream-style delayed retry started. One occurrence immediately adjacent to physical removal can be abort-before-remove ordering; repetition while attached or missing resumed input is a failure. |
| `ERR: HID_RX_INVARIANT` | Interrupt-IN completion did not match its live owner/epoch/buffer tuple. The callback parked it before parser publication. |
| `ERR: HID_ASYNC_XFER_TUPLE` | A serial-matched broker completion violated its address/epoch/endpoint/payload tuple; the existing task-side `-EIO` retirement path ran. Zero-data EP0 completion is normalized from TinyUSB/PIO's eight-byte SETUP accounting to Linux's zero payload bytes before this check. |
| `ERR: HID_ASYNC_HUB_PIN` | A pinned hub-reset slot changed identity during its immediate host-owner completion handoff. |
| `ERR: HID_WAIT_TIMEOUT` | Ordinary upstream-shaped `hid_hw_wait()` did not reach its durable idle predicate within 10 seconds. Teardown uses a separate unbounded lifetime fence. |
| `ERR: HID_TEARDOWN_WAIT` | The combined IN/CTRL/OUT teardown fence returned an error after producers were stopped; the HID lifetime must not be considered cleanly retired. |
| `DBG: HID_RESET_Q` | Terminal report recovery published an exact physical-device reset to lifecycle. |
| `DBG: HID_RESET_OK` | Old transport state retired and a fresh same-topology TinyUSB mount completed. |
| `ERR: HID_RESET_FAIL` | Coordinated teardown/reset/re-enumeration exhausted its bounded phase or hub retry deadline. |
| `ERR: HID_ASYNC_CANCEL_FAIL` | Pending async HID requests could not be cancelled during detach. |
| `ERR: HID_CTRL_DISPATCH_FAIL` | A completed control GET could not be published to its ordinary report queue or direct lifecycle owner. |
| `ERR: HID_XFER_TO` | A request which reached TinyUSB exceeded its active wire timeout. Synchronous generic USB messages retain their caller-supplied timeout from accepted queueing; asynchronous `hid_hw_request()` reports wait on the exact EP0/endpoint-idle event and have no firmware-only pre-wire timeout, matching the upstream URB queue. |
| `ERR: HID_DEV_DESC_XFER` / `HID_DEV_DESC_SHORT` / `HID_DEV_DESC_TYPE` | One full device-descriptor refetch attempt failed, returned fewer than 18 bytes, or returned the wrong descriptor type. The first three retain the pending HID and retry after 100 ms; the fourth terminates that preprobe. |
| `ERR: HID_EP0_CALLBACK_LOST` | Exact host cancellation reported that it invoked the terminal callback, but the retained async slot did not observe it. |
| `ERR: HID_EP0_OWNER_MISMATCH` | The cancel helper found a different live daddr + callback + serial owner and deliberately left it untouched. |
| `ERR: HID_ATTACH_OVERFLOW` | More distinct topologies arrived during one enumeration than the bounded device table can retain. The excess ATTACH was dropped instead of blocking TinyUSB on its own queue. |
| `ERR: HID_ENUM_CONFIG_NOMEM` | A validated 513..4096-byte full configuration descriptor could not obtain its exact transient host buffer; the enumeration epoch failed cleanly. |
| `ERR: HID_ENUM_CONFIG_TOO_LARGE` | The short configuration header declared `wTotalLength` above the firmware's 4096-byte bound; no full GET was submitted. |
| `ERR: HID_ENUM_CONFIG_INVALID` | The short header or completed full configuration descriptor was short, malformed, or inconsistent with its retained `wTotalLength`; no class parser received it. |
| `ERR: HID_WQ_INIT_TWICE` | Workqueue initialization was invoked after its mutex had already been published. |
| `ERR: HID_LOCK_INIT_TWICE` | Shared transport synchronization was initialized twice. Ordinary lock misuse is debug-asserted after evaluating the FreeRTOS call; it has no separate async diagnostic/fail-stop path. |
| `ERR: HID_FIELD_NOMEM` | Allocation of one exact persistent `hid_field` graph failed; parser and driver-core cleanup reject the interface rather than bind a partial graph. |
| `INFO: HID_FIELDS64_UPSTREAM256` | A report descriptor opened successfully with the firmware's 64-field table rather than upstream Linux's 256-field table. This is a policy marker, not proof that the limit was reached. |
| `INFO: HID_USAGES675_UP_12288` | The DJ HID++ keyboard-connection path selected the firmware's 675-usage policy rather than upstream Linux's 12,288. This policy marker is not itself an OOM result. |
| `WARN: HID_USAGE_CAP_DROP` | A report used an array selector outside the retained 675-entry field lookup. |
| `DBG: HID_REPORT_OUT_Q` / `DBG: HID_REPORT_OUT_OK` | `.request()` routed an OUTPUT report through interrupt OUT and it completed. |
| `DBG: HID_REPORT_SET_Q` / `DBG: HID_REPORT_SET_OK` | `.request()` routed SET_REPORT through EP0 (FEATURE or no interrupt OUT) and it completed. |
| `WARN: HID_CTRL_QUEUE_FULL` / `WARN: HID_OUT_QUEUE_FULL` | One per-HID logical `.request()` FIFO reached the corresponding upstream-effective limit (255 CTRL or 63 OUT entries); the void request was dropped as Linux does on a full ring. |
| `WARN: HID_REPORT_QUEUE_MEMORY` | Compact logical FIFO nodes reached the shared 4096-byte firmware budget before the upstream count limit. Increase it only after measuring heap with the target work device. |
| `WARN: EVDEV_BATCH_CAP` | An unusual input device computed more than the bounded 62-value host batch; input remains best-effort. |

The retired aggregate messages `HID_ASYNC_INVARIANT` and
`HID_ASYNC_HOST_OWNER` are not present in the current firmware. Classify broker
completion failures using `HID_ASYNC_XFER_TUPLE` and `HID_ASYNC_HUB_PIN`.

The old `USB_MOUNT_CB` and `HID_MOUNT_CB` callback markers are also absent:
callback context no longer invokes the normal logger.
