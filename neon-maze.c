/*
 * Neon Maze - un labyrinthe en 3D dans le terminal.
 * C99, un seul fichier, aucune dépendance.
 *
 * Compiler : gcc -std=c99 -O2 -Wall -Wextra -Wpedantic -o neon-maze neon-maze.c -lm
 * Jouer    : ./neon-maze
 */

/* Avec -std=c99, Linux et macOS masquent les fonctions POSIX (termios,
 * clock_gettime, nanosleep). Ces deux lignes les rendent visibles.
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

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#endif

/* ===================================================================
 * 1. RÉGLAGES
 * =================================================================== */

#define TARGET_FPS 60

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
    ACT_COUNT, /* nombre d'actions : toujours en dernier */
    ACT_NONE = -1
};

/* Une couleur 24 bits : rouge, vert, bleu, chacun de 0 à 255. */
struct Color {
    unsigned char r, g, b;
};

/* Mis à 1 pour quitter (Échap, Ctrl+C, fermeture). « volatile sig_atomic_t »
 * est le seul type qu'un gestionnaire de signal a le droit de modifier. */
static volatile sig_atomic_t quit_requested = 0;

/* Prototypes : la section clavier et la section plateforme s'appellent
 * l'une l'autre, le compilateur doit connaître ces fonctions à l'avance. */
static void key_press(int action, int code, double now);
static int platform_key_state(int code);

/* ===================================================================
 * 4. TAMPON D'IMAGE : un pixel = une couleur
 * =================================================================== */

/* L'image est un tableau de screen_w * screen_h couleurs, rangées ligne
 * par ligne : le pixel (x, y) est pixels[y * screen_w + x].
 * Chaque caractère du terminal affiche deux pixels l'un au-dessus de
 * l'autre, d'où une hauteur égale à deux fois le nombre de lignes. */
static struct Color *pixels = NULL;
static int screen_w = 0, screen_h = 0;

