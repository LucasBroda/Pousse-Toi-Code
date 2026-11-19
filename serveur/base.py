"""Base locale SQLite de la Serre Autonome.

Table `releves` : schéma exact de 04_contrat_esp32_pi.md (v0).
Ajout côté serveur, sans effet sur le contrat : un index unique sur
(horodatage, sequence), pour qu'un relevé renvoyé deux fois par l'ESP32
(réponse du Pi perdue) ne soit enregistré qu'une fois.
"""
import sqlite3
import time
from contextlib import closing, contextmanager
from datetime import date

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
"""

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
