#include "BootDiagnostics.hpp"

#include <SDL.h>

#ifdef __PSP__
#include <pspkernel.h>
PSP_HEAP_SIZE_KB(-1);
PSP_HEAP_THRESHOLD_SIZE_KB(1024);
#endif

int main(int argc, char* argv[]) {
    BootDiagnostics::initialize(argc > 0 ? argv[0] : nullptr);
    BootDiagnostics::stage(1, "smoke main entered");
    BootDiagnostics::holdIfRequested(1);

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_TIMER) != 0) {
        BootDiagnostics::fatal(SDL_GetError());
        return 1;
    }
    BootDiagnostics::stage(4, "smoke SDL initialized");
    BootDiagnostics::holdIfRequested(4);

    SDL_Window* window = SDL_CreateWindow("NexaManga PSP Smoke", SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED, 480, 272, 0);
    if (!window) {
        BootDiagnostics::fatal(SDL_GetError());
        return 2;
    }
    BootDiagnostics::stage(6, "smoke window created");
    BootDiagnostics::holdIfRequested(6);

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    if (!renderer) {
        BootDiagnostics::fatal(SDL_GetError());
        return 3;
    }
    BootDiagnostics::stage(7, "smoke renderer created");
    BootDiagnostics::holdIfRequested(7, renderer);

    SDL_SetRenderDrawColor(renderer, 12, 34, 92, 255);
    SDL_RenderClear(renderer);
    SDL_SetRenderDrawColor(renderer, 246, 196, 48, 255);
    SDL_Rect gold {52, 42, 376, 188};
    SDL_RenderFillRect(renderer, &gold);
    SDL_SetRenderDrawColor(renderer, 190, 32, 58, 255);
    SDL_Rect red {86, 78, 308, 116};
    SDL_RenderFillRect(renderer, &red);
    SDL_SetRenderDrawColor(renderer, 250, 250, 250, 255);
    SDL_Rect white {120, 112, 240, 48};
    SDL_RenderFillRect(renderer, &white);
    SDL_RenderPresent(renderer);
    BootDiagnostics::stage(15, "smoke first frame presented");
    BootDiagnostics::holdIfRequested(15, renderer);
    BootDiagnostics::stage(16, "smoke loop alive");

    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) running = false;
        }
        SDL_Delay(16);
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
