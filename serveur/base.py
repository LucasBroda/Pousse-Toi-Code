"""Base locale SQLite de la Serre Autonome.

Table `releves` : schéma exact de 04_contrat_esp32_pi.md (v0).
Ajout côté serveur, sans effet sur le contrat : un index unique sur
(horodatage, sequence), pour qu'un relevé renvoyé deux fois par l'ESP32
(réponse du Pi perdue) ne soit enregistré qu'une fois.
"""
import sqlite3
import time
from contextlib import closing, contextmanager
from datetime import date, datetime, timedelta

SCHEMA = """
CREATE TABLE IF NOT EXISTS releves (
  id INTEGER PRIMARY KEY,
  horodatage TEXT NOT NULL,         -- heure de l'ESP32, UTC
  recu_le TEXT NOT NULL,            -- heure de réception par le Pi, UTC
  sequence INTEGER,
  sol_a_brut INTEGER, sol_a_humidite_pct REAL,
  sol_b_brut INTEGER, sol_b_humidite_pct REAL,
  air_temperature_c REAL, air_humidite_pct REAL, air_pression_hpa REAL,
  lumiere_lux INTEGER,
  ventilateur_actif INTEGER,        -- 0 ou 1
  arrosage_ml INTEGER,
  voyant_eau TEXT, voyant_lumiere TEXT, voyant_chaleur TEXT
);
CREATE UNIQUE INDEX IF NOT EXISTS releves_sans_doublon ON releves (horodatage, sequence);
CREATE INDEX IF NOT EXISTS releves_par_horodatage ON releves (horodatage);
-- Réglages faits depuis la page web (plant de chaque bac). Hors contrat.
CREATE TABLE IF NOT EXISTS reglages (
  cle TEXT PRIMARY KEY,
  valeur TEXT NOT NULL
);
"""

FORMAT_UTC = "%Y-%m-%dT%H:%M:%SZ"

COLONNES = (
    "horodatage", "recu_le", "sequence",
    "sol_a_brut", "sol_a_humidite_pct", "sol_b_brut", "sol_b_humidite_pct",
    "air_temperature_c", "air_humidite_pct", "air_pression_hpa",
    "lumiere_lux", "ventilateur_actif", "arrosage_ml",
    "voyant_eau", "voyant_lumiere", "voyant_chaleur",
)


@contextmanager
def ouvrir(chemin_base):
    """Connexion courte : validée puis fermée à la sortie du bloc."""
    connexion = sqlite3.connect(chemin_base, timeout=10)
    connexion.row_factory = sqlite3.Row
    try:
        yield connexion
        connexion.commit()
    finally:
        connexion.close()


def initialiser(chemin_base):
    chemin_base.parent.mkdir(parents=True, exist_ok=True)
    with ouvrir(chemin_base) as connexion:
        connexion.executescript(SCHEMA)


def inserer(connexion, ligne):
    """Enregistre un relevé. Renvoie False si c'était un doublon."""
    requete = (f"INSERT OR IGNORE INTO releves ({', '.join(COLONNES)}) "
               f"VALUES ({', '.join('?' * len(COLONNES))})")
    curseur = connexion.execute(requete, [ligne[colonne] for colonne in COLONNES])
    return curseur.rowcount == 1


def dernier(connexion):
    ligne = connexion.execute(
        "SELECT * FROM releves ORDER BY horodatage DESC, id DESC LIMIT 1").fetchone()
    return dict(ligne) if ligne else None


def historique(connexion, limite):
    """Les `limite` relevés les plus récents, du plus récent au plus ancien."""
    lignes = connexion.execute(
        "SELECT * FROM releves ORDER BY horodatage DESC, id DESC LIMIT ?", (limite,))
    return [dict(ligne) for ligne in lignes]


def nombre(connexion):
    return connexion.execute("SELECT COUNT(*) FROM releves").fetchone()[0]


# Colonnes des courbes : moyenne sur le regroupement, sauf l'arrosage (somme)
# et le ventilateur (1 s'il a tourné au moins une fois).
_COURBES = """
  AVG(sol_a_humidite_pct) AS sol_a_humidite_pct,
  AVG(sol_b_humidite_pct) AS sol_b_humidite_pct,
  AVG(air_temperature_c) AS air_temperature_c,
  AVG(air_humidite_pct) AS air_humidite_pct,
  AVG(lumiere_lux) AS lumiere_lux,
  SUM(arrosage_ml) AS arrosage_ml,
  MAX(ventilateur_actif) AS ventilateur_actif
"""


def historique_periode(connexion, heures, regroupement_min):
    """Relevés des `heures` qui précèdent le DERNIER relevé, du plus ancien au
    plus récent. La fenêtre part de l'heure de la DS3231 (horodatage), pas de
    l'horloge du Pi, qui peut être fausse sans Internet.
    regroupement_min = 0 : un point par relevé ; 60 : moyenne par heure."""
    dernier_horodatage = connexion.execute("SELECT MAX(horodatage) FROM releves").fetchone()[0]
    if dernier_horodatage is None:
        return []
    fin = datetime.strptime(dernier_horodatage, FORMAT_UTC)
    debut = (fin - timedelta(hours=heures)).strftime(FORMAT_UTC)
    if regroupement_min == 60:
        cle = "substr(horodatage, 1, 13) || ':00:00Z'"
    elif regroupement_min == 0:
        cle = "horodatage"
    else:
        raise ValueError("regroupement_min doit valoir 0 ou 60")
    lignes = connexion.execute(
        f"SELECT {cle} AS horodatage, {_COURBES} FROM releves "
        f"WHERE horodatage > ? GROUP BY {cle} ORDER BY {cle}", (debut,))
    return [dict(ligne) for ligne in lignes]


def lire_plants(connexion, par_defaut):
    """Plant de chaque bac : {"A": "salade", "B": "radis"}."""
    plants = dict(par_defaut)
    for ligne in connexion.execute("SELECT cle, valeur FROM reglages WHERE cle LIKE 'plant_bac_%'"):
        plants[ligne["cle"].removeprefix("plant_bac_").upper()] = ligne["valeur"]
    return plants


def ecrire_plant(connexion, bac, plant):
    connexion.execute(
        "INSERT INTO reglages (cle, valeur) VALUES (?, ?) "
        "ON CONFLICT(cle) DO UPDATE SET valeur = excluded.valeur",
        (f"plant_bac_{bac.lower()}", plant))


def sauvegarde_due(dossier, intervalle_jours):
    copies = sorted(dossier.glob("serre_*.db"))
    if not copies:
        return True
    age_s = time.time() - copies[-1].stat().st_mtime
    return age_s >= intervalle_jours * 86400


def sauvegarder(chemin_base, dossier, a_garder):
    """Copie cohérente de la base (API de sauvegarde SQLite), puis ne garde
    que les `a_garder` copies les plus récentes. Renvoie le chemin créé."""
    dossier.mkdir(parents=True, exist_ok=True)
    cible = dossier / f"serre_{date.today():%Y-%m-%d}.db"
    with ouvrir(chemin_base) as source, closing(sqlite3.connect(cible)) as copie:
        source.backup(copie)
    copies = sorted(dossier.glob("serre_*.db"))
    for ancienne in copies[:-a_garder]:
        ancienne.unlink()
    return cible
