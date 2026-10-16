/*
 * Neon Maze - un labyrinthe en 3D dans le terminal.
 * C99, un seul fichier, aucune dépendance.
 *
 * Compiler : gcc -std=c99 -O2 -Wall -Wextra -Wpedantic -o neon-maze neon-maze.c -lm
 * Jouer    : ./neon-maze
 */

/* Avec -std=c99, Linux et macOS masquent une partie des fonctions POSIX
 * (clock_gettime, nanosleep, la taille du terminal sur macOS). Ces deux
 * lignes les rendent visibles.
 * Elles doivent venir AVANT tout #include. */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE
#endif

#include <ctype.h>
#include <math.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#endif

/* ===================================================================
 * 1. RÉGLAGES
 * =================================================================== */

#define TARGET_FPS 60
#define COLOR_TOLERANCE 4 /* écart de couleur (sur 255) qu'on ne prend pas la peine d'envoyer */

#define MOVE_SPEED 3.0     /* cases par seconde */
#define TURN_SPEED 2.5     /* radians par seconde (un tour complet ≈ 2,5 s) */
#define FOV_PLANE 0.66     /* largeur du plan caméra : 0.66 donne ~66° de champ de vision */
#define PLAYER_RADIUS 0.2  /* le joueur est un petit carré, pour ne pas frôler les murs */
#define FOG_DISTANCE 10.0  /* à cette distance (en cases), un mur a disparu dans le brouillard */

/* Labyrinthe */
#define MAZE_SIZE 10       /* taille par défaut, en couloirs par côté (changeable : ./neon-maze 20) */
#define MAZE_MIN 4
#define MAZE_MAX 30
#define MAZE_LOOPS 0.12    /* part de murs intérieurs ouverts en plus : des boucles, moins de culs-de-sac */
#define COLOR_ZONE 8       /* les murs changent de couleur toutes les COLOR_ZONE cases */

/* Néon */
#define LINE_WIDTH 1.3      /* épaisseur des tubes de néon, en pixels */
#define WALL_FACE 0.16      /* luminosité de la face d'un mur entre ses tubes (0 à 1) */
#define PANEL_LINE 0.30     /* jointure entre deux cases d'un même mur, moins vive qu'une arête */
#define FLOOR_GRID 0.85     /* intensité de la grille du sol */
#define CEILING_GRID 0.18   /* intensité de la grille du plafond */
#define BLOOM_STRENGTH 1.4  /* force du halo autour des néons */

/* Linux et macOS ne signalent pas quand une touche est relâchée : on la
 * considère tenue un petit moment après chaque appui. Le premier délai
 * couvre l'attente avant la répétition automatique du clavier. */
#define KEY_FIRST_HOLD 0.55  /* secondes */
#define KEY_REPEAT_HOLD 0.12 /* secondes */

/* ===================================================================
 * 2. TYPES ET DÉCLARATIONS
 * =================================================================== */

/* Les actions du joueur, indépendantes des touches qui les déclenchent. */
enum Action {
    ACT_FORWARD,
    ACT_BACKWARD,
    ACT_STRAFE_LEFT,
    ACT_STRAFE_RIGHT,
    ACT_TURN_LEFT,
    ACT_TURN_RIGHT,
    ACT_CONFIRM, /* Entrée ou Espace : commencer, rejouer */
    ACT_COUNT,   /* nombre d'actions : toujours en dernier */
    ACT_NONE = -1
};

/* Une couleur 24 bits : rouge, vert, bleu, chacun de 0 à 255. */
struct Color {
    unsigned char r, g, b;
};

/* Le joueur : sa position sur la carte (en cases), la direction de son
 * regard, et le « plan caméra », perpendiculaire au regard, qui représente
 * l'écran. Chaque colonne de l'écran correspond à un point de ce plan. */
struct Player {
    double x, y;
    double dir_x, dir_y;
    double plane_x, plane_y;
};

/* Les trois écrans du jeu. */
enum Screen {
    SCREEN_TITLE,
    SCREEN_PLAY,
    SCREEN_WON
};

/* L'état de la partie. */
struct Game {
    enum Screen screen;
    struct Player player;
    int maze_size;      /* couloirs par côté */
    double clock;       /* heure actuelle, pour les animations */
    double start_time;  /* début de la partie en cours */
    double elapsed;     /* temps de la dernière partie gagnée, en secondes */
    double best;        /* meilleur temps depuis le lancement (0 = aucun) */
};

/* Une lumière : comme une couleur, mais en nombres à virgule, pour pouvoir
 * additionner plusieurs lueurs sans être bloqué à 255. */
struct Light {
    float r, g, b;
};

/* Couleurs néon (rouge, vert, bleu). */
static const struct Color FOG_COLOR = {12, 0, 28};     /* violet nuit, au loin */
static const struct Color CEILING_COLOR = {30, 0, 60}; /* plafond, juste au-dessus du joueur */
static const struct Color FLOOR_COLOR = {20, 0, 45};   /* sol, juste sous les pieds */
static const struct Color FLOOR_GRID_COLOR = {255, 30, 190};
static const struct Color CEILING_GRID_COLOR = {120, 60, 255};
static const struct Color WALL_CYAN = {0, 255, 255};
static const struct Color WALL_MAGENTA = {255, 0, 200};
static const struct Color WALL_LIME = {180, 255, 0};
static const struct Color EXIT_COLOR = {255, 190, 30}; /* la sortie, dorée */
static const struct Color WHITE = {255, 255, 255};
static const struct Color MINIMAP_PLAYER = {255, 255, 255};
static const struct Color MINIMAP_VIEW = {255, 220, 0};

/* Mis à 1 pour quitter (Échap, Ctrl+C, fermeture). « volatile sig_atomic_t »
 * est le seul type qu'un gestionnaire de signal a le droit de modifier. */
static volatile sig_atomic_t quit_requested = 0;

/* Prototypes : la section clavier et la section plateforme s'appellent
 * l'une l'autre, le compilateur doit connaître ces fonctions à l'avance. */
static void key_press(int action, double now);
static int platform_action_state(int action);
static void die(const char *message);
static void platform_print_error(const char *message);

/* ===================================================================
 * 3. CARTE
 * =================================================================== */

/* Vue de dessus : une case = un caractère. Espace = vide ; '#', 'M' et 'L'
 * = murs cyan, magenta et vert ; 'X' = la sortie. Un labyrinthe de n
 * couloirs par côté occupe 2n + 1 cases : les couloirs sont sur les
 * cases impaires, les murs possibles entre eux sur les cases paires. */
#define MAP_MAX (2 * MAZE_MAX + 1)
static char map[MAP_MAX][MAP_MAX];
static bool seen[MAP_MAX][MAP_MAX]; /* cases déjà vues par le joueur, pour la minimap */
static int map_w = 0, map_h = 0;

/* Contenu de la case (x, y) ; hors de la carte, c'est un mur. */
static char map_at(int x, int y)
{
    if (x < 0 || y < 0 || x >= map_w || y >= map_h)
        return '#';
    return map[y][x];
}

static void mark_seen(int x, int y)
{
    if (x >= 0 && y >= 0 && x < map_w && y < map_h)
        seen[y][x] = true;
}

static struct Color wall_color(char cell)
{
    switch (cell) {
    case 'M': return WALL_MAGENTA;
    case 'L': return WALL_LIME;
    case 'X': return EXIT_COLOR;
    default:  return WALL_CYAN;
    }
}

/* Nombre entier au hasard, de 0 à n - 1. */
static int random_below(int n)
{
    return rand() % n;
}

