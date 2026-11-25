"""Serveur local de la Serre Autonome (Raspberry Pi Zero 2 W).

Reçoit les relevés de l'ESP32, les enregistre dans la base SQLite et sert la
page web. Bibliothèque standard de Python uniquement : rien à installer,
aucune requête vers Internet (contrat ESP32 <-> Pi, D-06).

Lancement : python3 serveur.py

Routes du contrat v0 (utilisées par l'ESP32) :
  POST /api/releves            enregistre un relevé, répond {"ok": true}
  GET  /api/config             seuils et réglages d'arrosage
Routes internes (utilisées par la page web) :
  GET  /                       page web ; GET /<fichier>.js|.css : fichiers de page/
  GET  /api/releves/dernier    dernier relevé et son ancienneté
  GET  /api/releves?limite=N   N derniers relevés, du plus récent au plus ancien
  GET  /api/historique?periode=24h|7j   points des courbes, du plus ancien au plus récent
  GET  /api/plants             plant de chaque bac et plants proposés
  POST /api/plants             {"bac": "A", "plant": "radis"} : change le plant d'un bac
"""
import json
import logging
import threading
from datetime import datetime, timezone
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlsplit

import base
import config
from releve import ReleveInvalide, convertir_releve

journal = logging.getLogger("serre")

DOSSIER_PAGE = Path(__file__).resolve().parent / "page"
TYPES_FICHIERS = {".html": "text/html; charset=utf-8",
                  ".js": "text/javascript; charset=utf-8",
                  ".css": "text/css; charset=utf-8"}
TAILLE_MAX_CORPS = 16 * 1024            # un relevé fait environ 500 octets
FORMAT_UTC = "%Y-%m-%dT%H:%M:%SZ"


def maintenant_utc():
    return datetime.now(timezone.utc).strftime(FORMAT_UTC)


def age_en_secondes(horodatage_utc):
    instant = datetime.strptime(horodatage_utc, FORMAT_UTC).replace(tzinfo=timezone.utc)
    return int((datetime.now(timezone.utc) - instant).total_seconds())


class CorpsInvalide(ValueError):
    """Corps de requête POST illisible."""

    def __init__(self, statut, message):
        super().__init__(message)
        self.statut = statut


