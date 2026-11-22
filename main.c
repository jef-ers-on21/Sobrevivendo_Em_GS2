#include <pspkernel.h>
#include <pspdebug.h>
#include <pspdisplay.h>
#include <pspctrl.h>
#include <pspgu.h>
#include <pspgum.h>
#include <math.h>
#include <string.h>

PSP_MODULE_INFO("Sobrevivendo Em GS", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

#define BUF_WIDTH 512
#define SCR_WIDTH 480
#define SCR_HEIGHT 272
#define LIST_SIZE (256 * 1024)

static unsigned int __attribute__((aligned(16))) list[LIST_SIZE / 4];
static unsigned int __attribute__((aligned(16))) fb0[BUF_WIDTH * SCR_HEIGHT];
static unsigned int __attribute__((aligned(16))) fb1[BUF_WIDTH * SCR_HEIGHT];
static unsigned int __attribute__((aligned(16))) zbuf[BUF_WIDTH * SCR_HEIGHT];

typedef struct {
    unsigned int color;
    float x, y, z;
} Vertex;

static Vertex cube[36];
static float playerX = 0.0f, playerZ = 4.0f;
static float yaw = 0.0f;
static int health = 100, hunger = 100, day = 1;
static unsigned int frameCount = 0;

static void make_cube(void) {
    /* 12 triângulos, cubo centrado na origem, lado 1. */
    const float p[8][3] = {
        {-0.5f,-0.5f,-0.5f},{ 0.5f,-0.5f,-0.5f},
        { 0.5f, 0.5f,-0.5f},{-0.5f, 0.5f,-0.5f},
        {-0.5f,-0.5f, 0.5f},{ 0.5f,-0.5f, 0.5f},
        { 0.5f, 0.5f, 0.5f},{-0.5f, 0.5f, 0.5f}
    };
    const int faces[12][3] = {
        {0,2,1},{0,3,2},{4,5,6},{4,6,7},
        {0,1,5},{0,5,4},{3,7,6},{3,6,2},
        {0,4,7},{0,7,3},{1,2,6},{1,6,5}
    };
    int i;
    for (i=0;i<12;i++) {
        int j;
        unsigned int c = (i<2)?0xff526b36:
                         (i<4)?0xff647b43:
                         (i<8)?0xff777777:0xff555555;
        for (j=0;j<3;j++) {
            Vertex *v=&cube[i*3+j];
            v->color=c;
            v->x=p[faces[i][j]][0];
            v->y=p[faces[i][j]][1];
            v->z=p[faces[i][j]][2];
        }
    }
}

static void draw_cube(float x,float y,float z,float sx,float sy,float sz,
                      unsigned int color) {
    Vertex temp[36];
    int i;
    for(i=0;i<36;i++) {
        temp[i]=cube[i];
        temp[i].color=color;
        temp[i].x*=sx; temp[i].y*=sy; temp[i].z*=sz;
    }
    sceGumPushMatrix();
    {
        ScePspFVector3 pos={x,y,z};
        sceGumTranslate(&pos);
        sceGumDrawArray(GU_TRIANGLES,
            GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D,
            36,0,temp);
    }
    sceGumPopMatrix();
}

static void setup_gu(void) {
    sceGuInit();
    sceGuStart(GU_DIRECT,list);
    sceGuDrawBuffer(GU_PSM_8888,(void*)0,BUF_WIDTH);
    sceGuDispBuffer(SCR_WIDTH,SCR_HEIGHT,(void*)(BUF_WIDTH*SCR_HEIGHT*4),BUF_WIDTH);
    sceGuDepthBuffer((void*)(BUF_WIDTH*SCR_HEIGHT*8),BUF_WIDTH);
    sceGuOffset(2048-(SCR_WIDTH/2),2048-(SCR_HEIGHT/2));
    sceGuViewport(2048,2048,SCR_WIDTH,SCR_HEIGHT);
    sceGuDepthRange(65535,0);
    sceGuScissor(0,0,SCR_WIDTH,SCR_HEIGHT);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuEnable(GU_DEPTH_TEST);
    sceGuDepthFunc(GU_GEQUAL);
    sceGuEnable(GU_CULL_FACE);
    sceGuFrontFace(GU_CW);
    sceGuShadeModel(GU_SMOOTH);
    sceGuClearColor(0xff87b6d8);
    sceGuClearDepth(0);
    sceGuFinish();
    sceGuSync(0,0);
    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);
}

