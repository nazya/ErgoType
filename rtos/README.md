# RTOS Integration

This folder contains FreeRTOS integration glue for this project.

Contents:
- `FreeRTOSConfig.h`: project FreeRTOS configuration.
- `FreeRTOS_Kernel_import.cmake`: local import helper for kernel integration.
- `freertos_hook.c`: application hook implementations (malloc fail, stack overflow, static task memory).

Scope:
- Keep RTOS framework config/hooks here.
- Keep feature tasks in their feature modules (`pointing/`, `tusb/`, `keyscan`, etc.).
