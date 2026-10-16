# TODO - Neon Maze

> Idée validée par Leture le 2026-10-14 : labyrinthe 3D façon Wolfenstein, rendu dans le terminal
> en couleurs 24 bits avec des demi-blocs, C99, un seul fichier, zéro dépendance, style néon.

## Phase 0 - Fondations
- [x] Dossier, git, `.gitignore`, `.gitattributes`
- [x] Compilateur GCC installé (Scoop) et test de compilation des couleurs 24 bits
- [x] Vérifier le rendu réel dans le terminal de Leture : couleurs 24 bits et demi-blocs OK dans le terminal de VS Code (2026-10-14)
- [x] Vérifier la lecture du clavier (flèches, ZQSD) avec le premier prototype (2026-10-16)
- [ ] Choisir le nom définitif
- [x] Organisation du code validée par Leture (plan mode, 2026-10-14) : un fichier en 10 sections, phase 1 en 4 étapes testées une par une

## Phase 1 - Premier rendu 3D
- [x] Terminal : mode brut, curseur caché, restauration propre en quittant (même Ctrl+C), vérifié par Leture le 2026-10-16
- [x] Tampon d'image en couleurs, affichage en une écriture par image, sortie synchronisée (2026-10-14). Test de charge (plasma, pire cas) : Windows Terminal tient 60 images/s à 120x60 pixels (130 Ko par image) ; le terminal de VS Code saccade → **Windows Terminal = terminal conseillé dans le README**
- [x] Raycasting (algorithme DDA) : murs, distance, hauteur des colonnes (2026-10-16)
- [x] Déplacement et rotation au clavier, collisions avec les murs (2026-10-16)

## Phase 2 - Néon
- [x] Murs néon (faces sombres, tubes lumineux aux arêtes, cœur blanc), halo (bloom), brouillard, sol et plafond en grille (2026-10-16)
- [x] Minimap, compteur d'images par seconde (2026-10-16)
- [x] Adaptation à la taille du terminal : perspective calée sur la largeur, minimap masquée si elle ne tient pas (2026-10-16)
- [x] Poids des images : tolérance de couleur (47 Ko au lieu de 80 à 120x30)
- [x] Test de Leture dans Windows Terminal : validé le 2026-10-16, 60 images/s en plein écran (88 Ko par image)

## Phase 3 - Le jeu
- [x] Labyrinthe aléatoire (backtracker + boucles), sortie en portail sur la case la plus éloignée (parcours en largeur), chrono, record (2026-10-16)
- [x] Minimap avec brouillard de guerre (seules les cases vues apparaissent)
- [x] Écran titre (caméra qui tourne, lettres néon) et écran de fin (temps, record, rejouer)
- [x] Taille réglable : `./neon-maze 20` (de 4 à 30)
- [x] Test de Leture dans Windows Terminal : partie complète validée le 2026-10-16
- [ ] Le fichier fait 1 495 lignes : à la limite des ~1 500, à rediscuter avant toute nouvelle fonction

## Phase 4 - Publication
- [x] GitHub Actions : compilation à 0 avertissement (`-Werror`) sur Windows, Linux et macOS + clang, et lancement du jeu dans un faux terminal sous Linux (Échap → sortie propre), vert le 2026-10-16
- [x] Badge de la compilation dans le README
- [x] README : GIF (fabriqué hors git par `build/gif.c` + `build/make_gif.py` : course sur le plus court chemin), une commande pour compiler, explication illustrée du raycasting, licence MIT (2026-10-16)
- [x] Image d'aperçu pour les partages (envoyée par Leture le 2026-10-16) : `D:/_journal/neon-maze/apercu-social-1280x640.png`, à envoyer par Leture dans Settings → Social preview
- [x] Revue à froid (2026-10-16) : aucun bug mémoire ; 9 corrections (PR #4). Restent pour le README : sous Linux/macOS, deux touches tenues à la fois ne marchent pas bien (le terminal ne répète que la dernière) ; ▀ peut s'afficher sur deux cases dans un terminal réglé en chinois, japonais ou coréen
- [x] Repo GitHub public (Leture, 2026-10-16) + description + sujets
- [x] Ligne sur le profil GitHub (PR TeeBo8/TeeBo8#8, à fusionner par Leture)
- [x] Succès GitHub « Quickdraw » : issue #6 (deux touches d'une même action sous Windows) fermée en 57 s par la PR #7 (2026-10-16)
- [x] Discussions activées (2026-10-16)
- [ ] Succès « Galaxy Brain » : répondre aux questions des visiteurs dans les Discussions
