// IHC-bus: forbinder Sender og Receiver med de fysiske ben.
//
// Et hardware-timer-interrupt på kerne 1 kører hvert 39 µs:
//   - hver 4. gang (156 µs) sættes alle 16 output-linjer med én registerskrivning
//   - hver gang læses alle 8 input-linjer og gives til Receiver
// Wi-Fi, MQTT og web kører på kerne 0 og kan ikke forstyrre timingen.
#pragma once

#include <cstdint>

#include "ihc_receiver.h"
#include "ihc_sender.h"

namespace ihc_bus {

// Starter bussen. Returnerer først, når interruptet kører.
void start();

// Outputmodul 0-15: bit 0 = udgang 1 ... bit 7 = udgang 8.
void set_output_module(uint32_t module, uint8_t outputs);
uint8_t output_module(uint32_t module);

const ihc::Sender &sender();
ihc::Receiver &receiver();

// Længste målte tid i interruptet (µs), til diagnose.
uint32_t isr_max_us();

}  // namespace ihc_bus
