/*
 * test_window.c - Simple test for CosmoWindow
 * 
 * Build with: make test_window
 * Run: ./test_window
 */

#include <stdio.h>
#include "cosmo_window.h"

int main(void) {
    CosmoWindowConfig config = {
        .title = "CosmoWindow Test",
        .width = 800,
        .height = 600,
        .mode = COSMO_WINDOW_WINDOWED,
        .vsync = true
    };
    
    printf("Creating window...\n");
    CosmoWindow *window = cosmo_window_create(&config);
    
    if (!window) {
        fprintf(stderr, "Failed to create window: %s\n", cosmo_window_get_error());
        return 1;
    }
    
    printf("Window created! Running event loop...\n");
    printf("Press ESC or close window to exit.\n");
    
    CosmoEvent event;
    int frame = 0;
    
    while (1) {
        while (cosmo_window_poll_event(window, &event)) {
            if (event.type == COSMO_EVENT_QUIT) {
                printf("Quit event received\n");
                goto cleanup;
            }
            if (event.type == COSMO_EVENT_KEY_DOWN) {
                printf("Key down: %d\n", event.key.key);
                if (event.key.key == 27) {  /* ESC */
                    goto cleanup;
                }
            }
            if (event.type == COSMO_EVENT_RESIZE) {
                printf("Resize: %dx%d\n", event.resize.width, event.resize.height);
            }
        }
        
        /* Simple rendering - just swap buffers */
        cosmo_window_swap_buffers(window);
        
        frame++;
        if (frame % 60 == 0) {
            printf("Frame %d\n", frame);
        }
    }
    
cleanup:
    printf("Cleaning up...\n");
    cosmo_window_destroy(window);
    printf("Done.\n");
    
    return 0;
}

