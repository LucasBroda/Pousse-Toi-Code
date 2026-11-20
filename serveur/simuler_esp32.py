"""Simulateur d'ESP32 : envoie au serveur des relevés au format du contrat v0.

Sert à tester le serveur et la page web sans la serre.

Exemples :
  python simuler_esp32.py                         un relevé maintenant
  python simuler_esp32.py --historique 288        24 h de relevés passés, toutes les 5 min
  python simuler_esp32.py --nombre 0 --intervalle 300   un relevé toutes les 5 min, sans fin
  python simuler_esp32.py --capteur-absent        bac B et BH1750 à null (capteurs débranchés)
  python simuler_esp32.py --hote 192.168.4.1      vers le Pi au lieu de ce PC
"""
import argparse
import json
import math
import random
import time
import urllib.error
import urllib.request
from datetime import datetime, timedelta, timezone

import config


# Humidité de la terre simulée : elle baisse lentement, plus vite le jour, et
# remonte d'un coup quand la serre arrose sous le seuil de 40 %.
SOL = {"A": 55.0, "B": 60.0}
SECHAGE_PAR_RELEVE = {"A": 0.22, "B": 0.16}     # points de % perdus toutes les 5 min


def faire_evoluer_sol(jour):
    arrose_ml = 0
    for bac in SOL:
        SOL[bac] -= SECHAGE_PAR_RELEVE[bac] * (0.5 + jour) + random.uniform(-0.05, 0.05)
        if SOL[bac] < 40:
            SOL[bac] += 18
            arrose_ml += 100
    return round(SOL["A"], 1), round(SOL["B"], 1), arrose_ml


def fabriquer_releve(instant, sequence, capteur_absent):
    heure = instant.hour + instant.minute / 60
    jour = max(0.0, math.sin((heure - 7) / 12 * math.pi))     # 0 la nuit, 1 vers 13h
    temperature = round(17 + 7 * jour + random.uniform(-0.3, 0.3), 1)
    lux = int(150 + 4500 * jour + random.uniform(-100, 100)) if jour > 0 else 0
    sol_a, sol_b, arrose_ml = faire_evoluer_sol(jour)

    def etat(valeur, alerte, critique):
        return "critique" if critique(valeur) else "alerte" if alerte(valeur) else "ok"

    return {
        "version_contrat": config.VERSION_CONTRAT,
        "appareil": "serre-esp32",
        "horodatage": instant.strftime("%Y-%m-%dT%H:%M:%SZ"),
        "sequence": sequence,
        "sol": [
            {"bac": "A", "brut": int(3000 - sol_a * 20), "humidite_pct": sol_a},
            {"bac": "B", "brut": None if capteur_absent else int(3000 - sol_b * 20),
             "humidite_pct": None if capteur_absent else sol_b},
        ],
        "air": {"temperature_c": temperature,
                "humidite_pct": round(68 - 12 * jour + random.uniform(-0.5, 0.5), 1),
                "pression_hpa": round(1013 + 3 * math.sin(sequence / 400) + random.uniform(-0.2, 0.2), 1)},
        "lumiere_lux": None if capteur_absent else lux,
        "ventilateur_actif": temperature > 25,
        "arrosage": {"ml_depuis_dernier_releve": arrose_ml,
                     "secondes_depuis_dernier_releve": round(arrose_ml / 85 * 60)},
        "voyants": {
            "eau": etat(min(sol_a, sol_b), lambda v: v < 40, lambda v: v < 25),
            "lumiere": "ok" if capteur_absent or not 8 <= heure <= 18 or lux >= 2000 else "alerte",
            "chaleur": etat(temperature, lambda v: v > 25, lambda v: v > 30),
        },
        "duree_fonctionnement_s": sequence * config.CADENCE_RELEVE_S,
    }


def envoyer(url, releve):
    requete = urllib.request.Request(url, data=json.dumps(releve).encode("utf-8"),
                                     headers={"Content-Type": "application/json"}, method="POST")
    try:
        with urllib.request.urlopen(requete, timeout=5) as reponse:
            return reponse.status, reponse.read().decode("utf-8")
    except urllib.error.HTTPError as erreur:
        return erreur.code, erreur.read().decode("utf-8")


def main():
    arguments = argparse.ArgumentParser(description=__doc__,
                                        formatter_class=argparse.RawDescriptionHelpFormatter)
    arguments.add_argument("--hote", default="127.0.0.1")
    arguments.add_argument("--port", type=int, default=config.PORT)
    arguments.add_argument("--historique", type=int, default=0,
                           help="envoie N relevés passés espacés de 5 min, puis s'arrête")
    arguments.add_argument("--nombre", type=int, default=1, help="0 = sans fin")
    arguments.add_argument("--intervalle", type=float, default=config.CADENCE_RELEVE_S)
    arguments.add_argument("--capteur-absent", action="store_true")
    options = arguments.parse_args()

    url = f"http://{options.hote}:{options.port}/api/releves"
    maintenant = datetime.now(timezone.utc).replace(microsecond=0)

    if options.historique:
        for i in range(options.historique):
            instant = maintenant - timedelta(seconds=config.CADENCE_RELEVE_S * (options.historique - 1 - i))
            statut, _ = envoyer(url, fabriquer_releve(instant, i, options.capteur_absent))
            if statut != 200:
                print(f"Relevé {i} refusé : HTTP {statut}")
        print(f"{options.historique} relevés passés envoyés à {url}")
        return

    sequence = 0
    while options.nombre == 0 or sequence < options.nombre:
        releve = fabriquer_releve(datetime.now(timezone.utc).replace(microsecond=0),
                                  sequence, options.capteur_absent)
        statut, corps = envoyer(url, releve)
        print(f"{releve['horodatage']} séquence {sequence} -> HTTP {statut} {corps}")
        sequence += 1
        if options.nombre == 0 or sequence < options.nombre:
            time.sleep(options.intervalle)


if __name__ == "__main__":
    main()
