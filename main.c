/*
 * SOBREVIVENDO EM GS - PSP (PSPSDK) - acao e sobrevivencia em mundo aberto 3D
 * Desenvolvido por MECANIC ENGINE GAMER
 *
 * Controles:
 *   Analogico  : andar            L / R : girar camera
 *   X          : interagir (pegar arma, revistar, beber, acampamento, helicoptero)
 *   Quadrado   : atacar (segure para armas automaticas)   Circulo : rolar (esquiva)
 *   Triangulo  : trocar de arma   Esq/Dir : escolher item   Cima : usar item
 *   Start      : menu (salvar/sair)       Select : ajuda
 */
#include <pspkernel.h>
#include <pspdebug.h>
#include <pspdisplay.h>
#include <pspctrl.h>
#include <pspiofilemgr.h>
#include <pspaudio.h>
#include <pspgu.h>
#include <pspgum.h>
#include <math.h>
#include <string.h>
#include <stdio.h>

PSP_MODULE_INFO("SobrevivendoEmGS", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER);

#define BUF_W   512
#define SCR_W   480
#define SCR_H   272
#define FRAME   0x88000
#define PI_F    3.14159265f
#define WORLD   95.0f

typedef struct { float nx, ny, nz, x, y, z; } Vtx;
#define VTX_TYPE (GU_NORMAL_32BITF | GU_VERTEX_32BITF | GU_TRANSFORM_3D)

static unsigned int __attribute__((aligned(16))) list[262144];
static Vtx __attribute__((aligned(16))) cube[36];
#define GTILE 12.0f
#define GN    16
static Vtx __attribute__((aligned(16))) groundA[GN * GN / 2 * 6];
static Vtx __attribute__((aligned(16))) groundB[GN * GN / 2 * 6];

/* ---------- callbacks de saida ---------- */
static volatile int running = 1;
static int exit_cb(int a, int b, void *c) { running = 0; return 0; }
static int cb_thread(SceSize args, void *argp) {
    int id = sceKernelCreateCallback("exit", exit_cb, NULL);
    sceKernelRegisterExitCallback(id);
    sceKernelSleepThreadCB();
    return 0;
}
static void setup_callbacks(void) {
    int t = sceKernelCreateThread("cb", cb_thread, 0x11, 0xFA0, 0, NULL);
    if (t >= 0) sceKernelStartThread(t, 0, NULL);
}

/* ---------- cubo unitario (base em y=0, centrado em x/z) ---------- */
static void build_cube(void) {
    static const float F[6][9] = {
        { 1, 0, 0,   0, 1, 0,   0, 0, 1},   /* +X */
        {-1, 0, 0,   0, 0, 1,   0, 1, 0},   /* -X */
        { 0, 1, 0,   0, 0, 1,   1, 0, 0},   /* +Y */
        { 0,-1, 0,   1, 0, 0,   0, 0, 1},   /* -Y */
        { 0, 0, 1,   1, 0, 0,   0, 1, 0},   /* +Z */
        { 0, 0,-1,   0, 1, 0,   1, 0, 0}    /* -Z */
    };
    static const int S[6][2] = {{-1,-1},{1,-1},{1,1},{-1,-1},{1,1},{-1,1}};
    int i = 0;
    for (int f = 0; f < 6; f++) {
        const float *d = F[f];
        for (int k = 0; k < 6; k++) {
            float su = S[k][0] * 0.5f, sv = S[k][1] * 0.5f;
            cube[i].nx = d[0]; cube[i].ny = d[1]; cube[i].nz = d[2];
            cube[i].x = d[0]*0.5f + d[3]*su + d[6]*sv;
            cube[i].y = 0.5f + d[1]*0.5f + d[4]*su + d[7]*sv;
            cube[i].z = d[2]*0.5f + d[5]*su + d[8]*sv;
            i++;
        }
    }
    sceKernelDcacheWritebackAll();
}

/* chao: ladrilhos pequenos (pecas gigantes bugam no PSP) */
static void build_ground(void) {
    int na = 0, nb = 0;
    for (int i = 0; i < GN; i++) {
        for (int j = 0; j < GN; j++) {
            float x0 = -96.0f + i * GTILE, z0 = -96.0f + j * GTILE;
            float x1 = x0 + GTILE, z1 = z0 + GTILE;
            float xs[6] = { x0, x1, x1, x0, x1, x0 };
            float zs[6] = { z0, z0, z1, z0, z1, z1 };
            Vtx *v = ((i + j) & 1) ? &groundB[nb] : &groundA[na];
            for (int k = 0; k < 6; k++) {
                v[k].nx = 0; v[k].ny = 1; v[k].nz = 0;
                v[k].x = xs[k]; v[k].y = 0; v[k].z = zs[k];
            }
            if ((i + j) & 1) nb += 6; else na += 6;
        }
    }
    sceKernelDcacheWritebackAll();
}

static unsigned int groundC1 = 0xff3c9a3c, groundC2 = 0xff328a34;

static void draw_ground(void) {
    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();
    sceGumUpdateMatrix();
    sceGuColor(groundC1);
    sceGumDrawArray(GU_TRIANGLES, VTX_TYPE, GN * GN / 2 * 6, 0, groundA);
    sceGuColor(groundC2);
    sceGumDrawArray(GU_TRIANGLES, VTX_TYPE, GN * GN / 2 * 6, 0, groundB);
}

/* desenha caixa: posicao (base), escala, rotacao Y, cor ABGR */
static void box(float x, float y, float z, float sx, float sy, float sz,
                float ry, unsigned int color) {
    ScePspFVector3 p = {x, y, z}, s = {sx, sy, sz};
    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();
    sceGumTranslate(&p);
    sceGumRotateY(ry);
    sceGumScale(&s);
    sceGumUpdateMatrix();
    sceGuColor(color);
    sceGumDrawArray(GU_TRIANGLES, VTX_TYPE, 36, 0, cube);
}


/* ---------- audio (musica + efeitos) ---------- */
static volatile int musicOn = 1;
static volatile int sfxQ = 0;
static volatile int songId = 0;   /* 0 menu, 1 dia, 2 noite, 3 combate, 4 final */

static const unsigned char MEL0[][2] = {
    {69,2},{72,2}, {76,2},{72,2}, {65,2},{69,2}, {72,2},{69,2},
    {67,2},{71,2}, {74,2},{71,2}, {64,2},{68,2}, {69,4}
};
static const unsigned char MEL1[][2] = {
    {69,1},{76,1},{72,1},{76,1}, {65,1},{72,1},{69,1},{72,1},
    {67,1},{74,1},{71,1},{74,1}, {64,1},{71,1},{68,1},{71,1},
    {69,1},{76,1},{72,1},{76,1}, {65,1},{72,1},{69,1},{72,1},
    {67,1},{71,1},{74,1},{71,1}, {69,4}
};
static const unsigned char MEL2[][2] = {
    {57,4},{60,4},{53,4},{55,4},{57,4},{64,4},{56,4},{57,4}
};
static const unsigned char MEL3[][2] = {
    {69,1},{69,1},{72,1},{69,1}, {70,1},{70,1},{74,1},{70,1},
    {69,1},{69,1},{72,1},{76,1}, {75,1},{72,1},{70,1},{69,1},
    {69,1},{69,1},{72,1},{69,1}, {70,1},{70,1},{74,1},{70,1},
    {76,1},{75,1},{74,1},{72,1}, {69,4}
};
static const unsigned char MEL4[][2] = {
    {72,1},{76,1},{79,1},{76,1}, {71,1},{74,1},{79,1},{74,1},
    {69,1},{72,1},{76,1},{72,1}, {69,1},{72,1},{77,1},{72,1},
    {72,1},{76,1},{79,1},{84,1}, {83,1},{79,1},{74,1},{79,1},
    {77,1},{76,1},{74,1},{71,1}, {72,4}
};
static const unsigned char BASS_[5][8] = {
    { 45, 45, 41, 41, 43, 43, 40, 45 },
    { 45, 41, 43, 40, 45, 41, 43, 45 },
    { 45, 48, 41, 43, 45, 40, 44, 45 },
    { 45, 46, 45, 43, 45, 46, 40, 45 },
    { 48, 43, 45, 41, 48, 43, 43, 48 }
};
typedef struct { const unsigned char (*mel)[2]; int n; int step; int bassMode; float vol; } Song;
#define NM(a) ((int)(sizeof(a) / sizeof((a)[0])))
static const Song SONGS[5] = {
    { MEL0, NM(MEL0), 18000, 0, 0.20f },
    { MEL1, NM(MEL1), 12500, 0, 0.21f },
    { MEL2, NM(MEL2), 22000, 0, 0.18f },
    { MEL3, NM(MEL3),  7500, 1, 0.24f },
    { MEL4, NM(MEL4), 14000, 0, 0.22f }
};
/* frequencia negativa = ruido */
static const float SFXF[13][3]  = { {0,0,0}, {-1,150,0}, {400,250,0}, {-1,0,0}, {880,1175,0}, {440,0,0}, {120,90,0},
                                    {80,70,0}, {300,450,0}, {523,659,784}, {160,0,0}, {1000,800,1000}, {-1,0,0} };
static const int   SFXMS[13][3] = { {0,0,0}, {60,50,0}, {30,45,0}, {55,0,0}, {60,100,0}, {40,0,0}, {90,90,0},
                                    {200,200,0}, {80,80,0}, {60,60,90}, {180,0,0}, {90,90,90}, {300,0,0} };

/* 1 tiro 2 golpe 3 acerto 4 coleta 5 clique 6 dano 7 gemido 8 rolar 9 abrir 10 erro 11 radio 12 helice */
static void play_sfx(int t) { sfxQ = t; }

static float midi_freq(int m) { return 440.0f * powf(2.0f, (m - 69) / 12.0f); }

static int audio_thread(SceSize args, void *argp) {
    static short buf[1024 * 2] __attribute__((aligned(64)));
    static float mfreq[5][40], bfreq[5][8];
    int ch = sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL, 1024, PSP_AUDIO_FORMAT_STEREO);
    if (ch < 0) return 0;
    for (int sg = 0; sg < 5; sg++) {
        for (int i = 0; i < SONGS[sg].n && i < 40; i++) mfreq[sg][i] = midi_freq(SONGS[sg].mel[i][0]);
        for (int i = 0; i < 8; i++) bfreq[sg][i] = midi_freq(BASS_[sg][i]);
    }
    int cs = 0, mi = 0, mpos = 0, songStep = 0, sType = 0, sPos = 0;
    float mph = 0, bph = 0, sph = 0;
    unsigned int rng = 12345u;
    while (running) {
        int sid = songId;
        if (sid < 0 || sid > 4) sid = 0;
        if (sid != cs) { cs = sid; mi = 0; mpos = 0; songStep = 0; }
        const Song *sg = &SONGS[cs];
        for (int i = 0; i < 1024; i++) {
            float out = 0;
            if (musicOn) {
                int nlen = sg->mel[mi][1] * sg->step;
                float env = 1.0f - 0.6f * ((float)mpos / (float)nlen);
                float att = (mpos < 400) ? (mpos / 400.0f) : 1.0f;
                float bf = bfreq[cs][(songStep / 4) % 8];
                if (sg->bassMode && (songStep & 1)) bf *= 1.5f;
                mph += mfreq[cs][mi] / 44100.0f; if (mph >= 1.0f) mph -= 1.0f;
                bph += bf / 44100.0f; if (bph >= 1.0f) bph -= 1.0f;
                out += (4.0f * fabsf(mph - 0.5f) - 1.0f) * env * att * sg->vol;
                out += (4.0f * fabsf(bph - 0.5f) - 1.0f) * 0.14f;
                if (++mpos >= nlen) {
                    mpos = 0; songStep += sg->mel[mi][1];
                    if (++mi >= sg->n) { mi = 0; songStep = 0; }
                }
            }
            if (sfxQ) { sType = sfxQ; sfxQ = 0; sPos = 0; }
            if (sType) {
                int ms = sPos * 1000 / 44100, acc = 0, seg = -1;
                for (int k = 0; k < 3; k++) {
                    int d = SFXMS[sType][k];
                    if (d == 0) break;
                    if (ms < acc + d) { seg = k; break; }
                    acc += d;
                }
                if (seg < 0) sType = 0;
                else {
                    float f = SFXF[sType][seg];
                    float dec = 1.0f - 0.5f * (float)(ms - acc) / (float)SFXMS[sType][seg];
                    float v;
                    if (f < 0) { rng = rng * 1664525u + 1013904223u; v = ((rng >> 16) & 1) ? 1.0f : -1.0f; }
                    else { sph += f / 44100.0f; if (sph >= 1.0f) sph -= 1.0f; v = (sph < 0.5f) ? 1.0f : -1.0f; }
                    out += v * 0.15f * dec;
                    sPos++;
                }
            }
            float o = out * 30000.0f;
            if (o > 32000.0f) o = 32000.0f;
            if (o < -32000.0f) o = -32000.0f;
            buf[i * 2] = buf[i * 2 + 1] = (short)o;
        }
        sceAudioOutputBlocking(ch, 0x5000, buf);
    }
    sceAudioChRelease(ch);
    return 0;
}

/* ---------- dados do mundo ---------- */
#define NITEMS  10
#define NITEMS_OLD 8
#define NW      8
#define NWP     10
#define NLOOT   64
#define NTREES  220
#define NZ      40
#define NZREG   32
#define NB      13
#define NROADS  8
#define NCARS   10
#define DAY_LEN 360.0f
#define SAFE_R  13.0f
#define BASE_X  0.0f
#define BASE_Z  0.0f
#define HELI_X  11.0f
#define HELI_Z  7.0f
#define LAKE_X  (-48.0f)
#define LAKE_Z  32.0f
#define LAKE_HX 14.0f
#define LAKE_HZ 10.0f

enum { I_MAD, I_SUC, I_PANO, I_COM, I_AGUA, I_BAND, I_MUN, I_PECA, I_CART, I_RIFLE };
static const char *itemName[NITEMS] = { "Madeira", "Sucata", "Pano", "Comida", "Agua", "Bandagem", "Municao", "Peca radio", "Cartuchos", "Balas rifle" };
static const char *wName[NW] = { "Punhos", "Bastao", "Machado", "Facao", "Pistola", "SMG", "Escopeta", "Rifle" };
static const float wDmg[NW]   = { 8.0f, 20.0f, 38.0f, 28.0f, 28.0f, 16.0f, 14.0f, 70.0f };
static const float wRange[NW] = { 2.0f, 2.7f, 2.9f, 2.8f, 26.0f, 24.0f, 12.0f, 40.0f };
static const float wCD[NW]    = { 0.35f, 0.45f, 0.70f, 0.38f, 0.40f, 0.13f, 0.85f, 1.00f };
static const int   wAmmo[NW]  = { -1, -1, -1, -1, I_MUN, I_MUN, I_CART, I_RIFLE };
static const int   wAuto[NW]  = { 0, 0, 0, 0, 0, 1, 0, 0 };
static const int   wGive[NW]  = { 0, 0, 0, 0, 12, 30, 8, 6 };
static const char *survNames[8] = { "Rafa", "Bia", "Leo", "Ana", "Dudu", "Nina", "Caio", "Lia" };
static const char *diffNames[3] = { "Facil", "Normal", "Dificil" };
static const float diffDmg[3]   = { 0.6f, 1.0f, 1.5f };
static const float diffSpd[3]   = { 0.9f, 1.0f, 1.1f };

