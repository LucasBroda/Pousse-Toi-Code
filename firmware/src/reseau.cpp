#include "reseau.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFi.h>
#include <math.h>
#include <string.h>

#include "config.h"
#include "horloge.h"
#include "regulation.h"

// Identifiants Wi-Fi : secrets.h, ignoré par Git (copier secrets.example.h)
#if __has_include("secrets.h")
#include "secrets.h"
#define WIFI_CONFIGURE 1
#else
#define WIFI_CONFIGURE 0
#define WIFI_SSID ""
#define WIFI_MOTDEPASSE ""
#endif

namespace {

Preferences memoire;

// File circulaire des relevés en attente (12 = 1 heure, contrat v0)
String file_releves[FILE_RELEVES_MAX];
uint8_t file_debut = 0;
uint8_t file_taille = 0;
uint32_t sequence = 0;  // remis à 0 au redémarrage (contrat v0)

uint32_t dernier_essai_wifi_ms = 0;
bool config_deja_demandee = false;
uint32_t derniere_config_ms = 0;

String url(const char *chemin) {
  return String("http://") + PI_HOST + ":" + PI_PORT + chemin;
}

// Une décimale pour température, humidité et pression (contrat v0)
void ecrire_decimal(JsonVariant cible, float valeur) {
  if (isnan(valeur)) cible.set(nullptr);
  else cible.set(serialized(String(valeur, 1)));
}

void ecrire_entier(JsonVariant cible, long valeur) {
  if (valeur == VALEUR_ABSENTE) cible.set(nullptr);
  else cible.set(valeur);
}

void reglages_par_defaut(Reglages &r) {
  strlcpy(r.plant, "salade", sizeof(r.plant));
  r.sol_humidite_min_pct = DEFAUT_SOL_HUMIDITE_MIN_PCT;
  r.air_temperature_min_c = DEFAUT_AIR_TEMPERATURE_MIN_C;
  r.air_temperature_max_c = DEFAUT_AIR_TEMPERATURE_MAX_C;
  r.air_humidite_max_pct = DEFAUT_AIR_HUMIDITE_MAX_PCT;
  r.lumiere_min_lux = DEFAUT_LUMIERE_MIN_LUX;
  r.debit_ml_min = DEFAUT_DEBIT_ML_MIN;
  r.dose_ml = DEFAUT_DOSE_ML;
  r.delai_entre_doses_min = DEFAUT_DELAI_ENTRE_DOSES_MIN;
}

// Une valeur null ou absente garde la valeur actuelle
void lire_nombre(JsonVariantConst source, float &cible) {
  if (source.is<float>() || source.is<int>()) cible = source.as<float>();
}

void demarrer_wifi() {
  if (!WIFI_CONFIGURE) {
    Serial.println("[wifi] secrets.h absent : Wi-Fi désactivé, fonctions vitales seules");
    return;
  }
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_MOTDEPASSE);
  dernier_essai_wifi_ms = millis();
  Serial.printf("[wifi] connexion à « %s » lancée\n", WIFI_SSID);
}

void surveiller_wifi() {
  static bool etait_connecte = false;
  bool connecte = wifi_connecte();
  if (connecte != etait_connecte) {
    if (connecte) Serial.printf("[wifi] connecté, adresse %s\n", WiFi.localIP().toString().c_str());
    else Serial.println("[wifi] déconnecté");
    etait_connecte = connecte;
  }
  if (!connecte && WIFI_CONFIGURE && millis() - dernier_essai_wifi_ms >= WIFI_NOUVEL_ESSAI_MS) {
    dernier_essai_wifi_ms = millis();
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_MOTDEPASSE);
  }
}

