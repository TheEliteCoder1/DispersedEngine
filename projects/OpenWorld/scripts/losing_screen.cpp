#include "losing_screen.h"

REGISTER_SCRIPT(LosingMenuScript, "losing_screen")

void LosingMenuScript::onStart() {
    if (!ctx) return;
    elapsed = 0.0f;
    requestPlay = false;
    SDL_Log("[LosingMenuScript] onStart");

    ProjectScript_TTF_LoadFont("loseTitleFont", "OpenWorld/assets/fonts/fredoka.ttf", 52);
    ProjectScript_TTF_LoadFont("loseBodyFont",  "OpenWorld/assets/fonts/fredoka.ttf", 26); // slightly larger for subtitle
    titleFont = ProjectScript_TTF_GetFont("loseTitleFont");
    bodyFont  = ProjectScript_TTF_GetFont("loseBodyFont");

    wireCallbacks();
    centerButton(); // position button in the center of the window
}

void LosingMenuScript::onUpdate(float dt) {
    elapsed += dt;
    centerButton(); // re-center on window resize

    if (requestPlay) {
        requestPlay = false;
    
        SDL_Log("[LosingMenu] Respawn clicked");
        change_scene(*ctx->sceneParser, *ctx->scene, ctx->sceneFilePath,
                     "OpenWorld/scenes/world1.json",
                     static_cast<void*>(ctx),
                     ctx->physicsWorld);
    }
}

void LosingMenuScript::onDraw() {
    if (!ctx || !ctx->renderer || !ctx->textEngine || !ctx->window) return;

    int winW, winH;
    SDL_GetWindowSize(ctx->window, &winW, &winH);
    float w = (float)winW, h = (float)winH;

    // --- Subtitle / instruction ---
    if (bodyFont) {
        float alpha = 160.0f + 80.0f * std::sin(elapsed * 2.0f);
        TTF_Text* sub = TTF_CreateText(ctx->textEngine, bodyFont, "Click Respawn to try again", 0);
        if (sub) {
            TTF_SetTextColor(sub, 140, 150, 200, (Uint8)alpha);
            int sw, sh;
            TTF_GetTextSize(sub, &sw, &sh);
            float buttonCenterY = h * 0.5f;
            float subY = buttonCenterY - 100.0f; // 50 pixels above button center
            TTF_DrawRendererText(sub, (w - sw) * 0.5f, subY);
            TTF_DestroyText(sub);
        }
    }
}

void LosingMenuScript::onEnd() {
    SDL_Log("[LosingMenuScript] onEnd");
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
void LosingMenuScript::centerButton() {
    if (!ctx || !ctx->scene || !respawnBtn) return;
    int w, h;
    SDL_GetWindowSize(ctx->window, &w, &h);
    float bw = respawnBtn->getWidth();
    float bh = respawnBtn->getHeight();
    // Center the button in the window
    respawnBtn->setRect({ ((float)w - bw) * 0.5f, ((float)h - bh) * 0.5f, bw, bh });
}

void LosingMenuScript::wireCallbacks() {
    if (!ctx->scene) return;
    // Find the Button directly in the top-level guiElements
    for (auto& elem : ctx->scene->guiElements) {
        if (elem->getType() == "Button") {
            auto* btn = static_cast<Gui::Button*>(elem.get());
            if (btn->getText() == "Respawn") {
                respawnBtn = btn;
                btn->onClicked = [this]() {
                    requestPlay = true;
                };
                break;
            }
        }
    }
    if (!respawnBtn) {
        SDL_Log("[LosingMenu] Warning: Respawn button not found in guiElements");
    }
}
// ---------------------------------------------------------------------------
// Standalone entry point — only compiled when NOT part of the game build.
// Guarded so linking with game.cpp doesn't produce two main() symbols.
// ---------------------------------------------------------------------------
#ifndef LOSING_SCREEN_STANDALONE
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
        std::cerr << "Window/Renderer creation failed\n";
        TTF_Quit(); SDL_Quit(); return -1;
    }

    TTF_TextEngine* textEngine = TTF_CreateRendererTextEngine(renderer);

    const std::string sceneFilePath = "OpenWorld/scenes/losing_screen.json";
    SceneParser parser(renderer, textEngine, nullptr, window);
    Scene scene = parser.ProjectScript_loadFromFile(sceneFilePath);

    // Build a GameContext for standalone mode.
    GameContext ctx;
    ctx.renderer      = renderer;
    ctx.textEngine    = textEngine;
    ctx.window        = window;
    ctx.scene         = &scene;
    ctx.sceneParser   = &parser;
    ctx.sceneFilePath = sceneFilePath;

    if (scene.script) {
        scene.script->setContext(&ctx);
        scene.script->onStart();
    }

    bool running = true;
    Uint64 lastTime = SDL_GetTicks();
    SDL_Event e;
    while (running) {
        Uint64 now = SDL_GetTicks();
        float dt = (float)(now - lastTime) / 1000.0f;
        lastTime = now;

        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) running = false;
            for (auto& elem : scene.guiElements)
                elem->handleEvent(e, window, 0.0f, 0.0f);
        }

        if (scene.script) scene.script->onUpdate(dt);

        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 10, 10, 20, 255);
        SDL_RenderClear(renderer);
        int winW, winH;
        SDL_GetWindowSize(window, &winW, &winH);
        float w = (float)winW;
        for (int i = 0; i < 9; ++i) {
            SDL_SetRenderDrawColor(renderer, 40, 50, 100, (Uint8)(8 + i * 3));
            SDL_FRect band = { 0, (float)(i * 100), w, 100 };
            SDL_RenderFillRect(renderer, &band);
        }
        if (scene.script) scene.script->onDraw();
        for (auto& elem : scene.guiElements)
            elem->render(0.0f, 0.0f);
        SDL_RenderPresent(renderer);
    }

    if (scene.script) scene.script->onEnd();
    ProjectScript_TTF_Clear();
    TTF_Quit();
    if (textEngine) TTF_DestroyRendererTextEngine(textEngine);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
#endif // LOSING_SCREEN_EMBEDDED