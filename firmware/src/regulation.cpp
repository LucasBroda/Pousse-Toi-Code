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

// --- Pompes : une par bac (D-35) ---
struct Pompe {
  uint8_t broche;
  float debit_ml_min;  // config.h, mesuré pour chaque pompe
  char nom;            // 'A' ou 'B'
  bool active;
  bool en_test;
  uint32_t debut_ms;
  uint32_t duree_ms;
  // Dose décidée pendant que l'autre pompe tournait : lancée juste après
  bool dose_en_attente;
  uint32_t attente_duree_ms;
  bool dose_deja_faite;
  uint32_t derniere_dose_ms;
  uint32_t ml_jour;    // gardé en mémoire non volatile
  uint32_t releve_ml;  // depuis le dernier relevé
  uint32_t releve_ms;
};

Pompe pompes[NB_BACS] = {
    {BROCHE_MOSFET_POMPE_A, DEBIT_POMPE_A_ML_MIN, 'A'},
    {BROCHE_MOSFET_POMPE_B, DEBIT_POMPE_B_ML_MIN, 'B'},
};
const char *CLES_ML_JOUR[NB_BACS] = {"ml_a", "ml_b"};

// Compteur journalier en mémoire non volatile : un redémarrage en boucle
// ne fait pas arroser au-delà de la limite du jour
Preferences memoire;
long jour_courant = 0;

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
  for (uint8_t b = 0; b < NB_BACS; b++) memoire.putUInt(CLES_ML_JOUR[b], pompes[b].ml_jour);
}

void verifier_changement_jour() {
  long jour = horloge_numero_jour();
  if (jour != jour_courant) {
    jour_courant = jour;
    for (Pompe &p : pompes) p.ml_jour = 0;
    enregistrer_ml_jour();
  }
}

bool une_pompe_tourne() {
  for (const Pompe &p : pompes) {
    if (p.active) return true;
  }
  return false;
}

void demarrer_pompe(Pompe &p, uint32_t duree_ms, bool test) {
  p.active = true;
  p.en_test = test;
  p.dose_en_attente = false;
  p.debut_ms = millis();
  p.duree_ms = duree_ms;
  digitalWrite(p.broche, HIGH);
}

void arreter_pompe(Pompe &p) {
  digitalWrite(p.broche, LOW);
  if (!p.active) return;
  p.active = false;
  uint32_t duree = millis() - p.debut_ms;
  if (p.en_test) {
    Serial.printf("[pompe %c] test terminé après %lu s\n", p.nom, (unsigned long)(duree / 1000));
    return;
  }
  // Volume = durée × débit (D-15, pas de débitmètre)
  uint32_t ml = lroundf(duree / 60000.0f * p.debit_ml_min);
  p.releve_ml += ml;
  p.releve_ms += duree;
  p.ml_jour += ml;
  enregistrer_ml_jour();
  Serial.printf("[pompe %c] dose terminée : %lu ml en %lu s (%lu ml aujourd'hui)\n", p.nom,
                (unsigned long)ml, (unsigned long)(duree / 1000), (unsigned long)p.ml_jour);
}