/* Creuse un labyrinthe de n couloirs par côté, avec l'algorithme du
 * « backtracker » : on part d'une case, on creuse vers une voisine pas
 * encore visitée tirée au hasard, et ainsi de suite. Coincé dans un
 * cul-de-sac, on revient sur ses pas (la pile garde le chemin parcouru)
 * jusqu'à une case qui a encore une voisine libre. Quand la pile est vide,
 * chaque case a été visitée : tout le labyrinthe est relié. */
static void carve_maze(int n)
{
    static int stack_x[MAZE_MAX * MAZE_MAX], stack_y[MAZE_MAX * MAZE_MAX];
    static const int dx[4] = {2, -2, 0, 0}, dy[4] = {0, 0, 2, -2};

    map_w = map_h = 2 * n + 1;
    memset(map, '#', sizeof map);
    memset(seen, 0, sizeof seen);

    int top = 0;
    map[1][1] = ' ';
    stack_x[top] = 1;
    stack_y[top] = 1;
    top++;
    while (top > 0) {
        int x = stack_x[top - 1], y = stack_y[top - 1];
        /* les voisines à deux cases (la case entre les deux est le mur) */
        int free_dirs[4], count = 0;
        for (int d = 0; d < 4; d++) {
            int nx = x + dx[d], ny = y + dy[d];
            if (nx > 0 && ny > 0 && nx < map_w - 1 && ny < map_h - 1 && map[ny][nx] == '#')
                free_dirs[count++] = d;
        }
        if (count == 0) {
            top--; /* cul-de-sac : on revient en arrière */
            continue;
        }
        int d = free_dirs[random_below(count)];
        map[y + dy[d] / 2][x + dx[d] / 2] = ' '; /* on abat le mur entre les deux */
        map[y + dy[d]][x + dx[d]] = ' ';
        stack_x[top] = x + dx[d];
        stack_y[top] = y + dy[d];
        top++;
    }

    /* Quelques murs intérieurs en moins : plusieurs chemins possibles. */
    int loops = (int)(n * n * MAZE_LOOPS);
    for (int i = 0; i < loops; i++) {
        /* un mur entre deux couloirs : une coordonnée paire, l'autre impaire */
        int x = 1 + random_below(map_w - 2), y = 1 + random_below(map_h - 2);
        if ((x + y) % 2 == 1)
            map[y][x] = ' ';
    }
}

/* Place la sortie sur la case la plus éloignée du départ, en nombre de
 * pas. Pour la trouver : un parcours « en largeur ». On visite d'abord les
 * cases à 1 pas du départ, puis celles à 2 pas, etc. (une file d'attente
 * garde l'ordre). La dernière case visitée est la plus lointaine. */
static void place_exit(void)
{
    static int queue[MAP_MAX * MAP_MAX];
    static bool visited[MAP_MAX][MAP_MAX];
    static const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};

    memset(visited, 0, sizeof visited);
    int head = 0, tail = 0, farthest = 1 * map_w + 1;
    queue[tail++] = farthest; /* une case (x, y) est rangée comme le nombre y * map_w + x */
    visited[1][1] = true;
    while (head < tail) {
        int cell = queue[head++];
        int x = cell % map_w, y = cell / map_w;
        /* Seulement une case de couloir (deux coordonnées impaires), pas un
         * passage entre deux : la sortie est ainsi au bout d'un couloir. */
        if (x % 2 == 1 && y % 2 == 1)
            farthest = cell;
        for (int d = 0; d < 4; d++) {
            int nx = x + dx[d], ny = y + dy[d];
            if (map[ny][nx] == ' ' && !visited[ny][nx]) {
                visited[ny][nx] = true;
                queue[tail++] = ny * map_w + nx;
            }
        }
    }
    map[farthest / map_w][farthest % map_w] = 'X';
}

/* Les murs changent de couleur par grandes zones, pour se repérer. */
static void paint_walls(void)
{
    static const char colors[3] = {'#', 'M', 'L'};
    for (int y = 0; y < map_h; y++)
        for (int x = 0; x < map_w; x++)
            if (map[y][x] == '#')
                map[y][x] = colors[(x / COLOR_ZONE + y / COLOR_ZONE) % 3];
}

static void generate_maze(int n)
{
    carve_maze(n);
    place_exit();
    paint_walls();
}

/* ===================================================================
 * 4. TAMPON D'IMAGE : un pixel = une couleur
 * =================================================================== */

/* L'image est un tableau de screen_w * screen_h couleurs, rangées ligne
 * par ligne : le pixel (x, y) est pixels[y * screen_w + x].
 * Chaque caractère du terminal affiche deux pixels l'un au-dessus de
 * l'autre, d'où une hauteur égale à deux fois le nombre de lignes. */
static struct Color *pixels = NULL;
static int screen_w = 0, screen_h = 0;

/* Calque de lumière, de la même taille que l'image : les néons y déposent
 * leur éclat, qui est ensuite flouté et ajouté à l'image (le « halo »).
 * light_tmp sert d'étape intermédiaire pendant le flou. */
static struct Light *light = NULL, *light_tmp = NULL;

/* Réserve de la mémoire, ou quitte s'il n'y en a plus. */
static void *alloc_or_die(size_t bytes)
{
    void *p = malloc(bytes);
    if (!p)
        die("Plus de mémoire.");
    return p;
}

/* (Ré)alloue l'image si la taille du terminal a changé. */
static void resize_screen(int cols, int rows)
{
    if (cols < 1)
        cols = 1;
    if (rows < 1)
        rows = 1;
    if (cols == screen_w && rows * 2 == screen_h)
        return;
    size_t count = (size_t)cols * (size_t)rows * 2;
    free(pixels); /* free(NULL) ne fait rien : sûr au premier appel */
    free(light);
    free(light_tmp);
    pixels = alloc_or_die(sizeof *pixels * count);
    light = alloc_or_die(sizeof *light * count);
    light_tmp = alloc_or_die(sizeof *light_tmp * count);
    screen_w = cols;
    screen_h = rows * 2;
}

/* ===================================================================
 * 5. AFFICHAGE : un tampon de texte envoyé en une seule écriture
 * =================================================================== */

/* Toute l'image est d'abord écrite ici, puis envoyée d'un coup au terminal.
 * Des milliers de petits printf seraient beaucoup trop lents. */
static char *out_data = NULL; /* pointeur vers la mémoire réservée */
static size_t out_len = 0;    /* octets utilisés */
static size_t out_cap = 0;    /* octets réservés */

/* Garantit la place pour « extra » octets de plus, en agrandissant si besoin. */
static void out_reserve(size_t extra)
{
    if (out_len + extra <= out_cap)
        return;
    size_t cap = out_cap ? out_cap : 65536;
    while (cap < out_len + extra)
        cap *= 2;
    char *bigger = realloc(out_data, cap);
    if (!bigger)
        die("Plus de mémoire.");
    out_data = bigger;
    out_cap = cap;
}

/* Ajoute du texte formaté, comme printf, mais dans le tampon. */
static void out_printf(const char *fmt, ...)
{
    va_list args;
    out_reserve(256);
    va_start(args, fmt);
    int n = vsnprintf(out_data + out_len, out_cap - out_len, fmt, args);
    va_end(args);
    if (n < 0)
        return;
    if ((size_t)n >= out_cap - out_len) { /* pas assez de place : on recommence */
        out_reserve((size_t)n + 1);
        va_start(args, fmt);
        vsnprintf(out_data + out_len, out_cap - out_len, fmt, args);
        va_end(args);
    }
    out_len += (size_t)n;
}

/* Ajoute un nombre de 0 à 255, sans passer par printf (bien plus rapide). */
static void out_byte_number(unsigned char n)
{
    if (n >= 100)
        out_data[out_len++] = (char)('0' + n / 100);
    if (n >= 10)
        out_data[out_len++] = (char)('0' + n / 10 % 10);
    out_data[out_len++] = (char)('0' + n % 10);
}

