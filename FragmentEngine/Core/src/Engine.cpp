#include "Engine.h"

#include <SDL3/SDL.h>

namespace fe {
    int Engine::Run(const char* title,const int width,const int height)
    {
        if (!SDL_Init(SDL_INIT_VIDEO)) {
            SDL_Log("SDL_Init failed: %s", SDL_GetError());
            return 1;
        }

        SDL_Window *window = SDL_CreateWindow(title, width, height, SDL_WINDOW_RESIZABLE);
        if (!window) {
            SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
            SDL_Quit();
            return 1;
        }


        bool running = true;
        while (running) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_EVENT_QUIT)
                    running = false;
            }


        }

        SDL_DestroyWindow(window);
        SDL_Quit();
        return 0;
    }
}