// Lance la dose en attente d'un bac dès que plus aucune pompe ne tourne
void lancer_dose_en_attente() {
  if (une_pompe_tourne()) return;
  for (Pompe &p : pompes) {
    if (p.dose_en_attente) {
      Serial.printf("[pompe %c] dose en attente lancée\n", p.nom);
      demarrer_pompe(p, p.attente_duree_ms, false);
      return;
    }
  }
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

// --- Arrosage par durée, bac par bac (D-15, D-35) ---
bool bac_trop_sec(const Mesures &m, const Reglages &r, uint8_t b) {
  return !isnan(m.sol_humidite_pct[b]) && m.sol_humidite_pct[b] < r.sol_humidite_min_pct;
}

void reguler_arrosage_bac(const Mesures &m, const Reglages &r, uint8_t b) {
  Pompe &p = pompes[b];
  if (p.active || p.dose_en_attente || !bac_trop_sec(m, r, b)) return;
  uint32_t delai_ms = uint32_t(r.delai_entre_doses_min * 60000.0f);
  if (p.dose_deja_faite && millis() - p.derniere_dose_ms < delai_ms) return;
  if (p.ml_jour >= ARROSAGE_MAX_ML_PAR_JOUR_PAR_BAC || p.debit_ml_min <= 0) return;

  float dose = min(r.dose_ml, float(ARROSAGE_MAX_ML_PAR_JOUR_PAR_BAC - p.ml_jour));
  uint32_t duree_ms = uint32_t(dose / p.debit_ml_min * 60000.0f);
  if (duree_ms == 0) return;
  p.dose_deja_faite = true;
  p.derniere_dose_ms = millis();
  Serial.printf("[pompe %c] dose de %.0f ml : %lu s de pompage\n", p.nom, dose,
                (unsigned long)(duree_ms / 1000));
  if (une_pompe_tourne()) {
    // Jamais deux pompes à la fois : celle-ci attend la fin de l'autre
    p.dose_en_attente = true;
    p.attente_duree_ms = duree_ms;
    Serial.printf("[pompe %c] en attente de la fin de l'autre pompe\n", p.nom);
  } else {
    demarrer_pompe(p, duree_ms, false);
  }
}

// --- Voyants (sens PROPOSÉ du contrat v0, règles PROVISOIRES, P-15) ---
EtatVoyant evaluer_eau(const Mesures &m, const Reglages &r) {
  bool aucune_sonde = isnan(m.sol_humidite_pct[BAC_A]) && isnan(m.sol_humidite_pct[BAC_B]);
  if (aucune_sonde) return VOYANT_ALERTE;  // capteur à vérifier
  EtatVoyant etat = VOYANT_OK;
  for (uint8_t b = 0; b < NB_BACS; b++) {
    if (!bac_trop_sec(m, r, b)) continue;
    // Sol sec alors que la limite du jour de ce bac est atteinte : intervention nécessaire
    if (pompes[b].ml_jour >= ARROSAGE_MAX_ML_PAR_JOUR_PAR_BAC) return VOYANT_CRITIQUE;
    etat = VOYANT_ALERTE;
  }
  return etat;
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
  // Pompes et ventilateur à l'arrêt dès le démarrage (03_architecture.md)
  for (const Pompe &p : pompes) {
    digitalWrite(p.broche, LOW);
    pinMode(p.broche, OUTPUT);
  }
  digitalWrite(BROCHE_MOSFET_VENTILATEUR, LOW);
  pinMode(BROCHE_MOSFET_VENTILATEUR, OUTPUT);

  memoire.begin("arrosage", false);
  jour_courant = memoire.getLong("jour", 0);
  for (uint8_t b = 0; b < NB_BACS; b++) pompes[b].ml_jour = memoire.getUInt(CLES_ML_JOUR[b], 0);
  // Pas de verifier_changement_jour() ici : la DS3231 n'est pas encore
  // démarrée, le jour serait faux et le compteur remis à zéro à chaque
  // redémarrage. Il est vérifié à la première régulation, après l'horloge.
  Serial.printf("[pompes] aujourd'hui : bac A %lu ml, bac B %lu ml\n",
                (unsigned long)pompes[BAC_A].ml_jour, (unsigned long)pompes[BAC_B].ml_jour);
}

void regulation_appliquer(const Mesures &mesures, const Reglages &reglages) {
  verifier_changement_jour();
  reguler_ventilation(mesures, reglages);
  for (uint8_t b = 0; b < NB_BACS; b++) reguler_arrosage_bac(mesures, reglages, b);
  voyants[VOYANT_EAU] = evaluer_eau(mesures, reglages);
  voyants[VOYANT_LUMIERE] = evaluer_lumiere(mesures, reglages, voyants[VOYANT_LUMIERE]);
  voyants[VOYANT_CHALEUR] = evaluer_chaleur(mesures, reglages);
}

void regulation_surveiller() {
  for (Pompe &p : pompes) {
    if (p.active && millis() - p.debut_ms >= p.duree_ms) arreter_pompe(p);
  }
  lancer_dose_en_attente();
  // Comparaison signée : reste juste au passage de millis() par zéro (49 jours)
  if (ventilateur_mode != MODE_AUTO && int32_t(millis() - ventilateur_force_fin_ms) >= 0) {
    ventilateur_mode = MODE_AUTO;
    piloter_ventilateur(false);
    Serial.println("[ventilateur] fin du test, retour en automatique");
  }
}

bool ventilateur_actif() { return ventilateur; }
bool pompe_active() { return une_pompe_tourne(); }
bool pompe_bac_active(Bac bac) { return pompes[bac].active; }
EtatVoyant etat_voyant(Voyant voyant) { return voyants[voyant]; }
uint32_t arrosage_ml_aujourdhui(Bac bac) { return pompes[bac].ml_jour; }

void arrosage_prelever_compteurs(uint32_t &ml, uint32_t &secondes) {
  uint32_t total_ms = 0;
  ml = 0;
  for (Pompe &p : pompes) {
    ml += p.releve_ml;
    total_ms += p.releve_ms;
    p.releve_ml = 0;
    p.releve_ms = 0;
  }
  secondes = total_ms / 1000;
}

bool pompe_tester(Bac bac, uint32_t secondes) {
  if (une_pompe_tourne() || secondes == 0 || secondes > TEST_ACTIONNEUR_MAX_S) return false;
  demarrer_pompe(pompes[bac], secondes * 1000UL, true);
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
