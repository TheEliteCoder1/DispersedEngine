#include "game.h"

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif


static SDL_Renderer* g_renderer = nullptr;
static SDL_Window* g_window = nullptr;
static TTF_TextEngine* g_textEngine = nullptr;
static PingPongScript* g_script = nullptr;
static PingPongContext* g_ctx = nullptr;
static bool g_running = true;
static Uint64 g_lastTime = 0;
EngineResources g_resources;

#ifdef __EMSCRIPTEN__
void main_loop_callback() {
    if (!g_running) { emscripten_cancel_main_loop(); return; }
    Uint64 now = SDL_GetTicks(); float dt = (float)(now - g_lastTime) / 1000.0f; g_lastTime = now;
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_EVENT_QUIT) g_running = false;
        if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) g_running = false;
    }
    g_script->onUpdate(dt);
    SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(g_renderer, 10, 10, 20, 255);
    SDL_RenderClear(g_renderer);
    int winW = 1600;
    int winH = 900; 
    SDL_GetWindowSize(g_window, &winW, &winH); 
    g_script->onDraw();
    SDL_RenderPresent(g_renderer);
}
#endif

void PingPongScript::onStart() { if (!ctx) return; elapsed = 0.0f; SDL_Log("[PingPongScript] onStart"); }
void PingPongScript::onUpdate(float dt) { elapsed += dt; }
void PingPongScript::onDraw() {
    if (!ctx || !ctx->renderer || !ctx->textEngine || !ctx->window) return;
    int winW = 1600, winH = 900; SDL_GetWindowSize(ctx->window, &winW, &winH);
    SDL_SetRenderDrawBlendMode(ctx->renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ctx->renderer, 12, 12, 22, 220);
    
}

void PingPongScript::onEnd() { SDL_Log("[PingPongScript] onEnd"); }

int main(int argc, char* argv[]) {
    const float windowWidth = 1600.0f, windowHeight = 900.0f;
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) { std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n"; return -1; }
    if (!TTF_Init()) { std::cerr << "TTF_Init failed: " << SDL_GetError() << "\n"; SDL_Quit(); return -1; }
    SDL_Window* window = SDL_CreateWindow("PingPong", windowWidth, windowHeight, SDL_WINDOW_RESIZABLE);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    if (!window || !renderer) { std::cerr << "Window/Renderer creation failed\n"; TTF_Quit(); SDL_Quit(); return -1; }
    TTF_TextEngine* textEngine = TTF_CreateRendererTextEngine(renderer);
    TTF_Font* bodyFont = ProjectScript_TTF_OpenFont("SampleProject/assets/fonts/fredoka.ttf", 22);
    SceneParser parser(renderer, textEngine, bodyFont, window);
    Scene scene = parser.ProjectScript_loadFromFile("PingPong/scenes/game.json");
    if (!scene.scriptValid)
        std::cerr << "[Warning] Scene script validation failed — running anyway.\n";
    PingPongContext ctx; 
    ctx.renderer = renderer; 
    ctx.textEngine = textEngine;
    ctx.window = window;
    ctx.scene = &scene;
    PingPongScript script; 
    script.ctx = &ctx; 
    script.onStart();
    g_renderer = renderer; 
    g_window = window; 
    g_textEngine = textEngine;
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
        while (SDL_PollEvent(&e)) { if (e.type == SDL_EVENT_QUIT) running = false; 
        if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) running = false; }
        script.onUpdate(dt);
        //movement_system(scene.world, dt);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 10, 10, 20, 255);
        SDL_RenderClear(renderer);
        render_system_game(renderer, textEngine, bodyFont, scene.world);
        script.onDraw(); 
        SDL_RenderPresent(renderer);
    }
#endif
    script.onEnd();
    if (textEngine) TTF_DestroyRendererTextEngine(textEngine);
    SDL_DestroyRenderer(renderer); 
    SDL_DestroyWindow(window); 
    SDL_Quit();
    return 0;
}
