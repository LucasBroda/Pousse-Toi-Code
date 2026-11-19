// Firmware des fonctions vitales de la Serre Autonome — v0.2
//
// L'ESP32 mesure, arrose, ventile et allume les voyants seul, même serveur
// éteint et Wi-Fi coupé. Le Raspberry Pi ne fait que recevoir les relevés
// et fournir les seuils (contrat ESP32 ↔ Pi v0).
//
// Toutes les valeurs réglables sont dans include/config.h.

#include <Arduino.h>
#include <Wire.h>
#include <math.h>

#include "capteurs.h"
#include "commandes.h"
#include "config.h"
#include "horloge.h"
#include "regulation.h"
#include "reseau.h"
#include "signalisation.h"

namespace {

Mesures mesures = {{VALEUR_ABSENTE, VALEUR_ABSENTE}, {NAN, NAN}, NAN, NAN, NAN, VALEUR_ABSENTE};
Reglages reglages;
uint32_t derniere_lecture_ms = 0;
uint32_t dernier_releve_ms = 0;

void lire_et_reguler() {
  capteurs_lire(mesures);
  regulation_appliquer(mesures, reglages);
}

}  // namespace

void setup() {
  // Actionneurs à l'arrêt avant toute autre chose
  regulation_demarrer();
  signalisation_demarrer();

  Serial.begin(115200);
  delay(200);
  Serial.println("\n=== Serre Autonome — firmware v0.2 ===");

  Wire.begin(BROCHE_I2C_SDA, BROCHE_I2C_SCL);
  capteurs_demarrer();
  horloge_demarrer();
  reseau_demarrer(reglages);
  reglages_afficher(reglages);
  Serial.println("Taper « aide » pour la liste des commandes.");

  lire_et_reguler();
  derniere_lecture_ms = millis();
  dernier_releve_ms = millis();
}

void loop() {
  uint32_t maintenant = millis();

  commandes_traiter(mesures, reglages);
  regulation_surveiller();

  // Régulation toutes les 60 s (contrat v0)
  if (maintenant - derniere_lecture_ms >= PERIODE_LECTURE_MS) {
    derniere_lecture_ms = maintenant;
    lire_et_reguler();
  }

  // Relevé toutes les 5 minutes, avec la dernière lecture (contrat v0)
  if (maintenant - dernier_releve_ms >= PERIODE_RELEVE_MS) {
    dernier_releve_ms = maintenant;
    reseau_ajouter_releve(mesures);
  }

  // Pas de requête réseau pendant un arrosage : une attente HTTP de 3 s
  // retarderait l'arrêt de la pompe et fausserait la dose
  if (!pompe_active()) reseau_tache(reglages);

  signalisation_mettre_a_jour();
}
