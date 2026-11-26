// Sikkerhed: UI-adgangskode, login-sessioner og nulstilling med BOOT-knappen.
//
// Adgangskoden gemmes som PBKDF2-SHA256-hash med tilfældigt salt. Den bruges
// også som kode til nødbetjenings-AP'et (WPA2), som kræver selve koden; den
// gemmes derfor også i klartekst i "konfig"-partitionen.
//
// Holdes BOOT-knappen inde i 10 sekunder under drift, slettes adgangskoden,
// og alle logger ud. Wi-Fi, MQTT, navne og regler bevares. Ved næste besøg
// i web-UI'et vælges en ny adgangskode som ved første opstart.
#pragma once

#include <cstdint>
#include <string>

namespace sikkerhed {

constexpr size_t kMinKode = 8;   // WPA2 kræver mindst 8 tegn
constexpr size_t kMaxKode = 63;  // WPA2 tillader højst 63 tegn

void init();  // kræver lager::init()
// Starter overvågning af BOOT-knappen.
void start_boot_knap();

bool har_kode();
// Sætter adgangskoden (første opstart eller efter nulstilling).
bool saet_kode(const std::string &ny, std::string &fejl);
bool skift_kode(const std::string &gammel, const std::string &ny, std::string &fejl);

enum class LoginResultat { kOk, kForkert, kVent };
// Ved kOk sættes token; ved kVent sættes vent_s til antal sekunder.
LoginResultat login(const std::string &kode, std::string &token, uint32_t &vent_s);
// Opretter en session uden kode (lige efter at koden er sat første gang).
std::string ny_session();
bool session_gyldig(const std::string &token);
void log_ud(const std::string &token);

}  // namespace sikkerhed
