#include "Engine.h"

#include <exception>
#include <stdexcept>
#include <SDL3/SDL.h>
#include <string>
#include "../../Log/include/LogInternal.h"
#include "Renderer.h"

namespace fe {
    Engine::Engine(const char *title, const int width, const int height): m_title(title), m_width(width),
                                                                          m_height(height) {
    }

    int Engine::Run() {
        Log::Init();
        FE_CORE_INFO("Logger initialized");
        try {
            initWindow();
            initRenderer();
            mainLoop();
        } catch (const std::exception& e) {
            FE_CORE_ERROR("Engine failed: {}", e.what());
            cleanup();
            return EXIT_FAILURE;
        } catch (...) {
            FE_CORE_ERROR("Engine failed: unknown exception");
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
        FE_CORE_INFO("Window created");
    }

    void Engine::initRenderer() {
        m_renderer = new Renderer(m_window);
        try {
            m_renderer->Init();
        } catch (std::exception &e) {
            throw std::runtime_error("Renderer failed to initialize: " + std::string(e.what()));
        }
        FE_CORE_INFO("Renderer initialized");
    }

    void Engine::handle_events(bool &running, bool &stop_rendering) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                FE_CORE_INFO("Quit Window event received");
                running = false;
            }
            if (event.type == SDL_EVENT_WINDOW_MINIMIZED) {
                FE_CORE_INFO("Window minimized");
                stop_rendering = true;
            }
            if (event.type == SDL_EVENT_WINDOW_FOCUS_GAINED) {
                FE_CORE_INFO("Window focus gained");
                stop_rendering = false;
            }
            if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
                FE_CORE_INFO("Window focus lost");
                stop_rendering = true;
            }
            if (event.type == SDL_EVENT_WINDOW_RESIZED) {
                FE_CORE_INFO("Window resized to {}x{}", event.window.data1, event.window.data2);
            }
            if (event.type == SDL_EVENT_WINDOW_HIDDEN) {
                FE_CORE_INFO("Window hidden");
                stop_rendering = true;
            }
            if (event.type == SDL_EVENT_WINDOW_SHOWN) {
                FE_CORE_INFO("Window shown");
                stop_rendering = false;
            }
            ImGui_ImplSDL3_ProcessEvent(&e);

        }
    }

    void Engine::mainLoop() {
        bool running = true;
        bool stop_rendering = false;
        FE_CORE_INFO("Main loop started");
        while (running) {
            handle_events(running, stop_rendering);
            // Render
            if (stop_rendering) {
                // throttle the speed to avoid the endless spinning
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }

            // imgui new frame
            ImGui_ImplVulkan_NewFrame();
            ImGui_ImplSDL3_NewFrame();
            ImGui::NewFrame();

            //some imgui UI to test
            ImGui::ShowDemoWindow();

            //make imgui calculate internal draw structures
            ImGui::Render();
            m_renderer->Render();
        }
        FE_CORE_INFO("Main loop ended");
    }

    void Engine::cleanup() const {
        FE_CORE_INFO("Cleaning up");
        delete m_renderer;
        SDL_DestroyWindow(m_window);
        SDL_Quit();
        Log::Shutdown();
    }
}
