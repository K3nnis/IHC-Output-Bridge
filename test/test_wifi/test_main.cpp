// Unit-tests af Wi-Fi-logikken (genforbindelse og access points).
// Kører på PC: pio test -e native
#include <unity.h>

#include <vector>

#include "wifi_styring.h"

using namespace netvaerk;

constexpr int64_t S = 1000;
constexpr int64_t MIN = 60 * S;

// Kører tiden frem i skridt af 100 ms og samler tidspunkter for forsøg.
// Hvert forsøg fejler med det samme, medmindre lykkes_ved >= 0 og er nået.
static std::vector<int64_t> koer(WifiStyring &w, int64_t &nu, int64_t til, int64_t lykkes_ved = -1) {
  std::vector<int64_t> forsoeg;
  for (; nu <= til; nu += 100) {
    if (w.tick(nu)) {
      forsoeg.push_back(nu);
      if (lykkes_ved >= 0 && nu >= lykkes_ved) {
        w.forbundet(nu);
      } else {
        w.afbrudt(nu);
      }
    }
  }
  return forsoeg;
}

void test_uden_netvaerk_aabent_opsaetnings_ap() {
  WifiStyring w;
  w.start(0, false, false);
  TEST_ASSERT_EQUAL(Fase::kIntetNetvaerk, w.fase());
  TEST_ASSERT_EQUAL(ApTilstand::kOpsaetning, w.ap(0));
  TEST_ASSERT_FALSE(w.tick(1000));  // ingen forsøg uden netværk
  TEST_ASSERT_EQUAL(ApTilstand::kOpsaetning, w.ap(60 * MIN));
}

void test_forbinder_straks_ved_opstart() {
  WifiStyring w;
  w.start(0, true, true);
  TEST_ASSERT_TRUE(w.tick(0));
  TEST_ASSERT_EQUAL(Fase::kForbinder, w.fase());
  w.forbundet(800);
  TEST_ASSERT_EQUAL(Fase::kForbundet, w.fase());
  TEST_ASSERT_EQUAL(ApTilstand::kSlukket, w.ap(800));
}

void test_genforbindelse_hurtigt_og_derefter_hvert_10_minut() {
  WifiStyring w;
  w.start(0, true, true);
  int64_t nu = 0;
  koer(w, nu, 1000, 0);  // forbundet med det samme
  TEST_ASSERT_EQUAL(Fase::kForbundet, w.fase());

  w.afbrudt(10 * S);  // forbindelsen tabes
  nu = 10 * S;
  const auto f = koer(w, nu, 10 * S + 45 * MIN);
  // Straks, +5 s, +15 s, +30 s, +60 s, derefter hvert 10. minut.
  const int64_t forventet[] = {0, 5 * S, 20 * S, 50 * S, 110 * S, 110 * S + 10 * MIN, 110 * S + 20 * MIN,
                               110 * S + 30 * MIN, 110 * S + 40 * MIN};
  TEST_ASSERT_EQUAL_UINT32(9, f.size());
  for (size_t i = 0; i < 9; ++i) TEST_ASSERT_EQUAL_INT64(10 * S + forventet[i], f[i]);
}

void test_bliver_ved_med_at_prove() {
  WifiStyring w;
  w.start(0, true, true);
  int64_t nu = 0;
  const auto f = koer(w, nu, 24 * 60 * MIN);  // et døgn uden router
  TEST_ASSERT_TRUE(f.size() > 140);
  // Kommer routeren tilbage, forbindes ved næste forsøg.
  const auto g = koer(w, nu, nu + 11 * MIN, nu);
  TEST_ASSERT_EQUAL_UINT32(1, g.size());
  TEST_ASSERT_EQUAL(Fase::kForbundet, w.fase());
}

void test_noedbetjenings_ap_efter_10_minutter() {
  WifiStyring w;
  w.start(0, true, true);
  int64_t nu = 0;
  koer(w, nu, 1000, 0);
  w.afbrudt(2 * S);
  nu = 2 * S;
  koer(w, nu, 2 * S + 9 * MIN);
  TEST_ASSERT_EQUAL(ApTilstand::kSlukket, w.ap(nu));
  koer(w, nu, 2 * S + 10 * MIN + 1);
  TEST_ASSERT_EQUAL(ApTilstand::kNoedbetjening, w.ap(nu));

  // Forbindelsen kommer tilbage: AP lukkes straks.
  koer(w, nu, nu + 11 * MIN, nu);
  TEST_ASSERT_EQUAL(Fase::kForbundet, w.fase());
  TEST_ASSERT_EQUAL(ApTilstand::kSlukket, w.ap(nu));
}

