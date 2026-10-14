# TODO - Neon Maze

> Idée validée par Leture le 2026-10-14 : labyrinthe 3D façon Wolfenstein, rendu dans le terminal
> en couleurs 24 bits avec des demi-blocs, C99, un seul fichier, zéro dépendance, style néon.

## Phase 0 - Fondations
- [x] Dossier, git, `.gitignore`, `.gitattributes`
- [x] Compilateur GCC installé (Scoop) et test de compilation des couleurs 24 bits
- [x] Vérifier le rendu réel dans le terminal de Leture : couleurs 24 bits et demi-blocs OK dans le terminal de VS Code (2026-10-14)
- [ ] Vérifier la lecture du clavier (flèches, ZQSD) avec le premier prototype
- [ ] Choisir le nom définitif
- [x] Organisation du code validée par Leture (plan mode, 2026-10-14) : un fichier en 10 sections, phase 1 en 4 étapes testées une par une

## Phase 1 - Premier rendu 3D
- [ ] Terminal : mode brut, curseur caché, restauration propre en quittant (même Ctrl+C)
- [ ] Tampon d'image en couleurs, affichage en une écriture par image
- [ ] Raycasting (algorithme DDA) : murs, distance, hauteur des colonnes
- [ ] Déplacement et rotation au clavier, collisions avec les murs

## Phase 2 - Néon
- [ ] Murs lumineux, brouillard coloré selon la distance, sol et plafond en dégradé
- [ ] Minimap, compteur d'images par seconde
- [ ] Adaptation à la taille du terminal

## Phase 3 - Le jeu
- [ ] Objectif (trouver la sortie, chrono) et/ou labyrinthe généré au hasard
- [ ] Écran titre et fin de partie

## Phase 4 - Publication
- [ ] README : GIF, une commande pour compiler, explication illustrée du raycasting
- [ ] Revue à froid
- [ ] Repo GitHub public + description + sujets (Leture), ligne sur le profil GitHub
- [ ] Succès GitHub « Quickdraw » : une fois le repo public, ouvrir une issue pour un vrai petit défaut et la fermer en moins de 5 min avec sa correction
- [ ] Activer les Discussions (succès « Galaxy Brain » : répondre aux questions des visiteurs)
