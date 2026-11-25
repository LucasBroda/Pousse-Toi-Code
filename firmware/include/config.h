// Valeurs réglables du firmware des fonctions vitales, regroupées dans un
// seul fichier (07_conventions.md). Aucune valeur réglable ailleurs.
//
// Statuts : DÉCIDÉ (08_decisions.md), PROPOSÉ (valeur de travail, à confirmer),
// PROVISOIRE (valeur choisie dans le firmware en attendant une décision).

#pragma once

#include <stdint.h>

// ============================================================
// Brochage — PROPOSÉ (03_architecture.md)
// ============================================================
constexpr uint8_t BROCHE_I2C_SDA = 21;
constexpr uint8_t BROCHE_I2C_SCL = 22;
constexpr uint8_t BROCHE_SOL_BAC_A = 34;   // ADC1, utilisable Wi-Fi actif
constexpr uint8_t BROCHE_SOL_BAC_B = 35;   // ADC1, utilisable Wi-Fi actif
// Une pompe par bac (D-35). Pompe A : GPIO 16 (PROPOSÉ, 03_architecture.md).
// Pompe B : GPIO 18 PROVISOIRE, broche à valider par Axel Fournier.
constexpr uint8_t BROCHE_MOSFET_POMPE_A = 16;
constexpr uint8_t BROCHE_MOSFET_POMPE_B = 18;
constexpr uint8_t BROCHE_MOSFET_VENTILATEUR = 17;
constexpr uint8_t BROCHE_VOYANT_EAU = 25;
constexpr uint8_t BROCHE_VOYANT_LUMIERE = 26;
constexpr uint8_t BROCHE_VOYANT_CHALEUR = 27;
constexpr uint8_t BROCHE_BUZZER = 13;

// ============================================================
// Sondes de sol capacitives
// ============================================================
// Étalonnage (valeur brute de l'ADC, 0 à 4095).
// Bac A : mesuré le 19/11/2026 (3 190 à l'air, 900 dans l'eau).
// Bac B : PAS ENCORE MESURÉ, valeurs du bac A reprises en attendant.
// Mesurer avec la commande série « sol » puis remplacer ici.
constexpr int SOL_BAC_A_SEC = 3190;
constexpr int SOL_BAC_A_EAU = 900;
constexpr int SOL_BAC_B_SEC = 3190;
constexpr int SOL_BAC_B_EAU = 900;

constexpr int SOL_NB_ECHANTILLONS = 16;    // moyenne de 16 lectures par mesure
// PROVISOIRE : hors de cette plage, la sonde est considérée débranchée (null)
constexpr int SOL_BRUT_MIN_VALIDE = 300;
constexpr int SOL_BRUT_MAX_VALIDE = 3800;

// ============================================================
// Seuils par défaut — PROVISOIRES (D-30, P-08 ouvert)
// Utilisés tant qu'aucune configuration n'a été reçue du Pi.
// ============================================================
constexpr float DEFAUT_SOL_HUMIDITE_MIN_PCT = 40.0f;
constexpr float DEFAUT_AIR_TEMPERATURE_MIN_C = 8.0f;
constexpr float DEFAUT_AIR_TEMPERATURE_MAX_C = 25.0f;   // démarrage du ventilateur
constexpr float DEFAUT_AIR_HUMIDITE_MAX_PCT = 80.0f;    // démarrage du ventilateur
constexpr float DEFAUT_LUMIERE_MIN_LUX = 2000.0f;
constexpr float DEFAUT_DOSE_ML = 100.0f;                // par bac, une pompe par bac (D-35)
constexpr float DEFAUT_DELAI_ENTRE_DOSES_MIN = 60.0f;

// Recommandations de D-30, hors contrat
constexpr float VENTILATEUR_ARRET_TEMPERATURE_C = 23.0f;  // hystérésis
constexpr float VENTILATEUR_ARRET_HUMIDITE_PCT = 75.0f;   // hystérésis
// D-30 : au plus 500 ml par jour et par bac. Avec une pompe par bac (D-35),
// chaque bac a sa propre limite.
constexpr uint32_t ARROSAGE_MAX_ML_PAR_JOUR_PAR_BAC = 500;