/* Ajoute la séquence qui change la couleur du texte (38) ou du fond (48). */
static void out_color(int layer, struct Color c)
{
    out_reserve(32);
    out_data[out_len++] = '\x1b';
    out_data[out_len++] = '[';
    out_data[out_len++] = layer == 38 ? '3' : '4';
    out_data[out_len++] = '8';
    out_data[out_len++] = ';';
    out_data[out_len++] = '2';
    out_data[out_len++] = ';';
    out_byte_number(c.r);
    out_data[out_len++] = ';';
    out_byte_number(c.g);
    out_data[out_len++] = ';';
    out_byte_number(c.b);
    out_data[out_len++] = 'm';
}

/* Vrai si l'œil ne voit pas la différence entre deux couleurs : on garde
 * alors celle déjà envoyée, ce qui allège beaucoup l'image (le halo crée
 * des dégradés où chaque pixel diffère à peine de son voisin). */
static bool close_color(struct Color a, struct Color b)
{
    return abs(a.r - b.r) <= COLOR_TOLERANCE && abs(a.g - b.g) <= COLOR_TOLERANCE && abs(a.b - b.b) <= COLOR_TOLERANCE;
}

/* Convertit l'image en texte. Chaque caractère est un demi-bloc ▀ : sa
 * couleur de texte peint le pixel du haut, sa couleur de fond celui du bas.
 * On ne réécrit une couleur que si elle change visiblement : souvent, deux
 * caractères voisins ont les mêmes, et le terminal reçoit bien moins d'octets. */
static void draw_pixels(void)
{
    struct Color fg = {0, 0, 0}, bg = {0, 0, 0};
    bool first = true;
    for (int y = 0; y < screen_h; y += 2) {
        /* curseur au début de la ligne y/2 + 1 (le terminal compte à partir de 1) */
        out_printf("\x1b[%d;1H", y / 2 + 1);
        for (int x = 0; x < screen_w; x++) {
            struct Color top = pixels[y * screen_w + x];
            struct Color bottom = pixels[(y + 1) * screen_w + x];
            /* fg et bg retiennent les couleurs vraiment envoyées : comparer à
             * elles empêche les petits écarts de s'accumuler. */
            if (first || !close_color(top, fg)) {
                out_color(38, top);
                fg = top;
            }
            if (first || !close_color(bottom, bg)) {
                out_color(48, bottom);
                bg = bottom;
            }
            first = false;
            out_reserve(3);
            out_data[out_len++] = '\xe2'; /* ▀ s'écrit en 3 octets en UTF-8 */
            out_data[out_len++] = '\x96';
            out_data[out_len++] = '\x80';
        }
    }
    out_printf("\x1b[0m");
}

static void out_flush(void)
{
    /* Si l'écriture échoue, le terminal a disparu : on quitte. */
    if (fwrite(out_data, 1, out_len, stdout) != out_len || fflush(stdout) != 0)
        quit_requested = 1;
    out_len = 0;
}

/* ===================================================================
 * 6. RAYCASTING : la 3D
 * =================================================================== */

/* Mélange deux couleurs : t = 0 donne a, t = 1 donne b. */
static struct Color mix(struct Color a, struct Color b, double t)
{
    if (t < 0)
        t = 0;
    if (t > 1)
        t = 1;
    struct Color c;
    c.r = (unsigned char)(a.r + (b.r - a.r) * t);
    c.g = (unsigned char)(a.g + (b.g - a.g) * t);
    c.b = (unsigned char)(a.b + (b.b - a.b) * t);
    return c;
}

/* Ajoute la lumière b (multipliée par k) à la couleur a, sans dépasser 255. */
static struct Color add(struct Color a, struct Color b, double k)
{
    struct Color c;
    c.r = (unsigned char)fmin(255.0, a.r + b.r * k);
    c.g = (unsigned char)fmin(255.0, a.g + b.g * k);
    c.b = (unsigned char)fmin(255.0, a.b + b.b * k);
    return c;
}

static double clamp01(double v)
{
    return v < 0 ? 0 : v > 1 ? 1 : v;
}

/* Intensité d'un tube de néon, vu à « e » pixels de son centre :
 * 1 sur le tube, 0 dès qu'on s'en éloigne, avec un bord adouci. */
static double neon_line(double e)
{
    return clamp01(LINE_WIDTH - e);
}

/* Dépose l'éclat d'un néon dans le calque de lumière, pour le halo. */
static void add_light(int x, int y, struct Color c, double k)
{
    struct Light *l = &light[y * screen_w + x];
    l->r += (float)(c.r * k);
    l->g += (float)(c.g * k);
    l->b += (float)(c.b * k);
}

/* Distance focale, en pixels : un mur d'une case de haut, à une case de
 * distance, mesure focal pixels à l'écran. Elle dépend de la largeur de
 * l'écran et du champ de vision, pas de la hauteur : les proportions
 * restent justes quelle que soit la forme du terminal. */
static double focal_length(void)
{
    return screen_w / (2.0 * FOV_PLANE);
}

/* Sol et plafond : une grille de néon posée sur les cases de la carte.
 * Les yeux du joueur sont à mi-hauteur des murs. Chaque ligne de l'écran
 * sous l'horizon montre le sol à une certaine distance (plus la ligne est
 * basse, plus le sol est près). Pour chaque pixel, on calcule le point du
 * sol visé : s'il tombe près d'une ligne de la grille (une coordonnée
 * entière), il s'allume. Le plafond est le reflet du sol. */
static void draw_floor_and_ceiling(const struct Player *p)
{
    double focal = focal_length();
    double horizon = screen_h / 2.0;
    /* rayon du bord gauche de l'écran, et écart jusqu'à celui du bord droit */
    double left_x = p->dir_x - p->plane_x, left_y = p->dir_y - p->plane_y;
    double span_x = 2 * p->plane_x, span_y = 2 * p->plane_y;

    for (int y = 0; y < screen_h; y++) {
        bool ceiling = y < horizon;
        double offset = fabs(y + 0.5 - horizon);                 /* pixels d'écart avec l'horizon */
        double row_dist = 0.5 * focal / offset;                  /* distance du sol montré par cette ligne */
        double row_step = row_dist - 0.5 * focal / (offset + 1); /* distance gagnée d'une ligne à l'autre */
        double fog = clamp01(row_dist / FOG_DISTANCE);
        struct Color base = mix(ceiling ? CEILING_COLOR : FLOOR_COLOR, FOG_COLOR, fog);
        struct Color grid_color = ceiling ? CEILING_GRID_COLOR : FLOOR_GRID_COLOR;
        double grid = (ceiling ? CEILING_GRID : FLOOR_GRID) * (1 - fog);

        for (int x = 0; x < screen_w; x++) {
            double t = (x + 0.5) / screen_w;
            double ray_x = left_x + span_x * t, ray_y = left_y + span_y * t;
            double world_x = p->x + row_dist * ray_x, world_y = p->y + row_dist * ray_y;
            /* Taille d'un pixel posé sur le sol, en cases, sur chaque axe
             * (le 1e-9 évite une division par zéro). */
            double size_x = fabs(row_dist * span_x / screen_w) + fabs(row_step * ray_x) + 1e-9;
            double size_y = fabs(row_dist * span_y / screen_w) + fabs(row_step * ray_y) + 1e-9;
            /* Écart avec la ligne de grille la plus proche, en pixels. Au loin,
             * les lignes se serrent au point de se mélanger : on les efface. */
            double line_x = neon_line(fabs(world_x - floor(world_x + 0.5)) / size_x) * clamp01(2 - 4 * size_x);
            double line_y = neon_line(fabs(world_y - floor(world_y + 0.5)) / size_y) * clamp01(2 - 4 * size_y);
            double k = fmax(line_x, line_y) * grid;

            pixels[y * screen_w + x] = add(base, grid_color, k);
            if (!ceiling)
                add_light(x, y, grid_color, k * 0.5);
        }
    }
}

