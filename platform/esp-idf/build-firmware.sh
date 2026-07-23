#!/usr/bin/env bash

set -euo pipefail

root=$(cd "$(dirname "$0")/../.." && pwd)
target=${1:-}

case "$target" in
    esp32s3)
        boot_offset=0x0
        objcopy=xtensa-esp32s3-elf-objcopy
        board=waveshare-esp32s3-pico
        ;;
    esp32p4)
        boot_offset=0x2000
        objcopy=riscv32-esp-elf-objcopy
        board=dfrobot-firebeetle2-esp32p4
        ;;
    *)
        printf 'usage: %s {esp32s3|esp32p4}\n' "$0" >&2
        exit 2
        ;;
esac

port="$root/platform/esp-idf/$target"
build="$port/build"
firmware="$port/firmware"
full_output="$firmware/ErgoType-$board-full-0x000000.bin"
app_output="$firmware/ErgoType-$board-app-0x010000.bin"
full_staging="$build/ErgoType-$board-full-0x000000.bin"

mkdir -p "$firmware"
idf.py -C "$port" -B "$build" build

image_object=$(find "$build" -name fat12_image.c.obj -print -quit)
"$objcopy" \
    -O binary \
    --only-section=.rodata.disk_image \
    --gap-fill=0xff \
    --pad-to=0x10000 \
    "$image_object" "$build/storage.bin"

test "$(stat -c %s "$build/storage.bin")" -eq 65536

python -m esptool --chip "$target" merge-bin \
    -o "$full_staging" \
    --flash-mode dio \
    --flash-size 16MB \
    --flash-freq 80m \
    "$boot_offset" "$build/bootloader/bootloader.bin" \
    0x8000 "$build/partition_table/partition-table.bin" \
    0x10000 "$build/ErgoTypeESP.bin" \
    0x110000 "$build/storage.bin"

cp "$full_staging" "$full_output"
cp "$build/ErgoTypeESP.bin" "$app_output"

printf 'full image, flash at 0x000000: %s\n' "$full_output"
printf 'application update, flash at 0x010000: %s\n' "$app_output"
