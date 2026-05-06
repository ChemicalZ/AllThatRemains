//
// Created by davon on 5/4/2026.
//

#ifndef _ENGINE_H
#define _ENGINE_H

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
        void mainLoop();
        void cleanup() const;

        const char * m_title;
        int m_width;
        int m_height;
        SDL_Window *m_window;
        Renderer *m_renderer;
    };
} // fe

#endif //_ENGINE_H
