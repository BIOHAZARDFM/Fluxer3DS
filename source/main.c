#include <3ds.h>
#include <citro2d.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>

#define SOC_ALIGN       0x1000
#define SOC_BUFFERSIZE  0x100000

// Memory buffer allocated for 3DS network sockets
static u32 *SOC_buffer = NULL;

// Helper to initialize 3DS Networking (soc:u service)
Result init_network(void) {
    Result ret = 0;
    SOC_buffer = (u32*)memalign(SOC_ALIGN, SOC_BUFFERSIZE);
    if (SOC_buffer == NULL) {
        return -1;
    }

    if ((ret = socInit(SOC_buffer, SOC_BUFFERSIZE)) != 0) {
        free(SOC_buffer);
        SOC_buffer = NULL;
        return ret;
    }
    return 0;
}

// Clean up network socket allocations
void cleanup_network(void) {
    if (SOC_buffer != NULL) {
        socExit();
        free(SOC_buffer);
        SOC_buffer = NULL;
    }
}

int main(int argc, char* argv[]) {
    // 1. Initialize core 3DS services and Citro2D graphics engine
    gfxInitDefault();
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();

    // 2. Initialize Top and Bottom screen render targets
    C3D_RenderTarget* topTarget = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    C3D_RenderTarget* bottomTarget = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);

    // 3. Initialize 3DS console on top screen for debug printing
    PrintConsole debugConsole;
    consoleInit(GFX_TOP, &debugConsole);
    consoleSelect(&debugConsole);

    printf("===================================\n");
    printf("   Fluxer 3DS Homebrew Client      \n");
    printf("===================================\n");

    // 4. Initialize Network Stack
    printf("[*] Initializing network stack...\n");
    if (R_FAILED(init_network())) {
        printf("[!] Failed to initialize soc:u!\n");
    } else {
        printf("[+] Network stack initialized successfully.\n");
    }

    printf("\nPress START to exit application.\n");

    // Pre-define color palette for citro2d (ABGR format)
    u32 clrBg = C2D_Color32(40, 44, 52, 255);       // Dark Gray Background
    u32 clrHeader = C2D_Color32(97, 175, 239, 255); // Blue Accent
    u32 clrText = C2D_Color32(255, 255, 255, 255);   // White Text

    // Dynamic state variables
    touchPosition touch;
    u32 lastTouchX = 0, lastTouchY = 0;

    // Main Application Loop
    while (aptMainLoop()) {
        // Scan input hardware state
        hidScanInput();
        u32 kDown = hidKeysDown();
        u32 kHeld = hidKeysHeld();

        // Exit on START button press
        if (kDown & KEY_START) {
            break;
        }

        // Capture touchscreen coordinates when active
        if (kHeld & KEY_TOUCH) {
            hidTouchRead(&touch);
            lastTouchX = touch.px;
            lastTouchY = touch.py;
        }

        // -------------------------------------------------------------
        // RENDER STAGE
        // -------------------------------------------------------------
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);

        // --- BOTTOM SCREEN RENDER (Touch GUI Interface) ---
        C2D_TargetClear(bottomTarget, clrBg);
        C2D_SceneBegin(bottomTarget);

        // Draw top header bar on bottom screen
        C2D_DrawRectSolid(0, 0, 0, 320, 30, clrHeader);

        // Draw UI elements (simulated channel selector)
        C2D_DrawRectSolid(10, 40, 0, 300, 40, C2D_Color32(60, 64, 72, 255));
        C2D_DrawRectSolid(10, 90, 0, 300, 40, C2D_Color32(60, 64, 72, 255));

        // --- TOP SCREEN RENDER ---
        // (Handled automatically by libctru consoleInit for printf output)

        C3D_FrameEnd(0);
    }

    // Deinitialize graphics & networking on exit
    cleanup_network();
    C2D_Fini();
    C3D_Fini();
    gfxExit();

    return 0;
}