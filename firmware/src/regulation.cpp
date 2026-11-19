#include "regulation.h"

#include <Arduino.h>
#include <Preferences.h>
#include <math.h>

#include "config.h"
#include "horloge.h"

namespace {

// --- Ventilateur ---
bool ventilateur = false;
uint32_t ventilation_debut_ms = 0;
ModeForce ventilateur_mode = MODE_AUTO;
uint32_t ventilateur_force_fin_ms = 0;

// --- Pompe ---
bool pompe = false;
bool pompe_en_test = false;
uint32_t pompe_debut_ms = 0;
uint32_t pompe_duree_ms = 0;
float pompe_debit_ml_min = DEFAUT_DEBIT_ML_MIN;
bool dose_deja_faite = false;
uint32_t derniere_dose_ms = 0;

// Compteurs pour le relevé suivant
uint32_t releve_ml = 0;
uint32_t releve_ms = 0;

// Compteur journalier, gardé en mémoire non volatile pour qu'un redémarrage
// en boucle ne fasse pas arroser au-delà de la limite du jour
Preferences memoire;
long jour_courant = 0;
uint32_t ml_jour = 0;

// --- Voyants ---
EtatVoyant voyants[NB_VOYANTS] = {VOYANT_OK, VOYANT_OK, VOYANT_OK};
long lux_historique[LUMIERE_NB_LECTURES_MOYENNE];
uint8_t lux_nb = 0;
uint8_t lux_index = 0;

void piloter_ventilateur(bool marche) {
  if (marche && !ventilateur) ventilation_debut_ms = millis();
  ventilateur = marche;
  digitalWrite(BROCHE_MOSFET_VENTILATEUR, marche ? HIGH : LOW);
}

void enregistrer_ml_jour() {
  memoire.putLong("jour", jour_courant);
  memoire.putUInt("ml", ml_jour);
}

void verifier_changement_jour() {
  long jour = horloge_numero_jour();
  if (jour != jour_courant) {
    jour_courant = jour;
    ml_jour = 0;
    enregistrer_ml_jour();
  }
}

void demarrer_pompe(uint32_t duree_ms, bool test) {
  pompe = true;
  pompe_en_test = test;
  pompe_debut_ms = millis();
  pompe_duree_ms = duree_ms;
  digitalWrite(BROCHE_MOSFET_POMPE, HIGH);
}

void arreter_pompe() {
  digitalWrite(BROCHE_MOSFET_POMPE, LOW);
  if (!pompe) return;
  pompe = false;
  uint32_t duree = millis() - pompe_debut_ms;
  if (pompe_en_test) {
    Serial.printf("[pompe] test terminé après %lu s\n", (unsigned long)(duree / 1000));
    return;
  }
  // Volume = durée × débit (D-15, pas de débitmètre)
  uint32_t ml = lroundf(duree / 60000.0f * pompe_debit_ml_min);
  releve_ml += ml;
  releve_ms += duree;
  ml_jour += ml;
  enregistrer_ml_jour();
  Serial.printf("[pompe] dose terminée : %lu ml en %lu s (%lu ml aujourd'hui)\n",
                (unsigned long)ml, (unsigned long)(duree / 1000), (unsigned long)ml_jour);
}

// --- Ventilation sur seuil, avec hystérésis (D-30) ---
void reguler_ventilation(const Mesures &m, const Reglages &r) {
  if (ventilateur_mode != MODE_AUTO) return;
  if (isnan(m.air_temperature_c)) {
    piloter_ventilateur(false);  // sans mesure, on ne ventile pas à l'aveugle
    return;
  }
  bool trop_chaud = m.air_temperature_c > r.air_temperature_max_c;
  bool trop_humide = !isnan(m.air_humidite_pct) && m.air_humidite_pct > r.air_humidite_max_pct;
  bool redescendu = m.air_temperature_c < VENTILATEUR_ARRET_TEMPERATURE_C &&
                    (isnan(m.air_humidite_pct) || m.air_humidite_pct < VENTILATEUR_ARRET_HUMIDITE_PCT);
  if (!ventilateur && (trop_chaud || trop_humide)) {
    piloter_ventilateur(true);
    Serial.printf("[ventilateur] marche (%.1f °C, %.1f %%)\n", m.air_temperature_c, m.air_humidite_pct);
  } else if (ventilateur && redescendu) {
    piloter_ventilateur(false);
    Serial.printf("[ventilateur] arrêt (%.1f °C, %.1f %%)\n", m.air_temperature_c, m.air_humidite_pct);
  }
}

// --- Arrosage par durée (D-15) ---
bool un_bac_trop_sec(const Mesures &m, const Reglages &r) {
  for (uint8_t b = 0; b < NB_BACS; b++) {
    if (!isnan(m.sol_humidite_pct[b]) && m.sol_humidite_pct[b] < r.sol_humidite_min_pct) return true;
  }
  return false;
}

void reguler_arrosage(const Mesures &m, const Reglages &r) {
  if (pompe || !un_bac_trop_sec(m, r)) return;
  uint32_t delai_ms = uint32_t(r.delai_entre_doses_min * 60000.0f);
  if (dose_deja_faite && millis() - derniere_dose_ms < delai_ms) return;
  if (ml_jour >= ARROSAGE_MAX_ML_PAR_JOUR || r.debit_ml_min <= 0) return;

  float dose = min(r.dose_ml, float(ARROSAGE_MAX_ML_PAR_JOUR - ml_jour));
  uint32_t duree_ms = uint32_t(dose / r.debit_ml_min * 60000.0f);
  if (duree_ms == 0) return;
  pompe_debit_ml_min = r.debit_ml_min;
  dose_deja_faite = true;
  derniere_dose_ms = millis();
  demarrer_pompe(duree_ms, false);
  Serial.printf("[pompe] dose de %.0f ml : %lu s de pompage\n", dose, (unsigned long)(duree_ms / 1000));
}

// --- Voyants (sens PROPOSÉ du contrat v0, règles PROVISOIRES, P-15) ---
EtatVoyant evaluer_eau(const Mesures &m, const Reglages &r) {
  bool aucune_sonde = isnan(m.sol_humidite_pct[BAC_A]) && isnan(m.sol_humidite_pct[BAC_B]);
  if (aucune_sonde) return VOYANT_ALERTE;  // capteur à vérifier
  if (!un_bac_trop_sec(m, r)) return VOYANT_OK;
  // Sol sec alors que la limite du jour est atteinte : intervention nécessaire
  return ml_jour >= ARROSAGE_MAX_ML_PAR_JOUR ? VOYANT_CRITIQUE : VOYANT_ALERTE;
}

EtatVoyant evaluer_lumiere(const Mesures &m, const Reglages &r, EtatVoyant precedent) {
  if (!horloge_valide()) return precedent;
  uint8_t heure = horloge_maintenant().hour();
  bool jour = heure >= LUMIERE_HEURE_DEBUT_UTC && heure < LUMIERE_HEURE_FIN_UTC;
  if (!jour || m.lumiere_lux == VALEUR_ABSENTE) return precedent;  // la nuit, l'état reste figé

  lux_historique[lux_index] = m.lumiere_lux;
  lux_index = (lux_index + 1) % LUMIERE_NB_LECTURES_MOYENNE;
  if (lux_nb < LUMIERE_NB_LECTURES_MOYENNE) lux_nb++;
  // Moyenne sur une heure (D-30) ; on attend une heure complète avant de juger
  if (lux_nb < LUMIERE_NB_LECTURES_MOYENNE) return precedent;
  long somme = 0;
  for (uint8_t i = 0; i < lux_nb; i++) somme += lux_historique[i];
  return (somme / float(lux_nb)) < r.lumiere_min_lux ? VOYANT_ALERTE : VOYANT_OK;
}

EtatVoyant evaluer_chaleur(const Mesures &m, const Reglages &r) {
  if (isnan(m.air_temperature_c)) return VOYANT_ALERTE;  // capteur à vérifier
  if (m.air_temperature_c > r.air_temperature_max_c) {
    uint32_t ventilation_min = ventilateur ? (millis() - ventilation_debut_ms) / 60000UL : 0;
    return ventilation_min >= CHALEUR_CRITIQUE_APRES_VENTILATION_MIN ? VOYANT_CRITIQUE : VOYANT_ALERTE;
  }
  if (m.air_temperature_c < r.air_temperature_min_c) return VOYANT_ALERTE;
  return VOYANT_OK;
}

}  // namespace