/* Pour chaque colonne de l'écran, on lance un rayon depuis le joueur et on
 * le fait avancer case par case (algorithme DDA) jusqu'à toucher un mur.
 * Plus le mur est loin, plus sa colonne est courte : c'est la perspective.
 * Chaque mur est une face sombre bordée de tubes de néon. */
static void draw_walls(const struct Player *p, double clock)
{
    double focal = focal_length();
    double horizon = screen_h / 2.0;

    for (int x = 0; x < screen_w; x++) {
        /* Position de la colonne sur le plan caméra : -1 à gauche, +1 à droite. */
        double camera_x = 2.0 * (x + 0.5) / screen_w - 1.0;
        double ray_x = p->dir_x + p->plane_x * camera_x;
        double ray_y = p->dir_y + p->plane_y * camera_x;

        /* Case de départ, et distance que le rayon doit parcourir pour
         * traverser une case entière en x (delta_x) ou en y (delta_y). */
        int cell_x = (int)p->x, cell_y = (int)p->y;
        double delta_x = ray_x == 0 ? 1e30 : fabs(1.0 / ray_x);
        double delta_y = ray_y == 0 ? 1e30 : fabs(1.0 / ray_y);

        /* Sens du pas (+1 ou -1) et distance jusqu'au premier bord de case. */
        int step_x = ray_x < 0 ? -1 : 1;
        int step_y = ray_y < 0 ? -1 : 1;
        double side_x = (ray_x < 0 ? p->x - cell_x : cell_x + 1.0 - p->x) * delta_x;
        double side_y = (ray_y < 0 ? p->y - cell_y : cell_y + 1.0 - p->y) * delta_y;

        /* DDA : on saute toujours vers le bord de case le plus proche.
         * Chaque case traversée est visible : on la note pour la minimap. */
        bool hit_y_side = false; /* le mur touché est-il une face nord/sud ? */
        char cell = ' ';
        mark_seen(cell_x, cell_y);
        while (cell == ' ') {
            if (side_x < side_y) {
                side_x += delta_x;
                cell_x += step_x;
                hit_y_side = false;
            } else {
                side_y += delta_y;
                cell_y += step_y;
                hit_y_side = true;
            }
            cell = map_at(cell_x, cell_y);
            mark_seen(cell_x, cell_y);
        }

        /* Distance perpendiculaire au plan caméra, et non distance réelle
         * jusqu'au joueur : sinon les murs se bomberaient (effet « fish-eye »). */
        double dist = hit_y_side ? side_y - delta_y : side_x - delta_x;
        if (dist < 1e-4)
            dist = 1e-4;

        /* Point touché le long de la face, de 0 à 1 d'un bord de la case à l'autre. */
        double wall_x = hit_y_side ? p->x + dist * ray_x : p->y + dist * ray_y;
        wall_x -= floor(wall_x);

        /* Largeur d'un pixel sur la face, en cases : de combien wall_x
         * avance quand on passe à la colonne voisine. Elle grandit avec la
         * distance, et quand on voit le mur de biais. */
        double cross = p->plane_x * ray_y - p->plane_y * ray_x;
        double ray_across = hit_y_side ? ray_y : ray_x;
        double pixel_size = 2.0 * dist * fabs(cross) / (fabs(ray_across) * screen_w) + 1e-9;

        /* Tube vertical au bord de la case. On regarde la case voisine le
         * long de la face : si elle est vide ou d'une autre couleur (coin
         * saillant), ou si un mur se dresse devant elle (coin rentrant),
         * c'est une vraie arête, qui brille fort. Sinon c'est une simple
         * jointure entre deux cases du même mur, à peine visible. */
        int side = wall_x < 0.5 ? -1 : 1;
        int next_x = cell_x + (hit_y_side ? side : 0), next_y = cell_y + (hit_y_side ? 0 : side);
        int front_x = hit_y_side ? 0 : -step_x, front_y = hit_y_side ? -step_y : 0;
        bool edge = map_at(next_x, next_y) != cell || map_at(next_x + front_x, next_y + front_y) != ' ';
        double vertical = neon_line(fmin(wall_x, 1 - wall_x) / pixel_size) * (edge ? 1.0 : PANEL_LINE);

        double fog = clamp01(dist / FOG_DISTANCE);
        struct Color color = wall_color(cell);
        /* Faces nord/sud plus sombres que les faces est/ouest : c'est ce qui donne le relief. */
        struct Color face = mix((struct Color){0, 0, 0}, color, WALL_FACE * (hit_y_side ? 0.55 : 1.0));
        /* La sortie est un portail : des carrés de lumière qui filent vers
         * son centre. Vus de loin, ils seraient trop serrés pour l'écran
         * (ça fourmille) : leur contraste baisse avec la taille des pixels. */
        bool is_exit = cell == 'X';
        double ring_contrast = clamp01(1.5 - pixel_size * 15);
        if (is_exit)
            fog *= 0.5; /* elle perce le brouillard, pour se voir de loin */

        /* Le mur va de top à bottom (en pixels, avec décimales). */
        double half = 0.5 * focal / dist;
        double top = horizon - half, bottom = horizon + half;
        int y_start = top < 0 ? 0 : (int)top;
        int y_end = bottom > screen_h ? screen_h : (int)ceil(bottom);

        for (int y = y_start; y < y_end; y++) {
            double center = y + 0.5;
            if (center < top || center >= bottom)
                continue;
            /* Tubes horizontaux : le haut et le bas du mur. */
            double k = fmax(vertical, neon_line(fmin(center - top, bottom - center)));
            double glow = 0;
            if (is_exit) {
                /* ring : 0 au centre de la face, 0.5 sur ses bords */
                double v = (center - top) / (bottom - top);
                double ring = fmax(fabs(wall_x - 0.5), fabs(v - 0.5));
                glow = 0.3 + 0.3 * ring_contrast * sin(ring * 30.0 + clock * 6.0);
                face = mix((struct Color){0, 0, 0}, color, glow);
            }
            /* Le cœur d'un tube est presque blanc, sa lueur garde la couleur. */
            struct Color c = mix(add(face, color, k), WHITE, k * 0.45);
            pixels[y * screen_w + x] = mix(c, FOG_COLOR, fog);
            /* Le mur cache le sol derrière lui, et donc aussi sa lueur. */
            light[y * screen_w + x] = (struct Light){0, 0, 0};
            add_light(x, y, color, (k + glow * 0.4) * (1 - fog));
        }
    }
}

/* Le halo (« bloom ») : le calque de lumière est flouté, puis ajouté à
 * l'image, et les néons semblent rayonner. Le flou se fait en deux passes,
 * horizontale puis verticale : bien plus rapide qu'un flou en carré, pour
 * le même résultat. Les poids suivent une courbe en cloche (gaussienne) :
 * le pixel lui-même compte le plus, ses voisins de moins en moins. */
