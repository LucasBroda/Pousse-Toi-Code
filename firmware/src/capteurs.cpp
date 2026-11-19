#include "capteurs.h"

#include <Adafruit_BME280.h>
#include <Arduino.h>
#include <BH1750.h>
#include <Wire.h>
#include <math.h>

#include "config.h"

namespace {

Adafruit_BME280 bme;
BH1750 luxmetre;
bool bme_ok = false;
bool bh_ok = false;

const uint8_t BROCHES_SOL[NB_BACS] = {BROCHE_SOL_BAC_A, BROCHE_SOL_BAC_B};
const int SOL_SEC[NB_BACS] = {SOL_BAC_A_SEC, SOL_BAC_B_SEC};
const int SOL_EAU[NB_BACS] = {SOL_BAC_A_EAU, SOL_BAC_B_EAU};

// Le BME280 est en 0x76 ou 0x77 selon le câblage de SDO
void demarrer_bme280() {
  bme_ok = bme.begin(0x76, &Wire) || bme.begin(0x77, &Wire);
}

// Le BH1750 est en 0x23, ou 0x5C si ADDR est au niveau haut
void demarrer_bh1750() {
  bh_ok = luxmetre.begin(BH1750::CONTINUOUS_HIGH_RES_MODE, 0x23, &Wire) ||
          luxmetre.begin(BH1750::CONTINUOUS_HIGH_RES_MODE, 0x5C, &Wire);
}

float valeur_ou_absente(float valeur, float min, float max) {
  if (isnan(valeur) || valeur < min || valeur > max) return NAN;
  return valeur;
}

}  // namespace

void capteurs_demarrer() {
  analogReadResolution(12);
  for (uint8_t broche : BROCHES_SOL) analogSetPinAttenuation(broche, ADC_11db);
  demarrer_bme280();
  demarrer_bh1750();
  Serial.printf("[capteurs] BME280 %s, BH1750 %s\n", bme_ok ? "trouvé" : "ABSENT",
                bh_ok ? "trouvé" : "ABSENT");
}

int sol_lire_brut(Bac bac) {
  long somme = 0;
  for (int i = 0; i < SOL_NB_ECHANTILLONS; i++) {
    somme += analogRead(BROCHES_SOL[bac]);
    delay(2);
  }
  return somme / SOL_NB_ECHANTILLONS;
}

float sol_vers_pourcentage(Bac bac, int brut) {
  if (brut < SOL_BRUT_MIN_VALIDE || brut > SOL_BRUT_MAX_VALIDE) return NAN;
  // 0 % = étalonnage à sec, 100 % = étalonnage dans l'eau (contrat v0)
  float pct = 100.0f * (SOL_SEC[bac] - brut) / float(SOL_SEC[bac] - SOL_EAU[bac]);
  return constrain(pct, 0.0f, 100.0f);
}

void capteurs_lire(Mesures &mesures) {
  for (uint8_t b = 0; b < NB_BACS; b++) {
    Bac bac = static_cast<Bac>(b);
    int brut = sol_lire_brut(bac);
    float pct = sol_vers_pourcentage(bac, brut);
    mesures.sol_brut[b] = isnan(pct) ? VALEUR_ABSENTE : brut;
    mesures.sol_humidite_pct[b] = pct;
  }

  // Un module branché après le démarrage est détecté à la lecture suivante
  if (!bme_ok) demarrer_bme280();
  if (bme_ok) {
    mesures.air_temperature_c = valeur_ou_absente(bme.readTemperature(), -40, 85);
    mesures.air_humidite_pct = valeur_ou_absente(bme.readHumidity(), 0, 100);
    mesures.air_pression_hpa = valeur_ou_absente(bme.readPressure() / 100.0f, 300, 1100);
    if (isnan(mesures.air_temperature_c)) bme_ok = false;  // module perdu
  } else {
    mesures.air_temperature_c = NAN;
    mesures.air_humidite_pct = NAN;
    mesures.air_pression_hpa = NAN;
  }

  if (!bh_ok) demarrer_bh1750();
  float lux = bh_ok ? luxmetre.readLightLevel() : -1.0f;
  if (lux < 0) {
    bh_ok = false;
    mesures.lumiere_lux = VALEUR_ABSENTE;
  } else {
    mesures.lumiere_lux = lroundf(lux);
  }
}

bool bme280_present() { return bme_ok; }
bool bh1750_present() { return bh_ok; }
