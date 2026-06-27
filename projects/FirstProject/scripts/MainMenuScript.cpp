#include "engine.h"
#include "json.hpp"
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

// 1. Define your game script by inheriting from ScriptBase (found in engine.h)
class MainMenuScript : public ScriptBase {
public:
    void onStart() override {
        std::cout << "MainMenuScript Started!" << std::endl;
    }
    void onUpdate(float dt) override {
        // Update game logic here
    }
    void onDraw() override {
        // Draw custom game logic here (if not using ECS render)
    }
    void onEnd() override {
        std::cout << "MainMenuScript Ended!" << std::endl;
    }
    std::string getName() const override { return "MainMenuScript"; }
};

// 2. Pure Game Render System (NO editor canvas offset)
// This replaces render_system_in_editor by removing the "+ 105" offset
void render_system_game(SDL_Renderer* renderer, TTF_TextEngine* textEngine, TTF_Font* font, const ECSWorld& world) {
    for (Entity i = 0; i < world.entity_count; ++i) {
        if (world.has_position[i]) {
            // Pure position, NO + 105 offset!
            SDL_FRect outlineRect = { 
                world.position_pool[i].x, 
                world.position_pool[i].y, 
                50.0f, 50.0f 
            };
            if (world.has_rectangle_shape[i]) {
                outlineRect.w = world.rectangle_shape_pool[i].w;
                outlineRect.h = world.rectangle_shape_pool[i].h;
            }
            
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
            
            if (world.has_metadata[i]) {
                TTF_Text* textObj = TTF_CreateText(textEngine, font, world.metadata_pool[i].name.c_str(), 0);
                if (textObj) {
                    TTF_SetTextColor(textObj, 255, 255, 255, 255);
                    TTF_DrawRendererText(textObj, world.position_pool[i].x, world.position_pool[i].y);
                    TTF_DestroyText(textObj);
                }
            }
            SDL_RenderRect(renderer, &outlineRect);
        }
    }
}

// 3. Main Entry Point for the Game Executable
int main(int argc, char* argv[]) {
    // Initialize SDL3
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << std::endl;
        return -1;
    }
    if (!TTF_Init()) {
        std::cerr << "TTF_Init failed: " << SDL_GetError() << std::endl;
        return -1;
    }

    // Create Window & Renderer
    SDL_Window* window = SDL_CreateWindow("FirstProject - Pure Game", 1600, 900, 0);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);

    // Load Font (Adjust path to your actual font file)
    TTF_Font* font = TTF_OpenFont("assets/fonts/arial.ttf", 24); 
    if (!font) {
        std::cerr << "Warning: Could not load font. Text rendering might fail." << std::endl;
    }
    TTF_TextEngine* textEngine = TTF_CreateRendererTextEngine(renderer);

    // Load Scene using SceneParser from engine.h
    SceneParser parser(renderer, textEngine, font, window);
    
    // IMPORTANT: Ensure this path is correct relative to where the .exe runs!
    // If running from build_windows/projects/FirstProject/Release/, 
    // you might need "../../../../projects/FirstProject/assets/scenes/MainMenu.json"
    // OR set your Visual Studio Debugging "Working Directory" to "$(ProjectDir)"
    Scene scene = parser.loadFromFile("assets/scenes/MainMenu.json"); 

    // Initialize Script
    MainMenuScript script;
    script.onStart();

    // Game Loop
    bool running = true;
    Uint64 lastTime = SDL_GetTicks();

    while (running) {
        Uint64 currentTime = SDL_GetTicks();
        float dt = (currentTime - lastTime) / 1000.0f;
        lastTime = currentTime;

        // Handle Events
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) {
                running = false;
            }
            // Forward events to GUI elements in the scene
            for (auto& elem : scene.guiElements) {
                elem->handleEvent(e, window, 0.0f);
            }
        }

        // Update
        script.onUpdate(dt);
        movement_system(scene.world, dt); // Built-in engine system from engine.h

        // Render
        SDL_SetRenderDrawColor(renderer, 20, 20, 30, 255); // Dark background
        SDL_RenderClear(renderer);

        // Render ECS entities without editor canvas offset
        render_system_game(renderer, textEngine, font, scene.world);

        // Call script's custom draw logic
        script.onDraw();

        // Render GUI elements
        for (auto& elem : scene.guiElements) {
            elem->render(0.0f);
        }

        SDL_RenderPresent(renderer);
    }

    // Cleanup
    script.onEnd();
    if (font) TTF_CloseFont(font);
    if (textEngine) TTF_DestroyRendererTextEngine(textEngine);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    TTF_Quit();
    SDL_Quit();

    return 0;
}