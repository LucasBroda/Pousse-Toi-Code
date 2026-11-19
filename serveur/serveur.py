"""Serveur local de la Serre Autonome (Raspberry Pi Zero 2 W).

Reçoit les relevés de l'ESP32, les enregistre dans la base SQLite et sert la
page web. Bibliothèque standard de Python uniquement : rien à installer,
aucune requête vers Internet (contrat ESP32 <-> Pi, D-06).

Lancement : python3 serveur.py

Routes du contrat v0 (utilisées par l'ESP32) :
  POST /api/releves          enregistre un relevé, répond {"ok": true}
  GET  /api/config           seuils et réglages d'arrosage
Routes internes (utilisées par la page web) :
  GET  /                     page web
  GET  /api/releves/dernier  dernier relevé et son ancienneté
  GET  /api/releves?limite=N N derniers relevés, du plus récent au plus ancien
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

PAGE = Path(__file__).resolve().parent / "page" / "index.html"
TAILLE_MAX_CORPS = 16 * 1024            # un relevé fait environ 500 octets
FORMAT_UTC = "%Y-%m-%dT%H:%M:%SZ"


def maintenant_utc():
    return datetime.now(timezone.utc).strftime(FORMAT_UTC)


def age_en_secondes(horodatage_utc):
    instant = datetime.strptime(horodatage_utc, FORMAT_UTC).replace(tzinfo=timezone.utc)
    return int((datetime.now(timezone.utc) - instant).total_seconds())


class Gestionnaire(BaseHTTPRequestHandler):
    server_version = "SerreAutonome/0"

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

    # --- GET -------------------------------------------------------------
    def do_GET(self):
        url = urlsplit(self.path)
        if url.path in ("/", "/index.html"):
            self._envoyer(HTTPStatus.OK, PAGE.read_bytes(), "text/html; charset=utf-8")
        elif url.path == "/api/config":
            self._json(HTTPStatus.OK, config.CONFIG_ESP32)
        elif url.path == "/api/releves/dernier":
            with base.ouvrir(self.server.chemin_base) as connexion:
                releve = base.dernier(connexion)
            self._json(HTTPStatus.OK, {
                "releve": releve,
                "age_s": age_en_secondes(releve["recu_le"]) if releve else None,
                "retard_alerte_s": config.RETARD_ALERTE_S,
            })
        elif url.path == "/api/releves":
            self._historique(parse_qs(url.query))
        else:
            self._erreur(HTTPStatus.NOT_FOUND, f"route inconnue : {url.path}")

    def _historique(self, parametres):
        try:
            limite = int(parametres.get("limite", [config.LIMITE_HISTORIQUE_DEFAUT])[0])
        except ValueError:
            return self._erreur(HTTPStatus.BAD_REQUEST, "limite doit être un entier")
        limite = max(1, min(limite, config.LIMITE_HISTORIQUE_MAX))
        with base.ouvrir(self.server.chemin_base) as connexion:
            releves = base.historique(connexion, limite)
        self._json(HTTPStatus.OK, {"releves": releves})

    # --- POST ------------------------------------------------------------
    def do_POST(self):
        if urlsplit(self.path).path != "/api/releves":
            return self._erreur(HTTPStatus.NOT_FOUND, "route inconnue")
        try:
            longueur = int(self.headers.get("Content-Length", ""))
        except ValueError:
            return self._erreur(HTTPStatus.LENGTH_REQUIRED, "en-tête Content-Length manquant")
        if not 0 < longueur <= TAILLE_MAX_CORPS:
            return self._erreur(HTTPStatus.REQUEST_ENTITY_TOO_LARGE,
                                f"corps vide ou supérieur à {TAILLE_MAX_CORPS} octets")
        try:
            donnees = json.loads(self.rfile.read(longueur).decode("utf-8"))
            ligne, anomalies = convertir_releve(donnees, recu_le=maintenant_utc())
        except (UnicodeDecodeError, json.JSONDecodeError):
            journal.warning("Relevé refusé : JSON illisible")
            return self._erreur(HTTPStatus.BAD_REQUEST, "JSON illisible")
        except ReleveInvalide as erreur:
            journal.warning("Relevé refusé : %s", erreur)
            return self._erreur(HTTPStatus.BAD_REQUEST, str(erreur))

        for anomalie in anomalies:
            journal.warning("Relevé %s : %s", ligne["horodatage"], anomalie)
        with base.ouvrir(self.server.chemin_base) as connexion:
            nouveau = base.inserer(connexion, ligne)
        if not nouveau:
            journal.info("Relevé %s séquence %s déjà reçu, ignoré",
                         ligne["horodatage"], ligne["sequence"])
        # Réponse exacte du contrat v0, doublon ou non : l'ESP32 peut l'oublier.
        self._json(HTTPStatus.OK, {"ok": True})


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