typedef struct { float x, z, hw, hd, h; unsigned int wall, roof; int door; } Bld;
static const Bld BLD[NB] = {
    { 22, -30, 4.0f, 3.0f, 4.0f, 0xff8a9aa8, 0xff303050, -1 },
    { 34, -30, 3.5f, 3.5f, 5.0f, 0xff7080a0, 0xff202830, -1 },
    { 46, -30, 4.0f, 3.0f, 4.0f, 0xff909098, 0xff303050, -1 },
    { 58, -30, 3.5f, 3.0f, 4.5f, 0xff8a9aa8, 0xff202830, -1 },
    { 22, -44, 3.5f, 3.5f, 5.0f, 0xff5a6a88, 0xff202830, 1 },
    { 34, -44, 4.5f, 3.0f, 4.0f, 0xff8090a0, 0xff303050, 1 },
    { 46, -44, 3.5f, 3.5f, 6.0f, 0xff707080, 0xff202830, 1 },
    { 58, -44, 4.0f, 3.0f, 4.0f, 0xff7080a0, 0xff303050, 1 },
    { -29, -55, 3.0f, 2.5f, 3.5f, 0xffc0c8d0, 0xff2020b0, 1 },
    { 60, 20, 4.5f, 3.5f, 5.5f, 0xff2828b0, 0xff303030, 1 },
    { -65, -15, 1.6f, 1.6f, 16.0f, 0xff707070, 0xff2020d0, 1 },
    { 62, 50, 4.0f, 3.0f, 4.0f, 0xff506040, 0xff304028, 1 },
    { 74, 56, 3.0f, 3.0f, 4.0f, 0xff506040, 0xff304028, 1 }
};
static const float ROADS[NROADS][4] = {
    { 0, 0, 0, -37 }, { 0, -37, 70, -37 }, { 0, 0, 0, 70 }, { -70, 0, 0, 0 },
    { 0, 0, 50, 0 }, { 50, 0, 50, 50 }, { 50, 50, 70, 50 }, { -35, 0, -35, -55 }
};
static const float CARS[NCARS][3] = {
    { 26, -37, 1.57f }, { 42, -36.5f, 1.4f }, { 58, -37.5f, 1.7f }, { 0, -22, 0.1f }, { 1, 35, 0 },
    { -1, 55, 0.2f }, { -25, 0.5f, 1.57f }, { -52, -0.5f, 1.5f }, { 22, 0, 1.57f }, { 50, 28, 0.1f }
};

typedef struct { float x, z; int kind, looted; float t; } Loot;
typedef struct { float x, z, hx, hz, hr, tx, tz, hp, ang, t, atk, hurt, dead, corpse; int type, state, active, horde; } Zomb;
typedef struct { float x, z; int w, taken; } WPick;
typedef struct { float x, z, r; int n, brutes; } Zone;
static const Zone ZONES[5] = {
    { 40, -37, 22, 10, 1 }, { 68, 53, 12, 5, 3 }, { -29, -55, 10, 3, 0 }, { 60, 20, 10, 3, 0 }, { -65, -15, 10, 3, 0 }
};
static const float zHP[3]    = { 60.0f, 40.0f, 170.0f };
static const float zSpeed[3] = { 3.0f, 6.0f, 2.3f };
static const float zDmg[3]   = { 9.0f, 7.0f, 22.0f };

static Loot loot[NLOOT]; static int nloot = 0;
static Zomb zomb[NZ];
static float treeX[NTREES], treeZ[NTREES], treeH[NTREES];
static int inv[NITEMS];
static int hasW[NW] = { 1, 0, 0, 0, 0, 0, 0, 0 };
static WPick wpick[NWP];
static int curW = 0, selUse = 0, kills = 0, parts = 0, rescue = 0, deaths = 0, chasers = 0;
static int mode = 0, menuSel = 0, showHelp = 1, endingSeen = 0;
static float px = 3.0f, pz = 5.0f, cam = 0, pang = 0;
static float hp = 100, hunger = 100, thirst = 100, stam = 100;
static float gameTime = 0, rescueT = 0, hordeT = 0, attackCD = 0, swingT = 0, rollT = 0, rollX = 0, rollZ = 1;
static float hurtT = 0, shotT = 0, shotX1 = 0, shotZ1 = 0, shotX2 = 0, shotZ2 = 0, moanCD = 0, drinkCD = 0;
static float msgTimer = 0, deathT = 0;
static int facingRight = 1, moving = 0;
static float walkPhase = 0;
static char msg[64] = "", menuMsg[48] = "";
static unsigned int seed_rng = 12345;
static int worldSeed = 0, difficulty = 1, nameIdx = 0, curSlot = 0;

#define APP_SPLASH  0
#define APP_TITLE   1
#define APP_NEW     2
#define APP_LOAD    3
#define APP_CREDITS 4
#define APP_POST    5
#define APP_PLAY    6
static int appState = APP_SPLASH;
static float appT = 0, creditsY = 272.0f;
static int titleSel = 0, nwSel = 0, nwSlot = 0, nwName = 0, nwSeed = 0, nwDiff = 1;
static int slotSel = 0, delConfirm = -1, creditsFromGame = 0;
typedef struct { int used, nameIdx, day, parts, kills; } SlotInfo;
static SlotInfo slotInfos[3];

static float frand(void) {
    seed_rng = seed_rng * 1664525u + 1013904223u;
    return (float)((seed_rng >> 8) & 0xFFFF) / 65535.0f;
}
static float dist2d(float ax, float az, float bx, float bz) { float dx = ax - bx, dz = az - bz; return sqrtf(dx * dx + dz * dz); }
#define NEAR(x, z, r) (dist2d(px, pz, (x), (z)) < (r))
static unsigned int seed_for(int ws) { return ws <= 0 ? 12345u : (unsigned int)ws * 2654435761u + 12345u; }

static void say(const char *s) { strncpy(msg, s, sizeof(msg) - 1); msg[sizeof(msg) - 1] = 0; msgTimer = 3.0f; }

/* ---------- tempo ---------- */
static float hour_now(void) { return fmodf(gameTime / DAY_LEN * 24.0f + 8.0f, 24.0f); }
static float daylight(void) { return 0.5f + 0.5f * cosf((hour_now() - 13.0f) * (2.0f * PI_F / 24.0f)); }
static float nightness(void) { float n = (0.45f - daylight()) / 0.3f; return n < 0 ? 0 : (n > 1 ? 1 : n); }
static int day_number(void) { return (int)((gameTime / DAY_LEN * 24.0f + 8.0f) / 24.0f) + 1; }

/* ---------- geometria ---------- */
static int near_road(float x, float z, float m) {
    for (int i = 0; i < NROADS; i++) {
        float x1 = ROADS[i][0], z1 = ROADS[i][1], x2 = ROADS[i][2], z2 = ROADS[i][3];
        float minx = (x1 < x2 ? x1 : x2) - m - 2.5f, maxx = (x1 > x2 ? x1 : x2) + m + 2.5f;
        float minz = (z1 < z2 ? z1 : z2) - m - 2.5f, maxz = (z1 > z2 ? z1 : z2) + m + 2.5f;
        if (x >= minx && x <= maxx && z >= minz && z <= maxz) return 1;
    }
    return 0;
}

static void push_out_rect(float *x, float *z, float r, float cx, float cz, float hw, float hd) {
    float dx = *x - cx, dz = *z - cz;
    float ox = hw + r - fabsf(dx), oz = hd + r - fabsf(dz);
    if (ox > 0 && oz > 0) {
        if (ox < oz) *x += (dx < 0 ? -ox : ox); else *z += (dz < 0 ? -oz : oz);
    }
}
static void collide(float *x, float *z, float r) {
    for (int i = 0; i < NB; i++) push_out_rect(x, z, r, BLD[i].x, BLD[i].z, BLD[i].hw, BLD[i].hd);
    push_out_rect(x, z, r, LAKE_X, LAKE_Z, LAKE_HX, LAKE_HZ);
}
static void collide_trees(float *x, float *z) {
    for (int i = 0; i < NTREES; i++) {
        float dx = *x - treeX[i], dz = *z - treeZ[i];
        if (dx > 1.5f || dx < -1.5f || dz > 1.5f || dz < -1.5f) continue;
        float d = sqrtf(dx * dx + dz * dz);
        if (d < 0.9f && d > 0.001f) { *x = treeX[i] + dx / d * 0.9f; *z = treeZ[i] + dz / d * 0.9f; }
    }
}
static float lake_dist(float x, float z) {
    float dx = fabsf(x - LAKE_X) - LAKE_HX, dz = fabsf(z - LAKE_Z) - LAKE_HZ;
    if (dx < 0) dx = 0;
    if (dz < 0) dz = 0;
    return sqrtf(dx * dx + dz * dz);
}
static int blocked(float x, float z, float margin) {
    for (int i = 0; i < NB; i++)
        if (fabsf(x - BLD[i].x) < BLD[i].hw + margin && fabsf(z - BLD[i].z) < BLD[i].hd + margin) return 1;
    if (lake_dist(x, z) < margin + 1.0f) return 1;
    if (near_road(x, z, margin - 2.0f)) return 1;
    if (dist2d(x, z, BASE_X, BASE_Z) < SAFE_R + 4.0f) return 1;
    if (dist2d(x, z, 68, 53) < 19.0f) return 1;
    return 0;
}

/* ---------- mundo ---------- */
static void add_loot(float x, float z, int kind) {
    if (nloot >= NLOOT) return;
    loot[nloot].x = x; loot[nloot].z = z; loot[nloot].kind = kind; loot[nloot].looted = 0; loot[nloot].t = 0;
    nloot++;
}

static void respawn_zombie(Zomb *z) {
    float x, z2; int tries = 0;
    do {
        x = z->hx + (frand() * 2 - 1) * z->hr;
        z2 = z->hz + (frand() * 2 - 1) * z->hr;
    } while (++tries < 20 && (dist2d(x, z2, BASE_X, BASE_Z) < SAFE_R + 3.0f || lake_dist(x, z2) < 1.5f));
    z->x = x; z->z = z2; z->tx = x; z->tz = z2;
    z->hp = zHP[z->type]; z->state = 0; z->dead = 0; z->hurt = 0; z->atk = 0; z->t = frand() * 3.0f;
}

static void init_world(void) {
    nloot = 0;
    for (int i = 0; i < 8; i++) add_loot(BLD[i].x, BLD[i].z + BLD[i].door * (BLD[i].hd + 1.3f), 0);
    add_loot(-29, -51.2f, 0);                       /* posto */
    add_loot(60, 24.8f, 0);                         /* fazenda */
    add_loot(62, 53.5f, 2); add_loot(74, 59.5f, 2); add_loot(66, 60, 2);   /* militar */
    for (int i = 0; i < NCARS; i++) add_loot(CARS[i][0] + 2.2f, CARS[i][1] + 1.0f, 1);
    add_loot(-33, -58, 3); add_loot(66.5f, 20, 3); add_loot(-65, -11.5f, 3); add_loot(70, 48, 3);  /* pecas de radio */
    for (int i = 0; i < 8; i++) {
        float x, z; int tries = 0;
        do { x = (frand() * 2 - 1) * (WORLD - 6); z = (frand() * 2 - 1) * (WORLD - 6); } while (++tries < 30 && blocked(x, z, 3.0f));
        add_loot(x, z, 0);
    }
    for (int i = 0; i < NTREES; i++) {
        float x, z; int tries = 0;
        do { x = (frand() * 2 - 1) * (WORLD - 3); z = (frand() * 2 - 1) * (WORLD - 3); } while (++tries < 40 && blocked(x, z, 4.0f));
        treeX[i] = x; treeZ[i] = z; treeH[i] = 3.5f + frand() * 3.0f;
    }
    memset(zomb, 0, sizeof(zomb));
    int zi = 0;
    for (int zn = 0; zn < 5; zn++)
        for (int k = 0; k < ZONES[zn].n && zi < NZREG; k++, zi++) {
            Zomb *z = &zomb[zi];
            z->hx = ZONES[zn].x; z->hz = ZONES[zn].z; z->hr = ZONES[zn].r; z->active = 1;
            z->type = (k < ZONES[zn].brutes) ? 2 : (frand() < 0.3f ? 1 : 0);
            respawn_zombie(z);
        }
    for (; zi < NZREG; zi++) {
        Zomb *z = &zomb[zi];
        float x, zz; int tries = 0;
        do { x = (frand() * 2 - 1) * (WORLD - 8); zz = (frand() * 2 - 1) * (WORLD - 8); } while (++tries < 30 && (blocked(x, zz, 4.0f) || dist2d(x, zz, 0, 0) < 28.0f));
        z->hx = x; z->hz = zz; z->hr = 10.0f; z->active = 1; z->type = (frand() < 0.3f) ? 1 : 0;
        respawn_zombie(z);
    }
    /* armas coletaveis espalhadas pelo mundo */
    static const float WP[7][3] = { { -8, -2, 1 }, { 56, 14.5f, 2 }, { -31, -52, 3 }, { 30, -37, 4 }, { 64, 44, 5 }, { -62, -11, 6 }, { 78.5f, 43.5f, 7 } };
    for (int i = 0; i < 7; i++) { wpick[i].x = WP[i][0]; wpick[i].z = WP[i][1]; wpick[i].w = (int)WP[i][2]; wpick[i].taken = 0; }
    for (int i = 7; i < NWP; i++) {
        float x, zz; int tries = 0;
        do { x = (frand() * 2 - 1) * (WORLD - 8); zz = (frand() * 2 - 1) * (WORLD - 8); } while (++tries < 30 && blocked(x, zz, 3.0f));
        wpick[i].x = x; wpick[i].z = zz; wpick[i].w = 1 + (int)(frand() * 4.0f); if (wpick[i].w > 4) wpick[i].w = 4; wpick[i].taken = 0;
    }
}

static void reset_state(void) {
    memset(inv, 0, sizeof(inv));
    inv[I_COM] = 2; inv[I_AGUA] = 2; inv[I_BAND] = 1; inv[I_MAD] = 2; inv[I_MUN] = 0;
    for (int i = 0; i < NW; i++) hasW[i] = 0;
    hasW[0] = 1; curW = 0; selUse = 0;
    kills = 0; parts = 0; rescue = 0; deaths = 0; chasers = 0; mode = 0; menuSel = 0; showHelp = 1; endingSeen = 0;
    px = 3.0f; pz = 5.0f; cam = 0; pang = 0;
    hp = 100; hunger = 100; thirst = 100; stam = 100;
    gameTime = 0; rescueT = 0; hordeT = 0; attackCD = 0; swingT = 0; rollT = 0; hurtT = 0; shotT = 0;
    moanCD = 0; drinkCD = 0; msgTimer = 0; deathT = 0; msg[0] = 0; menuMsg[0] = 0;
    facingRight = 1; moving = 0; walkPhase = 0;
}

/* ---------- salvar / carregar (3 slots) ---------- */
#define SAVE_MAGIC  0x53564753u
#define SAVE_MAGIC2 0x32565347u
#define SAVE_DIR    "ms0:/PSP/SAVEDATA/SOBREVIVENDOGS"
typedef struct {
    unsigned int magic; int version;
    int worldSeed, difficulty, nameIdx, kills, parts, rescue, curW, selUse, deaths, endingSeen;
    int inv[NITEMS_OLD], hasW[3];
    float px, pz, cam, gameTime, hp, hunger, thirst, stam, rescueT;
    unsigned char looted[NLOOT]; float lootT[NLOOT];
} SaveData;
typedef struct { unsigned int magic; int inv2[2]; int hasW[NW]; int curW; unsigned char taken[NWP]; } SaveExtra;

