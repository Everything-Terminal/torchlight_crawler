#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <math.h>
#include <time.h>
#include <signal.h>
#include <stdarg.h>

/* Map is generated fresh each run at a random size within these bounds,
 * and displayed through a fixed-size scrolling viewport centered on the
 * player. This is why "map" (full generated dungeon) and "view" (what's
 * actually drawn to the terminal) are different sizes throughout. */
#define MAP_MAXW 120
#define MAP_MAXH 44
#define MAP_MINW 80
#define MAP_MINH 28

#define VIEW_W 60
#define VIEW_H 20

#define MAX_ROOMS 20
#define MAX_ACTORS 96
#define TORCH_RADIUS 6.5f

#define RESET         "\033[0m"
#define BOLD          "\033[1m"
#define CURSOR_HOME   "\033[H"
#define HIDE_CURSOR   "\033[?25l"
#define SHOW_CURSOR   "\033[?25h"

typedef enum { ACTOR_NONE, ACTOR_PLAYER, ACTOR_GOBLIN, ACTOR_POTION, ACTOR_AMULET } ActorType;

typedef struct {
    ActorType type;
    int x, y;
    int hp;
    char ch;
    int r, g, b;
} Actor;

typedef struct {
    int x, y, w, h;
} Room;

/* Terminal & Global State */

static struct termios orig_termios;
static volatile int running = 1;
static char message[128] = "Find the Amulet of Yendor...";

static void disable_raw_mode(void) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
    printf(SHOW_CURSOR);
}

static void enable_raw_mode(void) {
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(disable_raw_mode);
    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    printf(HIDE_CURSOR);
}

static void on_sigint(int s) { (void)s; running = 0; }

static int clamp255(int v) {
    if (v < 0) return 0;
    if (v > 255) return 255;
    return v;
}

