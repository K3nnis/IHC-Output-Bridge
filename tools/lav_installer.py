"""Samler web-installeren til GitHub Pages.

Bygger et samlet firmware-image (bootloader, partitionstabel, OTA-data og
app i én fil), som web-installeren flasher fra adresse 0, og lægger det
sammen med installationssiden og et ESP Web Tools-manifest i en mappe.

Kræver, at firmwaren er bygget først:

    pio run -e esp32s3
    python3 tools/lav_installer.py --version 2.0.0 --ud site

Resultat i mappen "site":
    index.html                       installationssiden
    manifest.json                    beskriver firmwaren for ESP Web Tools
    ihc-bridge-<version>-fuld.bin    til web-installeren / første flash
    ihc-bridge-<version>-ota.bin     til opdatering fra web-UI'et
"""

import argparse
import csv
import json
import os
import shutil
import subprocess
import sys

ROD = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BYG = os.path.join(ROD, ".pio", "build", "esp32s3")


def partition_offset(navn):
    """Finder en partitions adresse i partitions.csv, så scriptet altid
    følger partitionstabellen."""
    with open(os.path.join(ROD, "partitions.csv"), encoding="utf-8") as f:
        linjer = [l for l in f if l.strip() and not l.lstrip().startswith("#")]
    for raekke in csv.reader(linjer):
        felter = [x.strip() for x in raekke]
        if felter[0] == navn:
            return int(felter[3], 0)
    sys.exit(f"Partitionen '{navn}' findes ikke i partitions.csv")


def esptool_kommando():
    """Bruger esptool fra pip, hvis det er installeret, ellers PlatformIO's."""
    try:
        import esptool  # noqa: F401
        return [sys.executable, "-m", "esptool"]
    except ImportError:
        sti = os.path.expanduser("~/.platformio/packages/tool-esptoolpy/esptool.py")
        if not os.path.exists(sti):
            sys.exit("esptool mangler: pip install esptool")
        return [sys.executable, sti]


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--version", required=True, help="f.eks. 2.0.0")
    p.add_argument("--ud", default=os.path.join(ROD, "site"), help="mappe til resultatet")
    a = p.parse_args()

    for fil in ("bootloader.bin", "partitions.bin", "ota_data_initial.bin", "firmware.bin"):
        if not os.path.exists(os.path.join(BYG, fil)):
            sys.exit(f"{fil} mangler i {BYG}. Kør først: pio run -e esp32s3")

    os.makedirs(a.ud, exist_ok=True)
    fuld = f"ihc-bridge-{a.version}-fuld.bin"
    ota = f"ihc-bridge-{a.version}-ota.bin"

    # ESP32-S3: bootloader på 0x0, partitionstabel på 0x8000.
    subprocess.run(
        esptool_kommando()
        + ["--chip", "esp32s3", "merge_bin", "-o", os.path.join(a.ud, fuld),
           "--flash_mode", "keep", "--flash_freq", "keep", "--flash_size", "16MB",
           "0x0", os.path.join(BYG, "bootloader.bin"),
           "0x8000", os.path.join(BYG, "partitions.bin"),
           hex(partition_offset("otadata")), os.path.join(BYG, "ota_data_initial.bin"),
           hex(partition_offset("app0")), os.path.join(BYG, "firmware.bin")],
        check=True,
    )
    shutil.copy(os.path.join(BYG, "firmware.bin"), os.path.join(a.ud, ota))

    manifest = {
        "name": "IHC Bridge",
        "version": a.version,
        # Ved første installation spørges brugeren, om enheden skal slettes.
        "new_install_prompt_erase": True,
        # Firmwaren bruger ikke Improv; opsætning sker via opsætnings-Wi-Fi'et.
        "new_install_improv_wait_time": 0,
        "builds": [{"chipFamily": "ESP32-S3", "parts": [{"path": fuld, "offset": 0}]}],
    }
    with open(os.path.join(a.ud, "manifest.json"), "w", encoding="utf-8") as f:
        json.dump(manifest, f, ensure_ascii=False, indent=2)

    with open(os.path.join(ROD, "installer", "index.html"), encoding="utf-8") as f:
        side = f.read()
    side = side.replace("{{VERSION}}", a.version).replace("{{OTA_FIL}}", ota)
    with open(os.path.join(a.ud, "index.html"), "w", encoding="utf-8") as f:
        f.write(side)

    print(f"Installer klar i {a.ud}: {fuld}, {ota}, manifest.json, index.html")


if __name__ == "__main__":
    main()
