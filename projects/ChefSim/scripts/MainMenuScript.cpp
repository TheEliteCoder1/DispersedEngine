#include "engine.h"
#include <iostream>
#include <cmath>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

struct MainMenuContext {
    SDL_Renderer*   renderer    = nullptr;
    TTF_TextEngine* textEngine  = nullptr;
    TTF_Font*       titleFont   = nullptr;
    TTF_Font*       bodyFont    = nullptr;
    SDL_Window*     window      = nullptr;
};

class MainMenuScript : public ScriptBase {
public:
    std::string getName() const override { return "MainMenuScript"; }
    MainMenuContext* ctx = nullptr;
    void onStart() override;
    void onUpdate(float dt) override;
    void onDraw() override;
    void onEnd() override;
private:
    float elapsed = 0.0f;
};

static SDL_Renderer* g_renderer = nullptr;
static SDL_Window* g_window = nullptr;
static TTF_TextEngine* g_textEngine = nullptr;
static TTF_Font* g_bodyFont = nullptr;
static TTF_Font* g_titleFont = nullptr;
static MainMenuScript* g_script = nullptr;
static MainMenuContext* g_ctx = nullptr;
static bool g_running = true;
static Uint64 g_lastTime = 0;

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
    int winW = 1600, winH = 900; SDL_GetWindowSize(g_window, &winW, &winH); float w = (float)winW;
    for (int i = 0; i < 9; ++i) {
        SDL_SetRenderDrawColor(g_renderer, 40, 50, 100, (Uint8)(8 + i * 3));
        SDL_FRect band = { 0, (float)(i * 100), w, 100 };
        SDL_RenderFillRect(g_renderer, &band);
    }
    g_script->onDraw(); SDL_RenderPresent(g_renderer);
}
#endif

void ProgramScript::onStart() { if (!ctx) return; elapsed = 0.0f; SDL_Log("[ProgramScript] onStart"); }
void ProgramScript::onUpdate(float dt) { elapsed += dt; }
void ProgramScript::onDraw() {
    if (!ctx || !ctx->renderer || !ctx->textEngine || !ctx->window) return;
    int winW = 1600, winH = 900; SDL_GetWindowSize(ctx->window, &winW, &winH); float w = (float)winW;
    SDL_SetRenderDrawBlendMode(ctx->renderer, SDL_BLENDMODE_BLEND);
    SDL_FRect titleBg = { 0, 0, w, 110 };
    SDL_SetRenderDrawColor(ctx->renderer, 12, 12, 22, 220);
    SDL_RenderFillRect(ctx->renderer, &titleBg);
    TTF_Font* tf = ctx->titleFont ? ctx->titleFont : ctx->bodyFont;
    if (tf && ctx->textEngine) {
        TTF_Text* title = TTF_CreateText(ctx->textEngine, tf, "program", 0);
        if (title) {
            TTF_SetTextColor(title, 200, 210, 255, 255);
            int tw = 0, th = 0; TTF_GetTextSize(title, &tw, &th);
            TTF_DrawRendererText(title, (w - tw) * 0.5f, 18.0f);
            TTF_DestroyText(title);
        }
    }
}
void ProgramScript::onEnd() { SDL_Log("[ProgramScript] onEnd"); }

int main(int argc, char* argv[]) {
    const float windowWidth = 1600.0f, windowHeight = 900.0f;
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) { std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n"; return -1; }
    if (!TTF_Init()) { std::cerr << "TTF_Init failed: " << SDL_GetError() << "\n"; SDL_Quit(); return -1; }
    SDL_Window* window = SDL_CreateWindow("Program", windowWidth, windowHeight, SDL_WINDOW_RESIZABLE);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    if (!window || !renderer) { std::cerr << "Window/Renderer creation failed\n"; TTF_Quit(); SDL_Quit(); return -1; }
    TTF_Font* bodyFont = ProjectScript_TTF_OpenFont("SampleProject/assets/fonts/fredoka.ttf", 22);
    TTF_Font* titleFont = ProjectScript_TTF_OpenFont("SampleProject/assets/fonts/fredoka.ttf", 52);
    TTF_TextEngine* textEngine = TTF_CreateRendererTextEngine(renderer);
    ProgramContext ctx; ctx.renderer = renderer; ctx.textEngine = textEngine; ctx.titleFont = titleFont; ctx.bodyFont = bodyFont; ctx.window = window;
    ProgramScript script; script.ctx = &ctx; script.onStart();
    g_renderer = renderer; g_window = window; g_textEngine = textEngine; g_bodyFont = bodyFont; g_titleFont = titleFont; g_script = &script; g_ctx = &ctx; g_lastTime = SDL_GetTicks();
#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop(main_loop_callback, 0, 1);
#else
    bool running = true; Uint64 lastTime = SDL_GetTicks();
    while (running) {
        Uint64 now = SDL_GetTicks(); float dt = (float)(now - lastTime) / 1000.0f; lastTime = now;
        SDL_Event e;
        while (SDL_PollEvent(&e)) { if (e.type == SDL_EVENT_QUIT) running = false; if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) running = false; }
        script.onUpdate(dt);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 10, 10, 20, 255);
        SDL_RenderClear(renderer);
        int winW = 1600, winH = 900; SDL_GetWindowSize(window, &winW, &winH); float w = (float)winW;
        for (int i = 0; i < 9; ++i) { SDL_SetRenderDrawColor(renderer, 40, 50, 100, (Uint8)(8 + i * 3)); SDL_FRect band = { 0, (float)(i * 100), w, 100 }; SDL_RenderFillRect(renderer, &band); }
        script.onDraw(); SDL_RenderPresent(renderer);
    }
#endif
    script.onEnd();
    if (titleFont) TTF_CloseFont(titleFont); if (bodyFont) TTF_CloseFont(bodyFont);
    if (textEngine) TTF_DestroyRendererTextEngine(textEngine);
    SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window); TTF_Quit(); SDL_Quit();
    return 0;
}