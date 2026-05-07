//
// Created by davon on 5/4/2026.
//

#ifndef _RENDERER_H
#define _RENDERER_H
#include <memory>

struct SDL_Window;

namespace fe {
    class Renderer {
    public:

        Renderer(SDL_Window *window);
        ~Renderer();

        int Init();

        void drawFrame();

        void waitIdle();

    private:
        struct Impl;
        std::unique_ptr<Impl> _pImpl;


    };
} // fe

#endif //_RENDERER_H