static void apply_bloom(void)
{
    static const float weights[] = {0.161f, 0.149f, 0.117f, 0.078f, 0.045f, 0.022f, 0.009f};
    const int radius = (int)(sizeof weights / sizeof weights[0]) - 1;

    /* Passe horizontale : light → light_tmp. */
    for (int y = 0; y < screen_h; y++) {
        for (int x = 0; x < screen_w; x++) {
            struct Light sum = {0, 0, 0};
            for (int i = -radius; i <= radius; i++) {
                int sx = x + i;
                if (sx < 0 || sx >= screen_w)
                    continue;
                const struct Light *l = &light[y * screen_w + sx];
                float w = weights[i < 0 ? -i : i];
                sum.r += l->r * w;
                sum.g += l->g * w;
                sum.b += l->b * w;
            }
            light_tmp[y * screen_w + x] = sum;
        }
    }

    /* Passe verticale : light_tmp → ajoutée à l'image. */
    for (int y = 0; y < screen_h; y++) {
        for (int x = 0; x < screen_w; x++) {
            struct Light sum = {0, 0, 0};
            for (int i = -radius; i <= radius; i++) {
                int sy = y + i;
                if (sy < 0 || sy >= screen_h)
                    continue;
                const struct Light *l = &light_tmp[sy * screen_w + x];
                float w = weights[i < 0 ? -i : i];
                sum.r += l->r * w;
                sum.g += l->g * w;
                sum.b += l->b * w;
            }
            struct Color *px = &pixels[y * screen_w + x];
            px->r = (unsigned char)fmin(255.0, px->r + sum.r * BLOOM_STRENGTH);
            px->g = (unsigned char)fmin(255.0, px->g + sum.g * BLOOM_STRENGTH);
            px->b = (unsigned char)fmin(255.0, px->b + sum.b * BLOOM_STRENGTH);
        }
    }
}

/* Allume le pixel de la minimap qui correspond au point (x, y) de la carte. */
static void minimap_plot(int left, int top, int scale, double x, double y, struct Color c)
{
    int px = left + (int)(x * scale), py = top + (int)(y * scale);
    if (px >= 0 && py >= 0 && px < screen_w && py < screen_h)
        pixels[py * screen_w + px] = c;
}

/* La minimap, en haut à droite : une case = un carré de 1 ou 2 pixels de
 * côté. Elle ne montre que les cases déjà vues (le « brouillard de
 * guerre »), et n'apparaît que si elle tient à l'aise sur l'écran. */
static void draw_minimap(const struct Player *p, double clock)
{
    int scale = 2;
    while (scale > 0 && (map_w * scale > screen_w / 3 || map_h * scale > screen_h / 2))
        scale--;
    if (scale == 0)
        return;
    int left = screen_w - map_w * scale - 2, top = 2; /* top = 2 : sous le bandeau d'infos */
    double pulse = 0.5 + 0.5 * sin(clock * 4.0);

    for (int my = 0; my < map_h; my++) {
        for (int mx = 0; mx < map_w; mx++) {
            char cell = map[my][mx];
            for (int sy = 0; sy < scale; sy++) {
                for (int sx = 0; sx < scale; sx++) {
                    struct Color *px = &pixels[(top + my * scale + sy) * screen_w + left + mx * scale + sx];
                    /* Pas encore vue : presque noire. Vide : la scène assombrie,
                     * comme une vitre fumée. Mur : sa couleur. */
                    if (!seen[my][mx])
                        *px = mix(*px, (struct Color){0, 0, 0}, 0.9);
                    else if (cell == ' ')
                        *px = mix(*px, (struct Color){0, 0, 0}, 0.7);
                    else
                        *px = mix((struct Color){0, 0, 0}, wall_color(cell), cell == 'X' ? 0.5 + 0.5 * pulse : 0.8);
                }
            }
        }
    }

    /* La direction du regard : quelques points devant le joueur, puis le joueur. */
    for (double t = 0.5; t <= 2.0; t += 0.5 / scale)
        minimap_plot(left, top, scale, p->x + p->dir_x * t, p->y + p->dir_y * t, MINIMAP_VIEW);
    minimap_plot(left, top, scale, p->x, p->y, MINIMAP_PLAYER);
}

/* Police de 5 x 7 pixels pour les grands titres. Chaque glyphe fait 7
 * lignes ; dans chaque ligne, les 5 bits de droite disent quels pixels
 * allumer (0x10 = le plus à gauche, 0x01 = le plus à droite).
 * Exemple, le A : 01110 / 10001 / 10001 / 11111 / 10001 / 10001 / 10001. */
struct Glyph {
    char c;
    unsigned char rows[7];
};

static const struct Glyph FONT[] = {
    {'A', {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}}, {'B', {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}},
    {'E', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}}, {'M', {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}},
    {'N', {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11}}, {'O', {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
    {'R', {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}}, {'V', {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}},
    {'Z', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}}, {'0', {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}},
    {'1', {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}}, {'2', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}},
    {'3', {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E}}, {'4', {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}},
    {'5', {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}}, {'6', {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}},
    {'7', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}}, {'8', {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}},
    {'9', {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}}, {':', {0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00}},
    {'.', {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C}},
};

/* Les 7 lignes du glyphe de c, ou NULL s'il n'est pas dans la police (espace). */
static const unsigned char *glyph_rows(char c)
{
    for (size_t i = 0; i < sizeof FONT / sizeof FONT[0]; i++)
        if (FONT[i].c == c)
            return FONT[i].rows;
    return NULL;
}

/* Écrit un texte en grosses lettres de néon, centré, en commençant à la
 * ligne de pixels y. Chaque pixel de la police devient un carré de
 * « scale » pixels de côté : la plus grande taille qui tient dans
 * max_w x max_h. Renvoie la hauteur dessinée (0 si rien ne tient). */
static int draw_big_text(const char *text, int y, int max_w, int max_h, struct Color color)
{
    int len = (int)strlen(text);
    int width = len * 6 - 1; /* 5 pixels par lettre + 1 d'espace, sans espace après la dernière */
    int scale = 0;
    while (width * (scale + 1) <= max_w && 7 * (scale + 1) <= max_h)
        scale++;
    if (scale == 0)
        return 0;

    struct Color core = mix(color, WHITE, 0.5); /* cœur presque blanc, halo coloré */
    int left = (screen_w - width * scale) / 2;
    for (int i = 0; i < len; i++) {
        const unsigned char *rows = glyph_rows(text[i]);
        if (!rows)
            continue;
        for (int gy = 0; gy < 7; gy++) {
            for (int gx = 0; gx < 5; gx++) {
                if (!(rows[gy] & (0x10 >> gx)))
                    continue;
                for (int sy = 0; sy < scale; sy++) {
                    for (int sx = 0; sx < scale; sx++) {
                        int px = left + (i * 6 + gx) * scale + sx, py = y + gy * scale + sy;
                        if (px < 0 || py < 0 || px >= screen_w || py >= screen_h)
                            continue;
                        pixels[py * screen_w + px] = core;
                        add_light(px, py, color, 1.2);
                    }
                }
            }
        }
    }
    return 7 * scale;
}

/* Assombrit la scène (et sa lumière), pour que les titres ressortent. */
static void dim_scene(double amount)
{
    for (int i = 0; i < screen_w * screen_h; i++) {
        pixels[i] = mix(pixels[i], (struct Color){0, 0, 0}, amount);
        light[i].r *= (float)(1 - amount);
        light[i].g *= (float)(1 - amount);
        light[i].b *= (float)(1 - amount);
    }
}

/* Un temps en secondes, écrit « 01:23.4 ». */
static void format_time(char *buf, size_t size, double seconds)
{
    /* On arrondit d'abord au dixième, une seule fois : arrondir les secondes
     * après avoir calculé les minutes pourrait afficher « 00:60.0 ». */
    long tenths = (long)(seconds * 10 + 0.5);
    snprintf(buf, size, "%02ld:%02ld.%ld", tenths / 600, tenths / 10 % 60, tenths % 10);
}

