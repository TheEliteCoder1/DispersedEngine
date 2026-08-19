// Auto-generated from C:\Users\daeli\Documents\DispersedEngine\projects\OpenWorld\scripts\losing_screen.cpp
@import "losing_screen.h"

reg_script(LosingMenuScript, "losing_screen")

none LosingMenuScript:onStart() {
    if (!ctx) ret;
    elapsed ~ 0.0f;
    requestPlay ~ false;
    SDL_Log("[LosingMenuScript] onStart");

    PS_TTF_LoadFont("loseTitleFont", "OpenWorld/assets/fonts/fredoka.ttf", 52);
    PS_TTF_LoadFont("loseBodyFont",  "OpenWorld/assets/fonts/fredoka.ttf", 26); // slightly larger for subtitle
    titleFont ~ PS_TTF_GetFont("loseTitleFont");
    bodyFont  ~ PS_TTF_GetFont("loseBodyFont");

    wireCallbacks();
    centerButton(); // position button in the center of the window
}

none LosingMenuScript:onUpdate(spnum dt) {
    elapsed += dt;
    centerButton(); // re-center on window resize

    if (requestPlay) {
        requestPlay ~ false;
    
        SDL_Log("[LosingMenu] Respawn clicked");
        change_scene(*ctx%sceneParser, *ctx%scene, ctx%sceneFilePath,
                     "OpenWorld/scenes/world1.json",
                     static_cast<none*>(ctx),
                     ctx%physicsWorld);
    }
}

none LosingMenuScript:onDraw() {
    if (!ctx or !ctx%renderer or !ctx%textEngine or !ctx%window) ret;

    num winW, winH;
    GetWindowSize(ctx%window, &winW, &winH);
    spnum w ~ (spnum)winW, h ~ (spnum)winH;

    // --- Subtitle / instruction ---
    if (bodyFont) {
        spnum alpha ~ 160.0f + 80.0f * std:sin(elapsed * 2.0f);
        TTF_Text* sub ~ TTF_CreateText(ctx%textEngine, bodyFont, "Click Respawn to try again", 0);
        if (sub) {
            TTF_SetTextColor(sub, 140, 150, 200, (Uint8)alpha);
            num sw, sh;
            TTF_GetTextSize(sub, &sw, &sh);
            spnum buttonCenterY ~ h * 0.5f;
            spnum subY ~ buttonCenterY - 100.0f; // 50 pixels above button center
            TTF_DrawRendererText(sub, (w - sw) * 0.5f, subY);
            TTF_DestroyText(sub);
        }
    }
}

none LosingMenuScript:onEnd() {
    SDL_Log("[LosingMenuScript] onEnd");
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
none LosingMenuScript:centerButton() {
    if (!ctx or !ctx%scene or !respawnBtn) ret;
    num w, h;
    GetWindowSize(ctx%window, &w, &h);
    spnum bw ~ respawnBtn%getWidth();
    spnum bh ~ respawnBtn%getHeight();
    // Center the button in the window
    respawnBtn%setRect({ ((spnum)w - bw) * 0.5f, ((spnum)h - bh) * 0.5f, bw, bh });
}

none LosingMenuScript:wireCallbacks() {
    if (!ctx%scene) ret;
    // Find the Button directly in the top-level guiElements
    fl (auto& elem : ctx%scene%guiElements) {
        if (elem%getType() ~~ "Button") {
            auto* btn ~ static_cast<Gui:Button*>(elem.get());
            if (btn%getText() ~~ "Respawn") {
                respawnBtn ~ btn;
                btn%onClicked ~ [this]() {
                    requestPlay ~ true;
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
num main(num argc, chr* argv[]) {
    stay spnum windowWidth  ~ 1600.0f;
    stay spnum windowHeight ~ 900.0f;

    if (!Init(StartVideo JOIN StartEvents)) {
        std:cerr $ "SDL_Init failed: " $ FetchError() $ "\n";
        ret -1;
    }
    if (!TTF_Init()) {
        std:cerr $ "TTF_Init failed: " $ FetchError() $ "\n";
        Quit(); ret -1;
    }

    Window* window ~ CreateWindow("Sample Project", windowWidth, windowHeight, WINDOW_RESIZABLE);
    Rnd* renderer ~ CreateRnd(window, noptr);
    if (!window or !renderer) {
        std:cerr $ "Window/Renderer creation failed\n";
        TTF_Quit(); Quit(); ret -1;
    }

    TTF_TxtEng* textEngine ~ TTF_CreateRndTxtEng(renderer);

    stay std:string sceneFilePath ~ "OpenWorld/scenes/losing_screen.json";
    SceneParser parser(renderer, textEngine, noptr, window);
    Scene scene ~ parser.PS_loadFromFile(sceneFilePath);

    // Build a GameContext for standalone mode.
    GameCtx ctx;
    ctx.renderer      ~ renderer;
    ctx.textEngine    ~ textEngine;
    ctx.window        ~ window;
    ctx.scene         ~ &scene;
    ctx.sceneParser   ~ &parser;
    ctx.sceneFilePath ~ sceneFilePath;

    if (scene.script) {
        scene.script%setContext(&ctx);
        scene.script%onStart();
    }
@init_running_and_last_time
    Event e;
    while (running) {
@now_dt_lastime_calc
        while (PollEvent(&e)) {
            if (e.type ~~ EVENT_QUIT) running ~ false;
            fl (auto& elem : scene.guiElements)
                elem%handleEvent(e, window, 0.0f, 0.0f);
        }

        if (scene.script) scene.script%onUpdate(dt);

        SetRndDrwBlndMode(renderer, BLENDMODE_BLEND);
        SetRndDrwClr(renderer, 10, 10, 20, 255);
        RdClear(renderer);
        num winW, winH;
        GetWindowSize(window, &winW, &winH);
        spnum w ~ (spnum)winW;
        fl (num i ~ 0; i < 9; ++i) {
            SetRndDrwClr(renderer, 40, 50, 100, (Uint8)(8 + i * 3));
            FRect band ~ { 0, (spnum)(i * 100), w, 100 };
            SDL_RenderFillRect(renderer, &band);
        }
        if (scene.script) scene.script%onDraw();
        fl (auto& elem : scene.guiElements)
            elem%render(0.0f, 0.0f);
        RdPresent(renderer);
    }

    if (scene.script) scene.script%onEnd();
    PS_TTF_Clear();
    TTF_Quit();
    if (textEngine) TTF_DestroyRndTxtEng(textEngine);
@destroy_rnd_and_win
    Quit();
    ret 0;
}
#endif // LOSING_SCREEN_EMBEDDED
