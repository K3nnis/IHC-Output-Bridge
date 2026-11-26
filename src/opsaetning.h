// Opsætning: hvilke moduler der er tilsluttet, navne og typer på kanaler,
// og fælles enhedsoplysninger. Gemmes i "konfig"-partitionen.
//
// Outputmoduler slås til i web-UI'et (standard: ingen). Inputmoduler slås
// automatisk til, første gang de melder sig online, og kan fjernes i UI'et.
// Kun aktive moduler vises i MQTT/Home Assistant og web-UI.
//
// Moduler og kanaler tælles fra 1 i dette modul, som i UI og MQTT.
#pragma once

#include <cstdint>
#include <string>

#include "kanaltyper.h"

namespace opsaetning {

constexpr size_t kMaxNavn = 32;  // bytes (UTF-8)

void init();  // kræver lager::init()

uint16_t aktive_outputmoduler();  // bit 0 = modul 1
uint8_t aktive_inputmoduler();
bool gem_aktive_moduler(uint16_t output, uint8_t input);
// Slår et inputmodul til, hvis det ikke allerede er. Returnerer true, hvis
// det var nyt.
bool marker_inputmodul_set(uint32_t modul);

// Unikt ID ud fra MAC-adressen, f.eks. "a1b2c3".
const std::string &enheds_suffix();

// Navne. Uden eget navn bruges "Udgang 2.2" / "Indgang 3.15".
std::string udgangsnavn(uint32_t modul, uint32_t kanal);
std::string indgangsnavn(uint32_t modul, uint32_t kanal);
bool har_eget_udgangsnavn(uint32_t modul, uint32_t kanal);
bool har_eget_indgangsnavn(uint32_t modul, uint32_t kanal);

netvaerk::UdgangsType udgangstype(uint32_t modul, uint32_t kanal);
netvaerk::IndgangsType indgangstype(uint32_t modul, uint32_t kanal);

// Gemmer navn (tom = standardnavn) og type for én kanal.
bool gem_udgang(uint32_t modul, uint32_t kanal, const std::string &navn, netvaerk::UdgangsType type);
bool gem_indgang(uint32_t modul, uint32_t kanal, const std::string &navn, netvaerk::IndgangsType type);

}  // namespace opsaetning
