#include "main_menu.h"

REGISTER_SCRIPT(MainMenuScript, "main_menu")

void MainMenuScript::onStart() {
    if (!ctx) return;
    elapsed = 0.0f;
    requestPlay = false;
    SDL_Log("[MainMenuScript] onStart");

    ProjectScript_TTF_LoadFont("menuTitleFont", "OpenWorld/assets/fonts/fredoka.ttf", 72);
    ProjectScript_TTF_LoadFont("menuBodyFont",  "OpenWorld/assets/fonts/fredoka.ttf", 26);
    titleFont = ProjectScript_TTF_GetFont("menuTitleFont");
    bodyFont  = ProjectScript_TTF_GetFont("menuBodyFont");

    wireCallbacks();
    centerButton(); // position button in the center of the window
}

void MainMenuScript::onUpdate(float dt) {
    elapsed += dt;
    centerButton(); // re-center on window resize

    if (requestPlay) {
        requestPlay = false;

        SDL_Log("[MainMenu] Campaign clicked");
        change_scene(*ctx->sceneParser, *ctx->scene, ctx->sceneFilePath,
                     "OpenWorld/scenes/world1.json",
                     static_cast<void*>(ctx),
                     ctx->physicsWorld);
    }
}

void MainMenuScript::onDraw() {
    if (!ctx || !ctx->renderer || !ctx->textEngine || !ctx->window) return;

    int winW, winH;
    SDL_GetWindowSize(ctx->window, &winW, &winH);
    float w = (float)winW, h = (float)winH;

    // --- Title ---
    if (titleFont) {
        TTF_Text* title = TTF_CreateText(ctx->textEngine, titleFont, "Creatfighter", 0);
        if (title) {
            TTF_SetTextColor(title, 235, 235, 245, 255);
            int tw, th;
            TTF_GetTextSize(title, &tw, &th);
            float titleX = SDL_roundf((w - tw) * 0.5f);
            float titleY = SDL_roundf(h * 0.28f - (float)th * 0.5f);
            TTF_DrawRendererText(title, titleX, titleY);
            TTF_DestroyText(title);
        }
    }

    // --- Subtitle / instruction ---
    if (bodyFont) {
        float alpha = 160.0f + 80.0f * std::sin(elapsed * 2.0f);
        TTF_Text* sub = TTF_CreateText(ctx->textEngine, bodyFont, "Click Campaign to begin", 0);
        if (sub) {
            TTF_SetTextColor(sub, 140, 150, 200, (Uint8)alpha);
            int sw, sh;
            TTF_GetTextSize(sub, &sw, &sh);
            float buttonCenterY = h * 0.5f;
            float subX = SDL_roundf((w - sw) * 0.5f);
            float subY = SDL_roundf(buttonCenterY - 100.0f); // 50 pixels above button center
            TTF_DrawRendererText(sub, subX, subY);
            TTF_DestroyText(sub);
        }
    }
}

void MainMenuScript::onEnd() {
    SDL_Log("[MainMenuScript] onEnd");
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
void MainMenuScript::centerButton() {
    if (!ctx || !ctx->scene || !playBtn) return;
    int w, h;
    SDL_GetWindowSize(ctx->window, &w, &h);
    float bw = playBtn->getWidth();
    float bh = playBtn->getHeight();
    // Center the button in the window
    playBtn->setRect({ ((float)w - bw) * 0.5f, ((float)h - bh) * 0.5f, bw, bh });
}

void MainMenuScript::wireCallbacks() {
    if (!ctx->scene) return;
    // Find the Button directly in the top-level guiElements
    for (auto& elem : ctx->scene->guiElements) {
        if (elem->getType() == "Button") {
            auto* btn = static_cast<Gui::Button*>(elem.get());
            if (btn->getText() == "Campaign") {
                playBtn = btn;
                btn->onClicked = [this]() {
                    requestPlay = true;
                };
                break;
            }
        }
    }
    if (!playBtn) {
        SDL_Log("[MainMenu] Warning: Campaign button not found in guiElements");
    }
}

// ---------------------------------------------------------------------------
// Standalone entry point — only compiled when NOT part of the game build.
// Guarded so linking with game.cpp doesn't produce two main() symbols.
// ---------------------------------------------------------------------------
#ifndef MAIN_MENU_STANDALONE
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

    SDL_Window* window = SDL_CreateWindow("Creatfighter", windowWidth, windowHeight, SDL_WINDOW_RESIZABLE);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    if (!window || !renderer) {
        std::cerr << "Window/Renderer creation failed\n";
        TTF_Quit(); SDL_Quit(); return -1;
    }

    TTF_TextEngine* textEngine = TTF_CreateRendererTextEngine(renderer);

    const std::string sceneFilePath = "OpenWorld/scenes/main_menu.json";
    SceneParser parser(renderer, textEngine, nullptr, window);
    Scene scene = parser.ProjectScript_loadFromFile(sceneFilePath);

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
#endif // MAIN_MENU_STANDALONE