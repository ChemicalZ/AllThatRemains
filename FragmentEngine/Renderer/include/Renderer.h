//
// Created by davon on 5/4/2026.
//

#pragma once

struct SDL_Window;
struct VkInstance_T;
typedef struct VkInstance_T *VkInstance;

namespace fe {
    class Renderer {
    public:
        Renderer(SDL_Window *window);
        void Render();
        int Init();
    private:
        SDL_Window *m_window;
        VkInstance m_instance;
    };
} // fe