static void slot_path(int slot, char *out) { sprintf(out, SAVE_DIR "/save%d.dat", slot + 1); }

static int save_game(void) {
    SaveData d; memset(&d, 0, sizeof(d));
    d.magic = SAVE_MAGIC; d.version = 1;
    d.worldSeed = worldSeed; d.difficulty = difficulty; d.nameIdx = nameIdx; d.kills = kills; d.parts = parts;
    d.rescue = rescue; d.selUse = selUse; d.deaths = deaths; d.endingSeen = endingSeen;
    d.curW = (curW == 2) ? 1 : (curW == 4) ? 2 : 0;
    for (int i = 0; i < NITEMS_OLD; i++) d.inv[i] = inv[i];
    d.hasW[0] = hasW[1]; d.hasW[1] = hasW[2]; d.hasW[2] = hasW[4];
    d.px = px; d.pz = pz; d.cam = cam; d.gameTime = gameTime; d.hp = hp; d.hunger = hunger; d.thirst = thirst;
    d.stam = stam; d.rescueT = rescueT;
    for (int i = 0; i < nloot && i < NLOOT; i++) { d.looted[i] = (unsigned char)loot[i].looted; d.lootT[i] = loot[i].t; }
    SaveExtra e; memset(&e, 0, sizeof(e));
    e.magic = SAVE_MAGIC2; e.inv2[0] = inv[I_CART]; e.inv2[1] = inv[I_RIFLE]; e.curW = curW;
    for (int i = 0; i < NW; i++) e.hasW[i] = hasW[i];
    for (int i = 0; i < NWP; i++) e.taken[i] = (unsigned char)wpick[i].taken;
    sceIoMkdir("ms0:/PSP", 0777);
    sceIoMkdir("ms0:/PSP/SAVEDATA", 0777);
    sceIoMkdir(SAVE_DIR, 0777);
    char path[96]; slot_path(curSlot, path);
    int fd = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    if (fd < 0) return 0;
    int w = sceIoWrite(fd, &d, sizeof(d));
    int w2 = sceIoWrite(fd, &e, sizeof(e));
    sceIoClose(fd);
    return w == (int)sizeof(d) && w2 == (int)sizeof(e);
}

static int read_slot(int slot, SaveData *d, SaveExtra *e, int *hasE) {
    char p[96]; slot_path(slot, p);
    int fd = sceIoOpen(p, PSP_O_RDONLY, 0777);
    if (fd < 0) return 0;
    int r = sceIoRead(fd, d, sizeof(*d));
    int r2 = sceIoRead(fd, e, sizeof(*e));
    sceIoClose(fd);
    *hasE = (r2 == (int)sizeof(*e) && e->magic == SAVE_MAGIC2);
    return r == (int)sizeof(*d) && d->magic == SAVE_MAGIC && d->version == 1;
}

static void refresh_slots(void) {
    for (int s = 0; s < 3; s++) {
        SaveData d; SaveExtra e; int he; SlotInfo *si = &slotInfos[s]; memset(si, 0, sizeof(*si));
        if (!read_slot(s, &d, &e, &he)) continue;
        si->used = 1; si->nameIdx = (d.nameIdx >= 0 && d.nameIdx < 8) ? d.nameIdx : 0;
        si->day = (int)((d.gameTime / DAY_LEN * 24.0f + 8.0f) / 24.0f) + 1; si->parts = d.parts; si->kills = d.kills;
    }
}

static int load_game(void) {
    SaveData d; SaveExtra e; int he = 0;
    if (!read_slot(curSlot, &d, &e, &he)) return 0;
    reset_state();
    worldSeed = d.worldSeed; difficulty = (d.difficulty >= 0 && d.difficulty < 3) ? d.difficulty : 1;
    nameIdx = (d.nameIdx >= 0 && d.nameIdx < 8) ? d.nameIdx : 0;
    seed_rng = seed_for(worldSeed);
    init_world();
    kills = d.kills; parts = d.parts; rescue = (d.rescue == 1 || d.rescue == 2) ? 0 : d.rescue; selUse = d.selUse;
    deaths = d.deaths; endingSeen = d.endingSeen ? 1 : 0;
    for (int i = 0; i < NITEMS_OLD; i++) inv[i] = d.inv[i];
    if (he) {
        inv[I_CART] = e.inv2[0]; inv[I_RIFLE] = e.inv2[1];
        for (int i = 0; i < NW; i++) hasW[i] = e.hasW[i] ? 1 : 0;
        curW = e.curW;
        for (int i = 0; i < NWP; i++) wpick[i].taken = e.taken[i] ? 1 : 0;
    } else {
        static const int mp[3] = { 1, 2, 4 };
        hasW[1] = d.hasW[0]; hasW[2] = d.hasW[1]; hasW[4] = d.hasW[2];
        curW = (d.curW >= 0 && d.curW < 3) ? mp[d.curW] : 0;
    }
    hasW[0] = 1;
    if (curW < 0 || curW >= NW || !hasW[curW]) curW = 0;
    if (selUse < 0 || selUse > 2) selUse = 0;
    px = d.px; pz = d.pz; cam = d.cam; gameTime = d.gameTime; hp = d.hp; hunger = d.hunger; thirst = d.thirst;
    stam = d.stam; rescueT = d.rescueT;
    if (hp < 20) hp = 20;
    for (int i = 0; i < nloot && i < NLOOT; i++) { loot[i].looted = d.looted[i] ? 1 : 0; loot[i].t = d.lootT[i]; }
    return 1;
}

static void create_world(void) {
    reset_state();
    worldSeed = nwSeed ? nwSeed : (int)(sceKernelGetSystemTimeLow() % 99u) + 1;
    nameIdx = nwName; difficulty = nwDiff; curSlot = nwSlot;
    seed_rng = seed_for(worldSeed);
    init_world();
    save_game();
}

static void check_ending(void) {
    endingSeen = 1; creditsFromGame = 1; appState = APP_CREDITS; creditsY = 272.0f; appT = 0; mode = 0;
    rescue = 0; save_game();
}

/* ---------- jogador ---------- */
static void open_loot(int i) {
    static const int wt[7] = { 3, 3, 3, 2, 2, 1, 2 };   /* madeira sucata pano comida agua bandagem municao */
    Loot *L = &loot[i]; int got[NITEMS]; memset(got, 0, sizeof(got)); int wdrop = -1;
    if (L->kind == 0) {
        int rolls = 2 + (frand() < 0.5f ? 1 : 0);
        for (int r = 0; r < rolls; r++) {
            float tot = 16.0f, v = frand() * tot, acc = 0; int it = 0;
            for (int k = 0; k < 7; k++) { acc += wt[k]; if (v <= acc) { it = k; break; } it = k; }
            got[it] += 1 + (it < 3 ? (int)(frand() * 2) : 0);
        }
    } else if (L->kind == 1) {
        got[I_SUC] = 1 + (int)(frand() * 3);
        if (frand() < 0.4f) got[I_MUN] = 2 + (int)(frand() * 3);
        if (frand() < 0.25f) got[I_COM] = 1;
    } else if (L->kind == 2) {
        got[I_MUN] = 5 + (int)(frand() * 6);
        if (frand() < 0.6f) got[I_BAND] = 1;
        got[frand() < 0.5f ? I_COM : I_AGUA] = 1; got[I_SUC] += 1;
        if (frand() < 0.15f) { int c = 4 + (int)(frand() * 4.0f); if (c > 7) c = 7; if (!hasW[c]) wdrop = c; }
    } else {
        got[I_PECA] = 1; got[I_MUN] = 2 + (int)(frand() * 3); got[I_BAND] = 1;
    }
    char b[64]; b[0] = 0; int n = 0;
    for (int k = 0; k < NITEMS; k++) if (got[k]) {
        inv[k] += got[k]; if (k == I_PECA) parts++;
        char t[24]; sprintf(t, "%s%d %s", n ? ", " : "", got[k], itemName[k]);
        if (strlen(b) + strlen(t) < 40) strcat(b, t);
        n++;
    }
    if (wdrop >= 0) {
        hasW[wdrop] = 1; curW = wdrop; if (wAmmo[wdrop] >= 0) inv[wAmmo[wdrop]] += wGive[wdrop];
        char t[24]; sprintf(t, "%s%s", n ? ", " : "", wName[wdrop]); if (strlen(b) + strlen(t) < 40) strcat(b, t);
    }
    char m[64]; sprintf(m, "Achou: %s", b[0] ? b : "nada");
    say(m); L->looted = 1; L->t = (L->kind == 3) ? 1e9f : 600.0f; play_sfx(L->kind == 3 ? 11 : 9);
}

static void player_die(void) {
    mode = 3; deathT = 0; deaths++; play_sfx(10);
}

static void hurt_player(float dmg) {
    if (rollT > 0 || mode == 3) return;
    hp -= dmg * diffDmg[difficulty]; hurtT = 0.4f; play_sfx(6);
    if (hp <= 0) { hp = 0; player_die(); }
}

static void kill_zombie(Zomb *z) {
    z->state = 2; z->dead = 90.0f + frand() * 60.0f; z->corpse = 3.5f; kills++; play_sfx(3);
    if (z->type == 2) { int it = (frand() < 0.5f) ? I_MUN : I_CART; inv[it] += 3; char mm[48]; sprintf(mm, "Brutamontes caiu! +3 %s", itemName[it]); say(mm); }
    if (frand() < 0.45f) {
        int it = (int)(frand() * 5); static const int tbl[5] = { I_PANO, I_SUC, I_COM, I_MUN, I_MAD };
        inv[tbl[it]]++; char m[48]; sprintf(m, "Zumbi derrubado! +1 %s", itemName[tbl[it]]); say(m);
    }
    if (z->horde) z->active = 0;
}

static void damage_zombie(Zomb *z, float dmg, float fx, float fz) {
    z->hp -= dmg; z->hurt = 0.3f; z->x += fx * 0.8f; z->z += fz * 0.8f; z->state = 1;
    if (z->hp <= 0) kill_zombie(z); else play_sfx(3);
}

static int nearest_zombie(float maxd, float *outd) {
    int best = -1; float bd = maxd;
    for (int i = 0; i < NZ; i++) {
        Zomb *z = &zomb[i];
        if (!z->active || z->state == 2) continue;
        float d = dist2d(px, pz, z->x, z->z);
        if (d < bd) { bd = d; best = i; }
    }
    if (outd) *outd = bd;
    return best;
}

static int aim_target(float maxd, float maxAng) {
    int best = -1; float bd = maxd;
    for (int i = 0; i < NZ; i++) {
        Zomb *z = &zomb[i];
        if (!z->active || z->state == 2) continue;
        float dx = z->x - px, dz = z->z - pz, d = sqrtf(dx * dx + dz * dz);
        float da = atan2f(dx, dz) - pang;
        while (da > PI_F) da -= 2.0f * PI_F;
        while (da < -PI_F) da += 2.0f * PI_F;
        if (d < bd && fabsf(da) < maxAng) { bd = d; best = i; }
    }
    return best;
}

static float ray_hit(float fx, float fz, float rng, int *hit) {
    float bt = rng; *hit = -1;
    for (int i = 0; i < NZ; i++) {
        Zomb *z = &zomb[i];
        if (!z->active || z->state == 2) continue;
        float dx = z->x - px, dz = z->z - pz, along = dx * fx + dz * fz, perp = fabsf(dx * fz - dz * fx);
        if (along > 0 && along < bt && perp < 0.9f) { bt = along; *hit = i; }
    }
    return bt;
}

static void player_attack(void) {
    if (attackCD > 0 || rollT > 0 || mode != 0) return;
    int w = curW, ai = wAmmo[w];
    float rng = wRange[w];
    if (ai >= 0 && inv[ai] <= 0) { say("Sem municao!"); play_sfx(10); attackCD = 0.3f; return; }
    int t = aim_target(ai >= 0 ? rng : 3.8f, ai >= 0 ? 1.1f : 3.2f);
    if (t >= 0) pang = atan2f(zomb[t].x - px, zomb[t].z - pz);
    float fx = sinf(pang), fz = cosf(pang);
    attackCD = wCD[w];
    if (ai >= 0) {
        inv[ai]--; play_sfx(1); shotT = 0.1f;
        int hit; float bt;
        if (w == 6) {
            static const float off[5] = { -0.16f, -0.08f, 0.0f, 0.08f, 0.16f };
            bt = rng;
            for (int k = 0; k < 5; k++) {
                float a2 = pang + off[k], gx = sinf(a2), gz = cosf(a2); int h2;
                float t2 = ray_hit(gx, gz, rng, &h2);
                if (k == 2) { bt = t2; fx = gx; fz = gz; }
                if (h2 >= 0) damage_zombie(&zomb[h2], wDmg[w], gx, gz);
            }
        } else {
            bt = ray_hit(fx, fz, rng, &hit);
            if (hit >= 0) damage_zombie(&zomb[hit], wDmg[w], fx, fz);
        }
        shotX1 = px + fx * 0.8f; shotZ1 = pz + fz * 0.8f; shotX2 = px + fx * bt; shotZ2 = pz + fz * bt;
    } else {
        swingT = 0.18f; play_sfx(2);
        for (int i = 0; i < NZ; i++) {
            Zomb *z = &zomb[i];
            if (!z->active || z->state == 2) continue;
            float dx = z->x - px, dz = z->z - pz, d = sqrtf(dx * dx + dz * dz);
            if (d < rng + 0.6f && (d < 0.3f || (dx * fx + dz * fz) / d > 0.2f)) damage_zombie(z, wDmg[w], fx, fz);
        }
    }
}

