# Upstream Porting Rules

Goal: keep ported upstream Linux HID files as close to upstream as possible.
The upstream file is the source of truth. The port should look like upstream
code with small local glue, not like a rewritten local implementation.

## Main Rule

When changing upstream code, do not silently replace, delete, rename, or
restructure it.

If an upstream line or block cannot be used as-is:

1. Keep the original upstream line or block commented out directly next to the
   replacement.
2. Put the port-specific replacement immediately below it.
3. Add a short reason explaining why this port differs from upstream.
4. Keep upstream function names, order, control flow, comments, and structure
   whenever possible.

## One-Line Replacement

Correct:

```c
// struct hid_driver *hdrv = hid->driver;
// struct hid_device keeps the matched driver pointer const in this port.
const struct hid_driver *hdrv = hid->driver;
```

Wrong:

```c
const struct hid_driver *hdrv = hid->driver;
```

The wrong version hides the upstream line and the reason for the diff.

## Disabled Upstream Call

Correct:

```c
// ret = usbhid_parse(hid);
// TinyUSB passes the report descriptor to this callback; usbhid_parse()
// consumes it from hid_add_device(), matching upstream usbhid_probe() lifecycle.
hid->ll_rdesc = desc_report;
hid->ll_rsize = desc_len;
```

Wrong:

```c
hid->ll_rdesc = desc_report;
hid->ll_rsize = desc_len;
```

The wrong version loses the upstream lifecycle reference.

## Port-Specific Glue

If code does not exist in upstream because it connects TinyUSB, FreeRTOS, or
ErgoType to Linux HID, mark it clearly.

Correct:

```c
// Upstream Linux: no equivalent; TinyUSB dev_addr/instance lookup glue.
static struct hid_device *hid_lookup(uint8_t dev_addr, uint8_t instance)
{
	...
}
```

## Missing Linux Subsystems

If Linux has a subsystem that is not wired yet, keep the upstream call visible
and explain what is missing.

Correct:

```c
// ret = hidraw_report_event(hid, data, size);
// hidraw is not wired yet; raw/proxy support should reconnect here.
ret = 0;
```

Wrong:

```c
ret = 0;
```

The wrong version gives no clue where hidraw/proxy support should reconnect.

## Keep Upstream Branch Shape

Correct:

```c
if (hid->claimed & HID_CLAIMED_HIDRAW) {
	// ret = hidraw_report_event(hid, data, size);
	// hidraw is not wired yet; raw/proxy support should reconnect here.
	ret = 0;
	if (ret)
		return ret;
}
```

Wrong:

```c
if (hid->claimed & HID_CLAIMED_HIDRAW)
	return 0;
```

The wrong version destroys the upstream lifecycle shape.

## Added Code

If adding code that upstream does not have, comment why it exists and which
boundary it adapts.

Correct:

```c
// TinyUSB callback gives dev_addr + instance instead of Linux usb_interface.
// Store them so later interrupt callbacks can find the matching hid_device.
hid->dev_addr = dev_addr;
hid->instance = instance;
```

## No Local Mini-Rewrites

Do not extract upstream switch branches or inline logic into new local helpers
just because it looks cleaner.

Wrong:

```c
static bool is_desktop_axis(unsigned int usage)
{
	return usage == HID_GD_X || usage == HID_GD_Y || usage == HID_GD_WHEEL;
}
```

If upstream had the check inline, keep it inline unless there is a real port
boundary reason.

## Where Glue Belongs

TinyUSB, FreeRTOS, and ErgoType boundary code should live in glue files when
possible:

```text
usb_host/usbhid.c
usb_host/hid_async.c
usb_host/hid_workqueue.c
usb_host/hid_timer.c
usb_host/evdev.c
```

Linux-derived files should stay Linux-shaped:

```text
usb_host/linux/drivers/hid/hid-core.c
usb_host/linux/drivers/hid/hid-input.c
usb_host/linux/drivers/input/input.c
```

## Diff Review Checklist

For every diff against upstream, answer:

1. Is this required because Pico, FreeRTOS, TinyUSB, or ErgoType lacks a Linux
   subsystem?
2. Is the original upstream code still visible next to the replacement?
3. Is the reason written next to the replacement?
4. Does this preserve upstream flow as far as possible?
5. Could this glue live outside the upstream file instead?

If the answer is unclear, do not change the code yet.

## Do Not Add Defensive Checks

Do not add checks for hypothetical future failures.

Wrong:

```c
if (!hid)
	return;
```

This is only valid if the current call path can really pass `NULL`.

## Good Port Diff

Correct:

```c
// usb_set_intfdata(intf, hid);
// TinyUSB mount callback builds a local usb_interface shim for this HID instance.
usb_set_intfdata(&hid->usb_intf, hid);
```

Wrong:

```c
usb_set_intfdata(&hid->usb_intf, hid);
```

The code may work, but the upstream diff reason is gone.

## Summary

Upstream files are not a place for clever rewrites. They are a readable port of
Linux code.

Keep upstream code visible. Comment every changed or disabled upstream line or
block. Explain every port-specific replacement. Add new code only with a short
reason. Minimize diff first, cleanup second.