/* (Ré)alloue l'image si la taille du terminal a changé. */
static void resize_screen(int cols, int rows)
{
    if (cols < 1)
        cols = 1;
    if (rows < 1)
        rows = 1;
    if (cols == screen_w && rows * 2 == screen_h)
        return;
    free(pixels); /* free(NULL) ne fait rien : sûr au premier appel */
    pixels = malloc(sizeof *pixels * (size_t)cols * (size_t)rows * 2);
    if (!pixels) {
        fputs("Plus de mémoire.\n", stderr);
        exit(1);
    }
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
    if (!bigger) {
        fputs("Plus de mémoire.\n", stderr);
        exit(1);
    }
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

static bool same_color(struct Color a, struct Color b)
{
    return a.r == b.r && a.g == b.g && a.b == b.b;
}

/* Convertit l'image en texte. Chaque caractère est un demi-bloc ▀ : sa
 * couleur de texte peint le pixel du haut, sa couleur de fond celui du bas.
 * On ne réécrit une couleur que si elle change : souvent, deux caractères
 * voisins ont les mêmes, et le terminal reçoit alors bien moins d'octets. */
static void draw_pixels(void)
{
    struct Color fg = {0, 0, 0}, bg = {0, 0, 0};
    bool first = true;
    /* Début d'image « synchronisée » : le terminal attend la fin avant
     * d'afficher, l'image n'apparaît jamais à moitié dessinée. Les
     * terminaux qui ne connaissent pas cette séquence l'ignorent. */
    out_printf("\x1b[?2026h");
    for (int y = 0; y < screen_h; y += 2) {
        /* curseur au début de la ligne y/2 + 1 (le terminal compte à partir de 1) */
        out_printf("\x1b[%d;1H", y / 2 + 1);
        for (int x = 0; x < screen_w; x++) {
            struct Color top = pixels[y * screen_w + x];
            struct Color bottom = pixels[(y + 1) * screen_w + x];
            if (first || !same_color(top, fg))
                out_color(38, top);
            if (first || !same_color(bottom, bg))
                out_color(48, bottom);
            fg = top;
            bg = bottom;
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
    fwrite(out_data, 1, out_len, stdout);
    fflush(stdout);
    out_len = 0;
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
    (void)type;
    quit_requested = 1;
    return TRUE;
}

static void platform_init(void)
{
    con_in = GetStdHandle(STD_INPUT_HANDLE);
    con_out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!GetConsoleMode(con_in, &saved_in_mode) || !GetConsoleMode(con_out, &saved_out_mode)) {
        fputs("Neon Maze doit être lancé dans un terminal.\n", stderr);
        exit(1);
    }
    saved_codepage = GetConsoleOutputCP();

    SetConsoleOutputCP(CP_UTF8); /* pour le caractère ▀ */
    SetConsoleMode(con_out, saved_out_mode | ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
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
                key_press(action_from_key(key->wVirtualKeyCode), key->wVirtualKeyCode, now);
        }
    }
}

/* Windows sait si une touche est enfoncée en ce moment même. */
static int platform_key_state(int code)
{
    return (GetAsyncKeyState(code) & 0x8000) ? 1 : 0;
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
    if (tcgetattr(STDIN_FILENO, &saved_termios) != 0) {
        fputs("Neon Maze doit être lancé dans un terminal.\n", stderr);
        exit(1);
    }
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

static void platform_poll_input(double now)
{
    unsigned char buf[64];
    ssize_t n;
    while ((n = read(STDIN_FILENO, buf, sizeof buf)) > 0) {
        for (ssize_t i = 0; i < n; i++) {
            int c = buf[i];
            if (c == 27 && i + 2 < n && (buf[i + 1] == '[' || buf[i + 1] == 'O')) {
                key_press(action_from_arrow(buf[i + 2]), 1000 + buf[i + 2], now);
                i += 2;
            } else if (c == 27 || c == 3) { /* Échap seul ou Ctrl+C */
                quit_requested = 1;
            } else {
                key_press(action_from_key(tolower(c)), tolower(c), now);
            }
        }
    }
}

/* Un terminal Linux ou macOS ne dit pas si une touche est enfoncée. */
static int platform_key_state(int code)
{
    (void)code;
    return -1;
}

#endif

/* ===================================================================
 * 8. CLAVIER : quelles actions sont tenues en ce moment
 * =================================================================== */

static bool action_held[ACT_COUNT];
static int action_code[ACT_COUNT];         /* touche qui a déclenché l'action */
static double action_release_at[ACT_COUNT]; /* si le système ne sait pas le dire */

/* Appelée par la plateforme à chaque appui (ou répétition automatique). */
static void key_press(int action, int code, double now)
{
    if (action == ACT_NONE)
        return;
    action_release_at[action] = now + (action_held[action] ? KEY_REPEAT_HOLD : KEY_FIRST_HOLD);
    action_held[action] = true;
    action_code[action] = code;
}

/* Relâche les actions dont la touche n'est plus enfoncée. */
static void update_held_actions(double now)
{
    for (int a = 0; a < ACT_COUNT; a++) {
        if (!action_held[a])
            continue;
        int state = platform_key_state(action_code[a]);
        if (state == 0 || (state == -1 && now >= action_release_at[a]))
            action_held[a] = false;
    }
}

/* ===================================================================
 * 9. RESTAURATION DU TERMINAL
 * =================================================================== */

/* Enregistrée avec atexit() : appelée automatiquement à la sortie du programme. */
static void restore_terminal(void)
{
    /* couleurs par défaut, curseur visible, retour à l'écran normal */
    fputs("\x1b[0m\x1b[?25h\x1b[?1049l", stdout);
    fflush(stdout);
    platform_restore();
}

/* ===================================================================
 * 10. BOUCLE PRINCIPALE
 * =================================================================== */

int main(void)
{
    platform_init();
    atexit(restore_terminal);
    /* écran alternatif (le terminal retrouve son contenu en quittant), curseur caché */
    fputs("\x1b[?1049h\x1b[?25l\x1b[2J", stdout);

    double previous = platform_now();
    double next_frame = previous; /* heure prévue de la prochaine image */
    double fps_timer = previous, fps = 0.0;
    int frames = 0;
    double t = 0.0; /* temps de l'animation, en secondes */

    while (!quit_requested) {
        double now = platform_now();
        double dt = now - previous; /* temps écoulé depuis l'image précédente */
        previous = now;
        t += dt;

        platform_poll_input(now);
        update_held_actions(now);

        int cols, rows;
        platform_terminal_size(&cols, &rows);
        if (cols != screen_w || rows * 2 != screen_h)
            out_printf("\x1b[2J"); /* taille changée : on efface tout */
        resize_screen(cols, rows);

        /* Test de charge : un « plasma » où chaque pixel change à chaque
         * image, le pire cas pour le terminal. */
        for (int y = 0; y < screen_h; y++) {
            for (int x = 0; x < screen_w; x++) {
                struct Color *p = &pixels[y * screen_w + x];
                p->r = (unsigned char)(127.5 + 127.5 * sin(x * 0.10 + t));
                p->g = (unsigned char)(127.5 + 127.5 * sin(y * 0.15 - t * 1.3));
                p->b = (unsigned char)(127.5 + 127.5 * sin((x + y) * 0.07 + t * 0.7));
            }
        }
        draw_pixels();
        size_t frame_bytes = out_len;
        out_printf("\x1b[H\x1b[48;2;0;0;0m\x1b[38;2;0;255;255m NEON MAZE - test de charge | %dx%d pixels | "
                   "%.0f images/s | %u Ko par image | Echap pour quitter \x1b[0m",
                   screen_w, screen_h, fps, (unsigned)(frame_bytes / 1024));
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
    free(out_data);
    return 0; /* restore_terminal() est appelée ici, grâce à atexit() */
}
