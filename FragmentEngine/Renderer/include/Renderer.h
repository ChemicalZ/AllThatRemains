//
// Created by davon on 5/4/2026.
//

#ifndef _RENDERER_H
#define _RENDERER_H

struct SDL_Window;
namespace fe {
    class Renderer {
    public:
        Renderer(SDL_Window *window);
        void Render();
    private:
        SDL_Window *_window;

    };
} // fe

#endif //_RENDERER_H
