#include "Engine.h"

#include <exception>
#include <stdexcept>
#include <chrono>
#include <thread>
#include <algorithm>
#include <SDL3/SDL.h>
#include <string>
#include <glm/glm.hpp>
#include "../../Log/include/LogInternal.h"
#include "VkRender.h"
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

        constexpr SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);

        m_window = SDL_CreateWindow(m_title, m_width, m_height, window_flags);
        if (!m_window) {
            throw std::runtime_error("SDL_CreateWindow failed: " + std::string(SDL_GetError()));
        }
        FE_CORE_INFO("Window created");
    }

    void Engine::initRenderer() {
        try {
            m_renderer = new VkRender(m_window);
        } catch (std::exception &e) {
            throw std::runtime_error("Renderer failed to initialize: " + std::string(e.what()));
        }
        m_camera.velocity = glm::vec3(0.f);
        m_camera.position = glm::vec3(0.f, 0.f, 5.f);
        m_camera.pitch    = 0;
        m_camera.yaw      = 0;
        FE_CORE_INFO("Renderer initialized");
    }

    void Engine::handle_events(bool &running, bool &stop_rendering) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            m_renderer->process_event(event); // ImGui first

            // Right-click: lock mouse to window for rotation, release when done.
            // Only enter grab if ImGui isn't consuming the click.
            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN
                && event.button.button == SDL_BUTTON_RIGHT
                && !m_renderer->imguiWantsInput()) {
                SDL_SetWindowRelativeMouseMode(m_window, true);
            }
            if (event.type == SDL_EVENT_MOUSE_BUTTON_UP
                && event.button.button == SDL_BUTTON_RIGHT) {
                SDL_SetWindowRelativeMouseMode(m_window, false);
            }

            if (!m_renderer->imguiWantsInput()) {
                m_camera.processSDLEvent(event);
            }

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
                SDL_SetWindowRelativeMouseMode(m_window, false);
            }
            if (event.type == SDL_EVENT_WINDOW_RESIZED) {
                FE_CORE_INFO("Window resized to {}x{}", event.window.data1, event.window.data2);
                m_renderer->resize_requested = true;
            }
            if (event.type == SDL_EVENT_WINDOW_HIDDEN) {
                FE_CORE_INFO("Window hidden");
                stop_rendering = true;
            }
            if (event.type == SDL_EVENT_WINDOW_SHOWN) {
                FE_CORE_INFO("Window shown");
                stop_rendering = false;
            }

        }
    }

    void Engine::mainLoop() {
        bool running        = true;
        bool stop_rendering = false;

        using Clock   = std::chrono::steady_clock;
        using Seconds = std::chrono::duration<float>;
        constexpr float kFixedStep = 1.f / 60.f;

        auto prevTime  = Clock::now();
        float accumulator = 0.f;

        FE_CORE_INFO("Main loop started");
        while (running) {
            auto now = Clock::now();
            float frameTime = std::chrono::duration_cast<Seconds>(now - prevTime).count();
            prevTime = now;
            frameTime = std::min(frameTime, 0.25f); // clamp spiral-of-death

            handle_events(running, stop_rendering);

            if (stop_rendering) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
            if (m_renderer->resize_requested) {
                m_renderer->RequestResize();
            }

            accumulator += frameTime;
            while (accumulator >= kFixedStep) {
                m_camera.update(kFixedStep);
                accumulator -= kFixedStep;
            }

            m_renderer->UpdateScene(m_camera);
            m_renderer->Draw();
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
