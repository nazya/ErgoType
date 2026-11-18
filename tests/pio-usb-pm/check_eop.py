#!/usr/bin/env python3
"""Source-level TX control-flow check, not a PIO timing or hardware emulator."""

import ast
from pathlib import Path
import re
import subprocess
import sys

root = Path(__file__).resolve().parents[2]
host = (root / "Pico-PIO-USB/src/pio_usb_host.c").read_text()
ll = (root / "Pico-PIO-USB/src/pio_usb_ll.h").read_text()
source = (root / "Pico-PIO-USB/src/usb_tx.pio").read_text()

encoded = re.search(r"resume_eop_encoded\[1\]\s*=\s*\{([^}]+)\}", host)
if not encoded:
    sys.exit("Update this check: resume EOP data format changed")
expression = " ".join(encoded[1].strip().rstrip(",").split())
for name, value in re.findall(r"(PIO_USB_TX_ENCODED_DATA_\w+)\s*=\s*(\d+)", ll):
    expression = expression.replace(name, value)
tree = ast.parse(expression, mode="eval")
allowed = (ast.Expression, ast.BinOp, ast.Constant, ast.LShift, ast.BitOr)
if any(not isinstance(node, allowed) for node in ast.walk(tree)):
    sys.exit("Update this check: unsupported EOP expression")
byte = eval(compile(tree, "<EOP data>", "eval"), {"__builtins__": {}})
symbols = [(byte >> shift) & 3 for shift in (6, 4, 2, 0)]

program = source.split(".program usb_tx_dpdm\n", 1)[1].split(".program", 1)[0]
instructions = []
wrap_target = 0
for line in program.splitlines():
    line = line.split(";", 1)[0].strip()
    if line == ".wrap_target":
        wrap_target = len(instructions)
    elif line.startswith(("irq ", "out ", "set ")):
        instructions.append(line.split("side", 1)[0].strip())

pc, output_enabled, eop_seen = 0, True, False
trace = []
for _ in range(32):
    trace.append(pc)
    instruction = instructions[pc]
    if instruction.startswith("irq "):
        eop_seen = True
        pc += 1
    elif instruction == "out pc, 2":
        if not symbols:
            break  # Empty TX FIFO: autopull cannot supply another symbol.
        pc = symbols.pop(0)
    elif instruction.startswith("set pindirs,"):
        output_enabled = bool(int(instruction.split(",")[1], 0))
        pc += 1
    else:
        sys.exit(f"Update this check: unsupported instruction {instruction}")
    if pc == len(instructions):
        pc = wrap_target
else:
    sys.exit("Update this check: TX program did not reach its empty-FIFO stall")

print("EOP TX trace:", " -> ".join(map(str, trace)), "(FIFO empty)")
print(f"Terminal PC={pc}; EOP seen={eop_seen}; output enabled={output_enabled}")
if not eop_seen or output_enabled:
    sys.exit("FAIL: EOP does not leave the bus released")

resume_end = host.split("pio_usb_host_resume_signal_end)", 1)[1]
resume_end = resume_end.split("restore_fs_bus)", 1)[0]
if re.search(r"while\s*\(\s*\*pc\s*<=\s*PIO_USB_TX_ENCODED_DATA_COMP\s*\)",
             resume_end) and pc <= 2:
    sys.exit("FAIL: a CPU which samples after the trace waits forever at PC <= 2")
if "while (pio->dbg_padoe & pin_mask)" not in resume_end:
    sys.exit("FAIL: expected the persistent pin-release predicate")
print("PASS: pin release remains observable after the transient PC phase", flush=True)

# Compile the real helper, not a handwritten copy of its branches. The MMIO
# model supplies time and hardware outcomes; it does not qualify USB timing.
signature = re.search(
    r"static bool __no_inline_not_in_flash_func\(pio_usb_host_resume_signal_end\)"
    r"\([^{}]+\)\s*\{", host)
if not signature:
    sys.exit("Update this check: resume helper signature changed")
start = signature.start()
cursor = signature.end()
depth = 1
while depth:
    depth += (host[cursor] == "{") - (host[cursor] == "}")
    cursor += 1
