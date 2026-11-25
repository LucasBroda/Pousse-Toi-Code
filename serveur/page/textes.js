// Textes de la page web de la Serre Autonome.
// PROVISOIRE : à remplacer par le document « Conseils de la page web » de
// Lucas Manche (19/11/2026). Tutoiement ou vouvoiement et sens exact des
// voyants à trancher (P-15) ; vouvoiement en attendant. Tous les textes de la
// page sont ici, pour les changer en une seule fois.
"use strict";

window.TEXTES = {
  etatGeneral: {
    ok: "Tout va bien dans la serre.",
    gestes: n => n === 1 ? "Un geste à faire." : n + " gestes à faire.",
    aucunReleve: "Aucun relevé reçu pour l'instant. La serre est-elle branchée ?",
    retard: duree => "Aucun nouveau relevé depuis " + duree
      + ". Vérifiez que la serre est branchée et que le Wi-Fi fonctionne.",
    serveurMuet: "Le serveur de la serre ne répond pas. Nouvel essai dans une minute.",
  },

  // Un geste par voyant et par niveau (D-04, D-05). Les seuils viennent de
  // /api/config (D-30, provisoires) : ils ne sont jamais écrits ici.
  gestes: {
    eau: {
      nom: "Eau",
      alerte: s => "La terre d'un bac est sous " + s.sol_humidite_min_pct
        + " % d'humidité. La serre arrose seule : vérifiez seulement que le réservoir contient de l'eau.",
      critique: () => "La serre a atteint sa limite d'arrosage du jour et la terre reste sèche. "
        + "Remplissez le réservoir et vérifiez que le tuyau arrive bien dans le bac.",
    },
    lumiere: {
      nom: "Lumière",
      alerte: s => "La serre reçoit moins de " + s.lumiere_min_lux.toLocaleString("fr-FR")
        + " lux en journée. Rapprochez-la d'une fenêtre ou dégagez la fenêtre.",
      critique: () => "La lumière manque depuis longtemps. Placez la serre devant la fenêtre la plus claire du logement.",
    },
    chaleur: {
      nom: "Chaleur",
      alerte: s => "La température sort de la plage " + s.air_temperature_min_c + " à "
        + s.air_temperature_max_c + " °C. Éloignez la serre d'un radiateur ou du soleil direct, "
        + "ou rapprochez-la d'une pièce chauffée s'il fait froid.",
      critique: s => "Il fait trop chaud malgré le ventilateur. Ouvrez le couvercle et mettez la serre à l'ombre.",
    },
  },

  // Fiches des plants (D-09, D-31). Provisoire, à remplacer par les fiches de Lucas Manche.
  plants: {
    salade: {
      nom: "Salade à couper",
      fiche: [
        ["Arrosage", "La serre arrose seule quand la terre descend sous le seuil. Vérifiez le réservoir chaque semaine."],
        ["Lumière", "Elle pousse avec une fenêtre claire, sans soleil brûlant derrière la vitre."],
        ["Température", "Elle préfère la fraîcheur : une pièce peu chauffée lui convient bien."],
        ["Récolte", "Coupez les grandes feuilles extérieures à quelques centimètres de la terre ; le cœur repousse."],
      ],
    },
    radis: {
      nom: "Radis",
      fiche: [
        ["Arrosage", "Une terre régulièrement humide donne des radis tendres ; trop sèche, ils deviennent piquants."],
        ["Lumière", "Ils ont besoin de lumière pour former leur racine : gardez la serre près de la fenêtre."],
        ["Température", "Ils supportent la fraîcheur ; au chaud, ils montent en feuilles."],
        ["Récolte", "Arrachez-les quand le haut de la racine dépasse de la terre, environ un mois après le semis."],
      ],
    },
  },
};