void test_noedbetjenings_ap_kraever_adgangskode() {
  WifiStyring w;
  w.start(0, true, false);  // ingen UI-adgangskode endnu
  int64_t nu = 0;
  koer(w, nu, 30 * MIN);
  TEST_ASSERT_EQUAL(ApTilstand::kSlukket, w.ap(nu));  // aldrig et åbent AP
  w.noed_kode_sat(true);
  TEST_ASSERT_EQUAL(ApTilstand::kNoedbetjening, w.ap(nu));
}

void test_noedbetjening_ogsaa_hvis_aldrig_forbundet_efter_opstart() {
  WifiStyring w;
  w.start(0, true, true);
  int64_t nu = 0;
  koer(w, nu, 10 * MIN + 100);
  TEST_ASSERT_EQUAL(ApTilstand::kNoedbetjening, w.ap(nu));
}

void test_opsaetning_holder_ap_aabent_til_forbindelse() {
  WifiStyring w;
  w.start(0, false, false);
  w.netvaerk_gemt(5 * S);
  int64_t nu = 5 * S;
  // Forkert kode: forsøg fejler, AP forbliver åbent, så brugeren kan rette.
  koer(w, nu, 30 * MIN);
  TEST_ASSERT_EQUAL(ApTilstand::kOpsaetning, w.ap(nu));

  // Brugeren retter koden; nu lykkes det.
  w.netvaerk_gemt(nu);
  const int64_t forbundet_ved = nu;
  koer(w, nu, nu + 1000, nu);
  TEST_ASSERT_EQUAL(Fase::kForbundet, w.fase());
  // AP holdes åbent i 60 s, så brugeren kan se den nye IP-adresse.
  TEST_ASSERT_EQUAL(ApTilstand::kOpsaetning, w.ap(forbundet_ved + 59 * S));
  TEST_ASSERT_EQUAL(ApTilstand::kSlukket, w.ap(forbundet_ved + 61 * S));
}

void test_forsoeg_uden_svar_regnes_som_fejlet() {
  WifiStyring w;
  w.start(0, true, true);
  TEST_ASSERT_TRUE(w.tick(0));
  // Driveren svarer aldrig. Efter 30 s planlægges næste forsøg.
  TEST_ASSERT_FALSE(w.tick(29 * S));
  TEST_ASSERT_FALSE(w.tick(30 * S));
  TEST_ASSERT_EQUAL(Fase::kVenter, w.fase());
  TEST_ASSERT_EQUAL_INT64(5 * S, w.ms_til_naeste(30 * S));
  TEST_ASSERT_TRUE(w.tick(35 * S));
}

void test_sen_afbrydelse_ignoreres() {
  WifiStyring w;
  w.start(0, true, true);
  TEST_ASSERT_TRUE(w.tick(0));
  w.afbrudt(100);
  // En ekstra afbrydelses-hændelse fra driveren må ikke flytte planen.
  const int64_t foer = w.ms_til_naeste(200);
  w.afbrudt(200);
  TEST_ASSERT_EQUAL_INT64(foer, w.ms_til_naeste(200));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_uden_netvaerk_aabent_opsaetnings_ap);
  RUN_TEST(test_forbinder_straks_ved_opstart);
  RUN_TEST(test_genforbindelse_hurtigt_og_derefter_hvert_10_minut);
  RUN_TEST(test_bliver_ved_med_at_prove);
  RUN_TEST(test_noedbetjenings_ap_efter_10_minutter);
  RUN_TEST(test_noedbetjenings_ap_kraever_adgangskode);
  RUN_TEST(test_noedbetjening_ogsaa_hvis_aldrig_forbundet_efter_opstart);
  RUN_TEST(test_opsaetning_holder_ap_aabent_til_forbindelse);
  RUN_TEST(test_forsoeg_uden_svar_regnes_som_fejlet);
  RUN_TEST(test_sen_afbrydelse_ignoreres);
  return UNITY_END();
}
