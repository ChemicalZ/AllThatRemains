#include "Engine.h"

#include <exception>
#include <stdexcept>
#include <SDL3/SDL.h>
#include <string>
#include "Renderer.h"

namespace fe {
    Engine::Engine(const char* title,const int width,const int height): m_title(title), m_width(width), m_height(height) {

    }

    int Engine::Run()
    {
        try {
            initWindow();
            initRenderer();
            mainLoop();
        }
        catch (std::exception& e) {
            SDL_Log("Exception: %s", e.what());
            cleanup();
            return EXIT_FAILURE;
        }
        cleanup();

        return EXIT_SUCCESS;
    }

    void Engine::initWindow() {
        if (!SDL_Init(SDL_INIT_VIDEO)) {
            throw std::runtime_error("SDL_Init failed: " + std::string(SDL_GetError()));
        }

        m_window = SDL_CreateWindow(m_title, m_width, m_height, SDL_WINDOW_RESIZABLE);
        if (!m_window) {
            throw std::runtime_error("SDL_CreateWindow failed: " + std::string(SDL_GetError()));
        }
    }

    void Engine::initRenderer() {
        m_renderer = new fe::Renderer(m_window);
        try {
            m_renderer->Init();
        }
        catch (std::exception& e) {
            throw std::runtime_error("Renderer failed to initialize: " + std::string(e.what()));
        }

    }

    void Engine::mainLoop() {
        bool running = true;
        while (running) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_EVENT_QUIT)
                    running = false;

            }

        }
    }

    void Engine::cleanup() const {
        SDL_Log("Cleaning up");
        delete m_renderer;
        SDL_DestroyWindow(m_window);
        SDL_Quit();
    }
}