static int nearest_wpick(void) {
    int best = -1; float bd = 2.6f;
    for (int i = 0; i < NWP; i++) {
        if (wpick[i].taken) continue;
        float d = dist2d(px, pz, wpick[i].x, wpick[i].z);
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

static void take_weapon(int i) {
    int w = wpick[i].w; char m[64];
    wpick[i].taken = 1;
    if (wAmmo[w] >= 0) inv[wAmmo[w]] += wGive[w];
    if (!hasW[w]) { hasW[w] = 1; curW = w; sprintf(m, "Pegou: %s!", wName[w]); }
    else sprintf(m, "Ja tem %s: +%d %s", wName[w], wGive[w], wAmmo[w] >= 0 ? itemName[wAmmo[w]] : "nada");
    say(m); play_sfx(9);
}

static void use_item(void) {
    if (selUse == 0) { if (inv[I_COM] > 0) { inv[I_COM]--; hunger += 35; if (hunger > 100) hunger = 100; say("Comeu. Fome restaurada."); play_sfx(4); } else { say("Sem comida!"); play_sfx(10); } }
    else if (selUse == 1) { if (inv[I_AGUA] > 0) { inv[I_AGUA]--; thirst += 40; if (thirst > 100) thirst = 100; say("Bebeu agua."); play_sfx(4); } else { say("Sem agua!"); play_sfx(10); } }
    else { if (inv[I_BAND] > 0) { inv[I_BAND]--; hp += 40; if (hp > 100) hp = 100; say("Usou bandagem. Vida restaurada."); play_sfx(4); } else { say("Sem bandagem!"); play_sfx(10); } }
}

static int near_camp(void) { return NEAR(BASE_X, BASE_Z, 4.5f); }
static int near_lake(void) { return lake_dist(px, pz) < 3.0f; }
static int near_heli(void) { return rescue == 2 && NEAR(HELI_X, HELI_Z, 6.0f); }
static int nearest_loot(void) {
    int best = -1; float bd = 2.6f;
    for (int i = 0; i < nloot; i++) {
        if (loot[i].looted) continue;
        float d = dist2d(px, pz, loot[i].x, loot[i].z);
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

static void interact(void) {
    int l;
    if (near_heli()) { play_sfx(12); check_ending(); return; }
    if (near_camp()) { mode = 2; menuSel = 0; menuMsg[0] = 0; play_sfx(5); return; }
    l = nearest_wpick();
    if (l >= 0) { take_weapon(l); return; }
    l = nearest_loot();
    if (l >= 0) { open_loot(l); return; }
    if (near_lake()) {
        if (drinkCD > 0) return;
        thirst += 30; if (thirst > 100) thirst = 100; drinkCD = 1.0f; say("Bebeu agua do lago."); play_sfx(4); return;
    }
    say("Nada para fazer aqui."); play_sfx(10);
}

/* ---------- zumbis ---------- */
static void zombies_update(float dt) {
    float night = nightness();
    float aggro = 14.0f + 12.0f * night;
    int inSafe = (rescue == 0) && dist2d(px, pz, BASE_X, BASE_Z) < SAFE_R;
    chasers = 0;
    if (moanCD > 0) moanCD -= dt;
    for (int i = 0; i < NZ; i++) {
        Zomb *z = &zomb[i];
        if (!z->active) continue;
        if (z->state == 2) {
            if (z->corpse > 0) z->corpse -= dt;
            z->dead -= dt;
            if (z->dead <= 0 && i < NZREG) { z->state = 0; respawn_zombie(z); }
            continue;
        }
        if (z->hurt > 0) z->hurt -= dt;
        if (z->atk > 0) z->atk -= dt;
        float dx = px - z->x, dz = pz - z->z, d = sqrtf(dx * dx + dz * dz);
        float sp = zSpeed[z->type] * diffSpd[difficulty] * (1.0f + 0.15f * night);
        if (z->hurt > 0.15f) continue;
        if (z->state == 0) {
            z->t -= dt;
            if (z->t <= 0) { z->tx = z->hx + (frand() * 2 - 1) * z->hr; z->tz = z->hz + (frand() * 2 - 1) * z->hr; z->t = 3.0f + frand() * 4.0f; }
            float tx = z->tx - z->x, tz = z->tz - z->z, td = sqrtf(tx * tx + tz * tz);
            if (td > 0.6f) { z->x += tx / td * 1.1f * dt; z->z += tz / td * 1.1f * dt; z->ang = atan2f(tx, tz); }
            if (!inSafe && d < aggro * (z->type == 1 ? 1.2f : 1.0f)) {
                z->state = 1;
                if (moanCD <= 0 && d < 20.0f) { play_sfx(7); moanCD = 4.0f; }
            }
        } else {
            if (inSafe || (!z->horde && d > aggro * 2.0f)) z->state = 0;
            else {
                if (d > 1.0f) { z->x += dx / d * sp * dt; z->z += dz / d * sp * dt; } if (d > 0.01f) z->ang = atan2f(dx, dz);
                if (d < 1.7f && z->atk <= 0) { z->atk = 1.0f; hurt_player(zDmg[z->type]); }
                if (d < 22.0f) chasers++;
            }
        }
        collide(&z->x, &z->z, 0.5f);
        if (rescue == 0) {
            float bd = dist2d(z->x, z->z, BASE_X, BASE_Z);
            if (bd < SAFE_R && bd > 0.001f) { z->x = BASE_X + (z->x - BASE_X) / bd * SAFE_R; z->z = BASE_Z + (z->z - BASE_Z) / bd * SAFE_R; }
        }
    }
}

static void spawn_horde(int n) {
    for (int i = NZREG; i < NZ && n > 0; i++) {
        Zomb *z = &zomb[i];
        if (z->active) continue;
        float a = frand() * 6.283f;
        z->x = BASE_X + cosf(a) * 38.0f; z->z = BASE_Z + sinf(a) * 38.0f;
        z->hx = BASE_X; z->hz = BASE_Z; z->hr = 8.0f;
        z->type = (frand() < 0.3f) ? 1 : 0; z->hp = zHP[z->type]; z->state = 1; z->active = 1; z->horde = 1; z->hurt = 0; z->atk = 0;
        n--;
    }
}

static void do_sleep(void) {
    float hrs = gameTime / DAY_LEN * 24.0f + 8.0f;
    float target = (floorf((hrs - 7.0f) / 24.0f) + 1.0f) * 24.0f + 7.0f;
    float skip = (target - hrs) / 24.0f * DAY_LEN;
    gameTime += skip;
    hp += 60; if (hp > 100) hp = 100;
    hunger -= 15; if (hunger < 5) hunger = 5;
    thirst -= 20; if (thirst < 5) thirst = 5;
    for (int i = 0; i < nloot; i++) if (loot[i].looted && loot[i].kind != 3) { loot[i].t -= skip; if (loot[i].t <= 0) loot[i].looted = 0; }
    for (int i = 0; i < NZREG; i++) if (zomb[i].active && zomb[i].state == 2) { zomb[i].state = 0; respawn_zombie(&zomb[i]); }
    say(save_game() ? "Dormiu. Novo dia! Jogo salvo." : "Dormiu. Novo dia! (erro ao salvar)");
    play_sfx(4);
}


static void camp_update(unsigned int pressed) {
    if (pressed & PSP_CTRL_UP)   { menuSel = (menuSel + 6) % 7; play_sfx(5); }
    if (pressed & PSP_CTRL_DOWN) { menuSel = (menuSel + 1) % 7; play_sfx(5); }
    if (pressed & (PSP_CTRL_START | PSP_CTRL_CIRCLE | PSP_CTRL_TRIANGLE)) { mode = 0; return; }
    if (pressed & PSP_CTRL_CROSS) {
        if (menuSel == 0) {
            if (inv[I_PANO] >= 2) { inv[I_PANO] -= 2; inv[I_BAND]++; strcpy(menuMsg, "Fabricou 1 Bandagem!"); play_sfx(4); }
            else { strcpy(menuMsg, "Faltam materiais!"); play_sfx(10); }
        } else if (menuSel == 1) {
            if (inv[I_SUC] >= 2) { inv[I_SUC] -= 2; inv[I_MUN] += 6; strcpy(menuMsg, "Fabricou 6 Municao!"); play_sfx(4); }
            else { strcpy(menuMsg, "Faltam materiais!"); play_sfx(10); }
        } else if (menuSel == 2) {
            if (inv[I_SUC] >= 3) { inv[I_SUC] -= 3; inv[I_CART] += 4; strcpy(menuMsg, "Fabricou 4 Cartuchos!"); play_sfx(4); }
            else { strcpy(menuMsg, "Faltam materiais!"); play_sfx(10); }
        } else if (menuSel == 3) {
            if (inv[I_SUC] >= 4) { inv[I_SUC] -= 4; inv[I_RIFLE] += 3; strcpy(menuMsg, "Fabricou 3 Balas de rifle!"); play_sfx(4); }
            else { strcpy(menuMsg, "Faltam materiais!"); play_sfx(10); }
        } else if (menuSel == 4) {
            do_sleep(); mode = 0;
        } else if (menuSel == 5) {
            if (rescue != 0) { strcpy(menuMsg, "O resgate ja foi chamado!"); play_sfx(10); }
            else if (parts < 4) { sprintf(menuMsg, "Faltam %d pecas de radio!", 4 - parts); play_sfx(10); }
            else { rescue = 1; rescueT = 100.0f; hordeT = 3.0f; mode = 0; say("Resgate chamado! Aguente 100 segundos!"); play_sfx(11); }
        } else mode = 0;
    }
}

static void pause_update(unsigned int pressed) {
    if (pressed & PSP_CTRL_UP)   { menuSel = (menuSel + 5) % 6; play_sfx(5); }
    if (pressed & PSP_CTRL_DOWN) { menuSel = (menuSel + 1) % 6; play_sfx(5); }
    if (pressed & (PSP_CTRL_START | PSP_CTRL_CIRCLE)) { mode = 0; return; }
    if (pressed & PSP_CTRL_CROSS) {
        if (menuSel == 0) mode = 0;
        else if (menuSel == 1) { musicOn = !musicOn; play_sfx(5); }
        else if (menuSel == 2) strcpy(menuMsg, save_game() ? "Jogo salvo!" : "Erro ao salvar!");
        else if (menuSel == 3) {
            if (save_game()) { mode = 0; appState = APP_TITLE; titleSel = 0; }
            else strcpy(menuMsg, "Erro ao salvar!");
        } else if (menuSel == 4) { mode = 0; appState = APP_TITLE; titleSel = 0; }
        else running = 0;
    }
}

static void death_update(float dt, unsigned int pressed) {
    deathT += dt;
    if (deathT > 1.2f && (pressed & PSP_CTRL_CROSS)) {
        for (int i = 0; i < NITEMS; i++) inv[i] = inv[i] / 2;
        hp = 60; hunger = hunger < 40 ? 40 : hunger; thirst = thirst < 40 ? 40 : thirst; stam = 100;
        px = 3.0f; pz = 5.0f; rollT = 0; hurtT = 0; mode = 0; say("Voce renasceu no acampamento.");
        for (int i = 0; i < NZ; i++) if (zomb[i].active && zomb[i].state == 1 && dist2d(px, pz, zomb[i].x, zomb[i].z) < 20.0f) zomb[i].state = 0;
    }
}

static void update(float dt, const SceCtrlData *pad, unsigned int pressed) {
    gameTime += dt;
    if (msgTimer > 0) msgTimer -= dt;
    if (attackCD > 0) attackCD -= dt;
    if (swingT > 0) swingT -= dt;
    if (shotT > 0) shotT -= dt;
    if (hurtT > 0) hurtT -= dt;
    if (drinkCD > 0) drinkCD -= dt;

    if (pad->Buttons & PSP_CTRL_LTRIGGER) cam -= 1.8f * dt;
    if (pad->Buttons & PSP_CTRL_RTRIGGER) cam += 1.8f * dt;

    float dx = (pad->Lx - 128) / 128.0f, dy = (pad->Ly - 128) / 128.0f;
    if (fabsf(dx) < 0.2f) dx = 0;
    if (fabsf(dy) < 0.2f) dy = 0;
    float mx = 0, mz = 0;
    moving = (dx != 0 || dy != 0);
    if (moving) {
        float fx = sinf(cam), fz = cosf(cam), rx = -cosf(cam), rz = sinf(cam);
        mx = fx * (-dy) + rx * dx; mz = fz * (-dy) + rz * dx;
        float l = sqrtf(mx * mx + mz * mz); if (l > 1.0f) { mx /= l; mz /= l; }
    }
    if ((pressed & PSP_CTRL_CIRCLE) && rollT <= 0 && stam >= 25.0f) {
        float l = sqrtf(mx * mx + mz * mz);
        if (l > 0.1f) { rollX = mx / l; rollZ = mz / l; } else { rollX = sinf(pang); rollZ = cosf(pang); }
        rollT = 0.32f; stam -= 25.0f; play_sfx(8);
    }
    if (rollT > 0) { rollT -= dt; mx = rollX * 1.9f; mz = rollZ * 1.9f; }
    if (mx != 0 || mz != 0) {
        float sp = 7.5f * dt;
        px += mx * sp; pz += mz * sp;
        if (px < -WORLD) px = -WORLD;
        if (px > WORLD) px = WORLD;
        if (pz < -WORLD) pz = -WORLD;
        if (pz > WORLD) pz = WORLD;
        collide(&px, &pz, 0.5f); collide_trees(&px, &pz);
        pang = atan2f(mx, mz);
        float rx = -cosf(cam), rz = sinf(cam), side = mx * rx + mz * rz;
        if (side > 0.15f) facingRight = 1; else if (side < -0.15f) facingRight = 0;
        walkPhase += dt * 8.0f;
    } else walkPhase = 0;
    stam += 16.0f * dt; if (stam > 100) stam = 100;

    hunger -= 0.10f * dt; thirst -= 0.15f * dt;
    if (hunger < 0) hunger = 0;
    if (thirst < 0) thirst = 0;
    if (hunger <= 0 || thirst <= 0) {
        hp -= 1.5f * dt;
        if (hp <= 0) { hp = 0; player_die(); return; }
    } else if (hunger > 40 && thirst > 40 && hp < 100) { hp += 0.5f * dt; if (hp > 100) hp = 100; }

    for (int i = 0; i < nloot; i++) if (loot[i].looted && loot[i].kind != 3) { loot[i].t -= dt; if (loot[i].t <= 0) loot[i].looted = 0; }

    if (rescue == 1) {
        rescueT -= dt; hordeT -= dt;
        if (hordeT <= 0) { spawn_horde(2); hordeT = 7.0f; }
        if (rescueT <= 0) { rescue = 2; rescueT = 90.0f; say("O helicoptero chegou! Va ate ele (X)!"); play_sfx(12); }
    } else if (rescue == 2) {
        rescueT -= dt;
        if (rescueT <= 0) { rescue = 0; say("O helicoptero foi embora... chame de novo."); }
    }
    zombies_update(dt);

    if (pressed & PSP_CTRL_LEFT)  { selUse = (selUse + 2) % 3; play_sfx(5); }
    if (pressed & PSP_CTRL_RIGHT) { selUse = (selUse + 1) % 3; play_sfx(5); }
    if (pressed & PSP_CTRL_UP) use_item();
    if (pressed & PSP_CTRL_SELECT) showHelp = !showHelp;
    if (pressed & PSP_CTRL_TRIANGLE) {
        for (int k = 1; k <= NW; k++) { int w = (curW + k) % NW; if (hasW[w]) { curW = w; break; } }
        char m[40]; sprintf(m, "Arma: %s", wName[curW]); say(m); play_sfx(5);
    }
    if ((pressed & PSP_CTRL_SQUARE) || ((pad->Buttons & PSP_CTRL_SQUARE) && wAuto[curW])) player_attack();
    if (pressed & PSP_CTRL_CROSS) interact();
    if (pressed & PSP_CTRL_START) { mode = 1; menuSel = 0; menuMsg[0] = 0; }
}

static const unsigned char font5x7[95][5] = {
    {0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x5F,0x00,0x00},
    {0x00,0x07,0x00,0x07,0x00},
    {0x14,0x7F,0x14,0x7F,0x14},
    {0x24,0x2A,0x7F,0x2A,0x12},
    {0x23,0x13,0x08,0x64,0x62},
    {0x36,0x49,0x56,0x20,0x50},
    {0x00,0x08,0x07,0x03,0x00},
    {0x00,0x1C,0x22,0x41,0x00},
    {0x00,0x41,0x22,0x1C,0x00},
    {0x2A,0x1C,0x7F,0x1C,0x2A},
    {0x08,0x08,0x3E,0x08,0x08},
    {0x00,0x80,0x70,0x30,0x00},
    {0x08,0x08,0x08,0x08,0x08},
    {0x00,0x00,0x60,0x60,0x00},
    {0x20,0x10,0x08,0x04,0x02},
    {0x3E,0x51,0x49,0x45,0x3E},
    {0x00,0x42,0x7F,0x40,0x00},
    {0x72,0x49,0x49,0x49,0x46},
    {0x21,0x41,0x49,0x4D,0x33},
    {0x18,0x14,0x12,0x7F,0x10},
    {0x27,0x45,0x45,0x45,0x39},
    {0x3C,0x4A,0x49,0x49,0x31},
    {0x41,0x21,0x11,0x09,0x07},
    {0x36,0x49,0x49,0x49,0x36},
    {0x46,0x49,0x49,0x29,0x1E},
    {0x00,0x00,0x14,0x00,0x00},
    {0x00,0x40,0x34,0x00,0x00},
    {0x00,0x08,0x14,0x22,0x41},
    {0x14,0x14,0x14,0x14,0x14},
    {0x00,0x41,0x22,0x14,0x08},
    {0x02,0x01,0x59,0x09,0x06},
    {0x3E,0x41,0x5D,0x59,0x4E},
    {0x7C,0x12,0x11,0x12,0x7C},
    {0x7F,0x49,0x49,0x49,0x36},
    {0x3E,0x41,0x41,0x41,0x22},
    {0x7F,0x41,0x41,0x41,0x3E},
    {0x7F,0x49,0x49,0x49,0x41},
    {0x7F,0x09,0x09,0x09,0x01},
    {0x3E,0x41,0x41,0x51,0x73},
    {0x7F,0x08,0x08,0x08,0x7F},
    {0x00,0x41,0x7F,0x41,0x00},
    {0x20,0x40,0x41,0x3F,0x01},
    {0x7F,0x08,0x14,0x22,0x41},
    {0x7F,0x40,0x40,0x40,0x40},
    {0x7F,0x02,0x1C,0x02,0x7F},
    {0x7F,0x04,0x08,0x10,0x7F},
    {0x3E,0x41,0x41,0x41,0x3E},
    {0x7F,0x09,0x09,0x09,0x06},
    {0x3E,0x41,0x51,0x21,0x5E},
    {0x7F,0x09,0x19,0x29,0x46},
    {0x26,0x49,0x49,0x49,0x32},
    {0x03,0x01,0x7F,0x01,0x03},
    {0x3F,0x40,0x40,0x40,0x3F},
    {0x1F,0x20,0x40,0x20,0x1F},
    {0x3F,0x40,0x38,0x40,0x3F},
    {0x63,0x14,0x08,0x14,0x63},
    {0x03,0x04,0x78,0x04,0x03},
    {0x61,0x59,0x49,0x4D,0x43},
    {0x00,0x7F,0x41,0x41,0x41},
    {0x02,0x04,0x08,0x10,0x20},
    {0x00,0x41,0x41,0x41,0x7F},
    {0x04,0x02,0x01,0x02,0x04},
    {0x40,0x40,0x40,0x40,0x40},
    {0x00,0x03,0x07,0x08,0x00},
    {0x20,0x54,0x54,0x78,0x40},
    {0x7F,0x28,0x44,0x44,0x38},
    {0x38,0x44,0x44,0x44,0x28},
    {0x38,0x44,0x44,0x28,0x7F},
    {0x38,0x54,0x54,0x54,0x18},
    {0x00,0x08,0x7E,0x09,0x02},
    {0x18,0xA4,0xA4,0x9C,0x78},
    {0x7F,0x08,0x04,0x04,0x78},
    {0x00,0x44,0x7D,0x40,0x00},
    {0x20,0x40,0x40,0x3D,0x00},
    {0x7F,0x10,0x28,0x44,0x00},
    {0x00,0x41,0x7F,0x40,0x00},
    {0x7C,0x04,0x78,0x04,0x78},
    {0x7C,0x08,0x04,0x04,0x78},
    {0x38,0x44,0x44,0x44,0x38},
    {0xFC,0x18,0x24,0x24,0x18},
    {0x18,0x24,0x24,0x18,0xFC},
    {0x7C,0x08,0x04,0x04,0x08},
    {0x48,0x54,0x54,0x54,0x24},
    {0x04,0x04,0x3F,0x44,0x24},
    {0x3C,0x40,0x40,0x20,0x7C},
    {0x1C,0x20,0x40,0x20,0x1C},
    {0x3C,0x40,0x30,0x40,0x3C},
    {0x44,0x28,0x10,0x28,0x44},
    {0x4C,0x90,0x90,0x90,0x7C},
    {0x44,0x64,0x54,0x4C,0x44},
    {0x00,0x08,0x36,0x41,0x00},
    {0x00,0x00,0x77,0x00,0x00},
    {0x00,0x41,0x36,0x08,0x00},
    {0x08,0x04,0x08,0x10,0x08}
};

typedef struct { unsigned int color; short x, y, z; } V2;

static void rect2d(int x0, int y0, int x1, int y1, unsigned int color) {
    V2 *v = (V2 *)sceGuGetMemory(2 * sizeof(V2));
    v[0].color = color; v[0].x = x0; v[0].y = y0; v[0].z = 0;
    v[1].color = color; v[1].x = x1; v[1].y = y1; v[1].z = 0;
    sceGuDrawArray(GU_SPRITES, GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_2D, 2, 0, v);
}

/* texto desenhado pela GPU (fonte 5x7), funciona em qualquer emulador */
static void ui_text(int x, int y, int sc, unsigned int color, const char *s) {
    int n = 0;
    for (const char *p = s; *p; p++) {
        int c = (unsigned char)*p; if (c < 32 || c > 126) c = 32;
        for (int col = 0; col < 5; col++) {
            unsigned char b = font5x7[c - 32][col]; int in = 0;
            for (int r = 0; r < 8; r++) {
                int bit = (b >> r) & 1;
                if (bit && !in) n++;
                in = bit;
            }
        }
    }
    if (n == 0) return;
    V2 *v = (V2 *)sceGuGetMemory(n * 2 * sizeof(V2));
    int k = 0, cx = x;
    for (const char *p = s; *p; p++) {
        int c = (unsigned char)*p; if (c < 32 || c > 126) c = 32;
        for (int col = 0; col < 5; col++) {
            unsigned char b = font5x7[c - 32][col]; int start = -1;
            for (int r = 0; r <= 8; r++) {
                int bit = (r < 8) ? ((b >> r) & 1) : 0;
                if (bit && start < 0) start = r;
                else if (!bit && start >= 0) {
                    v[k].color = color; v[k].x = cx + col * sc; v[k].y = y + start * sc; v[k].z = 0; k++;
                    v[k].color = color; v[k].x = cx + (col + 1) * sc; v[k].y = y + r * sc; v[k].z = 0; k++;
                    start = -1;
                }
            }
        }
        cx += 6 * sc;
    }
    sceGuDrawArray(GU_SPRITES, GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_2D, k, 0, v);
}

static void ui_shadow(int x, int y, int sc, unsigned int color, const char *s) {
    ui_text(x, y, sc, color, s);
}

/* ---------- personagens e armas em 3D ---------- */
typedef struct { float ox, oy, oz, sx, sy, sz; unsigned int col; } WPart;
static const WPart WPARTS[NW][4] = {
    { { 0, 0, 0, 0, 0, 0, 0 } },
    { { 0, -0.04f, 0.5f, 0.09f, 0.09f, 1.0f, 0xff3a6a9a }, { 0, -0.07f, 0.95f, 0.14f, 0.14f, 0.3f, 0xff3a6a9a } },
    { { 0, -0.035f, 0.5f, 0.07f, 0.07f, 1.0f, 0xff3a6a9a }, { 0, -0.17f, 0.92f, 0.05f, 0.34f, 0.24f, 0xffa0a8a8 } },
    { { 0, -0.06f, 0.55f, 0.03f, 0.12f, 0.9f, 0xffd0d8d8 }, { 0, -0.04f, 0.05f, 0.05f, 0.08f, 0.22f, 0xff202830 } },
    { { 0, -0.05f, 0.25f, 0.10f, 0.14f, 0.45f, 0xff484848 }, { 0, -0.2f, 0.08f, 0.09f, 0.2f, 0.12f, 0xff202020 } },
    { { 0, -0.07f, 0.3f, 0.12f, 0.16f, 0.7f, 0xff404040 }, { 0, -0.3f, 0.25f, 0.08f, 0.24f, 0.1f, 0xff202020 },
      { 0, -0.05f, -0.2f, 0.08f, 0.1f, 0.3f, 0xff303030 } },
    { { 0, -0.04f, 0.6f, 0.09f, 0.09f, 1.2f, 0xff606870 }, { 0, -0.12f, -0.2f, 0.11f, 0.18f, 0.55f, 0xff3a6a9a },
      { 0, -0.1f, 0.55f, 0.13f, 0.12f, 0.3f, 0xff2a4a6a } },
    { { 0, -0.03f, 0.7f, 0.07f, 0.07f, 1.3f, 0xff505860 }, { 0, -0.09f, 0.05f, 0.11f, 0.16f, 0.65f, 0xff2a4a6a },
      { 0, 0.06f, 0.35f, 0.06f, 0.06f, 0.3f, 0xff101010 }, { 0, -0.1f, -0.35f, 0.1f, 0.2f, 0.4f, 0xff2a4a6a } }
};
static const int WPN[NW] = { 0, 2, 2, 2, 2, 3, 3, 4 };

/* arma presa na mao: pivo no ombro, gira com o braco; com pitch=-90 graus o eixo z da arma aponta para frente */
static void weapon_part(float x, float y, float z, float yaw, float pitch, float armLen, const WPart *p) {
    ScePspFVector3 t1 = { x, y, z }, t2 = { 0, -armLen, 0 }, t3 = { p->ox, p->oy, p->oz }, s = { p->sx, p->sy, p->sz };
    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();
    sceGumTranslate(&t1); sceGumRotateY(yaw); sceGumRotateX(pitch); sceGumTranslate(&t2);
    sceGumRotateX(1.5708f); sceGumTranslate(&t3); sceGumScale(&s);
    sceGumUpdateMatrix();
    sceGuColor(p->col);
    sceGumDrawArray(GU_TRIANGLES, VTX_TYPE, 36, 0, cube);
}
static void draw_weapon(int w, float x, float y, float z, float yaw, float pitch, float armLen) {
    for (int k = 0; k < WPN[w]; k++) weapon_part(x, y, z, yaw, pitch, armLen, &WPARTS[w][k]);
}

/* parte do corpo: origem no chao, gira com o personagem; hang=1 faz o bloco pender do pivo (bracos e pernas) */
static void body_part(float x, float z, float yaw, float ox, float oy, float oz, float pitch, int hang,
                      float sx, float sy, float sz, unsigned int col) {
    ScePspFVector3 t1 = { x, 0, z }, t2 = { ox, oy, oz }, t3 = { 0, -sy, 0 }, s = { sx, sy, sz };
    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();
    sceGumTranslate(&t1); sceGumRotateY(yaw); sceGumTranslate(&t2);
    if (pitch != 0.0f) sceGumRotateX(pitch);
    if (hang) sceGumTranslate(&t3);
    sceGumScale(&s);
    sceGumUpdateMatrix();
    sceGuColor(col);
    sceGumDrawArray(GU_TRIANGLES, VTX_TYPE, 36, 0, cube);
}

typedef struct { unsigned int skin, shirt, pants, hair, eye; } HPal;
static const HPal HPAL_PLAYER = { 0xff90b8f0, 0xff405830, 0xff604838, 0xff2040a0, 0xff202020 };
static const HPal HPAL_Z[3] = {
    { 0xff60b080, 0xff506890, 0xff404858, 0xff203030, 0xff2020e0 },
    { 0xff70c090, 0xff3030a0, 0xff303038, 0xff202040, 0xff30d0ff },
    { 0xff508060, 0xff403060, 0xff303038, 0xff182018, 0xff2020e0 }
};
static const HPal HPAL_HIT = { 0xffd0d0ff, 0xffc0c0ff, 0xffb0b0ff, 0xffd0d0ff, 0xff20e0ff };
static const float Z_K[3] = { 1.0f, 0.95f, 1.45f };

static void draw_humanoid(float x, float z, float yaw, float k, const HPal *p, float legSw, float armL, float armR,
                          int weapon, int lod, int player) {
    float c = cosf(yaw), s = sinf(yaw);
    if (lod) {
        body_part(x, z, yaw, 0, 0.0f, 0, 0, 0, 0.55f * k, 1.55f * k, 0.32f * k, p->shirt);
        body_part(x, z, yaw, 0, 1.5f * k, 0, 0, 0, 0.42f * k, 0.42f * k, 0.42f * k, p->skin);
        return;
    }
    body_part(x, z, yaw, -0.16f * k, 0.85f * k, 0, legSw, 1, 0.24f * k, 0.85f * k, 0.26f * k, p->pants);
    body_part(x, z, yaw,  0.16f * k, 0.85f * k, 0, -legSw, 1, 0.24f * k, 0.85f * k, 0.26f * k, p->pants);
    body_part(x, z, yaw, 0, 0.85f * k, 0, 0, 0, 0.62f * k, 0.7f * k, 0.34f * k, p->shirt);
    body_part(x, z, yaw, 0, 1.55f * k, 0, 0, 0, 0.42f * k, 0.4f * k, 0.4f * k, p->skin);
    body_part(x, z, yaw, 0, 1.93f * k, 0, 0, 0, 0.46f * k, 0.12f * k, 0.46f * k, p->hair);
    body_part(x, z, yaw, -0.1f * k, 1.72f * k, 0.2f * k, 0, 0, 0.07f * k, 0.07f * k, 0.05f * k, p->eye);
    body_part(x, z, yaw,  0.1f * k, 1.72f * k, 0.2f * k, 0, 0, 0.07f * k, 0.07f * k, 0.05f * k, p->eye);
    if (player) {
        body_part(x, z, yaw, 0, 1.86f * k, 0.25f * k, 0, 0, 0.32f * k, 0.05f * k, 0.18f * k, p->hair);
        body_part(x, z, yaw, 0, 0.95f * k, -0.26f * k, 0, 0, 0.44f * k, 0.52f * k, 0.2f * k, 0xff304048);
    }
    body_part(x, z, yaw, -0.40f * k, 1.5f * k, 0, armR, 1, 0.18f * k, 0.7f * k, 0.18f * k, p->shirt);
    body_part(x, z, yaw,  0.40f * k, 1.5f * k, 0, armL, 1, 0.18f * k, 0.7f * k, 0.18f * k, p->shirt);
    if (weapon > 0) draw_weapon(weapon, x - 0.40f * k * c, 1.5f * k, z + 0.40f * k * s, yaw, armR, 0.7f * k);
}

static void draw_corpse(float x, float z, float yaw, float k, const HPal *p) {
    body_part(x, z, yaw, 0, 0.2f * k, 0, -1.5708f, 0, 0.6f * k, 1.55f * k, 0.34f * k, p->shirt);
    body_part(x, z, yaw, 0, 0.05f * k, -1.75f * k, 0, 0, 0.42f * k, 0.42f * k, 0.42f * k, p->skin);
}

/* ---------- cena ---------- */
static void fence_line(float x1, float z1, float x2, float z2) {
    float dx = x2 - x1, dz = z2 - z1, len = sqrtf(dx * dx + dz * dz);
    float mx = (x1 + x2) * 0.5f, mz = (z1 + z2) * 0.5f;
    if (dist2d(px, pz, mx, mz) > len * 0.5f + 45.0f) return;
    float ang = atan2f(dx, dz); int n = (int)(len / 6.0f); if (n < 1) n = 1;
    box(mx, 0.5f, mz, 0.12f, 0.12f, len, ang, 0xff505860);
    box(mx, 1.1f, mz, 0.12f, 0.12f, len, ang, 0xff505860);
    for (int i = 0; i <= n; i++) box(x1 + dx * i / n, 0, z1 + dz * i / n, 0.25f, 1.5f, 0.25f, 0, 0xff303840);
}

static void draw_car(float x, float z, float ang, int idx) {
    static const unsigned int cc[4] = { 0xff3040a0, 0xff707878, 0xff305870, 0xff404040 };
    float c = cosf(ang), s = sinf(ang);
    box(x, 0.35f, z, 2.2f, 0.9f, 4.2f, ang, cc[idx % 4]);
    box(x - s * 0.2f, 1.25f, z - c * 0.2f, 1.9f, 0.8f, 2.2f, ang, 0xff202830);
    for (int a = -1; a <= 1; a += 2) for (int b = -1; b <= 1; b += 2) {
        float lx = a * 1.1f, lz = b * 1.3f;
        box(x + lx * c + lz * s, 0, z - lx * s + lz * c, 0.3f, 0.6f, 0.6f, ang, 0xff101010);
    }
}

static void draw_building(const Bld *b) {
    float dz = b->z + b->door * (b->hd + 0.05f);
    box(b->x, 0, b->z, b->hw * 2, b->h, b->hd * 2, 0, b->wall);
    box(b->x, b->h, b->z, b->hw * 2 + 0.5f, 0.6f, b->hd * 2 + 0.5f, 0, b->roof);
    if (b->h > 10.0f) return;
    box(b->x, 0, dz, 1.2f, 2.2f, 0.12f, 0, 0xff203040);
    box(b->x - b->hw * 0.55f, 1.6f, dz, 0.9f, 0.9f, 0.1f, 0, 0xff101418);
    box(b->x + b->hw * 0.55f, 1.6f, dz, 0.9f, 0.9f, 0.1f, 0, 0xff101418);
}

static void draw_world_pois(void) {
    /* roads */
    for (int i = 0; i < NROADS; i++) {
        float x1 = ROADS[i][0], z1 = ROADS[i][1], x2 = ROADS[i][2], z2 = ROADS[i][3];
        float mx = (x1 + x2) * 0.5f, mz = (z1 + z2) * 0.5f;
        float lx = fabsf(x2 - x1), lz = fabsf(z2 - z1);
        if (dist2d(px, pz, mx, mz) > (lx > lz ? lx : lz) * 0.5f + 45.0f) continue;
        if (lx > lz) box(mx, 0.04f, mz, lx, 0.08f, 5.0f, 0, 0xff484c50);
        else box(mx, 0.04f, mz, 5.0f, 0.08f, lz, 0, 0xff484c50);
    }
    /* lago */
    if (NEAR(LAKE_X, LAKE_Z, 70)) box(LAKE_X, 0.02f, LAKE_Z, LAKE_HX * 2, 0.1f, LAKE_HZ * 2, 0, 0xffa06030);
    /* cidade, posto, fazenda, torre, base militar */
    for (int i = 0; i < NB; i++) if (NEAR(BLD[i].x, BLD[i].z, 75)) draw_building(&BLD[i]);
    for (int i = 0; i < NCARS; i++) if (NEAR(CARS[i][0], CARS[i][1], 60)) draw_car(CARS[i][0], CARS[i][1], CARS[i][2], i);
    if (NEAR(-35, -52, 70)) {
        box(-33, 3.2f, -50, 6.0f, 0.4f, 4.0f, 0, 0xff2020b0);
        box(-35.5f, 0, -48.5f, 0.3f, 3.2f, 0.3f, 0, 0xffc0c0c0); box(-30.5f, 0, -48.5f, 0.3f, 3.2f, 0.3f, 0, 0xffc0c0c0);
        box(-34, 0, -50, 0.7f, 1.4f, 0.7f, 0, 0xff2020d0); box(-32, 0, -50, 0.7f, 1.4f, 0.7f, 0, 0xff2020d0);
    }
    if (NEAR(66, 24, 70)) { box(66, 0, 24, 2.5f, 7.0f, 2.5f, 0, 0xffa0a8a8); box(66, 7.0f, 24, 2.8f, 0.8f, 2.8f, 0, 0xff303030); }
    if (NEAR(-65, -15, 80)) {
        box(-65, 16.0f, -15, 4.5f, 0.6f, 4.5f, 0, 0xff404850);
        box(-65, 16.6f, -15, 0.2f, 6.0f, 0.2f, 0, 0xffc0c0c0);
        box(-65, 22.4f, -15, 0.6f, 0.6f, 0.6f, 0, (((int)(gameTime * 2.0f)) & 1) ? 0xff2020ff : 0xff202060);
    }
    if (NEAR(68, 53, 80)) {
        fence_line(54, 39, 82, 39); fence_line(82, 39, 82, 67); fence_line(82, 67, 54, 67); fence_line(54, 67, 54, 39);
        box(66, 0, 60, 4.0f, 2.2f, 3.0f, 0, 0xff486038); box(72, 0, 46, 4.0f, 2.2f, 3.0f, 0, 0xff486038);
        for (int a = -1; a <= 1; a += 2) for (int b = -1; b <= 1; b += 2) box(80 + a * 0.9f, 0, 42 + b * 0.9f, 0.25f, 6.0f, 0.25f, 0, 0xff505858);
        box(80, 6.0f, 42, 2.8f, 0.4f, 2.8f, 0, 0xff303838);
        box(76, 0.05f, 46, 6.0f, 0.1f, 6.0f, 0, 0xff303030); box(76, 0.12f, 46, 0.5f, 0.05f, 3.0f, 0, 0xffe0e0e0);
        box(76, 0.12f, 46, 2.0f, 0.05f, 0.5f, 0, 0xffe0e0e0);
    }
}

static void draw_base(void) {
    float fl = 0.5f + 0.5f * sinf(gameTime * 12.0f);
    box(BASE_X, 0.05f, BASE_Z, 1.4f, 0.3f, 0.3f, 0.6f, 0xff203050); box(BASE_X, 0.05f, BASE_Z, 1.4f, 0.3f, 0.3f, -0.6f, 0xff203050);
    box(BASE_X, 0.2f, BASE_Z, 0.7f, 0.5f + fl * 0.5f, 0.7f, 0, 0xff2080ff);
    box(BASE_X, 0.5f, BASE_Z, 0.4f, 0.4f + fl * 0.4f, 0.4f, 0, 0xff30e0ff);
    box(-5, 0, -4, 3.6f, 1.8f, 3.0f, 0, 0xff486038); box(-5, 1.8f, -4, 3.6f, 0.4f, 1.0f, 0, 0xff384828);
    box(5, 0, -4, 1.8f, 1.0f, 1.0f, 0, 0xff3a6a9a); box(5, 1.0f, -4, 0.8f, 0.5f, 0.6f, 0, 0xff303030);
    box(5.6f, 1.0f, -4, 0.1f, 2.4f, 0.1f, 0, 0xffc0c0c0);
    box(HELI_X, 0.05f, HELI_Z, 7.0f, 0.1f, 7.0f, 0, 0xff404040);
    box(HELI_X, 0.12f, HELI_Z, 0.5f, 0.05f, 3.0f, 0, 0xffe0e0e0); box(HELI_X, 0.12f, HELI_Z, 2.0f, 0.05f, 0.5f, 0, 0xffe0e0e0);
    if (rescue == 0) for (int i = 0; i < 16; i++) {
        float a = i * (PI_F / 8.0f);
        box(BASE_X + cosf(a) * SAFE_R, 0, BASE_Z + sinf(a) * SAFE_R, 0.4f, 0.5f, 0.4f, 0, 0xffa0e0a0);
    }
    if (rescue == 2) {
        float t = 90.0f - rescueT, y = (t < 5.0f) ? (1.0f - t / 5.0f) * 22.0f : 0.0f;
        box(HELI_X, 0.4f + y, HELI_Z, 2.2f, 1.8f, 5.0f, 0, 0xff405030);
        box(HELI_X, 1.1f + y, HELI_Z - 4.5f, 0.6f, 0.6f, 4.0f, 0, 0xff405030);
        box(HELI_X - 1.3f, 0.2f + y, HELI_Z, 0.15f, 0.15f, 4.5f, 0, 0xff202020); box(HELI_X + 1.3f, 0.2f + y, HELI_Z, 0.15f, 0.15f, 4.5f, 0, 0xff202020);
        box(HELI_X, 2.2f + y, HELI_Z, 9.0f, 0.08f, 0.5f, gameTime * 25.0f, 0xff202020);
        box(HELI_X, 2.2f + y, HELI_Z, 9.0f, 0.08f, 0.5f, gameTime * 25.0f + 1.57f, 0xff202020);
    }
}

static void draw_scene(void) {
    float dl = daylight(), nt = nightness();
    float b = 0.2f + 0.8f * dl;
    float skyR = 110.0f * (1 - nt) + 12.0f * nt, skyG = 190.0f * (1 - nt) + 18.0f * nt, skyB = 235.0f * (1 - nt) + 40.0f * nt;
    unsigned int sky = 0xff000000 | ((unsigned int)skyB << 16) | ((unsigned int)skyG << 8) | (unsigned int)skyR;
    sceGuClearColor(sky);
    sceGuClearDepth(0);
    sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT | GU_FAST_CLEAR_BIT);

    sceGumMatrixMode(GU_PROJECTION);
    sceGumLoadIdentity();
    sceGumPerspective(70.0f, 16.0f / 9.0f, 1.0f, 200.0f);
    sceGumMatrixMode(GU_VIEW);
    sceGumLoadIdentity();
    ScePspFVector3 eye = { px - sinf(cam) * 11.0f, 7.0f, pz - cosf(cam) * 11.0f };
    ScePspFVector3 ctr = { px, 1.5f, pz };
    ScePspFVector3 up  = { 0, 1, 0 };
    sceGumLookAt(&eye, &ctr, &up);

    sceGuEnable(GU_LIGHTING);
    sceGuEnable(GU_LIGHT0);
    ScePspFVector3 dir = { 0.4f, 1.0f, 0.3f };
    sceGuLight(0, GU_DIRECTIONAL, GU_DIFFUSE, &dir);
    unsigned int lc = 0xff000000 | ((unsigned int)(165 * b) << 16) | ((unsigned int)(165 * b) << 8) | (unsigned int)(165 * b);
    sceGuLightColor(0, GU_DIFFUSE, lc);
    sceGuAmbient(0xff000000 | ((unsigned int)(105 * b) << 16) | ((unsigned int)(105 * b) << 8) | (unsigned int)(105 * b));
    sceGuColorMaterial(GU_AMBIENT | GU_DIFFUSE);

    groundC1 = 0xff2e7a3e; groundC2 = 0xff296f38;
    draw_ground();
    draw_world_pois();
    draw_base();

    /* caixas de saque */
    for (int i = 0; i < nloot; i++) {
        Loot *L = &loot[i];
        if (L->looted) continue;
        float d = dist2d(px, pz, L->x, L->z);
        if (L->kind == 3) {
            if (d < 95.0f) box(L->x, 0, L->z, 0.5f, 14.0f, 0.5f, 0, 0xff30e0ff);
            if (d < 70.0f) box(L->x, 0, L->z, 1.0f, 0.8f, 1.0f, 0, 0xff30c0ff);
        } else if (d < 40.0f && L->kind != 1) {
            box(L->x, 0, L->z, 0.9f, 0.7f, 0.9f, 0, 0xff3a6a9a);
            box(L->x, 0.9f + 0.15f * sinf(gameTime * 3.0f + i), L->z, 0.35f, 0.35f, 0.35f, 0, L->kind == 2 ? 0xff40e040 : 0xff30e0ff);
        } else if (d < 25.0f && L->kind == 1) {
            box(L->x - 2.2f, 2.0f + 0.15f * sinf(gameTime * 3.0f + i), L->z - 1.0f, 0.35f, 0.35f, 0.35f, 0, 0xff30e0ff);
        }
    }

    /* armas coletaveis no chao */
    for (int i = 0; i < NWP; i++) {
        WPick *wp = &wpick[i];
        if (wp->taken) continue;
        float d = dist2d(px, pz, wp->x, wp->z);
        if (d > 60.0f) continue;
        float by = 0.9f + 0.12f * sinf(gameTime * 2.5f + i);
        draw_weapon(wp->w, wp->x, by, wp->z, gameTime * 1.5f, -1.5708f, 0.0f);
        box(wp->x, 0.0f, wp->z, 0.9f, 0.08f, 0.9f, 0, 0xffe0e040);
        if (d < 45.0f) box(wp->x, 0.0f, wp->z, 0.12f, 6.0f, 0.12f, 0, 0xffe0e040);
    }

    /* arvores */
    for (int i = 0; i < NTREES; i++) {
        float tdx = treeX[i] - px, tdz = treeZ[i] - pz;
        if (tdx * tdx + tdz * tdz > 55.0f * 55.0f) continue;
        box(treeX[i], 0, treeZ[i], 0.7f, treeH[i], 0.7f, 0, 0xff203848);
        if (i % 4 == 0) box(treeX[i] + 0.5f, treeH[i] - 1.2f, treeZ[i], 1.4f, 0.25f, 0.25f, 0.5f, 0xff203848);
        else box(treeX[i], treeH[i] - 0.6f, treeZ[i], 3.0f, 2.6f, 3.0f, 0.4f * i, (i % 3 == 0) ? 0xff1c5a30 : 0xff1a4a28);
    }

    /* efeitos de combate */
    if (swingT > 0) {
        float fx = sinf(pang), fz = cosf(pang);
        box(px + fx * 1.6f, 0.9f, pz + fz * 1.6f, 1.6f, 0.15f, 0.3f, pang + 1.57f + swingT * 8.0f, 0xffe0f0ff);
    }
    if (shotT > 0) {
        float mx = (shotX1 + shotX2) * 0.5f, mz = (shotZ1 + shotZ2) * 0.5f, len = dist2d(shotX1, shotZ1, shotX2, shotZ2);
        box(mx, 1.2f, mz, 0.08f, 0.08f, len, atan2f(shotX2 - shotX1, shotZ2 - shotZ1), 0xff60f0ff);
        box(shotX1, 1.1f, shotZ1, 0.4f, 0.4f, 0.4f, 0, 0xff30e0ff);
    }

    /* sombras */
    box(px, 0.1f, pz, 1.0f, 0.02f, 0.6f, 0, 0xff000000);
    for (int i = 0; i < NZ; i++)
        if (zomb[i].active && zomb[i].state != 2 && NEAR(zomb[i].x, zomb[i].z, 50))
            box(zomb[i].x, 0.1f, zomb[i].z, 1.0f * Z_K[zomb[i].type], 0.02f, 0.7f * Z_K[zomb[i].type], 0, 0xff000000);

    /* zumbis em 3D */
    for (int i = 0; i < NZ; i++) {
        Zomb *z = &zomb[i];
        if (!z->active) continue;
        float d = dist2d(px, pz, z->x, z->z);
        if (d > 60.0f) continue;
        float k = Z_K[z->type];
        if (z->state == 2) { if (z->corpse > 0) draw_corpse(z->x, z->z, z->ang, k, &HPAL_Z[z->type]); continue; }
        float ph = gameTime * (z->state == 1 ? 7.0f : 2.5f) + i * 1.7f;
        float sw = sinf(ph) * (z->state == 1 ? 0.8f : 0.35f);
        float arm = -1.35f + sinf(ph * 0.7f) * 0.15f;
        if (z->atk > 0.6f) arm = -1.95f + sinf(gameTime * 20.0f) * 0.2f;
        draw_humanoid(z->x, z->z, z->ang, k, z->hurt > 0 ? &HPAL_HIT : &HPAL_Z[z->type], sw, arm, arm, 0, d > 38.0f, 0);
    }

    /* jogador em 3D */
    {
        float sw = moving ? sinf(walkPhase) * 0.8f : 0.0f, aR, aL;
        if (curW >= 4) { aR = -1.45f + (shotT > 0 ? 0.18f : 0.0f); aL = -1.25f; }
        else {
            aR = -0.7f - sw * 0.4f; aL = sw * 0.6f;
            if (swingT > 0) aR = -2.6f + (1.0f - swingT / 0.18f) * 2.8f;
        }
        if (mode == 3) draw_corpse(px, pz, pang, 1.0f, &HPAL_PLAYER);
        else draw_humanoid(px, pz, pang, rollT > 0 ? 0.8f : 1.0f, hurtT > 0.25f ? &HPAL_HIT : &HPAL_PLAYER, sw, aL, aR, curW, 0, 1);
    }

    /* na tela de titulo: zumbis ao lado, olhando para o personagem */
    if (appState != APP_PLAY && appState != APP_SPLASH && appState != APP_POST) {
        float rx = -cosf(cam), rz = sinf(cam), fx2 = sinf(cam), fz2 = cosf(cam);
        float sw = sinf(appT * 3.0f) * 0.3f;
        float zx = px - rx * 8.0f + fx2 * 1.0f, zz = pz - rz * 8.0f + fz2 * 1.0f;
        draw_humanoid(zx, zz, atan2f(px - zx, pz - zz), 1.0f, &HPAL_Z[0], sw, -1.35f, -1.35f, 0, 0, 0);
        float wx = px + rx * 8.5f + fx2 * 2.0f, wz = pz + rz * 8.5f + fz2 * 2.0f;
        draw_humanoid(wx, wz, atan2f(px - wx, pz - wz), 1.45f, &HPAL_Z[2], -sw, -1.35f, -1.35f, 0, 0, 0);
    }
    sceGuDisable(GU_LIGHTING);
}

/* ---------- interface ---------- */
static void window(int x0, int y0, int x1, int y1) {
    rect2d(x0, y0, x1, y1, 0xf060c0f0);
    rect2d(x0 + 4, y0 + 4, x1 - 4, y1 - 4, 0xf0201008);
}
static void ui_center(int y, int sc, unsigned int col, const char *str) {
    ui_text((480 - (int)strlen(str) * 6 * sc) / 2, y, sc, col, str);
}
static void bar(int x, int y, int w, int h, float v, unsigned int col) {
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    rect2d(x - 1, y - 1, x + w + 1, y + h + 1, 0xff000000);
    rect2d(x, y, x + w, y + h, 0xff383838);
    if (v > 0) rect2d(x, y, x + (int)(w * v / 100.0f), y + h, col);
}

static void minimap_dot(float wx, float wz, int sz, unsigned int col, int clampEdge) {
    const int X0 = 408, Y0 = 4, S = 68; const float k = (S / 2.0f) / 60.0f;
    float dx = wx - px, dz = wz - pz;
    float sx = (dx * (-cosf(cam)) + dz * sinf(cam)) * k, sy = (dx * sinf(cam) + dz * cosf(cam)) * k;
    float lim = S / 2.0f - 3.0f;
    if (sx > lim || sx < -lim || sy > lim || sy < -lim) {
        if (!clampEdge) return;
        float m = fmaxf(fabsf(sx), fabsf(sy)); sx = sx / m * lim; sy = sy / m * lim;
    }
    int ix = X0 + S / 2 + (int)sx, iy = Y0 + S / 2 - (int)sy;
    rect2d(ix - sz / 2, iy - sz / 2, ix + sz - sz / 2, iy + sz - sz / 2, col);
}

static void draw_minimap(void) {
    rect2d(406, 2, 478, 74, 0xe0000000);
    rect2d(408, 4, 476, 72, 0xff2a3a2a);
    minimap_dot(BASE_X, BASE_Z, 4, 0xff40d040, 1);
    if (rescue > 0) minimap_dot(HELI_X, HELI_Z, 4, 0xffe08040, 1);
    for (int i = 0; i < nloot; i++) if (loot[i].kind == 3 && !loot[i].looted) minimap_dot(loot[i].x, loot[i].z, 3, 0xff30e0ff, 1);
    for (int i = 0; i < NWP; i++) if (!wpick[i].taken) minimap_dot(wpick[i].x, wpick[i].z, 3, 0xffe040e0, 0);
    for (int i = 0; i < NZ; i++) if (zomb[i].active && zomb[i].state != 2) minimap_dot(zomb[i].x, zomb[i].z, 2, zomb[i].state == 1 ? 0xff2020ff : 0xff3030a0, 0);
    rect2d(440, 36, 445, 41, 0xffffffff);
}

static void draw_ui(void) {
    char buf[96];
    sceGuDisable(GU_DEPTH_TEST);
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);

    /* barras de status */
    rect2d(0, 0, 204, 84, 0x90000000);
    ui_text(4, 4, 1, 0xffffffff, "VIDA");  bar(44, 4, 150, 7, hp, 0xff3030e0);
    ui_text(4, 16, 1, 0xffffffff, "FOME"); bar(44, 16, 150, 7, hunger, 0xff30a0f0);
    ui_text(4, 28, 1, 0xffffffff, "SEDE"); bar(44, 28, 150, 7, thirst, 0xffe08030);
    ui_text(4, 40, 1, 0xffffffff, "ENER"); bar(44, 40, 150, 7, stam, 0xff40d0a0);
    int hh = (int)hour_now(), mm = (int)((hour_now() - hh) * 60.0f);
    sprintf(buf, "Dia %d  %02d:%02d  Abates:%d", day_number(), hh, mm, kills);
    ui_text(4, 54, 1, 0xffc0ffc0, buf);
    if (rescue == 0) { if (parts < 4) sprintf(buf, "Obj: achar pecas de radio %d/4", parts); else strcpy(buf, "Obj: use o radio no acampamento"); }
    else if (rescue == 1) strcpy(buf, "Obj: sobreviva ate o resgate!");
    else strcpy(buf, "Obj: embarque no helicoptero!");
    ui_text(4, 66, 1, 0xff40e0ff, buf);
    draw_minimap();
    rect2d(330, 76, 476, 100, 0x90000000);
    if (wAmmo[curW] >= 0) sprintf(buf, "%s  %s:%d", wName[curW], itemName[wAmmo[curW]], inv[wAmmo[curW]]); else strcpy(buf, wName[curW]);
    ui_text(334, 80, 1, 0xffffffff, buf);
    { static const int ci[3] = { I_COM, I_AGUA, I_BAND };
      sprintf(buf, "< %s x%d >  Cima: usar", itemName[ci[selUse]], inv[ci[selUse]]);
      ui_text(334, 90, 1, 0xffc0c0c0, buf); }
    if (rescue == 1 || rescue == 2) {
        rect2d(210, 4, 402, 24, 0xb0000000);
        if (rescue == 1) sprintf(buf, "RESGATE: helicoptero em %ds!", (int)rescueT + 1);
        else sprintf(buf, "HELICOPTERO: embarque em %ds!", (int)rescueT + 1);
        ui_text(214, 10, 1, 0xff40e0ff, buf);
    }

    if (mode == 0) {
        if (showHelp) {
            rect2d(0, 86, 250, 126, 0x90000000);
            ui_text(4, 90, 1, 0xffffffff, "Analog:andar  L/R:camera  X:interagir");
            ui_text(4, 100, 1, 0xffffffff, "Quad:atacar  Circ:rolar  Tri:trocar arma");
            ui_text(4, 110, 1, 0xffffffff, "Esq/Dir:item  Cima:usar  START:menu");
            ui_text(4, 118, 1, 0xff909090, "SELECT: esconder ajuda");
        }
        if (msgTimer > 0) {
            int w = (int)strlen(msg) * 12; if (w > 470) w = 470;
            int x = (480 - w) / 2;
            rect2d(x - 6, 186, x + w + 6, 208, 0xa0000000);
            ui_text(x, 190, 2, 0xffffffff, msg);
        }
        const char *pr = NULL; char pb[64];
        if (near_heli()) pr = "X: embarcar no helicoptero!";
        else if (near_camp()) pr = "ACAMPAMENTO  X: abrir";
        else if (nearest_wpick() >= 0) { sprintf(pb, "X: pegar %s", wName[wpick[nearest_wpick()].w]); pr = pb; }
        else {
            int l = nearest_loot();
            if (l >= 0) pr = loot[l].kind == 3 ? "X: pegar peca de radio!" : (loot[l].kind == 1 ? "X: revistar carro" : "X: revistar");
            else if (near_lake()) pr = "X: beber agua";
            else {
                float dd; int zi = nearest_zombie(5.0f, &dd);
                if (zi >= 0) { sprintf(pb, "Zumbi perto! Quad: atacar"); pr = pb; }
            }
        }
        if (pr) {
            int w = (int)strlen(pr) * 12, x = (480 - w) / 2;
            rect2d(x - 6, 238, x + w + 6, 266, 0xa0000000);
            ui_text(x, 245, 2, 0xff40e0ff, pr);
        }
        if (hurtT > 0) rect2d(0, 0, 480, 272, ((unsigned int)(hurtT / 0.4f * 110.0f) << 24) | 0x000000ff);
        if (hp < 25 && ((int)(gameTime * 3.0f) & 1)) rect2d(0, 0, 480, 272, 0x300000ff);
    } else if (mode == 1) {
        static const char *opt[6] = { "Continuar", "", "", "Salvar e ir ao menu", "Menu sem salvar", "Sair do jogo" };
        rect2d(0, 0, 480, 272, 0x80000000);
        window(96, 30, 384, 242);
        ui_text(180, 38, 2, 0xff40e0ff, "== MENU ==");
        sprintf(buf, "%s - slot %d - %s", survNames[nameIdx], curSlot + 1, diffNames[difficulty]);
        ui_text(108, 56, 1, 0xffc0c0c0, buf);
        for (int i = 0; i < 6; i++) {
            int y = 68 + i * 20;
            if (i == menuSel) rect2d(106, y - 3, 374, y + 17, 0xc0a06020);
            if (i == 1) sprintf(buf, "%s Musica: %s", i == menuSel ? ">" : " ", musicOn ? "ligada" : "desligada");
            else if (i == 2) sprintf(buf, "%s Salvar (slot %d)", i == menuSel ? ">" : " ", curSlot + 1);
            else sprintf(buf, "%s %s", i == menuSel ? ">" : " ", opt[i]);
            ui_text(110, y, 2, 0xffffffff, buf);
        }
        ui_text(108, 194, 1, 0xffc0c0c0, "Cima/Baixo + X     START volta");
        if (menuMsg[0]) ui_text(110, 208, 2, 0xff60ff60, menuMsg);
    } else if (mode == 2) {
        rect2d(0, 0, 480, 272, 0x80000000);
        window(36, 24, 444, 254);
        ui_text(150, 30, 2, 0xff40e0ff, "== ACAMPAMENTO ==");
        for (int i = 0; i < 7; i++) {
            int y = 54 + i * 21;
            if (i == menuSel) rect2d(46, y - 3, 434, y + 16, 0xc0a06020);
            if (i == 0) strcpy(buf, "Bandagem (2 Pano)");
            else if (i == 1) strcpy(buf, "Municao x6 (2 Sucata)");
            else if (i == 2) strcpy(buf, "Cartuchos x4 (3 Sucata)");
            else if (i == 3) strcpy(buf, "Balas de rifle x3 (4 Sucata)");
            else if (i == 4) strcpy(buf, "Dormir ate amanhecer");
            else if (i == 5) sprintf(buf, "Radio: chamar resgate (%d/4)", parts);
            else strcpy(buf, "Sair");
            ui_text(56, y, 2, 0xffffffff, buf);
        }
        sprintf(buf, "Madeira:%d Sucata:%d Pano:%d Bandagem:%d Pecas:%d/4", inv[I_MAD], inv[I_SUC], inv[I_PANO], inv[I_BAND], parts);
        ui_text(46, 204, 1, 0xffc0ffc0, buf);
        sprintf(buf, "Municao:%d Cartuchos:%d Balas rifle:%d Comida:%d Agua:%d", inv[I_MUN], inv[I_CART], inv[I_RIFLE], inv[I_COM], inv[I_AGUA]);
        ui_text(46, 214, 1, 0xffc0ffc0, buf);
        ui_text(46, 224, 1, 0xffc0c0c0, "Cima/Baixo + X escolhe   START/O sai");
        if (menuMsg[0]) ui_text(46, 235, 2, 0xff60ff60, menuMsg);
    } else if (mode == 3) {
        rect2d(0, 0, 480, 272, 0xc0000020);
        ui_center(80, 4, 0xff2020e0, "VOCE MORREU");
        sprintf(buf, "Zumbis derrubados: %d    Mortes: %d", kills, deaths);
        ui_center(130, 1, 0xffc0c0c0, buf);
        if (deathT > 1.2f) {
            ui_center(160, 2, 0xffffffff, "X: renascer no acampamento");
            ui_center(190, 1, 0xffc0c0c0, "Voce perde metade dos itens ao renascer.");
        }
    }

    sceGuDisable(GU_BLEND);
    sceGuEnable(GU_DEPTH_TEST);
}

/* ---------- telas: abertura, titulo, novo jogo, carregar, creditos ---------- */
typedef struct { const char *t; int sc; unsigned int col; } CLine;
static const CLine CREDITS[] = {
    { "SOBREVIVENDO EM GS", 3, 0xff40e0ff },
    { "", 1, 0 },
    { "Um jogo desenvolvido por", 2, 0xffffffff },
    { "MECANIC ENGINE GAMER", 3, 0xfff0e040 },
    { "", 1, 0 },
    { "Direcao de jogo", 1, 0xffc0c0c0 },
    { "MECANIC ENGINE GAMER", 2, 0xffffffff },
    { "", 1, 0 },
    { "Arte e personagens", 1, 0xffc0c0c0 },
    { "MECANIC ENGINE GAMER", 2, 0xffffffff },
    { "", 1, 0 },
    { "Musica e efeitos sonoros", 1, 0xffc0c0c0 },
    { "MECANIC ENGINE GAMER", 2, 0xffffffff },
    { "", 1, 0 },
    { "Programacao", 1, 0xffc0c0c0 },
    { "MECANIC ENGINE GAMER", 2, 0xffffffff },
    { "", 1, 0 },
    { "Sobreviventes", 1, 0xffc0c0c0 },
    { "Rafa  Bia  Leo  Ana  Dudu  Nina  Caio  Lia", 1, 0xffffffff },
    { "", 1, 0 },
    { "Feito para PSP e PPSSPP", 2, 0xffffffff },
    { "", 1, 0 },
    { "", 1, 0 },
    { "Obrigado por jogar!", 3, 0xff40e0ff }
};
#define NCRED ((int)(sizeof(CREDITS) / sizeof(CREDITS[0])))
static int cred_h(const CLine *l) { return l->sc * 8 + (l->sc >= 2 ? 10 : 6); }
static int credits_height(void) { int h = 0; for (int i = 0; i < NCRED; i++) h += cred_h(&CREDITS[i]); return h; }

static void draw_heli_art(int x, int y, int blink) {
    rect2d(x, y + 6, x + 60, y + 26, 0xff405030);
    rect2d(x + 40, y + 9, x + 56, y + 18, 0xffe0c080);
    rect2d(x - 36, y + 10, x, y + 16, 0xff405030);
    rect2d(x - 40, y + 2, x - 34, y + 20, 0xff304020);
    rect2d(x - 14, y, x + 74, y + 3, 0xff202020);
    rect2d(x + 28, y + 3, x + 32, y + 6, 0xff202020);
    rect2d(x + 4, y + 28, x + 56, y + 31, 0xff202020);
    if (blink) rect2d(x + 26, y - 4, x + 32, y, 0xff2020ff);
}

static void draw_app_ui(void) {
    char buf[96];
    sceGuDisable(GU_DEPTH_TEST);
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);

    if (appState == APP_SPLASH) {
        ui_center(108, 3, 0xfff0e040, "MECANIC ENGINE GAMER");
        ui_center(148, 2, 0xffffffff, "apresenta");
        float a = (appT < 0.8f) ? 1.0f - appT / 0.8f : (appT > 2.2f ? (appT - 2.2f) / 0.8f : 0.0f);
        if (a > 1.0f) a = 1.0f;
        if (a > 0) rect2d(0, 0, 480, 272, ((unsigned int)(a * 255.0f) << 24));
    } else if (appState == APP_TITLE) {
        static const char *to[4] = { "Novo jogo", "Carregar jogo", "Creditos", "Sair" };
        rect2d(0, 0, 480, 272, 0x70000000);
        window(66, 6, 414, 124);
        ui_center(14, 4, 0xff2020e0, "SOBREVIVENDO");
        ui_center(50, 6, 0xff40e0ff, "EM GS");
        ui_center(104, 2, 0xfff0e040, "MECANIC ENGINE GAMER");
        window(130, 130, 350, 240);
        for (int i = 0; i < 4; i++) {
            int y = 140 + i * 24;
            if (i == titleSel) rect2d(138, y - 3, 342, y + 17, 0xc0a06020);
            sprintf(buf, "%s %s", i == titleSel ? ">" : " ", to[i]);
            ui_text(146, y, 2, 0xffffffff, buf);
        }
        ui_center(254, 1, 0xffc0c0c0, "(c) MECANIC ENGINE GAMER - Sobrevivendo Em GS");
    } else if (appState == APP_NEW) {
        rect2d(0, 0, 480, 272, 0x80000000);
        window(36, 20, 444, 252);
        ui_center(28, 2, 0xff40e0ff, "== NOVO JOGO ==");
        for (int i = 0; i < 6; i++) {
            int y = 58 + i * 24;
            if (i == nwSel) rect2d(46, y - 3, 434, y + 17, 0xc0a06020);
            unsigned int col = (i == 4) ? 0xff60ff60 : 0xffffffff;
            if (i == 0) sprintf(buf, "Slot: %d %s", nwSlot + 1, slotInfos[nwSlot].used ? "(sobrescrever!)" : "(vazio)");
            else if (i == 1) sprintf(buf, "Sobrevivente: %s", survNames[nwName]);
            else if (i == 2) { if (nwSeed) sprintf(buf, "Semente do mundo: %d", nwSeed); else strcpy(buf, "Semente do mundo: aleatoria"); }
            else if (i == 3) sprintf(buf, "Dificuldade: %s", diffNames[nwDiff]);
            else if (i == 4) strcpy(buf, "COMECAR A SOBREVIVER");
            else strcpy(buf, "Voltar");
            ui_text(56, y, 2, col, buf);
        }
        ui_text(46, 216, 1, 0xffc0c0c0, "Cima/Baixo escolhe   Esq/Dir muda   X confirma   O volta");
        ui_text(46, 228, 1, 0xffc0c0c0, "A semente muda a posicao dos zumbis, arvores e saques.");
    } else if (appState == APP_LOAD) {
        rect2d(0, 0, 480, 272, 0x80000000);
        window(36, 24, 444, 254);
        ui_center(32, 2, 0xff40e0ff, "== CARREGAR JOGO ==");
        for (int i = 0; i < 3; i++) {
            int y = 62 + i * 50;
            if (i == slotSel) rect2d(46, y - 4, 434, y + 36, 0xc0a06020);
            if (slotInfos[i].used) {
                sprintf(buf, "Slot %d: %s", i + 1, survNames[slotInfos[i].nameIdx]);
                ui_text(56, y, 2, 0xffffffff, buf);
                sprintf(buf, "Dia %d   Pecas de radio: %d/4   Abates: %d", slotInfos[i].day, slotInfos[i].parts, slotInfos[i].kills);
                ui_text(56, y + 20, 1, 0xffc0ffc0, buf);
            } else { sprintf(buf, "Slot %d: (vazio)", i + 1); ui_text(56, y, 2, 0xff909090, buf); }
        }
        if (menuMsg[0]) ui_text(46, 214, 2, 0xff60ff60, menuMsg);
        ui_text(46, 238, 1, 0xffc0c0c0, "X: carregar     Quadrado: apagar     O: voltar");
    } else if (appState == APP_CREDITS) {
        rect2d(0, 0, 480, 272, 0xd8000000);
        float y = creditsY;
        for (int i = 0; i < NCRED; i++) {
            if (CREDITS[i].t[0] && y > -40 && y < 280) ui_center((int)y, CREDITS[i].sc, CREDITS[i].col, CREDITS[i].t);
            y += cred_h(&CREDITS[i]);
        }
        rect2d(0, 254, 480, 272, 0xe0000000);
        ui_center(260, 1, 0xff909090, "START: pular    X: acelerar");
    } else if (appState == APP_POST) {
        float t = appT;
        for (int i = 0; i < 8; i++) rect2d(0, i * 34, 480, i * 34 + 34, 0xff000000 | ((20 + i * 3) << 16) | ((10 + i) << 8) | 8);
        if (t < 3.0f) ui_center(120, 2, 0xffffffff, "Algumas semanas depois...");
        else if (t < 8.0f) {
            ui_center(40, 2, 0xff40e0ff, "[RADIO] Aqui e a Central...");
            ui_center(70, 1, 0xffffffff, "Captamos um novo sinal de socorro vindo de uma ilha distante.");
            for (int i = 0; i < 24; i++) { int h = 6 + (int)(24.0f * fabsf(sinf(appT * 6.0f + i * 0.7f))); rect2d(120 + i * 10, 150 - h, 126 + i * 10, 150 + h, 0xff40e0ff); }
            ui_center(200, 1, 0xffc0c0c0, "...repetindo: ha sobreviventes. Preciso de voluntarios.");
        } else if (t < 12.0f) {
            ui_center(40, 2, 0xffffffff, "O helicoptero decola de novo...");
            draw_heli_art(-60 + (int)((t - 8.0f) * 140.0f), 130, ((int)(t * 4.0f)) & 1);
        } else if (t < 14.0f) {
            ui_center(120, 2, 0xffffffff, "A sobrevivencia continua...");
        } else {
            ui_center(60, 3, 0xff40e0ff, "SOBREVIVENDO EM GS 2");
            ui_center(110, 4, 0xfff0e040, "EM BREVE");
            ui_center(170, 2, 0xffffffff, "MECANIC ENGINE GAMER");
            if (((int)(appT * 2.0f)) & 1) ui_center(230, 2, 0xffc0c0c0, creditsFromGame ? "X: voltar ao jogo" : "X: voltar ao menu");
        }
        ui_center(260, 1, 0xff707070, "START: pular");
    }

    sceGuDisable(GU_BLEND);
    sceGuEnable(GU_DEPTH_TEST);
}

static void app_update(float dt, unsigned int pressed, unsigned int held) {
    appT += dt;
    if (appState != APP_SPLASH && appState != APP_POST) cam += dt * 0.12f;
    if (appState == APP_SPLASH) {
        if (appT > 3.0f || (pressed & (PSP_CTRL_START | PSP_CTRL_CROSS))) { appState = APP_TITLE; titleSel = 0; appT = 0; }
    } else if (appState == APP_TITLE) {
        if (pressed & PSP_CTRL_UP)   { titleSel = (titleSel + 3) % 4; play_sfx(5); }
        if (pressed & PSP_CTRL_DOWN) { titleSel = (titleSel + 1) % 4; play_sfx(5); }
        if (pressed & PSP_CTRL_CROSS) {
            play_sfx(5);
            if (titleSel == 0) {
                refresh_slots(); nwSel = 0; nwName = 0; nwSeed = 0; nwDiff = 1; nwSlot = 0;
                for (int i = 2; i >= 0; i--) if (!slotInfos[i].used) nwSlot = i;
                menuMsg[0] = 0; appState = APP_NEW;
            } else if (titleSel == 1) { refresh_slots(); slotSel = 0; delConfirm = -1; menuMsg[0] = 0; appState = APP_LOAD; }
            else if (titleSel == 2) { creditsFromGame = 0; creditsY = 272.0f; appT = 0; appState = APP_CREDITS; }
            else running = 0;
        }
    } else if (appState == APP_NEW) {
        if (pressed & PSP_CTRL_UP)   { nwSel = (nwSel + 5) % 6; play_sfx(5); }
        if (pressed & PSP_CTRL_DOWN) { nwSel = (nwSel + 1) % 6; play_sfx(5); }
        int d = 0;
        if (pressed & PSP_CTRL_LEFT) d = -1;
        if (pressed & PSP_CTRL_RIGHT) d = 1;
        if (d) {
            play_sfx(5);
            if (nwSel == 0) nwSlot = (nwSlot + 3 + d) % 3;
            else if (nwSel == 1) nwName = (nwName + 8 + d) % 8;
            else if (nwSel == 2) { nwSeed += d; if (nwSeed < 0) nwSeed = 99; if (nwSeed > 99) nwSeed = 0; }
            else if (nwSel == 3) nwDiff = (nwDiff + 3 + d) % 3;
        }
        if ((pressed & PSP_CTRL_LTRIGGER) && nwSel == 2) nwSeed = (nwSeed + 90) % 100;
        if ((pressed & PSP_CTRL_RTRIGGER) && nwSel == 2) nwSeed = (nwSeed + 10) % 100;
        if (pressed & PSP_CTRL_CIRCLE) appState = APP_TITLE;
        if (pressed & PSP_CTRL_CROSS) {
            if (nwSel == 4) { create_world(); appState = APP_PLAY; mode = 0; say("Sobreviva! Ache 4 pecas de radio."); play_sfx(4); }
            else if (nwSel == 5) appState = APP_TITLE;
        }
    } else if (appState == APP_LOAD) {
        if (pressed & PSP_CTRL_UP)   { slotSel = (slotSel + 2) % 3; delConfirm = -1; menuMsg[0] = 0; play_sfx(5); }
        if (pressed & PSP_CTRL_DOWN) { slotSel = (slotSel + 1) % 3; delConfirm = -1; menuMsg[0] = 0; play_sfx(5); }
        if (pressed & PSP_CTRL_CIRCLE) appState = APP_TITLE;
        if (pressed & PSP_CTRL_SQUARE) {
            if (!slotInfos[slotSel].used) { strcpy(menuMsg, "Slot vazio."); play_sfx(10); }
            else if (delConfirm == slotSel) {
                char pth[96]; slot_path(slotSel, pth); sceIoRemove(pth);
                refresh_slots(); delConfirm = -1; strcpy(menuMsg, "Slot apagado."); play_sfx(4);
            } else { delConfirm = slotSel; strcpy(menuMsg, "Quadrado de novo para apagar!"); play_sfx(10); }
        }
        if (pressed & PSP_CTRL_CROSS) {
            if (!slotInfos[slotSel].used) { strcpy(menuMsg, "Slot vazio."); play_sfx(10); }
            else {
                curSlot = slotSel;
                if (load_game()) { appState = APP_PLAY; mode = 0; say("Jogo carregado!"); play_sfx(4); }
                else { strcpy(menuMsg, "Erro ao carregar!"); play_sfx(10); }
            }
        }
    } else if (appState == APP_CREDITS) {
        float sp = 28.0f + ((held & PSP_CTRL_CROSS) ? 70.0f : 0.0f);
        creditsY -= sp * dt;
        if (creditsY + credits_height() < 0 || (pressed & PSP_CTRL_START)) { appState = APP_POST; appT = 0; }
        else if ((pressed & PSP_CTRL_CIRCLE) && !creditsFromGame) appState = APP_TITLE;
    } else if (appState == APP_POST) {
        if ((pressed & PSP_CTRL_START) || (appT > 14.0f && (pressed & PSP_CTRL_CROSS))) {
            if (creditsFromGame) { appState = APP_PLAY; mode = 0; say("Voce escapou! Continue sobrevivendo."); }
            else { appState = APP_TITLE; titleSel = 0; }
        }
    }
}

int main(void) {
    setup_callbacks();
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    build_cube();
    build_ground();
    reset_state();
    seed_rng = seed_for(0);
    init_world();

    int at = sceKernelCreateThread("audio", audio_thread, 0x12, 0x10000, PSP_THREAD_ATTR_USER | PSP_THREAD_ATTR_VFPU, NULL);
    if (at >= 0) sceKernelStartThread(at, 0, NULL);

    sceGuInit();
    sceGuStart(GU_DIRECT, list);
    sceGuDrawBuffer(GU_PSM_8888, (void *)0, BUF_W);
    sceGuDispBuffer(SCR_W, SCR_H, (void *)FRAME, BUF_W);
    sceGuDepthBuffer((void *)(FRAME * 2), BUF_W);
    sceGuOffset(2048 - (SCR_W / 2), 2048 - (SCR_H / 2));
    sceGuViewport(2048, 2048, SCR_W, SCR_H);
    sceGuDepthRange(65535, 0);
    sceGuScissor(0, 0, SCR_W, SCR_H);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuDepthFunc(GU_GEQUAL);
    sceGuEnable(GU_DEPTH_TEST);
    sceGuShadeModel(GU_SMOOTH);
    sceGuEnable(GU_CLIP_PLANES);
    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);

    SceCtrlData pad; memset(&pad, 0, sizeof(pad));
    unsigned int oldButtons = 0;
    unsigned int last = sceKernelGetSystemTimeLow();

    while (running) {
        unsigned int now = sceKernelGetSystemTimeLow();
        float dt = (now - last) / 1000000.0f; last = now;
        if (dt > 0.1f) dt = 0.1f;

        sceCtrlPeekBufferPositive(&pad, 1);
        unsigned int pressed = pad.Buttons & ~oldButtons;
        oldButtons = pad.Buttons;

        if (appState != APP_PLAY) app_update(dt, pressed, pad.Buttons);
        else if (mode == 1) pause_update(pressed);
        else if (mode == 2) camp_update(pressed);
        else if (mode == 3) death_update(dt, pressed);
        else update(dt, &pad, pressed);

        songId = (appState == APP_PLAY) ? ((chasers > 0) ? 3 : (nightness() > 0.5f ? 2 : 1))
                                        : ((appState == APP_CREDITS || appState == APP_POST) ? 4 : 0);

        sceGuStart(GU_DIRECT, list);
        if (appState == APP_SPLASH || appState == APP_POST) {
            sceGuClearColor(0xff000000);
            sceGuClearDepth(0);
            sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT | GU_FAST_CLEAR_BIT);
        } else draw_scene();
        if (appState == APP_PLAY) draw_ui(); else draw_app_ui();
        sceGuFinish();
        sceGuSync(0, 0);

        sceDisplayWaitVblankStart();
        sceGuSwapBuffers();
    }

    sceGuTerm();
    sceKernelExitGame();
    return 0;
}
