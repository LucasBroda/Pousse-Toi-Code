#include "signalisation.h"

#include <Arduino.h>

#include "config.h"
#include "regulation.h"

namespace {

const uint8_t BROCHES_VOYANTS[NB_VOYANTS] = {BROCHE_VOYANT_EAU, BROCHE_VOYANT_LUMIERE,
                                             BROCHE_VOYANT_CHALEUR};
const char *NOMS_VOYANTS[NB_VOYANTS] = {"eau", "lumière", "chaleur"};

// Séquence de bips : 2 bips courts toutes les PERIODE_BIPS_CRITIQUE_MS
uint32_t derniers_bips_ms = 0;
bool bips_deja_faits = false;
uint8_t bip_etape = 0;  // 0 = repos ; 1..4 = bip, pause, bip, fin
uint32_t bip_etape_debut_ms = 0;

bool un_voyant_critique() {
  for (uint8_t v = 0; v < NB_VOYANTS; v++) {
    if (etat_voyant(static_cast<Voyant>(v)) == VOYANT_CRITIQUE) return true;
  }
  return false;
}

void mettre_a_jour_bips() {
  uint32_t maintenant = millis();
  if (bip_etape == 0) {
    if (!un_voyant_critique()) return;
    if (bips_deja_faits && maintenant - derniers_bips_ms < PERIODE_BIPS_CRITIQUE_MS) return;
    bips_deja_faits = true;
    derniers_bips_ms = maintenant;
    bip_etape = 1;
    bip_etape_debut_ms = maintenant;
    tone(BROCHE_BUZZER, BUZZER_FREQUENCE_HZ, BIP_DUREE_MS);
    return;
  }
  uint32_t ecoule = maintenant - bip_etape_debut_ms;
  if (bip_etape == 1 && ecoule >= BIP_DUREE_MS + BIP_PAUSE_MS) {
    tone(BROCHE_BUZZER, BUZZER_FREQUENCE_HZ, BIP_DUREE_MS);
    bip_etape = 2;
    bip_etape_debut_ms = maintenant;
  } else if (bip_etape == 2 && ecoule >= BIP_DUREE_MS) {
    bip_etape = 0;
  }
}

}  // namespace

void signalisation_demarrer() {
  for (uint8_t broche : BROCHES_VOYANTS) {
    digitalWrite(broche, LOW);
    pinMode(broche, OUTPUT);
  }
  digitalWrite(BROCHE_BUZZER, LOW);
  pinMode(BROCHE_BUZZER, OUTPUT);
}

void signalisation_mettre_a_jour() {
  bool phase_allumee = (millis() / CLIGNOTEMENT_DEMI_PERIODE_MS) % 2 == 0;
  for (uint8_t v = 0; v < NB_VOYANTS; v++) {
    EtatVoyant etat = etat_voyant(static_cast<Voyant>(v));
    bool allume = etat == VOYANT_ALERTE || (etat == VOYANT_CRITIQUE && phase_allumee);
    digitalWrite(BROCHES_VOYANTS[v], allume ? HIGH : LOW);
  }
  mettre_a_jour_bips();
}

void signalisation_tester_voyants() {
  for (uint8_t v = 0; v < NB_VOYANTS; v++) {
    Serial.printf("[test] voyant %s allumé 1 s\n", NOMS_VOYANTS[v]);
    digitalWrite(BROCHES_VOYANTS[v], HIGH);
    delay(1000);
    digitalWrite(BROCHES_VOYANTS[v], LOW);
  }
}

// Vérifie le type de buzzer (P-16).
// Étape 1, niveau haut continu : un buzzer ACTIF sonne, un buzzer PASSIF
//   ne fait qu'un clic ou reste muet.
// Étape 2, signal à 2 kHz : les deux types sonnent.
void signalisation_tester_buzzer() {
  Serial.println("[test buzzer] étape 1 : niveau haut continu pendant 2 s");
  noTone(BROCHE_BUZZER);
  pinMode(BROCHE_BUZZER, OUTPUT);
  digitalWrite(BROCHE_BUZZER, HIGH);
  delay(2000);
  digitalWrite(BROCHE_BUZZER, LOW);
  delay(1000);
  Serial.println("[test buzzer] étape 2 : signal à 2 kHz pendant 2 s");
  tone(BROCHE_BUZZER, BUZZER_FREQUENCE_HZ, 2000);
  delay(2100);
  noTone(BROCHE_BUZZER);
  Serial.println("[test buzzer] Sonne aux étapes 1 et 2 -> ACTIF. "
                 "Muet ou simple clic à l'étape 1, sonne à l'étape 2 -> PASSIF.");
}