// Envoie les relevés dans l'ordre ; s'arrête au premier échec pour
// renvoyer le même relevé au tour suivant (contrat v0, Cadence)
void envoyer_file() {
  while (file_taille > 0 && wifi_connecte()) {
    HTTPClient http;
    http.setTimeout(HTTP_DELAI_MAX_MS);
    http.setConnectTimeout(HTTP_DELAI_MAX_MS);
    if (!http.begin(url("/api/releves"))) return;
    http.addHeader("Content-Type", "application/json");
    int code = http.POST(file_releves[file_debut]);
    http.end();
    if (code != 200) {
      Serial.printf("[pi] relevé non accepté (code %d), %u en attente\n", code, file_taille);
      return;
    }
    file_releves[file_debut] = String();
    file_debut = (file_debut + 1) % FILE_RELEVES_MAX;
    file_taille--;
    Serial.printf("[pi] relevé envoyé, %u en attente\n", file_taille);
  }
}

void enregistrer_reglages(const Reglages &r) {
  memoire.putBytes("reglages", &r, sizeof(r));
}

bool demander_config(Reglages &reglages) {
  HTTPClient http;
  http.setTimeout(HTTP_DELAI_MAX_MS);
  http.setConnectTimeout(HTTP_DELAI_MAX_MS);
  if (!http.begin(url("/api/config"))) return false;
  int code = http.GET();
  if (code != 200) {
    http.end();
    Serial.printf("[pi] configuration non reçue (code %d)\n", code);
    return false;
  }
  JsonDocument doc;
  DeserializationError erreur = deserializeJson(doc, http.getString());
  http.end();
  if (erreur) {
    Serial.printf("[pi] configuration illisible : %s\n", erreur.c_str());
    return false;
  }
  if (doc["version_contrat"] != VERSION_CONTRAT) {
    Serial.printf("[pi] version de contrat %d reçue, %d attendue : configuration ignorée\n",
                  doc["version_contrat"].as<int>(), VERSION_CONTRAT);
    return false;
  }
  Reglages r = reglages;
  if (doc["plant"].is<const char *>()) strlcpy(r.plant, doc["plant"], sizeof(r.plant));
  JsonObjectConst seuils = doc["seuils"];
  lire_nombre(seuils["sol_humidite_min_pct"], r.sol_humidite_min_pct);
  lire_nombre(seuils["air_temperature_min_c"], r.air_temperature_min_c);
  lire_nombre(seuils["air_temperature_max_c"], r.air_temperature_max_c);
  lire_nombre(seuils["air_humidite_max_pct"], r.air_humidite_max_pct);
  lire_nombre(seuils["lumiere_min_lux"], r.lumiere_min_lux);
  JsonObjectConst arrosage = doc["arrosage"];
  lire_nombre(arrosage["debit_ml_min"], r.debit_ml_min);
  lire_nombre(arrosage["dose_ml"], r.dose_ml);
  lire_nombre(arrosage["delai_entre_doses_min"], r.delai_entre_doses_min);

  if (memcmp(&r, &reglages, sizeof(r)) != 0) {
    reglages = r;
    enregistrer_reglages(reglages);
    Serial.println("[pi] nouvelle configuration reçue et enregistrée");
    reglages_afficher(reglages);
  }
  return true;
}

}  // namespace

void reseau_demarrer(Reglages &reglages) {
  memoire.begin("reseau", false);
  reglages_par_defaut(reglages);
  // Dernière configuration reçue du Pi, réutilisée après un redémarrage
  if (memoire.getBytesLength("reglages") == sizeof(Reglages)) {
    memoire.getBytes("reglages", &reglages, sizeof(Reglages));
    Serial.println("[config] réglages enregistrés rechargés");
  } else {
    Serial.println("[config] réglages par défaut (D-30, provisoires)");
  }
  demarrer_wifi();
}

void reseau_tache(Reglages &reglages) {
  surveiller_wifi();
  if (!wifi_connecte()) return;
  bool config_due = !config_deja_demandee || millis() - derniere_config_ms >= PERIODE_CONFIG_MS;
  if (config_due) {
    config_deja_demandee = true;
    derniere_config_ms = millis();
    demander_config(reglages);
  }
  envoyer_file();
}

