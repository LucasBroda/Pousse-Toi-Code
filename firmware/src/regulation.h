// Fonctions vitales : ventilation, arrosage et état des voyants.
// Ne dépendent jamais du Pi ni du Wi-Fi (contrat v0, principes).

#pragma once

#include "types.h"

enum ModeForce : uint8_t { MODE_AUTO = 0, MODE_FORCE_MARCHE = 1, MODE_FORCE_ARRET = 2 };

void regulation_demarrer();
// Appelée après chaque lecture des capteurs (toutes les 60 s)
void regulation_appliquer(const Mesures &mesures, const Reglages &reglages);
// Appelée à chaque tour de boucle : arrête la pompe à la fin de la dose
void regulation_surveiller();

bool ventilateur_actif();
bool pompe_active();
EtatVoyant etat_voyant(Voyant voyant);

// Volume et durée d'arrosage depuis le dernier relevé, puis remise à zéro
void arrosage_prelever_compteurs(uint32_t &ml, uint32_t &secondes);
uint32_t arrosage_ml_aujourdhui();

// Tests manuels (commandes série). La pompe en test ne compte pas dans
// la limite journalière ni dans les relevés.
bool pompe_tester(uint32_t secondes);
void ventilateur_forcer(ModeForce mode, uint32_t secondes);
