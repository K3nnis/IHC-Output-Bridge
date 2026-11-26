#!/usr/bin/env bash
# Tjekker at bus-interruptet og alt, det kalder, ligger i intern RAM (IRAM).
# Kode i flash kan ikke køre, mens der skrives til flash (NVS, OTA), og vil
# give crash eller forsinkede IHC-frames.
#
# Projektets egne funktioner følges rekursivt. ESP-IDF-funktioner (lås,
# task-besked) tjekkes for placering, men følges ikke videre.
# Indirekte kald (funktionspointere) er kun tilladt i de funktioner, der
# står i ALLOW_INDIRECT; deres mål tilføjes som ekstra startpunkter.
#
# Brug: tools/check_isr_iram.sh [sti/til/firmware.elf]
set -euo pipefail

ELF="${1:-.pio/build/esp32s3/firmware.elf}"
TC="${TOOLCHAIN:-$HOME/.platformio/packages/toolchain-xtensa-esp-elf/bin}"
NM="$TC/xtensa-esp32s3-elf-nm"
OBJDUMP="$TC/xtensa-esp32s3-elf-objdump"

# Startpunkter: timer-interruptet og de handlere, som Modtager kalder via pointer.
ROOTS=("ihc_bus::(anonymous namespace)::on_alarm(" "ihc_bus::(anonymous namespace)::on_input_change("
       "ihc_bus::(anonymous namespace)::on_frame(")
# Funktioner, hvor et indirekte kald er forventet.
ALLOW_INDIRECT=("ihc::Modtager::finish_frame(")

# IRAM på ESP32-S3: 0x40370000 - 0x403E0000
in_iram() { local a=$((16#$1)); (( a >= 0x40370000 && a < 0x403E0000 )); }

SYMS=$("$NM" -S -C "$ELF")
declare -A seen=()
queue=()
fail=0
for r in "${ROOTS[@]}"; do
  a=$(awk -v r="$r" 'index($0, r) {print $1; exit}' <<<"$SYMS")
  [[ -n "$a" ]] || { echo "FEJL: $r ikke fundet i $ELF"; exit 1; }
  queue+=("$a")
done

while ((${#queue[@]})); do
  addr="${queue[0]}"; queue=("${queue[@]:1}")
  [[ -n "${seen[$addr]:-}" ]] && continue
  seen[$addr]=1
  line=$(awk -v a="$addr" '$1 == a && NF >= 4 {print; exit}' <<<"$SYMS")
  size=$(awk '{print $2}' <<<"$line"); name=$(cut -d' ' -f4- <<<"$line")
  [[ -n "$name" ]] || name=$(awk -v a="$addr" '$1 == a {print $NF; exit}' <<<"$SYMS")
  if ! in_iram "$addr"; then
    echo "FEJL: $name (0x$addr) ligger IKKE i IRAM"; fail=1; continue
  fi
  echo "ok    0x$addr  $name"
  # Kun projektets egen kode følges videre.
  [[ "$name" == *ihc* && -n "$size" ]] || continue
  stop=$(printf "0x%x" $((16#$addr + 16#$size)))
  dis=$("$OBJDUMP" -d --start-address="0x$addr" --stop-address="$stop" "$ELF")
  while read -r target; do
    [[ -n "$target" ]] && queue+=("$target")
  done < <(grep -oE "call(0|4|8|12)\s+[0-9a-f]{8}" <<<"$dis" | awk '{print $2}' | sort -u)
  if grep -qE "callx(0|4|8|12)\s" <<<"$dis"; then
    allowed=0
    for f in "${ALLOW_INDIRECT[@]}"; do [[ "$name" == *"$f"* ]] && allowed=1; done
    if ((allowed)); then
      echo "      (indirekte kald forventet her; målet er tjekket som startpunkt)"
    else
      echo "FEJL: $name laver et uventet indirekte kald"; fail=1
    fi
  fi
done

if ((fail)); then echo "IRAM-tjek FEJLEDE"; exit 1; fi
echo "IRAM-tjek OK: hele interrupt-kæden ligger i intern RAM"