/* Dessine une image complète de la scène dans « pixels ». */
static void render_scene(const struct Game *g)
{
    /* Calque de lumière remis au noir. Des octets à 0 donnent bien 0.0 pour
     * un float (norme IEEE 754, celle de tous les ordinateurs actuels). */
    memset(light, 0, sizeof *light * (size_t)screen_w * (size_t)screen_h);
    draw_floor_and_ceiling(&g->player);
    draw_walls(&g->player, g->clock);

    /* Titres : deux lignes de grosses lettres, en haut de l'écran. */
    int max_w = screen_w * 7 / 10, max_h = screen_h / 4, y = screen_h / 8;
    if (g->screen == SCREEN_TITLE) {
        dim_scene(0.4);
        int h = draw_big_text("NEON", y, max_w, max_h, WALL_MAGENTA);
        draw_big_text("MAZE", y + h + h / 4, max_w, max_h, WALL_CYAN);
    } else if (g->screen == SCREEN_WON) {
        char time_text[32];
        format_time(time_text, sizeof time_text, g->elapsed);
        dim_scene(0.4);
        int h = draw_big_text("BRAVO", y, max_w, max_h, EXIT_COLOR);
        draw_big_text(time_text, y + h + h / 4, max_w, max_h, WALL_CYAN);
    }

    apply_bloom();
    if (g->screen == SCREEN_PLAY)
        draw_minimap(&g->player, g->clock);
}

/* ===================================================================
 * 7. PLATEFORME : tout ce qui dépend du système est ici
 * =================================================================== */

#ifdef _WIN32

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

static HANDLE con_in, con_out;
static DWORD saved_in_mode, saved_out_mode;
static UINT saved_codepage;
static HANDLE frame_timer; /* minuterie précise : Sleep() arrondit à ~15 ms */
static LARGE_INTEGER clock_freq;

/* Appelée par Windows à la fermeture de la fenêtre ou sur Ctrl+Pause. */
static BOOL WINAPI on_console_event(DWORD type)
{
    /* Fenêtre ou terminal fermé : il n'y a plus rien à restaurer, et la
     * boucle principale peut être bloquée sur une écriture qui n'aboutira
     * jamais. On arrête le programme tout de suite. */
    if (type == CTRL_CLOSE_EVENT || type == CTRL_LOGOFF_EVENT || type == CTRL_SHUTDOWN_EVENT)
        ExitProcess(0);
    quit_requested = 1;
    return TRUE;
}

static void platform_init(void)
{
    con_in = GetStdHandle(STD_INPUT_HANDLE);
    con_out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!GetConsoleMode(con_in, &saved_in_mode) || !GetConsoleMode(con_out, &saved_out_mode))
        die("Neon Maze doit être lancé dans un terminal.");
    /* Sans les séquences de couleur, l'écran se remplirait de codes illisibles
     * (vieille console de Windows). */
    if (!SetConsoleMode(con_out, saved_out_mode | ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING))
        die("Ce terminal n'affiche pas les couleurs : lance Neon Maze dans Windows Terminal.");
    saved_codepage = GetConsoleOutputCP();
    SetConsoleOutputCP(CP_UTF8); /* pour le caractère ▀ */
    /* Clavier brut : pas d'écho, pas d'attente de la touche Entrée, Ctrl+C
     * arrive comme une touche normale, pas de sélection à la souris. */
    SetConsoleMode(con_in, ENABLE_EXTENDED_FLAGS);
    FlushConsoleInputBuffer(con_in);
    SetConsoleCtrlHandler(on_console_event, TRUE);

    QueryPerformanceFrequency(&clock_freq);
    frame_timer = CreateWaitableTimerExW(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
}

static void platform_restore(void)
{
    SetConsoleMode(con_in, saved_in_mode);
    SetConsoleMode(con_out, saved_out_mode);
    SetConsoleOutputCP(saved_codepage);
    if (frame_timer)
        CloseHandle(frame_timer);
}

/* Secondes écoulées depuis un instant fixe : seule la différence compte. */
static double platform_now(void)
{
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return (double)t.QuadPart / (double)clock_freq.QuadPart;
}

static void platform_sleep(double seconds)
{
    if (frame_timer) {
        LARGE_INTEGER due;
        due.QuadPart = -(LONGLONG)(seconds * 1e7); /* négatif = durée relative, en 100 ns */
        SetWaitableTimer(frame_timer, &due, 0, NULL, NULL, FALSE);
        WaitForSingleObject(frame_timer, INFINITE);
    } else {
        Sleep((DWORD)(seconds * 1000.0));
    }
}

static void platform_terminal_size(int *cols, int *rows)
{
    CONSOLE_SCREEN_BUFFER_INFO info;
    *cols = 80;
    *rows = 24;
    if (GetConsoleScreenBufferInfo(con_out, &info)) {
        *cols = info.srWindow.Right - info.srWindow.Left + 1;
        *rows = info.srWindow.Bottom - info.srWindow.Top + 1;
    }
}

static int action_from_key(WORD vk)
{
    switch (vk) {
    case 'Z': case 'W': case VK_UP: return ACT_FORWARD;    /* AZERTY et QWERTY */
    case 'S': case VK_DOWN:         return ACT_BACKWARD;
    case 'Q': case 'A':             return ACT_STRAFE_LEFT;
    case 'D':                       return ACT_STRAFE_RIGHT;
    case VK_LEFT:                   return ACT_TURN_LEFT;
    case VK_RIGHT:                  return ACT_TURN_RIGHT;
    case VK_RETURN: case VK_SPACE:  return ACT_CONFIRM;
    default:                        return ACT_NONE;
    }
}

/* Lit tous les événements clavier en attente, sans jamais bloquer. */
static void platform_poll_input(double now)
{
    DWORD pending = 0;
    while (GetNumberOfConsoleInputEvents(con_in, &pending) && pending > 0) {
        INPUT_RECORD events[64];
        DWORD count = 0;
        if (!ReadConsoleInputW(con_in, events, 64, &count) || count == 0)
            break;
        for (DWORD i = 0; i < count; i++) {
            KEY_EVENT_RECORD *key = &events[i].Event.KeyEvent;
            if (events[i].EventType != KEY_EVENT || !key->bKeyDown)
                continue;
            if (key->wVirtualKeyCode == VK_ESCAPE || key->uChar.UnicodeChar == 3) /* 3 = Ctrl+C */
                quit_requested = 1;
            else
                key_press(action_from_key(key->wVirtualKeyCode), now);
        }
    }
}

/* Écrit un message d'erreur. La console de Windows n'affiche bien les
 * accents qu'en UTF-8 : on l'y passe le temps du message, puis on remet
 * son réglage d'origine. */
static void platform_print_error(const char *message)
{
    UINT codepage = GetConsoleOutputCP();
    SetConsoleOutputCP(CP_UTF8);
    fprintf(stderr, "%s\n", message);
    fflush(stderr);
    SetConsoleOutputCP(codepage);
}

/* Windows sait si une touche est enfoncée en ce moment même. Une action
 * reste tenue tant qu'UNE de ses touches l'est : Z et ↑ font avancer tous
 * les deux, relâcher l'une ne doit pas arrêter le joueur. La liste reprend
 * les touches de action_from_key, à garder à jour avec elle. */
static int platform_action_state(int action)
{
    static const WORD keys[] = {'Z', 'W', 'S', 'Q', 'A', 'D', VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT, VK_RETURN, VK_SPACE};
    for (size_t i = 0; i < sizeof keys / sizeof keys[0]; i++)
        if (action_from_key(keys[i]) == action && (GetAsyncKeyState(keys[i]) & 0x8000))
            return 1;
    return 0;
}

#else /* Linux, macOS */

static struct termios saved_termios;

static void on_signal(int sig)
{
    (void)sig;
    quit_requested = 1;
}

static void platform_init(void)
{
    if (tcgetattr(STDIN_FILENO, &saved_termios) != 0)
        die("Neon Maze doit être lancé dans un terminal.");
    /* Clavier brut : pas d'écho, pas d'attente de la touche Entrée, Ctrl+C
     * arrive comme une touche normale, et read() ne bloque jamais. */
    struct termios raw = saved_termios;
    raw.c_lflag &= ~(tcflag_t)(ECHO | ICANON | ISIG | IEXTEN);
    raw.c_iflag &= ~(tcflag_t)(IXON | ICRNL);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGHUP, on_signal);
}

