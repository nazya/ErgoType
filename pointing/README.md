# Pointing Module

This folder contains pointing-device runtime code.

Current:
- `pointer.c`: active pointing task + MOT interrupt wiring.
- `pointer.h`: public interface for pointing task + motion IRQ binding.
- `pmw3360/`: PMW3360 sensor driver module (kept as separate submodule under pointing).
- `pmw3389/`: PMW3389 sensor driver module.

Planned additions:
- Cirque trackpad integration.
- Azoteq touch integration.

Config example (JSON):
```json
	{
	  "spi0": { "sck": 6, "mosi": 3, "miso": 4, "baud": 500000 },
	  "drivers": {
	    "pmw3389": [
	      { "role": "mousemove", "spi_idx": 0, "cs": 5, "irq": 7, "cpi": 800 }
	    ]
	  }
	}
	```
PMW3360/PMW3389 SPI clock mode is fixed to mode 3 (CPOL=1, CPHA=1).

Guideline:
- Keep shared IRQ/task-handle ownership inside this module when possible.
- Keep `main.c` focused on task orchestration and wiring only.
