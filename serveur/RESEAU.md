# Réseau de la serre : point d'accès Wi-Fi créé par le Pi (P-06)

Proposition de Valentin Delattre du 19/11/2026. À appliquer après accord de l'équipe ; à tester avant le 25/11.

## Principe

Le Raspberry Pi crée son propre réseau Wi-Fi, `SerreAutonome`. L'ESP32 et le téléphone s'y connectent. Rien ne dépend du Wi-Fi de l'école, d'une box ou d'un téléphone.

```
ESP32 ──Wi-Fi──┐
               ├── Pi (point d'accès, 10.42.0.1) ── serveur :8000
Téléphone ─────┘
```

| Réglage | Valeur |
|---|---|
| Nom du réseau | `SerreAutonome` |
| Sécurité | WPA2 (mot de passe dans `secrets.h` de l'ESP32 et sur le Pi, jamais dans `00_context`) |
| Bande | 2,4 GHz (seule bande du Pi Zero 2 W et de l'ESP32) |
| Adresse du Pi | `10.42.0.1`, fixe |
| `PI_HOST` du firmware | `10.42.0.1` |
| Page web | `http://10.42.0.1:8000` |
| Adresses des autres appareils | Données automatiquement par le Pi (DHCP), en `10.42.0.x` |

## Pourquoi pas les autres options

| Option | Problème |
|---|---|
| Wi-Fi de l'école | N'existe ni chez la cliente ni chez un membre (P-14). Risque d'isolation entre appareils et d'identifiant personnel à écrire dans le firmware. Non testé |
| Partage de connexion d'un téléphone (configuration provisoire actuelle) | La serre perd son historique dès que le téléphone s'éloigne ou s'éteint |
| Box du logement | Mot de passe à changer dans l'ESP32 et le Pi à chaque lieu ; pas de box à la soutenance. Piste possible pour janvier-mars 2027 |

Inconvénient du point d'accès : le réseau `SerreAutonome` n'a pas Internet. La plupart des téléphones gardent leurs données mobiles pour Internet et passent par le Wi-Fi pour la page de la serre ; à vérifier sur Android et sur iPhone (test 2 plus bas).

## Avant de basculer (tant que le Pi a encore Internet)

Le Pi est aujourd'hui sur le partage de connexion de Lucas Broda. En SSH :

```bash
sudo apt update && sudo apt full-upgrade -y
dpkg -l dnsmasq-base | grep ^ii || sudo apt install -y dnsmasq-base
```

`dnsmasq-base` distribue les adresses aux appareils connectés. Sans lui, le point d'accès démarre mais personne ne reçoit d'adresse.

Copier aussi le dossier `serveur/` et installer le service (voir `README.md`) avant la bascule.

## Créer le point d'accès

Raspberry Pi OS récent (Bookworm et suivants) utilise NetworkManager. Remplacer `<mot-de-passe>` (8 caractères au moins) :

```bash
sudo nmcli connection add type wifi ifname wlan0 con-name serre-ap ssid SerreAutonome \
  802-11-wireless.mode ap 802-11-wireless.band bg \
  ipv4.method shared ipv4.addresses 10.42.0.1/24 \
  wifi-sec.key-mgmt wpa-psk wifi-sec.proto rsn wifi-sec.pairwise ccmp wifi-sec.group ccmp \
  wifi-sec.pmf disable wifi-sec.psk '<mot-de-passe>' \
  connection.autoconnect yes connection.autoconnect-priority 100
```

Puis basculer :

```bash
sudo nmcli connection up serre-ap
```

La session SSH se coupe à ce moment-là : le Pi quitte le partage de connexion. Se connecter depuis le PC au Wi-Fi `SerreAutonome`, puis :

```bash
ssh <utilisateur>@10.42.0.1
```

## Revenir en arrière

Le profil du partage de connexion reste enregistré sur le Pi. Pour y revenir (mises à jour, installation d'un paquet) :

```bash
sudo nmcli connection down serre-ap
```

NetworkManager se reconnecte alors au partage de connexion s'il est à portée. Au redémarrage suivant, le point d'accès repart (priorité 100). Si le point d'accès ne démarre pas, NetworkManager essaie les autres profils enregistrés.

## Tests de validation de P-06

| N° | Test | Résultat attendu |
|---|---|---|
| 1 | Téléphone connecté à `SerreAutonome`, ouvrir `http://10.42.0.1:8000` | Page de la serre affichée |
| 2 | Sur ce même téléphone, ouvrir un site Internet | Fonctionne par les données mobiles (noter Android ou iPhone, et le message affiché) |
| 3 | ESP32 avec `PI_HOST` = `10.42.0.1` | Relevé reçu en `200`, visible sur la page |
| 4 | Redémarrer le Pi | Point d'accès et serveur reviennent seuls en moins de 2 minutes |
| 5 | Débrancher le Pi 15 min puis le rebrancher | L'ESP32 continue d'arroser et de ventiler ; les relevés gardés en mémoire arrivent au retour du Pi |
| 6 | Couper toute la serre puis la rallumer | Tout repart sans intervention |

## Heure du Pi

Sans Internet, le Pi ne peut pas se mettre à l'heure, et il n'a pas d'horloge sauvegardée. Après une coupure, son heure est fausse. Conséquences :

- L'heure de mesure (`horodatage`) vient de la DS3231 de l'ESP32 : juste.
- La page calcule l'ancienneté du dernier relevé avec l'heure du téléphone et `horodatage` : juste.
- `recu_le` (heure de réception) et la date des copies de la base utilisent l'heure du Pi : peuvent être fausses après une coupure. Sans effet sur les plants ni sur la page.
