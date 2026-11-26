#!/usr/bin/env bash
# Tjekker at bus-interruptet og alt, det kalder, ligger i intern RAM (IRAM).
# Kode i flash kan ikke køre, mens der skrives til flash (NVS, OTA), og vil
# give crash eller forsinkede IHC-frames.
#
# Brug: tools/check_isr_iram.sh [sti/til/firmware.elf]
set -euo pipefail

ELF="${1:-.pio/build/esp32s3/firmware.elf}"
TC="${TOOLCHAIN:-$HOME/.platformio/packages/toolchain-xtensa-esp-elf/bin}"
NM="$TC/xtensa-esp32s3-elf-nm"
OBJDUMP="$TC/xtensa-esp32s3-elf-objdump"

# IRAM på ESP32-S3: 0x40370000 - 0x403E0000
in_iram() { local a=$((16#$1)); (( a >= 0x40370000 && a < 0x403E0000 )); }

declare -A seen=()
queue=()
start=$("$NM" "$ELF" | awk '/on_alarm/ {print $1; exit}')
[[ -n "$start" ]] || { echo "FEJL: on_alarm ikke fundet i $ELF"; exit 1; }
queue+=("$start")
fail=0

while ((${#queue[@]})); do
  addr="${queue[0]}"; queue=("${queue[@]:1}")
  [[ -n "${seen[$addr]:-}" ]] && continue
  seen[$addr]=1
  line=$("$NM" -S -C "$ELF" | awk -v a="$addr" '$1 == a && NF >= 4 {print; exit}')
  size=$(awk '{print $2}' <<<"$line"); name=$(cut -d' ' -f4- <<<"$line")
  if ! in_iram "$addr"; then
    echo "FEJL: $name (0x$addr) ligger IKKE i IRAM"; fail=1; continue
  fi
  echo "ok    0x$addr  $name"
  [[ -n "$size" ]] || continue
  stop=$(printf "0x%x" $((16#$addr + 16#$size)))
  while read -r target; do
    queue+=("$target")
  done < <("$OBJDUMP" -d --start-address="0x$addr" --stop-address="$stop" "$ELF" |
             grep -oE "call(0|4|8|12)\s+[0-9a-f]{8}" | awk '{print $2}' | sort -u)
  if "$OBJDUMP" -d --start-address="0x$addr" --stop-address="$stop" "$ELF" | grep -qE "callx(0|4|8|12)\s"; then
    echo "FEJL: $name laver et indirekte kald, som ikke kan kontrolleres"; fail=1
  fi
done

if ((fail)); then echo "IRAM-tjek FEJLEDE"; exit 1; fi
echo "IRAM-tjek OK: hele interrupt-kæden ligger i intern RAM"
