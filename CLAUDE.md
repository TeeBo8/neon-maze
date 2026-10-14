# CLAUDE.md - Neon Maze (nom provisoire)

> Lu automatiquement par Claude Code à chaque session dans ce dossier.

## Le projet

Un labyrinthe en 3D en vue subjective (façon Wolfenstein 3D), **affiché dans le terminal**, en couleurs 24 bits, à 60 images/seconde. Écrit en **C pur (C99), un seul fichier, zéro dépendance** : on clone, on compile en une commande, on joue.

- **But** : un repo qui impressionne les devs sur GitHub et récolte un maximum d'étoiles. Projet perso de Leture, repo **public** à terme (TeeBo8/neon-maze), décidé le 2026-10-14 après Astro Blaster. Il passe **avant** l'API FastAPI + LLM.
- **Ce qui doit faire « waouh »** : de la vraie 3D fluide et colorée là où tout le monde attend du texte. Chaque caractère `▀` porte deux pixels (couleur du texte en haut, couleur du fond en bas).
- **Style** : même ADN néon qu'Astro Blaster (`D:\astro-blaster`) : murs lumineux, brouillard coloré au loin, minimap.
- **Les ingrédients des repos qui récoltent des étoiles** : un GIF qui montre l'effet en 5 secondes, une seule commande pour compiler, un code court et lisible, un README qui explique les maths (raycasting) avec des schémas.
- **Nom** : « neon-maze » est provisoire. Pour renommer : le dossier, le fichier source, la commande de compilation dans ce fichier et le README.

## État au 2026-10-14

- Dossier initialisé : git (branche `main`, aucun commit), `.gitignore`, `.gitattributes`, README provisoire. Pas encore de repo GitHub.
- Compilateur : **GCC 15.2** installé via Scoop (`scoop install gcc`, distribution MinGW nuwen). Un test a confirmé que la compilation en `-std=c99 -Wall -Wextra -Wpedantic` passe sans avertissement avec les séquences de couleurs 24 bits et l'API console Windows.
- **Pas encore vérifié** : le rendu réel dans la fenêtre de terminal de Leture (Windows Terminal ou le terminal de VS Code). C'est le premier test de la prochaine session.
- Le code n'est pas commencé : proposer l'organisation (plan mode) avant d'écrire.

## Stack

| Rôle | Outil |
|---|---|
| Langage | C99, un seul fichier source, bibliothèque standard + `math.h` |
| Compilateur | GCC (Windows : MinGW via Scoop ; Linux/macOS : gcc ou clang) |
| « Lint » | avertissements du compilateur : `-Wall -Wextra -Wpedantic`, **0 avertissement** |
| Affichage | séquences ANSI (couleurs 24 bits, curseur), demi-blocs `▀` |
| Clavier | Windows : `conio.h` (`_kbhit`, `_getch`) ou API console ; Linux/macOS : `termios` en mode brut |

## Commandes (prévues)

```powershell
gcc -std=c99 -O2 -Wall -Wextra -Wpedantic -o neon-maze neon-maze.c -lm   # compiler
./neon-maze                                                                # jouer
```

Si `gcc` n'est pas reconnu dans un vieux terminal : redémarrer VS Code (le PATH a changé), ou ajouter `C:\Users\Leture\scoop\apps\gcc\current\bin` au PATH de la session.

## Règles du projet

- **Leture reprend le C (il se dit « très nul »)** : expliquer au fil de l'eau les notions propres au C (pointeurs, tableaux, `struct`, `#define`, `#ifdef` par plateforme, gestion mémoire, `static`, compilation et édition de liens) et les maths du raycasting. Fiche du journal dans `D:\_journal\neon-maze\`.
- **Un seul fichier** reste la règle d'or pour l'effet « clone et compile » ; il est organisé en sections claires (réglages, maths, carte, rendu, entrée clavier, terminal, boucle principale). Si un jour il dépasse ~1 500 lignes, en rediscuter avec Leture.
- **Lisible avant tout** : noms en anglais, commentaires en français, pas d'astuce obscure sans commentaire. Les réglages (vitesse, champ de vision, couleurs) sont des `#define` ou des `const` regroupés en haut du fichier.
- **Portable** : Windows, Linux et macOS. Tout ce qui dépend du système est isolé dans une section protégée par `#ifdef _WIN32`.
- **Le terminal doit être rendu propre en quittant** (curseur réaffiché, couleurs réinitialisées, mode clavier restauré), même avec Ctrl+C.
- **Fluide** : 60 images/s visées ; mouvements multipliés par le temps écoulé. Une seule écriture groupée (un tampon) par image, pas des milliers de `printf`.
- **Branches et PR comme sur les autres projets, chaque PR cible `main`** (jamais de PR empilées : leçon d'Astro Blaster). Standard des repos du CLAUDE.md global (README français avec GIF, description, sujets GitHub).
- C'est Leture qui crée le repo public ou le bascule en public.

## Checklist avant de pousser

1. Compilation avec `-std=c99 -Wall -Wextra -Wpedantic` : **0 avertissement**
2. Le jeu se lance, se quitte proprement et le terminal est restauré
3. Leture l'a testé dans son terminal

## Utilisateur cible

- Profil : développeurs qui découvrent le repo sur GitHub (curieux de C, de graphismes, de rétro) ; recruteurs et prospects de TeeBo Studio
- Niveau technique : intermédiaire (sait compiler un fichier C) à débutant curieux (lit le README et l'explication du raycasting)
- Contexte d'usage : desktop, dans un terminal (Windows Terminal, terminal macOS, Linux)
- Douleur principale : être bluffé en 10 secondes, puis comprendre comment c'est fait en lisant un code court