// ============================================================
// Pompes péristaltiques 12 V DC Telituny (A5, D-35), une par bac
// ============================================================
// Débit de chaque pompe, en ml/min. Valeur ANNONCÉE par le vendeur
// (« jusqu'à 90 ml/min ») : mesurer chaque pompe pendant 1 minute dans un
// verre doseur (D-15, commande « pompe A 60 ») et remplacer ici.
// Le débit est propre à chaque pompe : il est réglé ici et non par le Pi
// (le debit_ml_min unique du contrat v0 est ignoré, voir reseau.cpp).
constexpr float DEBIT_POMPE_A_ML_MIN = 90.0f;
constexpr float DEBIT_POMPE_B_ML_MIN = 90.0f;
// Les deux pompes ne tournent jamais en même temps : le bac B attend la fin
// de la dose du bac A. Limite l'appel de courant sur l'alimentation 12 V 3 A
// (D-16), partagée avec le ventilateur.

// Lumière évaluée de jour seulement (D-30). PROVISOIRE : 8h-16h UTC,
// soit 9h-17h à Paris en hiver.
constexpr uint8_t LUMIERE_HEURE_DEBUT_UTC = 8;
constexpr uint8_t LUMIERE_HEURE_FIN_UTC = 16;
constexpr uint8_t LUMIERE_NB_LECTURES_MOYENNE = 60;      // 1 heure à 1 lecture/min

// PROVISOIRE (P-15) : chaleur « critique » si la température reste au-dessus
// du maximum après 30 minutes de ventilation.
constexpr uint32_t CHALEUR_CRITIQUE_APRES_VENTILATION_MIN = 30;

// Durée maximale d'un test manuel de la pompe ou du ventilateur
constexpr uint32_t TEST_ACTIONNEUR_MAX_S = 120;

// ============================================================
// Cadences — PROPOSÉ (contrat ESP32 ↔ Pi v0)
// ============================================================
constexpr uint32_t PERIODE_LECTURE_MS = 60UL * 1000;          // 60 s
constexpr uint32_t PERIODE_RELEVE_MS = 5UL * 60 * 1000;       // 5 min
constexpr uint32_t PERIODE_CONFIG_MS = 60UL * 60 * 1000;      // 1 h
constexpr uint8_t FILE_RELEVES_MAX = 12;                      // 1 h de relevés

// ============================================================
// Réseau — À DÉFINIR (P-06). Identifiants Wi-Fi dans secrets.h.
// ============================================================
// PROVISOIRE : adresse du Pi sur le partage de connexion du 19/11/2026.
// Elle peut changer ; mettre à jour ici quand P-06 sera tranché.
constexpr const char *PI_HOST = "10.177.100.14";
constexpr uint16_t PI_PORT = 8000;
constexpr uint32_t HTTP_DELAI_MAX_MS = 3000;
constexpr uint32_t WIFI_NOUVEL_ESSAI_MS = 30UL * 1000;

constexpr int VERSION_CONTRAT = 0;
constexpr const char *NOM_APPAREIL = "serre-esp32";

// ============================================================
// Voyants et buzzer — PROPOSÉ (contrat v0, sens des voyants ; P-15)
// ============================================================
constexpr uint32_t CLIGNOTEMENT_DEMI_PERIODE_MS = 500;
// tone() fait sonner un buzzer passif comme un buzzer actif (P-16 ouvert)
constexpr uint32_t BUZZER_FREQUENCE_HZ = 2000;
constexpr uint32_t BIP_DUREE_MS = 100;
constexpr uint32_t BIP_PAUSE_MS = 150;
constexpr uint32_t PERIODE_BIPS_CRITIQUE_MS = 60UL * 1000;  // 2 bips par minute
