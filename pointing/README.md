# Pointing Module

This folder contains pointing-device runtime code.

Current:
- `pointer.c`: active pointing task + MOT interrupt wiring.
- `pointer.h`: public interface for pointing task + motion IRQ binding.
- `pmw3360/`: PMW3360 sensor driver module (kept as separate submodule under pointing).

Planned additions:
- Cirque trackpad integration.
- Azoteq touch integration.
- PMW3389 sensor support.

Guideline:
- Keep shared IRQ/task-handle ownership inside this module when possible.
- Keep `main.c` focused on task orchestration and wiring only.
