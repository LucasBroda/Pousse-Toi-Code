#include "horloge.h"

#include <Arduino.h>
#include <Wire.h>
#include <stdio.h>

namespace {

RTC_DS3231 rtc;
bool presente = false;
bool reglee = false;

// Avant 2026, l'heure n'a manifestement jamais été réglée
constexpr uint16_t ANNEE_MIN_VALIDE = 2026;

}  // namespace

void horloge_demarrer() {
  presente = rtc.begin(&Wire);
  if (!presente) {
    Serial.println("[horloge] DS3231 ABSENTE : relevés non horodatés, pas d'envoi au Pi");
    return;
  }
  reglee = !rtc.lostPower() && rtc.now().year() >= ANNEE_MIN_VALIDE;
  if (!reglee) Serial.println("[horloge] heure non réglée : commande « heure AAAA-MM-JJTHH:MM:SSZ »");
}

bool horloge_presente() { return presente; }
bool horloge_valide() { return presente && reglee; }

DateTime horloge_maintenant() { return presente ? rtc.now() : DateTime(uint32_t(0)); }

bool horloge_iso(char *tampon, size_t taille) {
  if (!horloge_valide()) return false;
  DateTime t = rtc.now();
  snprintf(tampon, taille, "%04u-%02u-%02uT%02u:%02u:%02uZ", t.year(), t.month(), t.day(),
           t.hour(), t.minute(), t.second());
  return true;
}

bool horloge_regler_iso(const char *texte) {
  if (!presente) return false;
  unsigned a, mo, j, h, mi, s;
  if (sscanf(texte, "%4u-%2u-%2uT%2u:%2u:%2uZ", &a, &mo, &j, &h, &mi, &s) != 6) return false;
  DateTime t(a, mo, j, h, mi, s);
  if (!t.isValid() || t.year() < ANNEE_MIN_VALIDE) return false;
  rtc.adjust(t);
  reglee = true;
  return true;
}

long horloge_numero_jour() {
  if (horloge_valide()) return rtc.now().unixtime() / 86400L;
  // Sans heure valide : jours écoulés depuis le démarrage, décalés pour ne
  // jamais se confondre avec un vrai numéro de jour
  return -1L - long(millis() / 86400000UL);
}