class Gestionnaire(BaseHTTPRequestHandler):
    server_version = "SerreAutonome/1"

    # --- outils ----------------------------------------------------------
    def log_message(self, format, *args):
        journal.info("%s %s", self.address_string(), format % args)

    def _envoyer(self, statut, corps, type_contenu):
        self.send_response(statut)
        self.send_header("Content-Type", type_contenu)
        self.send_header("Content-Length", str(len(corps)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(corps)

    def _json(self, statut, donnees):
        corps = json.dumps(donnees, ensure_ascii=False).encode("utf-8")
        self._envoyer(statut, corps, "application/json; charset=utf-8")

    def _erreur(self, statut, message):
        self._json(statut, {"ok": False, "erreur": message})

    def _lire_json(self):
        try:
            longueur = int(self.headers.get("Content-Length", ""))
        except ValueError:
            raise CorpsInvalide(HTTPStatus.LENGTH_REQUIRED, "en-tête Content-Length manquant")
        if not 0 < longueur <= TAILLE_MAX_CORPS:
            raise CorpsInvalide(HTTPStatus.REQUEST_ENTITY_TOO_LARGE,
                                f"corps vide ou supérieur à {TAILLE_MAX_CORPS} octets")
        try:
            return json.loads(self.rfile.read(longueur).decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError):
            raise CorpsInvalide(HTTPStatus.BAD_REQUEST, "JSON illisible")

    def _base(self):
        return base.ouvrir(self.server.chemin_base)

    # --- GET -------------------------------------------------------------
    def do_GET(self):
        url = urlsplit(self.path)
        parametres = parse_qs(url.query)
        if url.path == "/":
            return self._fichier("index.html")
        if url.path == "/api/config":
            return self._json(HTTPStatus.OK, self._config_esp32())
        if url.path == "/api/releves/dernier":
            with self._base() as connexion:
                releve = base.dernier(connexion)
            return self._json(HTTPStatus.OK, {
                "releve": releve,
                "age_s": age_en_secondes(releve["recu_le"]) if releve else None,
                "retard_alerte_s": config.RETARD_ALERTE_S,
            })
        if url.path == "/api/releves":
            return self._releves(parametres)
        if url.path == "/api/historique":
            return self._historique(parametres)
        if url.path == "/api/plants":
            return self._plants()
        nom = url.path.lstrip("/")
        if "/" not in nom and Path(nom).suffix in TYPES_FICHIERS:
            return self._fichier(nom)
        self._erreur(HTTPStatus.NOT_FOUND, f"route inconnue : {url.path}")

    def _fichier(self, nom):
        chemin = DOSSIER_PAGE / nom
        if not chemin.is_file():
            return self._erreur(HTTPStatus.NOT_FOUND, f"fichier inconnu : {nom}")
        self._envoyer(HTTPStatus.OK, chemin.read_bytes(), TYPES_FICHIERS[chemin.suffix])

    def _config_esp32(self):
        with self._base() as connexion:
            plants = base.lire_plants(connexion, config.PLANTS_PAR_DEFAUT)
        donnees = json.loads(json.dumps(config.CONFIG_ESP32))   # copie profonde
        donnees["plant"] = plants["A"]   # contrat v0 : un seul plant ; v1 : un par bac
        return donnees

    def _releves(self, parametres):
        try:
            limite = int(parametres.get("limite", [config.LIMITE_HISTORIQUE_DEFAUT])[0])
        except ValueError:
            return self._erreur(HTTPStatus.BAD_REQUEST, "limite doit être un entier")
        limite = max(1, min(limite, config.LIMITE_HISTORIQUE_MAX))
        with self._base() as connexion:
            releves = base.historique(connexion, limite)
        self._json(HTTPStatus.OK, {"releves": releves})

    def _historique(self, parametres):
        periode = parametres.get("periode", ["24h"])[0]
        if periode not in config.PERIODES_HISTORIQUE:
            return self._erreur(HTTPStatus.BAD_REQUEST,
                                f"periode : {', '.join(config.PERIODES_HISTORIQUE)} attendu")
        reglage = config.PERIODES_HISTORIQUE[periode]
        with self._base() as connexion:
            points = base.historique_periode(connexion, reglage["heures"],
                                             reglage["regroupement_min"])
        self._json(HTTPStatus.OK, {"periode": periode, "points": points})

    def _plants(self):
        with self._base() as connexion:
            plants = base.lire_plants(connexion, config.PLANTS_PAR_DEFAUT)
        self._json(HTTPStatus.OK, {"plants": plants, "disponibles": list(config.PLANTS_DISPONIBLES)})

    # --- POST ------------------------------------------------------------
    def do_POST(self):
        chemin = urlsplit(self.path).path
        if chemin not in ("/api/releves", "/api/plants"):
            return self._erreur(HTTPStatus.NOT_FOUND, "route inconnue")
        try:
            donnees = self._lire_json()
        except CorpsInvalide as erreur:
            journal.warning("Requête %s refusée : %s", chemin, erreur)
            return self._erreur(erreur.statut, str(erreur))
        if chemin == "/api/releves":
            self._recevoir_releve(donnees)
        else:
            self._changer_plant(donnees)

    def _recevoir_releve(self, donnees):
        try:
            ligne, anomalies = convertir_releve(donnees, recu_le=maintenant_utc())
        except ReleveInvalide as erreur:
            journal.warning("Relevé refusé : %s", erreur)
            return self._erreur(HTTPStatus.BAD_REQUEST, str(erreur))
        for anomalie in anomalies:
            journal.warning("Relevé %s : %s", ligne["horodatage"], anomalie)
        with self._base() as connexion:
            nouveau = base.inserer(connexion, ligne)
        if not nouveau:
            journal.info("Relevé %s séquence %s déjà reçu, ignoré",
                         ligne["horodatage"], ligne["sequence"])
        # Réponse exacte du contrat v0, doublon ou non : l'ESP32 peut l'oublier.
        self._json(HTTPStatus.OK, {"ok": True})

    def _changer_plant(self, donnees):
        bac = donnees.get("bac") if isinstance(donnees, dict) else None
        plant = donnees.get("plant") if isinstance(donnees, dict) else None
        if bac not in config.PLANTS_PAR_DEFAUT:
            return self._erreur(HTTPStatus.BAD_REQUEST, "bac : \"A\" ou \"B\" attendu")
        if plant not in config.PLANTS_DISPONIBLES:
            return self._erreur(HTTPStatus.BAD_REQUEST,
                                f"plant : {', '.join(config.PLANTS_DISPONIBLES)} attendu")
        with self._base() as connexion:
            base.ecrire_plant(connexion, bac, plant)
            plants = base.lire_plants(connexion, config.PLANTS_PAR_DEFAUT)
        journal.info("Bac %s : plant changé en %s", bac, plant)
        self._json(HTTPStatus.OK, {"ok": True, "plants": plants})


def creer_serveur(hote, port, chemin_base):
    base.initialiser(chemin_base)
    serveur = ThreadingHTTPServer((hote, port), Gestionnaire)
    serveur.chemin_base = chemin_base
    return serveur


def boucle_sauvegarde(chemin_base, arret):
    """Copie hebdomadaire de la base (D-17), vérifiée toutes les heures."""
    while not arret.is_set():
        try:
            if base.sauvegarde_due(config.DOSSIER_SAUVEGARDES, config.INTERVALLE_SAUVEGARDE_JOURS):
                copie = base.sauvegarder(chemin_base, config.DOSSIER_SAUVEGARDES,
                                         config.NOMBRE_SAUVEGARDES_GARDEES)
                journal.info("Copie de la base écrite : %s", copie)
        except Exception:
            journal.exception("Échec de la copie hebdomadaire de la base")
        arret.wait(config.VERIFICATION_SAUVEGARDE_S)


def main():
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
    serveur = creer_serveur(config.HOTE, config.PORT, config.CHEMIN_BASE)
    arret = threading.Event()
    threading.Thread(target=boucle_sauvegarde, args=(config.CHEMIN_BASE, arret),
                     daemon=True).start()
    journal.info("Serveur Serre Autonome à l'écoute sur %s:%s, base %s",
                 config.HOTE, config.PORT, config.CHEMIN_BASE)
    try:
        serveur.serve_forever()
    except KeyboardInterrupt:
        journal.info("Arrêt demandé")
    finally:
        arret.set()
        serveur.server_close()


if __name__ == "__main__":
    main()