static int clampi(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/* Map & Actors */

char map[MAP_MAXH][MAP_MAXW];
int map_w = 0, map_h = 0;

Room rooms[MAX_ROOMS];
int room_count  = 0;

Actor actors[MAX_ACTORS];
int actor_count = 0;
Actor *player = NULL;

static void room_center(const Room *r, int *cx, int *cy) {
    *cx = r->x + r->w / 2;
    *cy = r->y + r->h / 2;
}

static int rooms_overlap(const Room *a, const Room *b) {
    /* Padding of 1 tile so rooms never end up wall-to-wall. */
    return !(a->x + a->w + 1 <= b->x || b->x + b->w + 1 <= a->x ||
              a->y + a->h + 1 <= b->y || b->y + b->h + 1 <= a->y);
}

static void carve_room(const Room *r) {
    for (int y = r->y; y < r->y + r->h; y++)
        for (int x = r->x; x < r->x + r->w; x++)
            map[y][x] = '.';
}

static void carve_h_corridor(int x1, int x2, int y) {
    int lo = x1 < x2 ? x1 : x2;
    int hi = x1 < x2 ? x2 : x1;
    for (int x = lo; x <= hi; x++) map[y][x] = '.';
}

static void carve_v_corridor(int y1, int y2, int x) {
    int lo = y1 < y2 ? y1 : y2;
    int hi = y1 < y2 ? y2 : y1;
    for (int y = lo; y <= hi; y++) map[y][x] = '.';
}

/* Carves a random dungeon of rooms connected by L-shaped corridors into
 * map[][], picking a fresh random size each call. Retries the whole
 * layout if room placement got unlucky and produced too few rooms. */
static void generate_dungeon(void) {
    do {
        map_w = MAP_MINW + rand() % (MAP_MAXW - MAP_MINW + 1);
        map_h = MAP_MINH + rand() % (MAP_MAXH - MAP_MINH + 1);

        for (int y = 0; y < map_h; y++)
            for (int x = 0; x < map_w; x++)
                map[y][x] = '#';

        room_count = 0;
        int tries = 0;
        while (room_count < MAX_ROOMS && tries < 400) {
            tries++;
            int w = 4 + rand() % 7;
            int h = 3 + rand() % 4;
            int x = 1 + rand() % (map_w - w - 2);
            int y = 1 + rand() % (map_h - h - 2);
            Room r = { x, y, w, h};

            int ok = 1;
            for (int i = 0; i < room_count; i++) {
                if (rooms_overlap(&r, &rooms[i])) { ok = 0; break; }
            }
            if (!ok) continue;

            rooms[room_count++] = r;
            carve_room(&r);

            if (room_count > 1) {
                int cx1, cy1, cx2, cy2;
                room_center(&rooms[room_count - 2], &cx1, &cy1);
                room_center(&rooms[room_count - 1], &cx2, &cy2);
                if (rand() % 2) {
                    carve_h_corridor(cx1, cx2, cy1);
                    carve_v_corridor(cy1, cy2, cx2);
                } else {
                    carve_v_corridor(cy1, cy2, cx1);
                    carve_h_corridor(cx1, cx2, cy2);
                }
            }
        }
    } while (room_count < 4);
}

Actor* get_actor_at(int x, int y) {
    for (int i = 0; i < actor_count; i++) {
        if (actors[i].hp > 0 && actors[i].x == x && actors[i].y == y) return &actors[i];
    }
    return NULL;
}

static Actor* spawn_actor(ActorType type, int x, int y, int hp, char ch, int r, int g, int b) {
    if (actor_count >= MAX_ACTORS) return NULL;
    Actor *a = &actors[actor_count++];
    a->type = type;
    a->x = x; a->y = y;
    a->hp = hp;
    a->ch = ch;
    a->r = r; a->g = g; a->b = b;
    return a;
}

/* Finds a free floor tile inside room i (not on its outer edge, and not
 * already occupied by another actor). Falls back to the room's center
 * if it can't find one quickly, so placement never fails outright. */
static void free_spot_in_room(int room_idx, int *ox, int *oy) {
    const Room *r = &rooms[room_idx];
    for (int tries = 0; tries < 20; tries++) {
        int x = r->x + 1 + rand() % (r->w > 2 ? r->w - 2 : 1);
        int y = r->y + 1 + rand() % (r->h > 2 ? r->h - 2 : 1);
        if (!get_actor_at(x, y)) { *ox = x; *oy = y; return; }
    }
    room_center(r, ox, oy);
}

void init_game(void) {
    generate_dungeon();
    actor_count = 0;
    player = NULL;

    int px, py;
    room_center(&rooms[0], &px, &py);
    player = spawn_actor(ACTOR_PLAYER, px, py, 10, '@', 255, 255, 150);

    /* Amulet goes in whichever room is farthest from the start, so it's
     * always a real trek across the generated dungeon. */
    int farthest = 1, farthest_d2 = -1;
    for (int i = 1; i < room_count; i++) {
        int cx, cy;
        room_center(&rooms[i], &cx, &cy);
        int dx = cx - px, dy = cy - py;
        int d2 = dx * dx + dy * dy;
        if (d2 > farthest_d2) { farthest_d2 = d2; farthest = i; }
    }
    int ax, ay;
    free_spot_in_room(farthest, &ax, &ay);
    spawn_actor(ACTOR_AMULET, ax, ay, 1, '*', 100, 255, 255);

    int potion_count = clampi(2 + room_count / 5, 2, 6);
    for (int i = 0; i < potion_count; i++) {
        int ridx = 1 + rand() % (room_count - 1);
        int x, y;
        free_spot_in_room(ridx, &x, &y);
        if (!get_actor_at(x, y)) spawn_actor(ACTOR_POTION, x, y, 1, '!', 255, 100, 200);
    }
    int goblin_count = clampi(room_count + rand() % 4, 4, MAX_ACTORS - actor_count - 1);
    for (int i = 0; i < goblin_count; i++) {
        int ridx = 1 + rand() % (room_count - 1);
        int x, y;
        free_spot_in_room(ridx, &x, &y);
        if (!get_actor_at(x, y)) spawn_actor(ACTOR_GOBLIN, x, y, 3, 'g', 180, 40, 40);
    }
}

/* Game Logic */

void try_move_player(int dx, int dy) {
    if (!player) return;
    int nx = player->x + dx;
    int ny = player->y + dy;

    if (nx < 0 || nx >= map_w || ny < 0 || ny >= map_h) return;
    if (map[ny][nx] == '#') return;

    Actor *target = get_actor_at(nx, ny);
    if (target) {
        if (target->type == ACTOR_GOBLIN) {
            target->hp--;
            if (target->hp <= 0) {
                snprintf(message, sizeof(message), "You slew the goblin!");
            } else {
                snprintf(message, sizeof(message), "You hit the goblin! (%d HP left)", target->hp);
            }
        } else if (target->type == ACTOR_POTION) {
            player->hp = 10;
            target->hp = 0;
            snprintf(message, sizeof(message), "You drink a potion. Health restored!");
        } else if (target->type == ACTOR_AMULET) {
            target->hp = 0;
            snprintf(message, sizeof(message), "YOU FOUND THE AMULET! YOU WIN!");
        }
    } else {
        player->x = nx;
        player->y = ny;
    }
}

void enemy_turn(void) {
    for (int i = 0; i < actor_count; i++) {
        Actor *a = &actors[i];
        if (a->type != ACTOR_GOBLIN || a->hp <= 0 || !player) continue;

        float dx = (float)(player->x - a->x);
        float dy = (float)(player->y - a->y);
        float dist = sqrtf(dx * dx + dy * dy);

        if (dist > TORCH_RADIUS + 2.0f) continue;

        if (dist <= 1.5f) {
            player->hp--;
            snprintf(message, sizeof(message), "The goblin hits you! (%d HP left)", player->hp);
            continue;
        }

        int mx = (dx > 0) ? 1 : (dx < 0 ? -1 : 0);
        int my = (dy > 0) ? 1 : (dy < 0 ? -1 : 0);

        if (abs((int)dx) > abs((int)dy)) my = 0;
        else mx = 0;

        int nx = a->x + mx;
        int ny = a->y + my;

        if (nx < 0 || nx >= map_w || ny < 0 || ny >= map_h) continue;
        if (map[ny][nx] != '#' && !get_actor_at(nx, ny)) {
            a->x = nx;
            a->y = ny;
        }
    }
}

/* Rendering */

/* Buffer sized with headroom: worst case is one truecolor escape
 * sequece (~20 bytes) per visible viewport cell plus the HUD/footer text. */
#define RENDER_BUF_SIZE (VIEW_W * VIEW_H * 24 + 4096)

/* Wrapper around snprintf that tracks remaining space and never
 * writes past the end of buf, even if the terminal or map grows. */
static void buf_append(char *buf, size_t bufsize, int *p, const char *fmt, ...) {
    if (*p < 0 || (size_t)*p >= bufsize) return;
    va_list args;
    va_start(args, fmt);
    int written = vsnprintf(buf + *p, bufsize - (size_t)*p, fmt, args);
    va_end(args);
    if (written > 0) {
        *p += written;
        if ((size_t)*p > bufsize - 1) *p = (int)bufsize - 1;
    }
}

void render(void) {
    static char buf[RENDER_BUF_SIZE];
    int p = 0;
    buf_append(buf, sizeof(buf), &p, CURSOR_HOME);

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    float time_sec = (float)ts.tv_sec + (float)ts.tv_nsec / 1e9f;
    float flicker = 0.9f + 0.1f * sinf(time_sec * 12.0f);

    /* HUD Top */
    buf_append(buf, sizeof(buf), &p, "\033[1;38;2;255;50;50m HP: [");
    for (int i = 0; i < 10; i++) {
        buf_append(buf, sizeof(buf), &p,
            i < player->hp ? "\033[38;2;255;80;80m█" : "\033[38;2;80;20;20m░");
    }
    buf_append(buf, sizeof(buf), &p, "\033[0m\033[1;38;2;200;200;200m] %s \033[0m\033[K\n", message);

    /* Camera follows the player, clamped so the viewport never scrolls
     * past the edges of the generated map. */
    int cam_x = clampi(player->x - VIEW_W / 2, 0, map_w - VIEW_W);
    int cam_y = clampi(player->y - VIEW_H / 2, 0, map_h - VIEW_H);
    if (map_w <= VIEW_W) cam_x = 0;
    if (map_h <= VIEW_H) cam_y = 0;

    /* Map viewport */
    for (int vy = 0; vy < VIEW_H; vy++) {
        int wy = cam_y + vy;
        for (int vx = 0; vx < VIEW_W; vx++) {
            int wx = cam_x + vx;

            if (wx >= map_w || wy >= map_h) {
                buf_append(buf, sizeof(buf), &p, " ");
                continue;
            }

            char ch = map[wy][wx];
            int r = 40, g = 40, b = 45;

            if (ch == '#') { r = 80; g = 80; b = 90; }
            else { r = 30; g = 25; b = 20; }

            Actor *a = get_actor_at(wx, wy);
            if (a) {
                ch = a->ch;
                r = a->r;
                g = a->g;
                b = a->b;
            }

            float light = 0.0f;
            if (player) {
                float dx = (float)(wx - player->x);
                float dy = (float)(wy - player->y);
                float dist = sqrtf(dx * dx + dy * dy);
                light = 1.0f - (dist / (TORCH_RADIUS * flicker));
                if (light < 0.0f) light = 0.0f;
                if (light > 1.0f) light = 1.0f;
            }

            int fr = (int)(r * light + 60 * light);
            int fg = (int)(g * light + 20 * light);
            int fb = (int)(b * light);

            fr = clamp255(fr);
            fg = clamp255(fg);
            fb = clamp255(fb);

            if (a && a->type == ACTOR_PLAYER) {
                fr = 255;
                fg = 255;
                fb = 150;
            }

            if (light > 0.01f || (a && a->type == ACTOR_PLAYER)) {
                buf_append(buf, sizeof(buf), &p, "\033[38;2;%d;%d;%dm%c", fr, fg, fb, ch);
            } else {
                buf_append(buf, sizeof(buf), &p, " ");
            }
        }
        buf_append(buf, sizeof(buf), &p, "\n");
    }

    buf_append(buf, sizeof(buf), &p, "\033[38;2;120;120;130m hjkl/arrows: move | q: quit\033[0m");
    fputs(buf, stdout);
    fflush(stdout);
}

/* Main */

int main(void) {
    struct sigaction sa;
    sa.sa_handler = on_sigint;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);

    srand((unsigned int)time(NULL));
    enable_raw_mode();
    printf("\033[2J");

    init_game();

    while (running && player && player->hp > 0) {
        render();

        char c;
        if (read(STDIN_FILENO, &c, 1) == 1) {
            int dx = 0, dy = 0;

            if (c == 'q') break;
            else if (c == 'h' || c == 'D') dx = -1;
            else if (c == 'l' || c == 'C') dx = 1;
            else if (c == 'k' || c == 'A') dy = -1;
            else if (c == 'j' || c == 'B') dy = 1;

            if (c == 27) {
                char seq[2];
                if (read(STDIN_FILENO, &seq[0], 1) == 1 && read(STDIN_FILENO, &seq[1], 1) == 1) {
                    if (seq[0] == '[') {
                        if (seq[1] == 'A') dy = -1;
                        else if (seq[1] == 'B') dy = 1;
                        else if (seq[1] == 'C') dx = 1;
                        else if (seq[1] == 'D') dx = -1;
                    }
                }
            }

            if (dx != 0 || dy != 0) {
                try_move_player(dx, dy);
                if (player->hp > 0) {
                    enemy_turn();
                }
            }
        }

        if (player && player->hp <= 0) {
            snprintf(message, sizeof(message), "You died in the dark...");
            render();
            sleep(2);
            break;
        }

        int amulet_gone = 1;
        for (int i = 0; i < actor_count; i++) {
            if (actors[i].type == ACTOR_AMULET && actors[i].hp > 0) {
                amulet_gone = 0;
                break;
            }
        }
        if (amulet_gone) {
            render();
            sleep(2);
            break;
        }
    }

    printf("\033[2J\033[H\033[?25h");
    printf("Thanks for playing Torchlight Crawler!\n");
    return 0;
}
