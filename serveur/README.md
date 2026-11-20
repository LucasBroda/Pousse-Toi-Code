# Serveur Serre Autonome (Raspberry Pi Zero 2 W)

Base locale SQLite, réception des relevés de l'ESP32 et page web v0 (dernière mesure).
Python 3 et sa bibliothèque standard uniquement : rien à installer, aucune requête vers Internet.

Responsable : Valentin Delattre. Format des échanges : `00_context/04_contrat_esp32_pi.md` (v0).

## Fichiers

| Fichier | Rôle |
|---|---|
| `config.py` | Toutes les valeurs réglables : port, chemin de la base, seuils envoyés à l'ESP32 |
| `serveur.py` | Serveur HTTP sur le port 8000 |
| `releve.py` | Lecture et contrôle d'un relevé JSON |
| `base.py` | Table `releves`, lecture, copie hebdomadaire |
| `page/index.html` | Page web, actualisée toutes les minutes |
| `simuler_esp32.py` | Envoie de faux relevés pour tester sans la serre |
| `tests/` | Tests automatiques |

## Routes

| Route | Pour | Réponse |
|---|---|---|
| `POST /api/releves` | ESP32 (contrat) | `200 {"ok": true}`, ou `400` si JSON illisible ou horodatage invalide |
| `GET /api/config` | ESP32 (contrat) | Seuils et réglages d'arrosage (valeurs provisoires de D-30) ; `plant` = plant du bac A |
| `GET /` et `GET /textes.js` | Téléphone | Page web et ses textes |
| `GET /api/releves/dernier` | Page web | Dernier relevé et son ancienneté en secondes |
| `GET /api/releves?limite=N` | Page web | N derniers relevés, 288 par défaut (24 h), 2016 au plus |
| `GET /api/historique?periode=24h` ou `7j` | Page web | Points des courbes : chaque relevé sur 24 h, moyenne par heure sur 7 jours, fenêtre calée sur le dernier relevé |
| `GET /api/plants` / `POST /api/plants` | Page web | Plant de chaque bac ; `{"bac": "A", "plant": "radis"}` pour le changer |

Page web v1 (`page/index.html`) : état général et gestes à faire, voyants, humidité des deux bacs avec le seuil, choix du plant par bac, air et lumière, courbes sur 24 h ou 7 jours (SVG dessiné dans la page, sans bibliothèque), fiches de conseils. Tous les textes sont dans `page/textes.js`, provisoires (P-15, conseils de Lucas Manche).

Comportement :

- Un champ absent ou mal typé est enregistré à `NULL` et noté dans le journal du serveur ; le reste du relevé est gardé.
- Un relevé renvoyé deux fois par l'ESP32 (même `horodatage` et même `sequence`) n'est enregistré qu'une fois, avec la réponse `200` dans les deux cas.
- Une copie de la base est écrite dans `donnees/sauvegardes/` au démarrage puis tous les 7 jours. Les 4 dernières sont gardées.

## Essai sur un PC

```bash
python serveur.py
```

Dans un autre terminal :

```bash
python simuler_esp32.py --historique 288
```

Puis ouvrir http://localhost:8000.

Tests :

```bash
python -m unittest discover -s tests -v
```

## Installation sur le Raspberry Pi

1. Copier le dossier `serveur/` depuis le PC (remplacer `<utilisateur>` et `<nom-d-hote>`) :

   ```bash
   scp -r serveur <utilisateur>@<nom-d-hote>.local:~/serre-autonome/
   ```

2. Essai manuel sur le Pi, puis ouvrir `http://<nom-d-hote>.local:8000` depuis le téléphone :

   ```bash
   cd ~/serre-autonome/serveur && python3 serveur.py
   ```

3. Démarrage automatique :

   ```bash
   sed -i "s/UTILISATEUR/$USER/g" serre-serveur.service
   sudo cp serre-serveur.service /etc/systemd/system/
   sudo systemctl enable --now serre-serveur
   journalctl -u serre-serveur -f
   ```

4. Récupérer une copie de la base sur le PC (une fois par semaine, la carte microSD peut lâcher) :

   ```bash
   scp <utilisateur>@<nom-d-hote>.local:~/serre-autonome/serveur/donnees/sauvegardes/*.db .
   ```

## Points à surveiller

- **Heure du Pi.** Le Pi Zero 2 W n'a pas d'horloge sauvegardée : sans Internet, son heure est fausse après une coupure de courant. Seuls `recu_le` et la date des copies en dépendent. La page calcule l'ancienneté du relevé avec l'heure du téléphone et `horodatage` (DS3231), qui restent justes.
- **Réseau.** Point d'accès Wi-Fi créé par le Pi, proposé pour P-06 : voir `RESEAU.md`.
- **Seuils.** Les valeurs de `config.py` sont celles de D-30, provisoires. P-08 reste ouvert (Lucas Manche).
