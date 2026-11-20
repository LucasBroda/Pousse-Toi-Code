"""Configuration du serveur Serre Autonome (Raspberry Pi Zero 2 W).

Toutes les valeurs réglables du serveur sont ici et nulle part ailleurs
(voir 07_conventions.md). Aucun secret dans ce fichier.
"""
from pathlib import Path

DOSSIER = Path(__file__).resolve().parent

# --- Réseau -------------------------------------------------------------
# Port 8000 : PROPOSÉ dans le contrat ESP32 <-> Pi v0.
HOTE = "0.0.0.0"   # écoute sur toutes les interfaces du réseau local
PORT = 8000

# --- Base de données ----------------------------------------------------
# SQLite, un seul fichier serre.db (contrat v0), copie hebdomadaire (D-17).
CHEMIN_BASE = DOSSIER / "donnees" / "serre.db"
DOSSIER_SAUVEGARDES = DOSSIER / "donnees" / "sauvegardes"
INTERVALLE_SAUVEGARDE_JOURS = 7
NOMBRE_SAUVEGARDES_GARDEES = 4          # environ un mois d'historique de copies
VERIFICATION_SAUVEGARDE_S = 3600        # le serveur vérifie toutes les heures

# --- Contrat ------------------------------------------------------------
VERSION_CONTRAT = 0

# Cadence d'envoi de l'ESP32 : 5 minutes (contrat v0). La page web signale
# un retard au-delà de trois relevés manqués.
CADENCE_RELEVE_S = 300
RETARD_ALERTE_S = 3 * CADENCE_RELEVE_S

# Nombre maximal de relevés renvoyés par GET /api/releves (288 = 24 h).
LIMITE_HISTORIQUE_DEFAUT = 288
LIMITE_HISTORIQUE_MAX = 2016            # 7 jours

# Périodes des courbes de la page web : durée en heures et regroupement des
# relevés (0 = chaque relevé, 60 = moyenne par heure).
PERIODES_HISTORIQUE = {
    "24h": {"heures": 24, "regroupement_min": 0},
    "7j": {"heures": 24 * 7, "regroupement_min": 60},
}

# --- Plants -------------------------------------------------------------
# Cultures proposées dans l'application (D-31) et plant de départ de chaque
# bac (D-26 : A salade, B radis). Le choix fait sur la page est gardé en base.
PLANTS_DISPONIBLES = ("salade", "radis")
PLANTS_PAR_DEFAUT = {"A": "salade", "B": "radis"}

# --- Configuration envoyée à l'ESP32 (GET /api/config) -------------------
# Valeurs PROVISOIRES de D-30 (19/11/2026), à confirmer après calibration du
# sol, mesure de lumière (P-09) et réception de la pompe. P-08 reste ouvert
# pour les valeurs définitives (responsable : Lucas Manche).
CONFIG_ESP32 = {
    "version_contrat": VERSION_CONTRAT,
    "plant": "salade",                  # remplacé par le plant du bac A (contrat v0 : un seul plant)
    "seuils": {
        "sol_humidite_min_pct": 40,
        "air_temperature_min_c": 8,
        "air_temperature_max_c": 25,
        "air_humidite_max_pct": 80,
        "lumiere_min_lux": 2000,
    },
    "arrosage": {
        "debit_ml_min": 85,             # théorique, à remplacer par le débit mesuré
        "dose_ml": 100,
        "delai_entre_doses_min": 60,
    },
}
