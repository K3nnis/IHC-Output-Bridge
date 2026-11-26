"""Pakker web-UI'et (web/index.html) ind i firmwaren.

Komprimerer siden med gzip og skriver den som et C-array i
src/web_ui_data.h, som webserveren sender direkte. Køres automatisk af
PlatformIO før hvert build (extra_scripts), men kan også køres alene:

    python3 tools/byg_web.py
"""

import gzip
import hashlib
import os



def byg(rod):
    kilde = os.path.join(rod, "web", "index.html")
    maal = os.path.join(rod, "src", "web_ui_data.h")
    with open(kilde, "rb") as f:
        html = f.read()
    # mtime=0 giver samme bytes hver gang, så filen kun ændres, når siden gør.
    gz = gzip.compress(html, compresslevel=9, mtime=0)
    etag = hashlib.sha256(html).hexdigest()[:16]

    linjer = []
    for i in range(0, len(gz), 20):
        linjer.append("  " + ", ".join(f"0x{b:02x}" for b in gz[i:i + 20]) + ",")
    indhold = (
        "// Genereret af tools/byg_web.py ud fra web/index.html. Ret ikke i hånden.\n"
        "#pragma once\n\n"
        "#include <cstddef>\n#include <cstdint>\n\n"
        f'static constexpr const char *kWebUiEtag = "\\"{etag}\\"";\n'
        f"static constexpr size_t kWebUiGzLen = {len(gz)};\n"
        "static const uint8_t kWebUiGz[] = {\n" + "\n".join(linjer) + "\n};\n"
    )

    gammel = None
    if os.path.exists(maal):
        with open(maal, "r", encoding="utf-8") as f:
            gammel = f.read()
    if gammel != indhold:
        with open(maal, "w", encoding="utf-8") as f:
            f.write(indhold)
    print(f"Web-UI: {len(html)} bytes, {len(gz)} bytes komprimeret")


try:
    Import("env")  # noqa: F821  (findes kun, når PlatformIO kører scriptet)
    byg(env["PROJECT_DIR"])  # noqa: F821
except NameError:
    if __name__ == "__main__":
        byg(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
