// Structures partagées entre les modules du firmware.

#pragma once

#include <stdint.h>

// Une valeur absente (capteur en panne ou débranché) est notée
// NAN pour un décimal et -1 pour un entier ; elle part en null dans le JSON.
constexpr int VALEUR_ABSENTE = -1;

enum Bac : uint8_t { BAC_A = 0, BAC_B = 1, NB_BACS = 2 };

// Dernière lecture de tous les capteurs
struct Mesures {
  int sol_brut[NB_BACS];
  float sol_humidite_pct[NB_BACS];
  float air_temperature_c;
  float air_humidite_pct;
  float air_pression_hpa;
  long lumiere_lux;
};

// Seuils et réglages d'arrosage (GET /api/config du contrat v0).
// La dose et le délai s'appliquent à chaque bac (une pompe par bac, D-35).
// Le débit, propre à chaque pompe, est dans config.h.
struct Reglages {
  char plant[24];
  float sol_humidite_min_pct;
  float air_temperature_min_c;
  float air_temperature_max_c;
  float air_humidite_max_pct;
  float lumiere_min_lux;
  float dose_ml;
  float delai_entre_doses_min;
};

// États des voyants (contrat v0, « Sens des voyants »)
enum EtatVoyant : uint8_t { VOYANT_OK = 0, VOYANT_ALERTE = 1, VOYANT_CRITIQUE = 2 };
enum Voyant : uint8_t { VOYANT_EAU = 0, VOYANT_LUMIERE = 1, VOYANT_CHALEUR = 2, NB_VOYANTS = 3 };

// Texte du contrat v0 : "ok", "alerte" ou "critique"
inline const char *texte_etat_voyant(EtatVoyant etat) {
  switch (etat) {
    case VOYANT_ALERTE: return "alerte";
    case VOYANT_CRITIQUE: return "critique";
    default: return "ok";
  }
}
