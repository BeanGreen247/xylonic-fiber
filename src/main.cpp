// Entry point. Blocking event wait: no work when nothing changes. Audio device is opened only while playing.
#include <SDL.h>
#include <cstdio>
#include <cstring>

int main(int argc, char** argv) {
    const bool selftest = argc > 1 && std::strcmp(argv[1], "--selftest") == 0;
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) < 0) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window* win = SDL_CreateWindow("xylonic-fiber", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       1024, 768, SDL_WINDOW_RESIZABLE);
    SDL_Renderer* ren = win ? SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC) : nullptr;
    if (!ren) ren = win ? SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE) : nullptr;
    if (!ren) {
        std::fprintf(stderr, "window/renderer: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    auto draw = [&] {
        SDL_SetRenderDrawColor(ren, 20, 20, 30, 255);
        SDL_RenderClear(ren);
        SDL_RenderPresent(ren);
    };
    draw();
    if (selftest) {
        SDL_DestroyRenderer(ren);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 0;
    }
    SDL_Event e;
    bool run = true;
    while (run && SDL_WaitEvent(&e)) {
        if (e.type == SDL_QUIT) run = false;
        else if (e.type == SDL_WINDOWEVENT) draw();
    }
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
