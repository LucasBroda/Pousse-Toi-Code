"""Conversion d'un relevé JSON de l'ESP32 en ligne de la table `releves`.

Le format est celui de 04_contrat_esp32_pi.md (v0). Règle de robustesse :
- un relevé sans horodatage valide est refusé (400), l'ESP32 le garde ;
- un champ absent ou mal typé est enregistré à NULL et signalé dans le
  journal du serveur, sans refuser le reste du relevé.
"""
import re
from datetime import datetime

import config

ETATS_VOYANT = ("ok", "alerte", "critique")
FORMAT_HORODATAGE = re.compile(r"^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z$")
BRUT_MIN, BRUT_MAX = 0, 4095            # plage de l'ADC 12 bits de l'ESP32


class ReleveInvalide(ValueError):
    """Relevé impossible à enregistrer."""


def _est_nombre(valeur):
    return isinstance(valeur, (int, float)) and not isinstance(valeur, bool)


class _Lecteur:
    """Lit les champs d'un relevé et note les anomalies au passage."""

    def __init__(self):
        self.anomalies = []

    def champ(self, objet, cle, nom):
        if cle not in objet:
            self.anomalies.append(f"{nom} : clé absente")
            return None
        return objet[cle]

    def objet(self, donnees, cle):
        valeur = self.champ(donnees, cle, cle)
        if valeur is None:
            return {}
        if not isinstance(valeur, dict):
            self.anomalies.append(f"{cle} : objet attendu, reçu {valeur!r}")
            return {}
        return valeur

    def entier(self, objet, cle, nom):
        valeur = self.champ(objet, cle, nom)
        if valeur is None:
            return None
        if not _est_nombre(valeur) or (isinstance(valeur, float) and not valeur.is_integer()):
            self.anomalies.append(f"{nom} : entier attendu, reçu {valeur!r}")
            return None
        return int(valeur)

    def decimal(self, objet, cle, nom):
        valeur = self.champ(objet, cle, nom)
        if valeur is None:
            return None
        if not _est_nombre(valeur):
            self.anomalies.append(f"{nom} : nombre attendu, reçu {valeur!r}")
            return None
        return float(valeur)

    def booleen(self, objet, cle, nom):
        valeur = self.champ(objet, cle, nom)
        if valeur is None:
            return None
        if not isinstance(valeur, bool):
            self.anomalies.append(f"{nom} : true ou false attendu, reçu {valeur!r}")
            return None
        return int(valeur)

    def voyant(self, objet, cle):
        nom = f"voyants.{cle}"
        valeur = self.champ(objet, cle, nom)
        if valeur is None:
            return None
        if valeur not in ETATS_VOYANT:
            self.anomalies.append(f"{nom} : \"ok\", \"alerte\" ou \"critique\" attendu, reçu {valeur!r}")
            return None
        return valeur


def _lire_sol(lecteur, donnees):
    """Renvoie {"A": (brut, humidite_pct), "B": (...)}."""
    sol = {"A": (None, None), "B": (None, None)}
    liste = lecteur.champ(donnees, "sol", "sol")
    if liste is None:
        return sol
    if not isinstance(liste, list):
        lecteur.anomalies.append(f"sol : liste attendue, reçu {liste!r}")
        return sol
    vus = set()
    for element in liste:
        if not isinstance(element, dict) or element.get("bac") not in sol:
            lecteur.anomalies.append(f"sol : élément ignoré {element!r} (bac \"A\" ou \"B\" attendu)")
            continue
        bac = element["bac"]
        if bac in vus:
            lecteur.anomalies.append(f"sol : bac {bac} en double, seul le premier est gardé")
            continue
        vus.add(bac)
        brut = lecteur.entier(element, "brut", f"sol[{bac}].brut")
        if brut is not None and not BRUT_MIN <= brut <= BRUT_MAX:
            lecteur.anomalies.append(f"sol[{bac}].brut : {brut} hors de {BRUT_MIN}-{BRUT_MAX}")
            brut = None
        humidite = lecteur.decimal(element, "humidite_pct", f"sol[{bac}].humidite_pct")
        sol[bac] = (brut, humidite)
    for bac in sol:
        if bac not in vus:
            lecteur.anomalies.append(f"sol : bac {bac} absent du relevé")
    return sol


def convertir_releve(donnees, recu_le):
    """Renvoie (ligne, anomalies). Lève ReleveInvalide si l'horodatage manque."""
    if not isinstance(donnees, dict):
        raise ReleveInvalide("le relevé doit être un objet JSON")

    horodatage = donnees.get("horodatage")
    if not isinstance(horodatage, str) or not FORMAT_HORODATAGE.match(horodatage):
        raise ReleveInvalide("horodatage absent ou hors format AAAA-MM-JJTHH:MM:SSZ")
    try:
        datetime.strptime(horodatage, "%Y-%m-%dT%H:%M:%SZ")
    except ValueError as erreur:
        raise ReleveInvalide(f"horodatage impossible : {horodatage}") from erreur

    lecteur = _Lecteur()
    version = donnees.get("version_contrat")
    if version != config.VERSION_CONTRAT:
        lecteur.anomalies.append(
            f"version_contrat : {version!r} reçu, le serveur attend {config.VERSION_CONTRAT}")

    sol = _lire_sol(lecteur, donnees)
    air = lecteur.objet(donnees, "air")
    arrosage = lecteur.objet(donnees, "arrosage")
    voyants = lecteur.objet(donnees, "voyants")

    ligne = {
        "horodatage": horodatage,
        "recu_le": recu_le,
        "sequence": lecteur.entier(donnees, "sequence", "sequence"),
        "sol_a_brut": sol["A"][0],
        "sol_a_humidite_pct": sol["A"][1],
        "sol_b_brut": sol["B"][0],
        "sol_b_humidite_pct": sol["B"][1],
        "air_temperature_c": lecteur.decimal(air, "temperature_c", "air.temperature_c"),
        "air_humidite_pct": lecteur.decimal(air, "humidite_pct", "air.humidite_pct"),
        "air_pression_hpa": lecteur.decimal(air, "pression_hpa", "air.pression_hpa"),
        "lumiere_lux": lecteur.entier(donnees, "lumiere_lux", "lumiere_lux"),
        "ventilateur_actif": lecteur.booleen(donnees, "ventilateur_actif", "ventilateur_actif"),
        "arrosage_ml": lecteur.entier(
            arrosage, "ml_depuis_dernier_releve", "arrosage.ml_depuis_dernier_releve"),
        "voyant_eau": lecteur.voyant(voyants, "eau"),
        "voyant_lumiere": lecteur.voyant(voyants, "lumiere"),
        "voyant_chaleur": lecteur.voyant(voyants, "chaleur"),
    }
    return ligne, lecteur.anomalies