static void draw_world(void) {
    int x,z;
    sceGuStart(GU_DIRECT,list);
    sceGuClearColor(0xff87b6d8);
    sceGuClear(GU_COLOR_BUFFER_BIT|GU_DEPTH_BUFFER_BIT);

    sceGumMatrixMode(GU_PROJECTION);
    sceGumLoadIdentity();
    sceGumPerspective(65.0f,16.0f/9.0f,0.5f,200.0f);

    sceGumMatrixMode(GU_VIEW);
    sceGumLoadIdentity();
    {
        float lx=sinf(yaw), lz=-cosf(yaw);
        ScePspFVector3 eye={playerX,2.4f,playerZ+5.0f};
        ScePspFVector3 center={playerX+lx,1.2f,playerZ+5.0f+lz};
        ScePspFVector3 up={0,1,0};
        sceGumLookAt(&eye,&center,&up);
    }

    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();

    /* Chão e ruas */
    draw_cube(0,-0.65f,0,80,0.2f,80,0xff47723b);
    draw_cube(0,-0.52f,0,8,0.08f,80,0xff454545);
    draw_cube(0,-0.46f,0,1,0.04f,80,0xffd6c27a);

    /* Casas e prédios da pequena cidade */
    for(x=-3;x<=3;x+=2) {
        draw_cube((float)x,0.6f,-8,1.7f,2.0f,1.7f,0xffb7a184);
        draw_cube((float)x,1.8f,-8,1.9f,0.5f,1.9f,0xff713d32);
        draw_cube((float)x,0.0f,-7.12f,0.45f,0.9f,0.08f,0xff49382d);
    }
    draw_cube(-8,1.8f,-15,3.0f,4.0f,3.0f,0xff8a969c);
    draw_cube(-8,4.0f,-15,3.2f,0.5f,3.2f,0xff414a50);
    draw_cube(8,1.2f,-13,2.4f,2.8f,2.4f,0xffa78e6d);
    draw_cube(8,2.8f,-13,2.6f,0.4f,2.6f,0xff684334);

    /* Árvores estilizadas */
    for(x=-12;x<=12;x+=4) {
        draw_cube((float)x,0.7f,-4,0.35f,1.8f,0.35f,0xff68452b);
        draw_cube((float)x,2.0f,-4,1.5f,1.7f,1.5f,0xff245b31);
        draw_cube((float)x,0.7f,-20,0.35f,1.8f,0.35f,0xff68452b);
        draw_cube((float)x,2.0f,-20,1.5f,1.7f,1.5f,0xff245b31);
    }

    /* O personagem é representado por um modelo provisório em blocos. */
    draw_cube(playerX,0.35f,playerZ,0.55f,0.8f,0.35f,0xff305fbd);
    draw_cube(playerX,0.95f,playerZ,0.42f,0.42f,0.42f,0xffd5a878);

    sceGuFinish();
    sceGuSync(0,0);
    sceDisplayWaitVblankStart();
    sceGuSwapBuffers();
}

static void update_game(void) {
    SceCtrlData pad;
    float speed=0.075f;
    sceCtrlReadBufferPositive(&pad,1);
    if(pad.Buttons & PSP_CTRL_LTRIGGER) yaw-=0.045f;
    if(pad.Buttons & PSP_CTRL_RTRIGGER) yaw+=0.045f;

    if(pad.Buttons & PSP_CTRL_UP) {
        playerX+=sinf(yaw)*speed;
        playerZ-=cosf(yaw)*speed;
    }
    if(pad.Buttons & PSP_CTRL_DOWN) {
        playerX-=sinf(yaw)*speed;
        playerZ+=cosf(yaw)*speed;
    }
    if(pad.Buttons & PSP_CTRL_LEFT) {
        playerX-=cosf(yaw)*speed;
        playerZ-=sinf(yaw)*speed;
    }
    if(pad.Buttons & PSP_CTRL_RIGHT) {
        playerX+=cosf(yaw)*speed;
        playerZ+=sinf(yaw)*speed;
    }

    if(pad.Buttons & PSP_CTRL_CROSS) {
        /* Ação provisória: procurar suprimentos. */
        if(hunger<100) hunger+=1;
    }
    if(pad.Buttons & PSP_CTRL_CIRCLE) {
        /* Descansar recupera um pouco de vida. */
        if(health<100) health++;
    }

    if(++frameCount>=1800) {
        frameCount=0;
        day++;
        if(hunger>0) hunger-=5;
        else if(health>0) health-=5;
    }
    if(playerX>35) playerX=35;
    if(playerX< -35) playerX=-35;
    if(playerZ>35) playerZ=35;
    if(playerZ< -35) playerZ=-35;
}

int main(void) {
    pspDebugScreenInit();
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
    make_cube();
    setup_gu();

    while(1) {
        update_game();
        draw_world();

        /* HUD de depuração no terminal do PSP/PPSSPP. */
        pspDebugScreenSetXY(0,0);
        pspDebugScreenSetTextColor(0xffffffff);
        pspDebugScreenPrintf("SOBREVIVENDO EM GS - PROTOTIPO 0.1\n");
        pspDebugScreenPrintf("Dia: %d  Vida: %d  Fome: %d\n",day,health,hunger);
        pspDebugScreenPrintf("Direcional: mover | L/R: girar camera\n");
        pspDebugScreenPrintf("X: buscar suprimentos | O: descansar\n");
        pspDebugScreenPrintf("START: sair do jogo\n");
        {
            SceCtrlData pad;
            sceCtrlReadBufferPositive(&pad,1);
            if(pad.Buttons & PSP_CTRL_START) break;
        }
    }

    sceGuTerm();
    sceKernelExitGame();
    return 0;
}