static void platform_restore(void)
{
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &saved_termios);
}

static double platform_now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

static void platform_sleep(double seconds)
{
    struct timespec t;
    t.tv_sec = (time_t)seconds;
    t.tv_nsec = (long)((seconds - (double)t.tv_sec) * 1e9);
    nanosleep(&t, NULL);
}

static void platform_terminal_size(int *cols, int *rows)
{
    struct winsize ws;
    *cols = 80;
    *rows = 24;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
    }
}

static int action_from_key(int c)
{
    switch (c) {
    case 'z': case 'w': return ACT_FORWARD; /* AZERTY et QWERTY */
    case 's':           return ACT_BACKWARD;
    case 'q': case 'a': return ACT_STRAFE_LEFT;
    case 'd':           return ACT_STRAFE_RIGHT;
    case '\r': case '\n': case ' ': return ACT_CONFIRM;
    default:            return ACT_NONE;
    }
}

/* Les flèches arrivent en trois octets : Échap, '[' (ou 'O'), puis A à D. */
static int action_from_arrow(int c)
{
    switch (c) {
    case 'A': return ACT_FORWARD;
    case 'B': return ACT_BACKWARD;
    case 'C': return ACT_TURN_RIGHT;
    case 'D': return ACT_TURN_LEFT;
    default:  return ACT_NONE;
    }
}

/* Octets reçus mais pas encore traités : une flèche peut arriver en
 * plusieurs morceaux, on garde le début en attendant la suite. */
static unsigned char input[256];
static size_t input_len = 0;

static void platform_poll_input(double now)
{
    bool received = false;
    ssize_t n;
    while (input_len < sizeof input && (n = read(STDIN_FILENO, input + input_len, sizeof input - input_len)) > 0) {
        input_len += (size_t)n;
        received = true;
    }

    size_t i = 0;
    while (i < input_len) {
        int c = input[i];
        if (c == 27) {
            bool arrow_start = i + 1 < input_len && (input[i + 1] == '[' || input[i + 1] == 'O');
            if (i + 1 == input_len || (arrow_start && i + 2 == input_len)) {
                /* Peut-être le début d'une flèche : on attend l'image suivante.
                 * Si rien de plus n'est arrivé d'ici là, c'était Échap seul. */
                if (received)
                    break;
                if (i + 1 == input_len)
                    quit_requested = 1;
                i = input_len;
                break;
            }
            if (arrow_start) {
                key_press(action_from_arrow(input[i + 2]), now);
                i += 3;
            } else {
                i += 2; /* Échap suivi d'une touche : c'est Alt + touche, ignorée */
            }
        } else {
            if (c == 3) /* Ctrl+C */
                quit_requested = 1;
            else
                key_press(action_from_key(tolower(c)), now);
            i++;
        }
    }
    memmove(input, input + i, input_len - i);
    input_len -= i;
}

static void platform_print_error(const char *message)
{
    fprintf(stderr, "%s\n", message);
}

/* Un terminal Linux ou macOS ne dit pas si une touche est enfoncée. */
static int platform_action_state(int action)
{
    (void)action;
    return -1;
}

#endif

/* ===================================================================
 * 8. CLAVIER : quelles actions sont tenues en ce moment
 * =================================================================== */

static bool action_held[ACT_COUNT];
static bool confirm_pressed = false;        /* Entrée ou Espace vient d'être enfoncée */
static double action_release_at[ACT_COUNT]; /* si le système ne sait pas le dire */

/* Appelée par la plateforme à chaque appui (ou répétition automatique). */
static void key_press(int action, double now)
{
    if (action == ACT_NONE)
        return;
    /* Seulement au premier appui : tenir la touche ne doit pas valider deux fois. */
    if (action == ACT_CONFIRM && !action_held[action])
        confirm_pressed = true;
    action_release_at[action] = now + (action_held[action] ? KEY_REPEAT_HOLD : KEY_FIRST_HOLD);
    action_held[action] = true;
}

/* Relâche les actions dont aucune touche n'est plus enfoncée. */
static void update_held_actions(double now)
{
    for (int a = 0; a < ACT_COUNT; a++) {
        if (!action_held[a])
            continue;
        int state = platform_action_state(a);
        if (state == 0 || (state == -1 && now >= action_release_at[a]))
            action_held[a] = false;
    }
}

/* ===================================================================
 * 9. RESTAURATION DU TERMINAL
 * =================================================================== */

/* Vrai tant que le terminal est en mode jeu (clavier brut, écran alternatif). */
static bool terminal_in_game_mode = false;

/* Enregistrée avec atexit() : appelée automatiquement à la sortie du
 * programme. Elle ne fait rien si le terminal est déjà restauré. */
static void restore_terminal(void)
{
    if (!terminal_in_game_mode)
        return;
    terminal_in_game_mode = false;
    /* couleurs par défaut, curseur visible, retour à l'écran normal */
    fputs("\x1b[0m\x1b[?25h\x1b[?1049l", stdout);
    fflush(stdout);
    platform_restore();
}

/* Quitte sur une erreur. Le terminal est restauré AVANT d'écrire le
 * message : sinon la sortie de l'écran alternatif l'effacerait aussitôt. */
static void die(const char *message)
{
    restore_terminal();
    platform_print_error(message);
    exit(1);
}

/* ===================================================================
 * 10. BOUCLE PRINCIPALE
 * =================================================================== */

/* Ce que toucherait le joueur centré en (x, y). On teste les quatre coins
 * du petit carré qui l'entoure (i va de 0 à 3 : « i & 1 » choisit la
 * gauche ou la droite, « i & 2 » le haut ou le bas). Renvoie ' ' si rien
 * n'est touché ; la sortie passe en priorité, pour ne pas la rater dans un coin. */
static char touched_cell(double x, double y)
{
    const double r = PLAYER_RADIUS;
    char found = ' ';
    for (int i = 0; i < 4; i++) {
        char c = map_at((int)floor(x + (i & 1 ? r : -r)), (int)floor(y + (i & 2 ? r : -r)));
        if (c == 'X')
            return 'X';
        if (c != ' ')
            found = c;
    }
    return found;
}

/* Tourne un vecteur (x, y) d'un angle en radians. */
static void rotate(double *x, double *y, double angle)
{
    double old_x = *x;
    *x = old_x * cos(angle) - *y * sin(angle);
    *y = old_x * sin(angle) + *y * cos(angle);
}

/* Tourner = faire pivoter le regard ET le plan caméra du même angle. */
static void turn_player(struct Player *p, double angle)
{
    rotate(&p->dir_x, &p->dir_y, angle);
    rotate(&p->plane_x, &p->plane_y, angle);
}

/* Déplace le joueur selon les touches tenues. Renvoie vrai s'il touche la sortie. */
static bool move_player(struct Player *p, double dt)
{
    double turn = (action_held[ACT_TURN_RIGHT] ? 1 : 0) - (action_held[ACT_TURN_LEFT] ? 1 : 0);
    double forward = (action_held[ACT_FORWARD] ? 1 : 0) - (action_held[ACT_BACKWARD] ? 1 : 0);
    double strafe = (action_held[ACT_STRAFE_RIGHT] ? 1 : 0) - (action_held[ACT_STRAFE_LEFT] ? 1 : 0);

    if (turn != 0)
        turn_player(p, turn * TURN_SPEED * dt);

    /* La droite du joueur est son regard tourné d'un quart de tour : (-dir_y, dir_x). */
    double dx = p->dir_x * forward - p->dir_y * strafe;
    double dy = p->dir_y * forward + p->dir_x * strafe;
    double len = sqrt(dx * dx + dy * dy);
    if (len == 0)
        return false;
    dx = dx / len * MOVE_SPEED * dt; /* même vitesse en diagonale */
    dy = dy / len * MOVE_SPEED * dt;

    /* x puis y séparément : contre un mur, on glisse le long au lieu de coller. */
    bool reached_exit = false;
    char hit = touched_cell(p->x + dx, p->y);
    if (hit == ' ')
        p->x += dx;
    reached_exit |= hit == 'X';
    hit = touched_cell(p->x, p->y + dy);
    if (hit == ' ')
        p->y += dy;
    reached_exit |= hit == 'X';
    return reached_exit;
}