helper = host[start:cursor]
timeout = int(re.search(r"PIO_USB_HOST_RESUME_EOP_TIMEOUT_US\s*=\s*(\d+)", host)[1])
prefix = r'''
#include <cstdint>
#include <cstdio>
#include <cstdlib>
using uint = unsigned int;
#define __no_inline_not_in_flash_func(name) name
enum Case { NORMAL, DMA_STUCK, IRQ_MISSING, PINS_STUCK, CPU_LATE, SLOW_DMA };
struct Model {
    Case scenario;
    uint32_t now, epoch, eop_start, pins;
    bool enabled, irq, irq_fired, late;
    unsigned configured;
} model;
enum { IRQ_TX_ALL_MASK = 1, DMA_CH0_CTRL_TRIG_EN_BITS = 1, pio_pindirs = 4 };
enum { PIO_USB_HOST_RESUME_EOP_TIMEOUT_US = TIMEOUT_VALUE };
struct Register {
    bool is_irq;
    operator uint32_t() const {
        if (is_irq) {
            if (model.enabled && model.scenario != IRQ_MISSING &&
                !model.irq_fired && uint32_t(model.now - model.eop_start) >= 4) {
                model.irq = model.irq_fired = true;
                if (model.scenario == CPU_LATE && !model.late) {
                    model.now += 1000;
                    model.late = true;
                }
            }
            return model.irq ? IRQ_TX_ALL_MASK : 0;
        }
        if (model.enabled && model.scenario != PINS_STUCK &&
            uint32_t(model.now - model.eop_start) >= 8)
            model.pins = 0;
        return model.pins;
    }
    Register &operator=(uint32_t value) {
        if (is_irq && (value & IRQ_TX_ALL_MASK)) model.irq = false;
        return *this;
    }
};
struct FakePIO { Register irq{true}, dbg_padoe{false}; uint base; };
using PIO = FakePIO *;
struct pio_port_t { PIO pio_usb_tx; uint sm_tx, tx_ch, offset_tx; };
struct root_port_t { uint pin_dp, pin_dm; bool is_fullspeed; };
struct FakeDMA { struct { uint32_t al1_ctrl; } ch[1]; uint32_t abort; } dma;
FakeDMA *dma_hw = &dma;
uint8_t resume_eop_encoded[1] = {0x95};
uint pio_get_gpio_base(PIO pio) { return pio->base; }
uint pio_usb_host_line_state(root_port_t const *root, bool dp_high) {
    return dp_high == (root->pin_dp < root->pin_dm) ? 1u : 2u;
}
uint pio_encode_sideset(uint, uint value) { return value << 11; }
uint pio_encode_jmp(uint value) { return value; }
uint pio_encode_set(uint dest, uint value) { return 0xe000 | (dest << 5) | value; }
uint32_t get_time_us_32() { return model.now++; }
void pio_sm_clear_fifos(PIO, uint) {}
void pio_sm_restart(PIO, uint) { model.irq_fired = false; }
void dma_channel_transfer_from_buffer_now(uint, uint8_t *, unsigned long) {}
bool dma_channel_is_busy(uint) {
    return model.scenario == DMA_STUCK ||
        uint32_t(model.now - model.epoch) < (model.scenario == SLOW_DMA ? 98u : 2u);
}
void tight_loop_contents() {}
void pio_sm_exec(PIO, uint, uint instruction) {
    if ((instruction & 0xe0ff) == (0xe000 | (pio_pindirs << 5))) model.pins = 0;
}
void pio_sm_set_enabled(PIO, uint, bool enabled) {
    model.enabled = enabled;
    if (enabled) model.eop_start = model.now;
}
void configure_root_port(pio_port_t *, root_port_t *) { model.configured++; }
void hw_clear_bits(uint32_t *reg, uint32_t bits) { *reg &= ~bits; }
'''
suffix = r'''
int main() {
    struct Test { Case scenario; bool success; uint32_t start; uint dp, dm, base; };
    Test tests[] = {
        {NORMAL, true, 0, 21, 20, 0},
        {NORMAL, true, 0, 20, 21, 0},
        {NORMAL, true, 0, 45, 44, 16},
        {CPU_LATE, true, 0, 21, 20, 0},
        {DMA_STUCK, false, 0, 21, 20, 0},
        {IRQ_MISSING, false, 0, 21, 20, 0},
        {PINS_STUCK, false, 0, 21, 20, 0},
        {SLOW_DMA, false, 0, 21, 20, 0},
        {NORMAL, true, UINT32_MAX - 5, 21, 20, 0},
        {PINS_STUCK, false, UINT32_MAX - 5, 21, 20, 0},
    };
    unsigned count = 0;
    for (auto const &test : tests) {
        FakePIO pio;
        pio.base = test.base;
        pio_port_t pp{&pio, 0, 0, 0};
        root_port_t root{test.dp, test.dm, true};
        uint low_pin = test.dp < test.dm ? test.dp : test.dm;
        model = {test.scenario, test.start, test.start, 0,
                 3u << (low_pin - test.base), false, true, false, false, 0};
        dma = {};
        dma.ch[0].al1_ctrl = DMA_CH0_CTRL_TRIG_EN_BITS;
        bool result = pio_usb_host_resume_signal_end(&pp, &root);
        uint32_t elapsed = model.now - test.start;
        if (result != test.success ||
            (result && (model.configured != 1 || model.pins || !model.enabled ||
                        dma.abort || dma.ch[0].al1_ctrl != DMA_CH0_CTRL_TRIG_EN_BITS)) ||
            (!result && (model.pins || model.enabled || model.configured ||
                         dma.ch[0].al1_ctrl || dma.abort != 1 ||
                         elapsed > PIO_USB_HOST_RESUME_EOP_TIMEOUT_US + 2))) {
            std::fprintf(stderr, "FAIL: helper case=%u elapsed=%u result=%u\n",
                         unsigned(test.scenario), elapsed, result);
            return 1;
        }
        count++;
    }
    std::printf("PASS: %u real-helper cases (late CPU, DMA/IRQ/pins faults, shared deadline, timer wrap, GPIO base)\n", count);
}
'''
binary = root / "build/pio-pm-wait-check"
if not binary.parent.is_dir():
    sys.exit("Configure the main build/ first; this check creates no build directories")
native = prefix.replace("TIMEOUT_VALUE", str(timeout)) + helper + suffix
subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                "-x", "c++", "-", "-o", str(binary)],
               input=native, text=True, check=True)
subprocess.run([str(binary)], check=True, timeout=5)
