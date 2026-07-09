#include "MainMenuScript.h"
#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

// Global variables for the game loop
static SDL_Renderer* g_renderer = nullptr;
static SDL_Window* g_window = nullptr;
static TTF_TextEngine* g_textEngine = nullptr;
static TTF_Font* g_bodyFont = nullptr;
static TTF_Font* g_titleFont = nullptr;
static Scene* g_scene = nullptr;
static MainMenuScript* g_script = nullptr;
static MainMenuContext* g_ctx = nullptr;
static bool g_running = true;
static Uint64 g_lastTime = 0;


#ifdef __EMSCRIPTEN__
void main_loop_callback() {
    if (!g_running) {
        emscripten_cancel_main_loop();
        return;
    }
    Uint64 now = SDL_GetTicks();
    float dt = (float)(now - g_lastTime) / 1000.0f;
    g_lastTime = now;
    

    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_EVENT_QUIT) g_running = false;
        if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) {
            if (g_ctx->showOptions) g_ctx->showOptions = false;
            else g_running = false;
        }
        for (auto& elem : g_scene->guiElements)
            elem->handleEvent(e, g_window, 0.0f);
    }

    if (g_ctx->requestQuit) g_running = false;
    if (g_ctx->requestPlay) {
        SDL_Log("[Game] Starting play — player='%s' diff=%.0f",
                g_ctx->pendingPlayerName.c_str(), g_ctx->pendingDifficulty);
        g_ctx->requestPlay = false;
    }


    g_script->onUpdate(dt);
    movement_system(g_scene->world, dt);

    // Render
    SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(g_renderer, 10, 10, 20, 255);
    SDL_RenderClear(g_renderer);

    // Dynamic background bands
    int winW = 1600, winH = 900;
    SDL_GetWindowSize(g_window, &winW, &winH);
    float w = (float)winW;

    for (int i = 0; i < 9; ++i) {
        SDL_SetRenderDrawColor(g_renderer, 40, 50, 100, (Uint8)(8 + i * 3));
        SDL_FRect band = { 0, (float)(i * 100), w, 100 };
        SDL_RenderFillRect(g_renderer, &band);
    }

    render_system_game(g_renderer, g_textEngine, g_bodyFont, g_scene->world);

    for (auto& elem : g_scene->guiElements)
        elem->render(0.0f);

    g_script->onDraw();
    SDL_RenderPresent(g_renderer);
}
#endif

int main(int argc, char* argv[]) {
    const float windowWidth  = 1600.0f;
    const float windowHeight = 900.0f;

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
        return -1;
    }
    if (!TTF_Init()) {
        std::cerr << "TTF_Init failed: " << SDL_GetError() << "\n";
        SDL_Quit(); return -1;
    }

    SDL_Window* window = SDL_CreateWindow("Sample Project", windowWidth, windowHeight, SDL_WINDOW_RESIZABLE);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    if (!window || !renderer) {
        std::cerr << "Window/Renderer creation failed: " << SDL_GetError() << "\n";
        TTF_Quit(); SDL_Quit(); return -1;
    }

    TTF_Font* bodyFont = ProjectScript_TTF_OpenFont("SampleProject/assets/fonts/fredoka.ttf", 22);
    TTF_Font* titleFont = ProjectScript_TTF_OpenFont("SampleProject/assets/fonts/fredoka.ttf", 52);
    if (!bodyFont) std::cerr << "[Warning] Could not load body font.\n";
    if (!titleFont) std::cerr << "[Warning] Could not load title font.\n";

    TTF_TextEngine* textEngine = TTF_CreateRendererTextEngine(renderer);

    SceneParser parser(renderer, textEngine, bodyFont, window);
    Scene scene = parser.ProjectScript_loadFromFile("SampleProject/scenes/mainmenu.json");
    if (!scene.scriptValid)
        std::cerr << "[Warning] Scene script validation failed — running anyway.\n";

    MainMenuContext ctx;
    ctx.renderer = renderer;
    ctx.textEngine = textEngine;
    ctx.titleFont = titleFont;
    ctx.bodyFont = bodyFont;
    ctx.window = window;
    ctx.scene = &scene;

    MainMenuScript script;
    script.ctx = &ctx;
    script.onStart();

    g_renderer = renderer;
    g_window = window;
    g_textEngine = textEngine;
    g_bodyFont = bodyFont;
    g_titleFont = titleFont;
    g_scene = &scene;
    g_script = &script;
    g_ctx = &ctx;
    g_lastTime = SDL_GetTicks();

#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop(main_loop_callback, 0, 1);
#else
    bool running = true;
    Uint64 lastTime = SDL_GetTicks();
    while (running) {
        Uint64 now = SDL_GetTicks();
        float dt = (float)(now - lastTime) / 1000.0f;
        lastTime = now;

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) running = false;
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) {
                if (ctx.showOptions) ctx.showOptions = false;
                else running = false;
            }
            for (auto& elem : scene.guiElements)
                elem->handleEvent(e, window, 0.0f);
        }

        if (ctx.requestQuit) running = false;
        if (ctx.requestPlay) {
            SDL_Log("[Game] Starting play — player='%s' diff=%.0f",
                    ctx.pendingPlayerName.c_str(), ctx.pendingDifficulty);
            ctx.requestPlay = false;
        }

        script.onUpdate(dt);
        movement_system(scene.world, dt);

        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 10, 10, 20, 255);
        SDL_RenderClear(renderer);

        // Dynamic background bands for native build
        int winW = 1600, winH = 900;
        SDL_GetWindowSize(window, &winW, &winH);
        float w = (float)winW;

        for (int i = 0; i < 9; ++i) {
            SDL_SetRenderDrawColor(renderer, 40, 50, 100, (Uint8)(8 + i * 3));
            SDL_FRect band = { 0, (float)(i * 100), w, 100 };
            SDL_RenderFillRect(renderer, &band);
        }

        render_system_game(renderer, textEngine, bodyFont, scene.world);

        for (auto& elem : scene.guiElements)
            elem->render(0.0f);

        script.onDraw();
        SDL_RenderPresent(renderer);
    }
#endif

    script.onEnd();

    if (titleFont) TTF_CloseFont(titleFont);
    if (bodyFont) TTF_CloseFont(bodyFont);
    if (textEngine) TTF_DestroyRendererTextEngine(textEngine);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    TTF_Quit();
    SDL_Quit();
    return 0;
}