void regulation_demarrer() {
  // Pompe et ventilateur à l'arrêt dès le démarrage (03_architecture.md)
  digitalWrite(BROCHE_MOSFET_POMPE, LOW);
  digitalWrite(BROCHE_MOSFET_VENTILATEUR, LOW);
  pinMode(BROCHE_MOSFET_POMPE, OUTPUT);
  pinMode(BROCHE_MOSFET_VENTILATEUR, OUTPUT);

  memoire.begin("arrosage", false);
  jour_courant = memoire.getLong("jour", 0);
  ml_jour = memoire.getUInt("ml", 0);
  verifier_changement_jour();
  Serial.printf("[pompe] déjà %lu ml arrosés aujourd'hui\n", (unsigned long)ml_jour);
}

void regulation_appliquer(const Mesures &mesures, const Reglages &reglages) {
  verifier_changement_jour();
  reguler_ventilation(mesures, reglages);
  reguler_arrosage(mesures, reglages);
  voyants[VOYANT_EAU] = evaluer_eau(mesures, reglages);
  voyants[VOYANT_LUMIERE] = evaluer_lumiere(mesures, reglages, voyants[VOYANT_LUMIERE]);
  voyants[VOYANT_CHALEUR] = evaluer_chaleur(mesures, reglages);
}

void regulation_surveiller() {
  if (pompe && millis() - pompe_debut_ms >= pompe_duree_ms) arreter_pompe();
  // Comparaison signée : reste juste au passage de millis() par zéro (49 jours)
  if (ventilateur_mode != MODE_AUTO && int32_t(millis() - ventilateur_force_fin_ms) >= 0) {
    ventilateur_mode = MODE_AUTO;
    piloter_ventilateur(false);
    Serial.println("[ventilateur] fin du test, retour en automatique");
  }
}

bool ventilateur_actif() { return ventilateur; }
bool pompe_active() { return pompe; }
EtatVoyant etat_voyant(Voyant voyant) { return voyants[voyant]; }
uint32_t arrosage_ml_aujourdhui() { return ml_jour; }

void arrosage_prelever_compteurs(uint32_t &ml, uint32_t &secondes) {
  ml = releve_ml;
  secondes = releve_ms / 1000;
  releve_ml = 0;
  releve_ms = 0;
}

bool pompe_tester(uint32_t secondes) {
  if (pompe || secondes == 0 || secondes > TEST_ACTIONNEUR_MAX_S) return false;
  demarrer_pompe(secondes * 1000UL, true);
  return true;
}

void ventilateur_forcer(ModeForce mode, uint32_t secondes) {
  if (mode == MODE_AUTO || secondes == 0) {
    ventilateur_mode = MODE_AUTO;
    return;
  }
  ventilateur_mode = mode;
  ventilateur_force_fin_ms = millis() + min(secondes, TEST_ACTIONNEUR_MAX_S) * 1000UL;
  piloter_ventilateur(mode == MODE_FORCE_MARCHE);
}