bool reseau_ajouter_releve(const Mesures &m) {
  char horodatage[24];
  if (!horloge_iso(horodatage, sizeof(horodatage))) {
    Serial.println("[releve] horloge non réglée : relevé non envoyé");
    return false;
  }

  JsonDocument doc;
  doc["version_contrat"] = VERSION_CONTRAT;
  doc["appareil"] = NOM_APPAREIL;
  doc["horodatage"] = horodatage;
  doc["sequence"] = ++sequence;

  JsonArray sol = doc["sol"].to<JsonArray>();
  const char *noms_bacs[NB_BACS] = {"A", "B"};
  for (uint8_t b = 0; b < NB_BACS; b++) {
    JsonObject bac = sol.add<JsonObject>();
    bac["bac"] = noms_bacs[b];
    ecrire_entier(bac["brut"], m.sol_brut[b]);
    ecrire_decimal(bac["humidite_pct"], m.sol_humidite_pct[b]);
  }

  JsonObject air = doc["air"].to<JsonObject>();
  ecrire_decimal(air["temperature_c"], m.air_temperature_c);
  ecrire_decimal(air["humidite_pct"], m.air_humidite_pct);
  ecrire_decimal(air["pression_hpa"], m.air_pression_hpa);

  ecrire_entier(doc["lumiere_lux"], m.lumiere_lux);
  doc["ventilateur_actif"] = ventilateur_actif();

  uint32_t ml, secondes;
  arrosage_prelever_compteurs(ml, secondes);
  JsonObject arrosage = doc["arrosage"].to<JsonObject>();
  arrosage["ml_depuis_dernier_releve"] = ml;
  arrosage["secondes_depuis_dernier_releve"] = secondes;

  JsonObject voyants = doc["voyants"].to<JsonObject>();
  voyants["eau"] = texte_etat_voyant(etat_voyant(VOYANT_EAU));
  voyants["lumiere"] = texte_etat_voyant(etat_voyant(VOYANT_LUMIERE));
  voyants["chaleur"] = texte_etat_voyant(etat_voyant(VOYANT_CHALEUR));

  doc["duree_fonctionnement_s"] = millis() / 1000;

  // File pleine : le plus ancien relevé est perdu (contrat v0)
  if (file_taille == FILE_RELEVES_MAX) {
    file_debut = (file_debut + 1) % FILE_RELEVES_MAX;
    file_taille--;
    Serial.println("[releve] file pleine : relevé le plus ancien perdu");
  }
  uint8_t position = (file_debut + file_taille) % FILE_RELEVES_MAX;
  file_releves[position] = String();
  serializeJson(doc, file_releves[position]);
  file_taille++;
  Serial.printf("[releve] n°%lu : %s\n", (unsigned long)sequence, file_releves[position].c_str());
  return true;
}

bool wifi_connecte() { return WiFi.status() == WL_CONNECTED; }
uint8_t releves_en_attente() { return file_taille; }
uint32_t releves_sequence() { return sequence; }
void reseau_demander_config() { config_deja_demandee = false; }

void reglages_afficher(const Reglages &r) {
  Serial.printf("  plant %s | sol min %.0f %% | air %.0f à %.0f °C, humidité max %.0f %% | "
                "lumière min %.0f lux\n",
                r.plant, r.sol_humidite_min_pct, r.air_temperature_min_c, r.air_temperature_max_c,
                r.air_humidite_max_pct, r.lumiere_min_lux);
  Serial.printf("  arrosage : débit %.0f ml/min, dose %.0f ml, délai %.0f min, max %lu ml/jour\n",
                r.debit_ml_min, r.dose_ml, r.delai_entre_doses_min,
                (unsigned long)ARROSAGE_MAX_ML_PAR_JOUR);
}
