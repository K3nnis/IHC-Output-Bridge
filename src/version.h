#pragma once

// Firmwareversion. Vises i web-UI og i Home Assistant.
// Ved udgivelse sætter GitHub Actions versionen ud fra release-tagget
// (f.eks. v2.0.0 -> "2.0.0") via PLATFORMIO_BUILD_FLAGS.
#ifndef IHC_VERSION
#define IHC_VERSION "2.0.0-dev"
#endif
