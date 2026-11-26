// Opsætning: hvilke moduler der er tilsluttet, og fælles enhedsoplysninger.
//
// Kun aktive moduler vises i MQTT/Home Assistant og web-UI. Standard er,
// at alle 16 outputmoduler og 8 inputmoduler er aktive; de kan slås fra i
// web-UI'et.
#pragma once

#include <cstdint>
#include <string>

namespace opsaetning {

void init();  // kræver initialiseret NVS

uint16_t aktive_outputmoduler();  // bit 0 = modul 1
uint8_t aktive_inputmoduler();
bool gem_aktive_moduler(uint16_t output, uint8_t input);

// Unikt ID ud fra MAC-adressen, f.eks. "a1b2c3".
const std::string &enheds_suffix();

// Navne på kanaler (modul og kanal fra 1). Indtil navngivning kommer i
// web-UI'et, bruges "Udgang 2.2" / "Indgang 3.15".
std::string udgangsnavn(uint32_t modul, uint32_t kanal);
std::string indgangsnavn(uint32_t modul, uint32_t kanal);

}  // namespace opsaetning
