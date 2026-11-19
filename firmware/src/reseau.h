// Échanges avec le Raspberry Pi (contrat ESP32 ↔ Pi v0).
// Aucune fonction vitale n'attend une réponse du Pi.

#pragma once

#include "types.h"

// Charge les réglages enregistrés (ou les valeurs par défaut de config.h)
// puis lance la connexion Wi-Fi sans l'attendre
void reseau_demarrer(Reglages &reglages);
// Appelée à chaque tour de boucle : reconnexion, envoi de la file,
// configuration toutes les heures. Ne pas appeler pendant un arrosage.
void reseau_tache(Reglages &reglages);

// Construit le relevé du contrat et le met dans la file d'envoi.
// false si l'horloge n'est pas réglée (le Pi refuserait le relevé).
bool reseau_ajouter_releve(const Mesures &mesures);

bool wifi_connecte();
uint8_t releves_en_attente();
uint32_t releves_sequence();
// Force la demande de configuration au prochain tour
void reseau_demander_config();
void reglages_afficher(const Reglages &reglages);
