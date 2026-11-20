"""Tests du serveur Serre Autonome. Lancement : python -m unittest discover -s tests"""
import copy
import json
import sqlite3
import sys
import tempfile
import threading
import unittest
import urllib.error
import urllib.request
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import base      # noqa: E402
import config    # noqa: E402
import serveur   # noqa: E402

# Exemple exact du contrat v0 (04_contrat_esp32_pi.md).
EXEMPLE_CONTRAT = {
    "version_contrat": 0,
    "appareil": "serre-esp32",
    "horodatage": "2026-11-20T14:05:00Z",
    "sequence": 1234,
    "sol": [
        {"bac": "A", "brut": 2140, "humidite_pct": 46.5},
        {"bac": "B", "brut": 1980, "humidite_pct": 52.0},
    ],
    "air": {"temperature_c": 21.4, "humidite_pct": 58.2, "pression_hpa": 1013.2},
    "lumiere_lux": 3520,
    "ventilateur_actif": True,
    "arrosage": {"ml_depuis_dernier_releve": 0, "secondes_depuis_dernier_releve": 0},
    "voyants": {"eau": "ok", "lumiere": "alerte", "chaleur": "ok"},
    "duree_fonctionnement_s": 86400,
}


class TestServeur(unittest.TestCase):
    def setUp(self):
        self.dossier = tempfile.TemporaryDirectory()
        self.chemin_base = Path(self.dossier.name) / "serre.db"
        self.serveur = serveur.creer_serveur("127.0.0.1", 0, self.chemin_base)
        self.url = f"http://127.0.0.1:{self.serveur.server_address[1]}"
        self.fil = threading.Thread(target=self.serveur.serve_forever, daemon=True)
        self.fil.start()

    def tearDown(self):
        self.serveur.shutdown()
        self.serveur.server_close()
        self.dossier.cleanup()

    def requete(self, chemin, corps=None, brut=None):
        donnees = brut if brut is not None else (
            json.dumps(corps).encode("utf-8") if corps is not None else None)
        requete = urllib.request.Request(self.url + chemin, data=donnees,
                                         headers={"Content-Type": "application/json"})
        try:
            with urllib.request.urlopen(requete, timeout=5) as reponse:
                return reponse.status, reponse.read()
        except urllib.error.HTTPError as erreur:
            return erreur.code, erreur.read()

    def lignes(self):
        with base.ouvrir(self.chemin_base) as connexion:
            return [dict(l) for l in connexion.execute("SELECT * FROM releves ORDER BY id")]

    def test_exemple_du_contrat_enregistre(self):
        statut, corps = self.requete("/api/releves", EXEMPLE_CONTRAT)
        self.assertEqual(statut, 200)
        self.assertEqual(json.loads(corps), {"ok": True})
        [ligne] = self.lignes()
        attendu = {
            "horodatage": "2026-11-20T14:05:00Z", "sequence": 1234,
            "sol_a_brut": 2140, "sol_a_humidite_pct": 46.5,
            "sol_b_brut": 1980, "sol_b_humidite_pct": 52.0,
            "air_temperature_c": 21.4, "air_humidite_pct": 58.2, "air_pression_hpa": 1013.2,
            "lumiere_lux": 3520, "ventilateur_actif": 1, "arrosage_ml": 0,
            "voyant_eau": "ok", "voyant_lumiere": "alerte", "voyant_chaleur": "ok",
        }
        for cle, valeur in attendu.items():
            self.assertEqual(ligne[cle], valeur, cle)
        self.assertRegex(ligne["recu_le"], r"^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z$")

    def test_doublon_enregistre_une_seule_fois(self):
        for _ in range(3):
            statut, corps = self.requete("/api/releves", EXEMPLE_CONTRAT)
            self.assertEqual((statut, json.loads(corps)), (200, {"ok": True}))
        self.assertEqual(len(self.lignes()), 1)

    def test_capteurs_debranches_restent_null(self):
        releve = copy.deepcopy(EXEMPLE_CONTRAT)
        releve["sol"][1] = {"bac": "B", "brut": None, "humidite_pct": None}
        releve["air"] = {"temperature_c": None, "humidite_pct": None, "pression_hpa": None}
        releve["lumiere_lux"] = None
        self.assertEqual(self.requete("/api/releves", releve)[0], 200)
        [ligne] = self.lignes()
        for cle in ("sol_b_brut", "sol_b_humidite_pct", "air_temperature_c",
                    "air_humidite_pct", "air_pression_hpa", "lumiere_lux"):
            self.assertIsNone(ligne[cle], cle)
        self.assertEqual(ligne["sol_a_humidite_pct"], 46.5)

    def test_champ_mal_type_mis_a_null_sans_refuser(self):
        releve = copy.deepcopy(EXEMPLE_CONTRAT)
        releve["air"]["temperature_c"] = "21,4"
        releve["voyants"]["eau"] = "rouge"
        releve["sol"][0]["brut"] = 5000
        self.assertEqual(self.requete("/api/releves", releve)[0], 200)
        [ligne] = self.lignes()
        self.assertIsNone(ligne["air_temperature_c"])
        self.assertIsNone(ligne["voyant_eau"])
        self.assertIsNone(ligne["sol_a_brut"])
        self.assertEqual(ligne["air_humidite_pct"], 58.2)

    def test_releve_refuse_sans_horodatage_valide(self):
        for horodatage in (None, "20/11/2026 14:05", "2026-11-20T14:05:00", "2026-13-40T25:00:00Z"):
            releve = copy.deepcopy(EXEMPLE_CONTRAT)
            releve["horodatage"] = horodatage
            statut, corps = self.requete("/api/releves", releve)
            self.assertEqual(statut, 400, horodatage)
            self.assertFalse(json.loads(corps)["ok"])
        self.assertEqual(self.lignes(), [])

    def test_json_illisible_refuse(self):
        self.assertEqual(self.requete("/api/releves", brut=b"{pas du json")[0], 400)
        self.assertEqual(self.requete("/api/releves", brut=b"[1, 2]")[0], 400)

    def test_config_valeurs_d30(self):
        statut, corps = self.requete("/api/config")
        self.assertEqual(statut, 200)
        donnees = json.loads(corps)
        self.assertEqual(donnees["version_contrat"], 0)
        self.assertEqual(donnees["seuils"], {
            "sol_humidite_min_pct": 40, "air_temperature_min_c": 8,
            "air_temperature_max_c": 25, "air_humidite_max_pct": 80, "lumiere_min_lux": 2000})
        self.assertEqual(donnees["arrosage"],
                         {"debit_ml_min": 85, "dose_ml": 100, "delai_entre_doses_min": 60})

    def test_dernier_et_historique(self):
        statut, corps = self.requete("/api/releves/dernier")
        self.assertEqual(json.loads(corps)["releve"], None)
        for i, heure in enumerate(("14:05", "14:15", "14:10")):    # renvoi dans le désordre
            releve = copy.deepcopy(EXEMPLE_CONTRAT)
            releve["horodatage"] = f"2026-11-20T{heure}:00Z"
            releve["sequence"] = i
            self.requete("/api/releves", releve)
        dernier = json.loads(self.requete("/api/releves/dernier")[1])
        self.assertEqual(dernier["releve"]["horodatage"], "2026-11-20T14:15:00Z")
        self.assertLess(dernier["age_s"], 60)
        historique = json.loads(self.requete("/api/releves?limite=2")[1])["releves"]
        self.assertEqual([r["horodatage"] for r in historique],
                         ["2026-11-20T14:15:00Z", "2026-11-20T14:10:00Z"])
        self.assertEqual(self.requete("/api/releves?limite=abc")[0], 400)

    def test_page_web_servie_sans_ressource_externe(self):
        for chemin, attendu in (("/", "Serre Autonome"), ("/textes.js", "window.TEXTES")):
            statut, corps = self.requete(chemin)
            self.assertEqual(statut, 200, chemin)
            texte = corps.decode("utf-8")
            self.assertIn(attendu, texte)
            self.assertNotRegex(texte, r"""(src|href)\s*=\s*["']?(https?:)?//""",
                                "la page ne doit charger aucune ressource externe")
        self.assertEqual(self.requete("/inconnu.js")[0], 404)
        self.assertEqual(self.requete("/../serveur.py")[0], 404)
        self.assertEqual(self.requete("/config.py")[0], 404)

    def envoyer_serie(self, debut_heure, nombre, pas_min, **valeurs):
        """Envoie `nombre` relevés espacés de `pas_min` minutes à partir du 20/11 à debut_heure UTC."""
        for i in range(nombre):
            minutes = debut_heure * 60 + i * pas_min
            releve = copy.deepcopy(EXEMPLE_CONTRAT)
            releve["horodatage"] = (f"2026-11-{20 + minutes // 1440:02d}T"
                                    f"{minutes // 60 % 24:02d}:{minutes % 60:02d}:00Z")
            releve["sequence"] = i
            for cle, valeur in valeurs.items():
                releve["air"][cle] = valeur
            releve["arrosage"]["ml_depuis_dernier_releve"] = 10
            self.assertEqual(self.requete("/api/releves", releve)[0], 200)

    def test_historique_24h_part_du_dernier_releve(self):
        self.assertEqual(json.loads(self.requete("/api/historique")[1])["points"], [])
        self.envoyer_serie(0, 12 * 30, 5)          # 30 h de relevés, toutes les 5 min
        donnees = json.loads(self.requete("/api/historique?periode=24h")[1])
        points = donnees["points"]
        self.assertEqual(len(points), 24 * 12)      # fenêtre de 24 h, bornée par le dernier relevé
        self.assertEqual(points[-1]["horodatage"], "2026-11-21T05:55:00Z")
        self.assertLess(points[0]["horodatage"], points[-1]["horodatage"])
        self.assertEqual(points[0]["air_temperature_c"], 21.4)
        self.assertEqual(self.requete("/api/historique?periode=1an")[0], 400)

    def test_historique_7j_moyenne_par_heure(self):
        self.envoyer_serie(10, 12, 5, temperature_c=20.0)
        self.envoyer_serie(11, 12, 5, temperature_c=24.0)
        points = json.loads(self.requete("/api/historique?periode=7j")[1])["points"]
        self.assertEqual([p["horodatage"] for p in points], ["2026-11-20T10:00:00Z", "2026-11-20T11:00:00Z"])
        self.assertEqual([p["air_temperature_c"] for p in points], [20.0, 24.0])
        self.assertEqual(points[0]["arrosage_ml"], 120)       # somme sur l'heure
        self.assertEqual(points[0]["ventilateur_actif"], 1)

    def test_choix_du_plant_par_bac(self):
        donnees = json.loads(self.requete("/api/plants")[1])
        self.assertEqual(donnees["plants"], {"A": "salade", "B": "radis"})
        self.assertEqual(donnees["disponibles"], ["salade", "radis"])
        statut, corps = self.requete("/api/plants", {"bac": "A", "plant": "radis"})
        self.assertEqual(statut, 200)
        self.assertEqual(json.loads(corps)["plants"], {"A": "radis", "B": "radis"})
        self.assertEqual(json.loads(self.requete("/api/plants")[1])["plants"]["A"], "radis")
        # Contrat v0 : un seul plant envoyé à l'ESP32, celui du bac A.
        self.assertEqual(json.loads(self.requete("/api/config")[1])["plant"], "radis")
        for mauvais in ({"bac": "C", "plant": "radis"}, {"bac": "A", "plant": "tomate"}, [1]):
            self.assertEqual(self.requete("/api/plants", mauvais)[0], 400, mauvais)

    def test_route_inconnue(self):
        self.assertEqual(self.requete("/api/inconnue")[0], 404)


class TestSauvegarde(unittest.TestCase):
    def test_copie_lisible_et_rotation(self):
        with tempfile.TemporaryDirectory() as dossier:
            dossier = Path(dossier)
            chemin_base = dossier / "serre.db"
            base.initialiser(chemin_base)
            sauvegardes = dossier / "sauvegardes"
            self.assertTrue(base.sauvegarde_due(sauvegardes, 7))
            for nom in ("serre_2026-10-01.db", "serre_2026-10-08.db", "serre_2026-10-15.db"):
                sauvegardes.mkdir(exist_ok=True)
                (sauvegardes / nom).write_bytes(b"")
            copie = base.sauvegarder(chemin_base, sauvegardes, a_garder=2)
            self.assertEqual(sorted(p.name for p in sauvegardes.iterdir()),
                             ["serre_2026-10-15.db", copie.name])
            with sqlite3.connect(copie) as connexion:
                self.assertEqual(connexion.execute("SELECT COUNT(*) FROM releves").fetchone()[0], 0)
            connexion.close()
            self.assertFalse(base.sauvegarde_due(sauvegardes, 7))


if __name__ == "__main__":
    unittest.main()
