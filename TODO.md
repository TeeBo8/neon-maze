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
- [ ] Murs lumineux, brouillard coloré selon la distance, sol et plafond en dégradé
- [ ] Minimap, compteur d'images par seconde
- [ ] Adaptation à la taille du terminal

## Phase 3 - Le jeu
- [ ] Objectif (trouver la sortie, chrono) et/ou labyrinthe généré au hasard
- [ ] Écran titre et fin de partie

## Phase 4 - Publication
- [ ] GitHub Actions : compilation à 0 avertissement sur Windows, Linux et macOS à chaque PR (seule façon de vérifier la partie Linux/macOS, pas de Linux sur le PC de Leture) + badge dans le README
- [ ] README : GIF, une commande pour compiler, explication illustrée du raycasting
- [ ] Revue à froid
- [ ] Repo GitHub public + description + sujets (Leture), ligne sur le profil GitHub
- [ ] Succès GitHub « Quickdraw » : une fois le repo public, ouvrir une issue pour un vrai petit défaut et la fermer en moins de 5 min avec sa correction
- [ ] Activer les Discussions (succès « Galaxy Brain » : répondre aux questions des visiteurs)
