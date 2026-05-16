//
// Created by davon on 5/4/2026.
//

#pragma once
#include <SDL3/SDL_events.h>

struct SDL_Window;

namespace fe {
    class Renderer;

    class Engine {
    public:
        Engine(const char* title, int width, int height);
        int Run();

    private:
        void initWindow();
        void initRenderer();

        void handle_events(bool &running, bool &stop_rendering);

        void mainLoop();
        void cleanup() const;

        const char * m_title;
        int m_width;
        int m_height;
        SDL_Window *m_window;
        Renderer *m_renderer;
    };
} // fe
