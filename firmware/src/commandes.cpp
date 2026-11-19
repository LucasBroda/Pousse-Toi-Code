#include "commandes.h"

#include <Arduino.h>
#include <Wire.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "capteurs.h"
#include "config.h"
#include "horloge.h"
#include "regulation.h"
#include "reseau.h"
#include "signalisation.h"

namespace {

char ligne[64];
uint8_t longueur = 0;

void afficher_aide() {
  Serial.println(
      "Commandes :\n"
      "  aide                      cette liste\n"
      "  etat                      mesures, actionneurs, voyants, réseau\n"
      "  i2c                       scan du bus I2C (SDA 21, SCL 22)\n"
      "  sol                       calibration : 10 lectures brutes des bacs A et B\n"
      "  buzzer                    test du type de buzzer (P-16)\n"
      "  voyants                   allume chaque voyant 1 s\n"
      "  pompe <s>                 pompe pendant <s> secondes (test à vide, 120 s max)\n"
      "  ventilateur <s>           ventilateur pendant <s> secondes\n"
      "  ventilateur auto          retour en automatique\n"
      "  heure                     heure de la DS3231\n"
      "  heure AAAA-MM-JJTHH:MM:SSZ  règle la DS3231 en UTC\n"
      "  releve                    lit les capteurs et envoie un relevé tout de suite\n"
      "  config                    redemande la configuration au Pi");
}

const char *texte_valeur(float v, char *tampon, size_t taille) {
  if (isnan(v)) return "null";
  snprintf(tampon, taille, "%.1f", v);
  return tampon;
}

void afficher_etat(const Mesures &m, const Reglages &r) {
  char t1[12], t2[12];
  char heure[24] = "non réglée";
  horloge_iso(heure, sizeof(heure));
  Serial.printf("Heure : %s\n", heure);
  for (uint8_t b = 0; b < NB_BACS; b++) {
    Serial.printf("Sol %c : brut %d, %s %%\n", 'A' + b, m.sol_brut[b],
                  texte_valeur(m.sol_humidite_pct[b], t1, sizeof(t1)));
  }
  Serial.printf("Air : %s °C, %s %%, ", texte_valeur(m.air_temperature_c, t1, sizeof(t1)),
                texte_valeur(m.air_humidite_pct, t2, sizeof(t2)));
  Serial.printf("%s hPa | lumière %ld lux\n", texte_valeur(m.air_pression_hpa, t1, sizeof(t1)),
                m.lumiere_lux);
  Serial.printf("Ventilateur %s | pompe %s | %lu ml arrosés aujourd'hui\n",
                ventilateur_actif() ? "marche" : "arrêt", pompe_active() ? "marche" : "arrêt",
                (unsigned long)arrosage_ml_aujourdhui());
  Serial.printf("Voyants : eau %s, lumière %s, chaleur %s\n",
                texte_etat_voyant(etat_voyant(VOYANT_EAU)),
                texte_etat_voyant(etat_voyant(VOYANT_LUMIERE)),
                texte_etat_voyant(etat_voyant(VOYANT_CHALEUR)));
  Serial.printf("Wi-Fi %s | relevé n°%lu | %u en attente | Pi %s:%u\n",
                wifi_connecte() ? "connecté" : "non connecté", (unsigned long)releves_sequence(),
                releves_en_attente(), PI_HOST, PI_PORT);
  Serial.println("Réglages :");
  reglages_afficher(r);
}

const char *nom_peripherique_i2c(uint8_t adresse) {
  switch (adresse) {
    case 0x23: case 0x5C: return "BH1750 (lumière)";
    case 0x76: case 0x77: return "BME280 (air)";
    case 0x68: return "DS3231 (horloge)";
    case 0x57: return "AT24C32 (mémoire du module DS3231, sans usage)";
    default: return "inconnu";
  }
}

void scanner_i2c() {
  Serial.println("[i2c] scan de 0x01 à 0x7E...");
  uint8_t trouves = 0;
  for (uint8_t adresse = 1; adresse < 0x7F; adresse++) {
    Wire.beginTransmission(adresse);
    if (Wire.endTransmission() == 0) {
      Serial.printf("[i2c] 0x%02X : %s\n", adresse, nom_peripherique_i2c(adresse));
      trouves++;
    }
  }
  Serial.printf("[i2c] %u périphérique(s) trouvé(s). Attendus : 0x23, 0x76 ou 0x77, 0x68\n", trouves);
}

// Calibration : poser la sonde à l'air sec, puis dans l'eau jusqu'à la
// ligne, et relever la moyenne de chaque série
void calibrer_sol() {
  Serial.println("[sol] 10 lectures, une par seconde (moyenne de 16 échantillons chacune)");
  long somme[NB_BACS] = {0, 0};
  int mini[NB_BACS] = {4095, 4095};
  int maxi[NB_BACS] = {0, 0};
  for (int i = 1; i <= 10; i++) {
    int brut[NB_BACS];
    for (uint8_t b = 0; b < NB_BACS; b++) {
      brut[b] = sol_lire_brut(static_cast<Bac>(b));
      somme[b] += brut[b];
      mini[b] = min(mini[b], brut[b]);
      maxi[b] = max(maxi[b], brut[b]);
    }
    Serial.printf("[sol] %2d : A = %4d   B = %4d\n", i, brut[BAC_A], brut[BAC_B]);
    delay(1000);
  }
  for (uint8_t b = 0; b < NB_BACS; b++) {
    Serial.printf("[sol] bac %c : moyenne %ld, min %d, max %d\n", 'A' + b, somme[b] / 10, mini[b],
                  maxi[b]);
  }
  Serial.println("[sol] Reporter les moyennes « à l'air » et « dans l'eau » dans config.h");
}

void executer(char *commande, Mesures &mesures, Reglages &reglages) {
  char *argument = strchr(commande, ' ');
  if (argument) *argument++ = '\0';

  if (!strcmp(commande, "aide")) afficher_aide();
  else if (!strcmp(commande, "etat")) afficher_etat(mesures, reglages);
  else if (!strcmp(commande, "i2c")) scanner_i2c();
  else if (!strcmp(commande, "sol")) calibrer_sol();
  else if (!strcmp(commande, "buzzer")) signalisation_tester_buzzer();
  else if (!strcmp(commande, "voyants")) signalisation_tester_voyants();
  else if (!strcmp(commande, "pompe")) {
    uint32_t s = argument ? strtoul(argument, nullptr, 10) : 0;
    if (pompe_tester(s)) Serial.printf("[test] pompe en marche %lu s\n", (unsigned long)s);
    else Serial.println("[test] refusé : pompe déjà en marche, ou durée hors de 1 à 120 s");
  } else if (!strcmp(commande, "ventilateur")) {
    if (argument && !strcmp(argument, "auto")) {
      ventilateur_forcer(MODE_AUTO, 0);
      Serial.println("[test] ventilateur en automatique");
    } else {
      uint32_t s = argument ? strtoul(argument, nullptr, 10) : 0;
      if (s == 0 || s > TEST_ACTIONNEUR_MAX_S) {
        Serial.println("[test] durée hors de 1 à 120 s");
      } else {
        ventilateur_forcer(MODE_FORCE_MARCHE, s);
        Serial.printf("[test] ventilateur en marche %lu s\n", (unsigned long)s);
      }
    }
  } else if (!strcmp(commande, "heure")) {
    if (argument) {
      Serial.println(horloge_regler_iso(argument) ? "[horloge] réglée"
                                                  : "[horloge] refusé : format AAAA-MM-JJTHH:MM:SSZ "
                                                    "attendu, ou DS3231 absente");
    }
    char heure[24];
    Serial.printf("[horloge] %s\n", horloge_iso(heure, sizeof(heure)) ? heure : "non réglée");
  } else if (!strcmp(commande, "releve")) {
    capteurs_lire(mesures);
    regulation_appliquer(mesures, reglages);
    reseau_ajouter_releve(mesures);
  } else if (!strcmp(commande, "config")) {
    reseau_demander_config();
    Serial.println("[config] demande au Pi au prochain tour");
  } else if (commande[0] != '\0') {
    Serial.printf("Commande inconnue : « %s ». Taper « aide ».\n", commande);
  }
}

}  // namespace

void commandes_traiter(Mesures &mesures, Reglages &reglages) {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      ligne[longueur] = '\0';
      longueur = 0;
      executer(ligne, mesures, reglages);
    } else if (longueur < sizeof(ligne) - 1) {
      ligne[longueur++] = c;
    }
  }
}
