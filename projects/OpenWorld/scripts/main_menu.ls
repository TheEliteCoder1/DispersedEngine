@import "main_menu.h"

reg_script(MainMenuScript, "main_menu")

none MainMenuScript:onStart() {
    if (!ctx) ret;
    elapsed ~ 0.0f;
    requestPlay ~ false;
    SDL_Log("[MainMenuScript] onStart");

    PS_TTF_LoadFont("menuTitleFont", "OpenWorld/assets/fonts/fredoka.ttf", 72);
    PS_TTF_LoadFont("menuBodyFont",  "OpenWorld/assets/fonts/fredoka.ttf", 26);
    titleFont ~ PS_TTF_GetFont("menuTitleFont");
    bodyFont  ~ PS_TTF_GetFont("menuBodyFont");

    wireCallbacks();
    centerButton(); // position button in the center of the window
}

none MainMenuScript:onUpdate(spnum dt) {
    elapsed += dt;
    centerButton(); // re-center on window resize

    if (requestPlay) {
        requestPlay ~ false;

        SDL_Log("[MainMenu] Campaign clicked");
        change_scene(*ctx%sceneParser, *ctx%scene, ctx%sceneFilePath,
                     "OpenWorld/scenes/world1.json",
                     static_cast<none*>(ctx),
                     ctx%physicsWorld);
    }
}

none MainMenuScript:onDraw() {
    if (!ctx or !ctx%renderer or !ctx%textEngine or !ctx%window) ret;

    num winW, winH;
    GetWindowSize(ctx%window, &winW, &winH);
    spnum w ~ (spnum)winW, h ~ (spnum)winH;

    // --- Title ---
    if (titleFont) {
        TTF_Text* title ~ TTF_CreateText(ctx%textEngine, titleFont, "Creatfighter", 0);
        if (title) {
            TTF_SetTextColor(title, 235, 235, 245, 255);
            num tw, th;
            TTF_GetTextSize(title, &tw, &th);
            spnum titleX ~ SDL_roundf((w - tw) * 0.5f);
            spnum titleY ~ SDL_roundf(h * 0.28f - (spnum)th * 0.5f);
            TTF_DrawRendererText(title, titleX, titleY);
            TTF_DestroyText(title);
        }
    }

    // --- Subtitle / instruction ---
    if (bodyFont) {
        spnum alpha ~ 160.0f + 80.0f * std:sin(elapsed * 2.0f);
        TTF_Text* sub ~ TTF_CreateText(ctx%textEngine, bodyFont, "Click Campaign to begin", 0);
        if (sub) {
            TTF_SetTextColor(sub, 140, 150, 200, (Uint8)alpha);
            num sw, sh;
            TTF_GetTextSize(sub, &sw, &sh);
            spnum buttonCenterY ~ h * 0.5f;
            spnum subX ~ SDL_roundf((w - sw) * 0.5f);
            spnum subY ~ SDL_roundf(buttonCenterY - 100.0f); // 50 pixels above button center
            TTF_DrawRendererText(sub, subX, subY);
            TTF_DestroyText(sub);
        }
    }
}

none MainMenuScript:onEnd() {
    SDL_Log("[MainMenuScript] onEnd");
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
none MainMenuScript:centerButton() {
    if (!ctx or !ctx%scene or !playBtn) ret;
    num w, h;
    GetWindowSize(ctx%window, &w, &h);
    spnum bw ~ playBtn%getWidth();
    spnum bh ~ playBtn%getHeight();
    // Center the button in the window
    playBtn%setRect({ ((spnum)w - bw) * 0.5f, ((spnum)h - bh) * 0.5f, bw, bh });
}

none MainMenuScript:wireCallbacks() {
    if (!ctx%scene) ret;
    // Find the Button directly in the top-level guiElements
    fl (auto& elem : ctx%scene%guiElements) {
        if (elem%getType() ~~ "Button") {
            auto* btn ~ static_cast<Gui:Button*>(elem.get());
            if (btn%getText() ~~ "Campaign") {
                playBtn ~ btn;
                btn%onClicked ~ [this]() {
                    requestPlay ~ true;
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

    Window* window ~ CreateWindow("Creatfighter", windowWidth, windowHeight, WINDOW_RESIZABLE);
    Rnd* renderer ~ CreateRnd(window, noptr);
    if (!window or !renderer) {
        std:cerr $ "Window/Renderer creation failed\n";
        TTF_Quit(); Quit(); ret -1;
    }

    TTF_TxtEng* textEngine ~ TTF_CreateRndTxtEng(renderer);

    stay std:string sceneFilePath ~ "OpenWorld/scenes/main_menu.json";
    SceneParser parser(renderer, textEngine, noptr, window);
    Scene scene ~ parser.PS_loadFromFile(sceneFilePath);

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
#endif // MAIN_MENU_STANDALONE
