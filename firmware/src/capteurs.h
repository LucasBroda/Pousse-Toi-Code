// Lecture des capteurs : sondes de sol, BME280, BH1750.

#pragma once

#include "types.h"

void capteurs_demarrer();
void capteurs_lire(Mesures &mesures);

// Moyenne de SOL_NB_ECHANTILLONS lectures brutes de l'ADC (0 à 4095)
int sol_lire_brut(Bac bac);
// Conversion brute -> % selon l'étalonnage du bac ; NAN si sonde absente
float sol_vers_pourcentage(Bac bac, int brut);

bool bme280_present();
bool bh1750_present();
