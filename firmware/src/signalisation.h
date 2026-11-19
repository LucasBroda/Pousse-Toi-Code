// Voyants et buzzer (sens PROPOSÉ du contrat v0) :
//   ok       -> voyant éteint, buzzer silencieux
//   alerte   -> voyant allumé fixe, buzzer silencieux
//   critique -> voyant clignotant, bips courts

#pragma once

#include "types.h"

void signalisation_demarrer();
// Appelée à chaque tour de boucle, sans jamais bloquer
void signalisation_mettre_a_jour();
// Tests manuels (commandes série), bloquants pendant quelques secondes
void signalisation_tester_voyants();
void signalisation_tester_buzzer();
