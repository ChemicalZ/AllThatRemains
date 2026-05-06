//
// Created by davon on 5/4/2026.
//

#ifndef _RENDERER_H
#define _RENDERER_H
#include <complex.h>
#include <vector>
#include <vulkan/vulkan_core.h>

struct SDL_Window;
struct VkInstance_T;
typedef struct VkInstance_T *VkInstance;

namespace fe {
    class Renderer {
    public:
        Renderer(SDL_Window *window);
        ~Renderer();
        void Render();


        int Init();

        std::vector<const char *> getVulkanExtensions();

    private:
        void createVulkanInstance();

        SDL_Window *m_window;
        VkInstance m_instance{};
    };
} // fe

#endif //_RENDERER_H
