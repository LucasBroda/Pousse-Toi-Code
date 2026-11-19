// Modèle des identifiants du firmware.
// Copier ce fichier en "secrets.h" (même dossier) et remplir les valeurs.
// "secrets.h" est ignoré par Git : ne jamais y committer de vrai mot de passe.

#pragma once

// Réseau Wi-Fi auquel l'ESP32 se connecte.
// Réseau provisoire : P-06 pas encore tranché (proposition : point d'accès
// "SerreAutonome" créé par le Raspberry Pi).
#define WIFI_SSID "nom_du_reseau"
#define WIFI_MOTDEPASSE "mot_de_passe"
