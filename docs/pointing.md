# PMW3360/PMW3389 pointing

Pointing sensors use SPI mode 3, MSB first. Logical `spi0` and `spi1` map to
independent nRF52840 SPIM2 and SPIM3 instances; CS and active-low MOT/IRQ are
ordinary GPIOs. All configured pins accept global nRF52840 GPIO values `0..47`.

At startup each sensor is reset, receives its 4094-byte SROM one byte at a time
with the upstream timing, and has CPI applied. The Zephyr SPI boundary copies
each current one-byte TX transfer into Data RAM because nRF52840 EasyDMA cannot
read the flash-backed SROM arrays. The full SROM is never duplicated in RAM.

MOT wakes the pointing thread on a falling edge. The thread waits 1 ms to
coalesce events, reads the motion bursts, scales X to 125%, negates Y, then
applies the role-specific profile. Mouse-role devices produce x/y; scroll-role
devices produce horizontal pan/vertical wheel.

Profiles are `none`, `flat`, `adaptive`, and `custom`. Flat uses
`factor = 1 + speed / scale`. Adaptive chooses the current low/high-DPI curve
from sensor CPI and optionally averages velocity. Custom linearly interpolates
2..64 configured points. Full field ranges and an example are in
[config-json.md](config-json.md).

There is no polling fallback: MOT wiring is required. Sensor communication,
SROM acceptance, IRQ timing and the fixed coordinate transform remain
hardware-unverified.