/* Le joueur au départ, au centre de la case (1, 1), le regard tourné vers
 * le couloir ouvert (à droite, sinon en bas). */
static void reset_player(struct Player *p)
{
    bool right_open = map_at(2, 1) == ' ';
    p->x = 1.5;
    p->y = 1.5;
    p->dir_x = right_open ? 1 : 0;
    p->dir_y = right_open ? 0 : 1;
    /* le plan caméra : le regard tourné d'un quart de tour, de largeur FOV_PLANE */
    p->plane_x = -p->dir_y * FOV_PLANE;
    p->plane_y = p->dir_x * FOV_PLANE;
}

static void start_game(struct Game *g, double now)
{
    reset_player(&g->player);
    memset(seen, 0, sizeof seen); /* la minimap repart dans le noir */
    g->screen = SCREEN_PLAY;
    g->start_time = now;
}

/* Largeur d'un texte à l'écran. En UTF-8, une lettre accentuée prend
 * plusieurs octets mais une seule case : on ne compte que les octets qui
 * commencent un caractère (tous sauf ceux de la forme 10xxxxxx). */
static int text_width(const char *s)
{
    int w = 0;
    for (; *s; s++)
        if (((unsigned char)*s & 0xC0) != 0x80)
            w++;
    return w;
}

#define TEXT_STYLE "\x1b[48;2;0;0;0m\x1b[38;2;0;255;255m" /* texte cyan sur fond noir */

/* Écrit une ligne de texte centrée sur la ligne « row » du terminal (1 = en haut). */
static void overlay_text(int row, const char *text)
{
    int w = text_width(text);
    if (w <= screen_w && row >= 1 && row <= screen_h / 2)
        out_printf("\x1b[%d;%dH" TEXT_STYLE "%s\x1b[0m", row, (screen_w - w) / 2 + 1, text);
}

/* Les textes par-dessus l'image : consignes, chrono, record. */
static void draw_texts(const struct Game *g, double now, double fps)
{
    int row = screen_h / 2 * 7 / 10; /* aux 7/10 de la hauteur, sous les titres */
    char line[160], time_text[32];

    switch (g->screen) {
    case SCREEN_TITLE:
        overlay_text(row, " Trouve la sortie dorée, le plus vite possible ");
        overlay_text(row + 2, " ZQSD, WASD ou flèches : se déplacer ");
        overlay_text(row + 4, " Entrée : jouer    Échap : quitter ");
        break;
    case SCREEN_PLAY:
        /* le bandeau d'infos, en haut à gauche, seulement s'il tient */
        format_time(time_text, sizeof time_text, now - g->start_time);
        snprintf(line, sizeof line, " NEON MAZE | %s | %.0f images/s | Échap : quitter ", time_text, fps);
        if (text_width(line) <= screen_w)
            out_printf("\x1b[H" TEXT_STYLE "%s\x1b[0m", line);
        break;
    case SCREEN_WON:
        format_time(time_text, sizeof time_text, g->best);
        snprintf(line, sizeof line, " Record : %s ", time_text);
        overlay_text(row, line);
        overlay_text(row + 2, " Entrée : nouveau labyrinthe    Échap : quitter ");
        break;
    }
}

int main(int argc, char **argv)
{
    struct Game game = {.screen = SCREEN_TITLE, .maze_size = MAZE_SIZE};
    if (argc > 1) {
        /* strtol lit un nombre et dit où il s'arrête : « 12abc » est refusé. */
        char *end;
        long size = strtol(argv[1], &end, 10);
        if (argc > 2 || *end != '\0' || size < MAZE_MIN || size > MAZE_MAX) {
            char usage[512];
            snprintf(usage, sizeof usage, "Usage : %s [taille du labyrinthe, de %d à %d]", argv[0], MAZE_MIN, MAZE_MAX);
            die(usage);
        }
        game.maze_size = (int)size;
    }
    srand((unsigned)time(NULL)); /* l'heure comme graine : un labyrinthe différent à chaque lancement */
    generate_maze(game.maze_size);
    reset_player(&game.player);

    platform_init();
    terminal_in_game_mode = true;
    atexit(restore_terminal);
    /* écran alternatif (le terminal retrouve son contenu en quittant), curseur caché */
    fputs("\x1b[?1049h\x1b[?25l\x1b[2J", stdout);

    double previous = platform_now();
    double next_frame = previous; /* heure prévue de la prochaine image */
    double fps_timer = previous, fps = 0.0;
    int frames = 0;

    while (!quit_requested) {
        double now = platform_now();
        double dt = now - previous; /* temps écoulé depuis l'image précédente */
        previous = now;
        if (dt > 0.1) /* après un gel (fenêtre déplacée…), pas de bond à travers les murs */
            dt = 0.1;
        game.clock = now;

        platform_poll_input(now);
        update_held_actions(now);
        bool confirm = confirm_pressed;
        confirm_pressed = false;

        switch (game.screen) {
        case SCREEN_TITLE:
            turn_player(&game.player, 0.25 * dt); /* la caméra tourne lentement sur elle-même */
            if (confirm)
                start_game(&game, now);
            break;
        case SCREEN_PLAY:
            if (move_player(&game.player, dt)) {
                game.elapsed = now - game.start_time;
                if (game.best == 0 || game.elapsed < game.best)
                    game.best = game.elapsed;
                game.screen = SCREEN_WON;
            }
            break;
        case SCREEN_WON:
            turn_player(&game.player, 0.25 * dt);
            if (confirm) {
                generate_maze(game.maze_size);
                start_game(&game, now);
            }
            break;
        }

        /* Début d'image « synchronisée » : le terminal attend la fin avant
         * d'afficher, l'image n'apparaît jamais à moitié dessinée (ni
         * l'écran à moitié effacé). Les terminaux qui ne connaissent pas
         * cette séquence l'ignorent. */
        out_printf("\x1b[?2026h");
        int cols, rows;
        platform_terminal_size(&cols, &rows);
        if (cols != screen_w || rows * 2 != screen_h)
            out_printf("\x1b[2J"); /* taille changée : on efface tout */
        resize_screen(cols, rows);

        render_scene(&game);
        draw_pixels();
        draw_texts(&game, now, fps);
        out_printf("\x1b[?2026l"); /* fin de l'image synchronisée : le terminal peut afficher */
        out_flush();

        frames++;
        if (now - fps_timer >= 1.0) {
            fps = frames / (now - fps_timer);
            frames = 0;
            fps_timer = now;
        }

        /* Horaire fixe : une image tous les 1/60 s. Un petit retard est
         * rattrapé à l'image suivante ; un gros retard remet l'horaire à zéro. */
        next_frame += 1.0 / TARGET_FPS;
        double wait = next_frame - platform_now();
        if (wait > 0)
            platform_sleep(wait);
        else if (wait < -0.1)
            next_frame = platform_now();
    }

    free(pixels);
    free(light);
    free(light_tmp);
    free(out_data);
    return 0; /* restore_terminal() est appelée ici, grâce à atexit() */
}
