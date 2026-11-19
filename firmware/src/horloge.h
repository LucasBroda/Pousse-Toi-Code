// Horloge DS3231, réglée en UTC (contrat v0).

#pragma once

#include <RTClib.h>
#include <stddef.h>

void horloge_demarrer();
// true si la DS3231 répond et que son heure a été réglée
bool horloge_valide();
bool horloge_presente();
DateTime horloge_maintenant();
// Écrit « AAAA-MM-JJTHH:MM:SSZ » dans tampon (21 octets) ; false si heure invalide
bool horloge_iso(char *tampon, size_t taille);
// Analyse « AAAA-MM-JJTHH:MM:SSZ » et règle la DS3231
bool horloge_regler_iso(const char *texte);
// Numéro du jour UTC, pour remettre à zéro le compteur d'arrosage chaque jour
long horloge_numero_jour();
