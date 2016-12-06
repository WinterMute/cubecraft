#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include "global.h"
#include "text.h"
#include "title_menu.h"

#define DEFAULT_FIFO_SIZE	(256*1024)

int gDisplayWidth;
int gDisplayHeight;
u16 gControllerPressedKeys;
u16 gControllerHeldKeys;
s8 gAnalogStickX;
s8 gAnalogStickY;
s8 gCStickX;
s8 gCStickY;

static void (*mainCallback)(void) = NULL;
static void (*drawCallback)(void) = NULL;

static void *frameBuffers[2] = {NULL, NULL};
static int frameBufferNum = 0;

static void setup_graphics(void)
{
    static GXRModeObj *videoMode;
    void *gpFifo;
    f32 yScale;
    
    //Set up video mode and frame buffers
    VIDEO_Init();
    videoMode = VIDEO_GetPreferredMode(NULL);
    gDisplayWidth = videoMode->fbWidth;
    gDisplayHeight = videoMode->efbHeight;
    VIDEO_Configure(videoMode);
    frameBuffers[0] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(videoMode));
    frameBuffers[1] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(videoMode));
    VIDEO_SetNextFramebuffer(frameBuffers[0]);
    VIDEO_SetBlack(FALSE);
    VIDEO_Flush();
    
    //Allocate the GPU FIFO buffer
    gpFifo = memalign(32, DEFAULT_FIFO_SIZE);
    memset(gpFifo, 0, DEFAULT_FIFO_SIZE);
    GX_Init(gpFifo, DEFAULT_FIFO_SIZE);
    
    //Clear with blue background
    GX_SetCopyClear((GXColor){0x40, 0x40, 0xFF, 0xFF}, 0x00FFFFFF);
    
    GX_SetViewport(0.0, 0.0, videoMode->fbWidth, videoMode->efbHeight, 0.0, 1.0);  //Use the entire EFB for rendering
    yScale = GX_GetYScaleFactor(videoMode->efbHeight, videoMode->xfbHeight);
    GX_SetDispCopyYScale(yScale);  //Make the TV output look like the EFB
    GX_SetScissor(0, 0, videoMode->fbWidth, videoMode->efbHeight);
    
    GX_SetDispCopySrc(0, 0, videoMode->fbWidth, videoMode->efbHeight);  //EFB -> XFB copy dimensions
    GX_SetDispCopyDst(videoMode->fbWidth, videoMode->xfbHeight);
    GX_SetCopyFilter(videoMode->aa, videoMode->sample_pattern, GX_TRUE, videoMode->vfilter);
    //Turn on field mode if video is interlaced
    if (videoMode->viHeight == 2 * videoMode->xfbHeight)
        GX_SetFieldMode(videoMode->field_rendering, GX_ENABLE);
    else
        GX_SetFieldMode(videoMode->field_rendering, GX_DISABLE);
    
    GX_CopyDisp(frameBuffers[frameBufferNum], GX_TRUE);  //Draw first frame
    GX_SetDispCopyGamma(GX_GM_1_0);
    
    GX_SetNumTexGens(2);
}

static void read_input(void)
{
    PAD_ScanPads();
    gControllerPressedKeys = PAD_ButtonsDown(0);
    gControllerHeldKeys = PAD_ButtonsDown(0);
    gAnalogStickX = PAD_StickX(0);
    gAnalogStickY = PAD_StickY(0);
    gCStickX = PAD_SubStickX(0);
    gCStickY = PAD_SubStickY(0);
}

int main(void)
{
    PAD_Init();
    
    setup_graphics();
    text_init();
    title_menu_init();
    
    while (1)
    {       
        read_input();
        if (mainCallback != NULL)
            mainCallback();
        if (drawCallback != NULL)
            drawCallback();
        
        GX_Flush();
        GX_DrawDone();
        GX_CopyDisp(frameBuffers[frameBufferNum], GX_TRUE);
        VIDEO_SetNextFramebuffer(frameBuffers[frameBufferNum]);
        VIDEO_Flush();
        VIDEO_WaitVSync();
        frameBufferNum ^= 1;  //Switch to other framebuffer
    }
    return 0;
}

void set_main_callback(void (*callback)(void))
{
    mainCallback = callback;
}

void set_draw_callback(void (*callback)(void))
{
    drawCallback = callback;
}