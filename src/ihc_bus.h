// IHC-bus: forbinder Sender, Modtager og Styring med de fysiske ben.
//
// Et hardware-timer-interrupt på kerne 1 kører hvert 39 µs:
//   - hver 4. gang (156 µs) sættes alle 16 output-linjer med én registerskrivning
//   - hver gang læses alle 8 input-linjer og gives til Modtager
//   - når Modtager bekræfter et tryk, udfører Styring reglerne med det samme,
//     stadig inde i interruptet, så udgangen kommer med i næste frame
// Wi-Fi, MQTT og web kører på kerne 0 og kan ikke forstyrre timingen.
#pragma once

#include <cstddef>
#include <cstdint>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ihc_modtager.h"
#include "ihc_regler.h"
#include "ihc_sender.h"

namespace ihc_bus {

// Bits i task-notifikationen til hændelses-tasken.
constexpr uint32_t kEventOutputs = 1u << 0;  // en eller flere udgange er ændret
constexpr uint32_t kEventInputs = 1u << 1;   // en bekræftet indgangstilstand er ændret

// --- Opstart (kaldes før start()) ---

// Indlæser gemt udgangstilstand, så første frame allerede er korrekt.
void preload_outputs(const uint8_t outputs[ihc::kOutputModules]);
// Tasken, der får besked om ændringer.
void set_event_task(TaskHandle_t task);

// Starter bussen. Returnerer først, når interruptet kører.
void start();

// --- Regler ---

// Aktiverer nye regler med det samme. Kan kaldes når som helst.
// Returnerer antal gyldige regler.
size_t set_regler(const ihc::Regel *regler, size_t antal);
// Kopierer de aktive regler. Returnerer antal.
size_t regler(ihc::Regel *ud, size_t max_antal);

// --- Udgange (modul 0-15, kanal 0-7) ---

void set_output(uint32_t module, uint32_t channel, bool on);
void toggle_output(uint32_t module, uint32_t channel);
void set_output_module(uint32_t module, uint8_t outputs);
uint8_t output_module(uint32_t module);
void copy_outputs(uint8_t out[ihc::kOutputModules]);

// Henter og nulstiller bitsæt over ændrede outputmoduler / inputmoduler.
uint16_t take_changed_outputs();
uint8_t take_changed_inputs();

// --- Temperaturfølere ---

// Indgange på et inputmodul (0-7), der er temperaturfølere. De udløser ikke
// regler, og hver gyldig frame på dem lægges i en kø til afkodning.
void set_temperatur_kanaler(uint32_t module, uint16_t maske);

struct TempHaendelse {
  uint32_t sample;  // tidspunkt i samples à 39 µs
  uint16_t data;    // temperaturkanalernes niveau (maskeret)
  uint8_t modul;    // 0-7
};
// Henter næste ændring på temperaturkanalerne. Returnerer false, hvis køen er tom.
bool hent_temp_haendelse(TempHaendelse &ud);
uint32_t temp_koe_tabt();

// --- Diagnose ---

const ihc::Sender &sender();
ihc::Modtager &modtager();
// Længste målte tid i interruptet (µs).
uint32_t isr_max_us();

}  // namespace ihc_bus
