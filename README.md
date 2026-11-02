# ZEUS-80 — synthé analogique 8 voix pour M-VAVE FM-1

Firmware alternatif qui transforme le FM-1 en module polyphonique sur le modèle des expandeurs analogiques de 1984, comme le Roland MKS-80 Super Jupiter. Il ajoute un arpégiateur et un séquenceur 6 pistes, les mêmes que ceux de [LAX-32](https://github.com/pjac1810/lax32).

On y retrouve :

- **8 modules de voix**, chacun avec deux VCO, un filtre passe-haut, un filtre passe-bas résonant 24 dB, un VCA et deux enveloppes ADSR ;
- **deux sections, UPPER et LOWER**, réparties selon le mode de clavier : WHOLE (8 voix, un son), DUAL (deux sons superposés, 4 voix chacun) ou SPLIT (un son de chaque côté d'un point de partage) ;
- les **modes d'assignation** SOLO, UNISON 1, UNISON 2, POLY 1 et POLY 2, avec glide, hold, désaccord d'unisson et décalage d'octave ;
- **64 tons et 64 patchs**, numérotés de 11 à 88 (banque 1-8, numéro 1-8) comme sur l'original ;
- l'**afficheur LCD rétroéclairé de 16 caractères** en matrice de points, les boutons BANK et NUMBER avec leurs LED rouges, et les témoins UPPER, LOWER, TONE et PATCH ;
- un **arpégiateur** (14 modes) et un **séquenceur de 6 pistes** de 64 pas, avec 4 patterns.

C'est un projet indépendant, sans lien avec Roland ni M-VAVE :

- Aucun échantillon ni aucune donnée d'origine n'est utilisé. Le son est synthétisé et les 64 tons sont créés pour ce firmware.
- La police de l'afficheur est dessinée pour le projet.

Il est bâti sur [foma](https://github.com/konsumer/foma) (plateforme Felucca), avec installeur web, mode secours et retour au firmware d'origine.

## Le son

Chaque ton a 47 paramètres, rangés en 12 groupes de 4 :

| Groupe | Paramètres |
|---|---|
| LFO-1 | RATE, DELAY, WAVE (triangle, dent de scie montante et descendante, carré, aléatoire), VCA LFO |
| VCO MOD | profondeur par le LFO et par ENV-1, appliquée à VCO-1 et à VCO-2 (normal, off, inversé) |
| PWM | largeur d'impulsion, modulation par ENV-1 ou LFO-1, polarité |
| VCO-1 | forme (triangle, scie, impulsion, carré), registre 32' à 2', suivi du clavier |
| X-MOD | modulation croisée (VCO-2 vers VCO-1) manuelle et par ENV-1, polarité, SYNC (1→2, 2→1) |
| VCO-2 | forme (triangle, scie, impulsion, bruit), registre, accord fin, MIXER |
| VCF | fréquence, résonance (jusqu'à l'auto-oscillation), passe-haut, suivi du clavier |
| VCF MOD | enveloppe ENV-1 ou ENV-2, polarité, profondeur d'enveloppe et de LFO |
| ENV-1, ENV-2 | ATTACK, DECAY, SUSTAIN, RELEASE |
| ENV KEY | raccourcissement des enveloppes vers l'aigu, sensibilité à la vélocité |
| VCA | niveau, dynamique, ENV RESET |

Les patchs règlent, pour chaque section : le ton, l'assignation, le désaccord, l'octave, le glide, le hold (OFF, ON, MIDI), le LFO-2, la sensibilité au toucher (vibrato ou filtre), le pitch bend et son effet sur chaque VCO. S'y ajoutent le mode de clavier, le point de partage, la balance et la stéréo (MIX, ou SEPARATE : UPPER à gauche, LOWER à droite).

| Banque | Tons |
|---|---|
| 1 | cuivres |
| 2 | cordes |
| 3 | nappes et chœurs |
| 4 | basses |
| 5 | leads (SOLO, UNISON, glide) |
| 6 | claviers |
| 7 | cloches et modulation croisée |
| 8 | effets |

Les patchs 11 à 68 jouent chacun un ton. Les patchs 71 à 78 sont des combinaisons (cuivres + cordes en DUAL, basse | cuivres en SPLIT…), et 81 à 88 reprennent cloches et effets.

## L'afficheur et les commandes

L'**afficheur** montre, comme l'original :

- en jeu, `1 INT 73 S 41 11` : canal MIDI, mémoire, numéro du patch, mode de clavier (W, D, S), ton LOWER, ton UPPER ;
- en édition, `[ EDIT TONE 11 ]` ou `[EDIT PATCH 11 ]`, puis pour chaque réglage la valeur en mémoire et la valeur modifiée, par exemple `VCF FREQ  30> 45`. Rien n'est écrit tant qu'on n'appuie pas sur SAVE.

Dessous, les boutons BANK et NUMBER du patch, les témoins, les 8 modules (orange pour UPPER, vert pour LOWER), puis les quatre réglages des potentiomètres avec la valeur en mémoire marquée.

| Commande FM-1 | Fonction |
|---|---|
| SELECT | patch 11 à 88 |
| PRESETS | banque suivante ou précédente (en édition : le groupe de réglages) |
| ALGORITHM | le ton de la section |
| SEL | section UPPER ou LOWER |
| KNOB 1-4 | les 4 réglages du groupe (en jeu : VCF FREQ, VCF RES, VCF ENV, E2 REL) |
| EDIT | édition du ton (une 2e fois : retour au jeu) |
| HOME | édition du patch (une 2e fois : retour au jeu) |
| FX | groupe VCF, une 2e fois VCF MOD |
| ENV | groupe ENV-1, une 2e fois ENV-2 |
| LFO | groupe LFO-1 |
| SAVE | écrit le patch et ses tons, et garde tout (patterns compris) |
| OCT− / OCT+ | octave du clavier |
| MASTER | volume |

**MIDI** : canal 1 = le clavier (selon le mode de clavier), canal 2 = la section UPPER, canal 3 = la section LOWER. Notes, vélocité, pitch bend, modulation (CC 1) et aftertouch (vibrato ou filtre), sustain (CC 64, pour HOLD MIDI), all notes off.

## L'arpégiateur et le séquenceur

Ce sont ceux de LAX-32, sur le modèle de [Skiff](https://github.com/illuminus5/skiff).

- **Arpégiateur** : un pour le clavier, un pour UPPER et un pour LOWER. Il propose 14 modes (UP, DN, UPDN, RND, ORD, REPEAT, DNUP, UP+8, CONV, DIVG, PINKY, THUMB, WALK, CHORD). ARP règle le mode, la vitesse, les octaves et la durée des notes. ARP 2 (2e appui) règle le swing, la chance, le maintien et l'ordre.
- **Séquenceur** : 6 pistes de 64 pas et 4 patterns. Chaque piste joue le clavier (selon le mode de clavier), la section UPPER, la section LOWER, ou un canal MIDI externe de 1 à 16, pour piloter d'autres appareils.
- **Les pas** : accords de 4 notes, longueur, vélocité, accent, chance et conditions (FILL, 1:2 … 4:4), ratchet ou flam, nudge.
- **Les réglages** : swing, sens de lecture, enregistrement pas à pas et en direct.

| Commande | Fonction |
|---|---|
| SEQ | STEP, puis FEEL, puis TRACK (la destination de la piste, sa longueur, la taille du pas, le sens) |
| GLO | tempo, swing, pattern, métronome |
| SELECT / PRESETS | la piste, le pas |
| PLAY / REC | lecture, enregistrement (à l'arrêt : pas à pas ; SEL ajoute un silence) |
| EDIT | efface le pas (tenu : la piste) |
| LFO | mute de la piste |
| SEL tenu | FILL |

Au démarrage, le patch 73 (basse | cuivres) est chargé, et le pattern A est fait pour lui : SEQ puis PLAY.

## Obtenir et installer le firmware

- **GitHub** : pousse ce dossier sur la branche `main` d'un dépôt. Le workflow compile tout. Le `.fwsc` arrive sur la branche `firmware`. Avec Settings → Pages → Source : GitHub Actions, l'émulateur et l'installeur sont publiés sur GitHub Pages.
- **Docker Desktop** : `./fm1 build` ou `./fm1 dev` (http://localhost:8000).

L'installation se fait depuis la page Install (Chrome ou Edge, FM-1 branché en USB).

**Secours** : maintiens OCT− à l'allumage ; l'onglet Rescue remet le firmware M-VAVE V15. Installer un firmware non officiel se fait à tes risques.

## Développement

- `tools/gen_tones.py` : les 64 tons et 64 patchs → `src/tones.h`
- `src/synth.c` : les 8 modules, les sections, les modes, le MIDI (traité dans `audio()`)
- `src/seq.c`, `src/sq.h` : l'arpégiateur et le séquenceur (de LAX-32)
- `src/main.c` : l'afficheur, les boutons, l'édition, la mémoire
- `tests/run.sh` : chaque patch, les modes, le séquenceur et l'arpégiateur dans des WAV, puis un fuzz (UBSan)
- Identité du paquet : `FM-1_29` + le dernier chiffre de la version (0.1 : `FM-1_291`)

## Licence

GPL-3.0-only. Crédits : [LICENSING.md](LICENSING.md).
