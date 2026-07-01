#pragma once
#include "engine.h"

// ============================================================
// MainMenuScript
// Implements ScriptBase for the main menu scene.
// Handles:
//   - Title + subtitle rendering with a subtle pulse animation
//   - Button callbacks wired to scene GUI elements after load
//   - Game state transitions (Play, Options, Credits, Quit)
//   - Difficulty SpinBox + player name LineEdit readback
// ============================================================

// Forward-declared so MainMenuScript.cpp can set it after scene load.
// The script does not own these — MainMenuScript.cpp does.
struct MainMenuContext {
    SDL_Renderer*   renderer    = nullptr;
    TTF_TextEngine* textEngine  = nullptr;
    TTF_Font*       titleFont   = nullptr;   // larger font for the game title
    TTF_Font*       bodyFont    = nullptr;   // regular font
    SDL_Window*     window      = nullptr;
    Scene*          scene       = nullptr;   // non-owning pointer to the live scene

    // Resolved after scene load — pointers into scene.guiElements
    Gui::Button*   btnPlay    = nullptr;
    Gui::Button*   btnOptions = nullptr;
    Gui::Button*   btnLoad    = nullptr;
    Gui::Button*   btnCredits = nullptr;
    Gui::Button*   btnQuit    = nullptr;
    Gui::LineEdit* playerName = nullptr;
    Gui::SpinBox*  difficulty = nullptr;

    // Runtime state
    bool requestQuit    = false;
    bool requestPlay    = false;
    bool showOptions    = false;
    std::string pendingPlayerName;
    float       pendingDifficulty = 3.0f;
};

class MainMenuScript : public ScriptBase {
public:
    std::string getName() const override { return "MainMenuScript"; }

    // Set before onStart() is called.
    MainMenuContext* ctx = nullptr;

    void onStart() override {
        if (!ctx) return;
        elapsed = 0.0f;
        SDL_Log("[MainMenuScript] onStart");

        // Wire button callbacks by walking the loaded scene's GUI elements.
        // The Panel is gui_elements[0]; its children are the interactive widgets.
        wireCallbacks();
    }

    void onUpdate(float dt) override {
        elapsed += dt;
    }

    void onDraw() override {
        if (!ctx || !ctx->renderer || !ctx->textEngine) return;

        // ── Title bar background ───────────────────────────────────────────
        SDL_SetRenderDrawBlendMode(ctx->renderer, SDL_BLENDMODE_BLEND);
        SDL_FRect titleBg = { 0, 0, 1600, 110 };
        SDL_SetRenderDrawColor(ctx->renderer, 12, 12, 22, 220);
        SDL_RenderFillRect(ctx->renderer, &titleBg);

        // ── Game title ─────────────────────────────────────────────────────
        TTF_Font* tf = ctx->titleFont ? ctx->titleFont : ctx->bodyFont;
        if (tf && ctx->textEngine) {
            TTF_Text* title = TTF_CreateText(ctx->textEngine, tf, "DISPERSED ENGINE", 0);
            if (title) {
                TTF_SetTextColor(title, 200, 210, 255, 255);
                int tw = 0, th = 0;
                TTF_GetTextSize(title, &tw, &th);
                TTF_DrawRendererText(title, (1600.0f - tw) * 0.5f, 18.0f);
                TTF_DestroyText(title);
            }
        }

        // ── Subtitle with pulse ─────────────────────────────────────────────
        if (ctx->bodyFont && ctx->textEngine) {
            float alpha = 160.0f + 80.0f * std::sin(elapsed * 2.0f);
            TTF_Text* sub = TTF_CreateText(ctx->textEngine, ctx->bodyFont,
                                           "Select an option below", 0);
            if (sub) {
                TTF_SetTextColor(sub, 140, 150, 200, (Uint8)alpha);
                int sw = 0, sh = 0;
                TTF_GetTextSize(sub, &sw, &sh);
                TTF_DrawRendererText(sub, (1600.0f - sw) * 0.5f, 72.0f);
                TTF_DestroyText(sub);
            }
        }

        // ── Player name / difficulty readout (bottom-left) ─────────────────
        if (ctx->bodyFont && ctx->textEngine && ctx->playerName && ctx->difficulty) {
            std::string info = "Player: " + ctx->playerName->getText() +
                               "   Difficulty: " +
                               std::to_string((int)ctx->difficulty->getValue());
            TTF_Text* infoT = TTF_CreateText(ctx->textEngine, ctx->bodyFont, info.c_str(), 0);
            if (infoT) {
                TTF_SetTextColor(infoT, 120, 130, 160, 200);
                TTF_DrawRendererText(infoT, 20.0f, 860.0f);
                TTF_DestroyText(infoT);
            }
        }

        // ── Options overlay ─────────────────────────────────────────────────
        if (ctx->showOptions) {
            SDL_FRect overlay = { 380, 200, 840, 460 };
            SDL_SetRenderDrawColor(ctx->renderer, 18, 18, 30, 240);
            SDL_RenderFillRect(ctx->renderer, &overlay);
            SDL_SetRenderDrawColor(ctx->renderer, 80, 90, 140, 255);
            SDL_RenderRect(ctx->renderer, &overlay);
            if (ctx->bodyFont) {
                TTF_Text* ot = TTF_CreateText(ctx->textEngine, ctx->bodyFont,
                    "Options (placeholder) — press Esc to close", 0);
                if (ot) {
                    TTF_SetTextColor(ot, 200, 210, 255, 255);
                    TTF_DrawRendererText(ot, 420.0f, 400.0f);
                    TTF_DestroyText(ot);
                }
            }
        }
    }

    void onEnd() override {
        SDL_Log("[MainMenuScript] onEnd");
    }

private:
    float elapsed = 0.0f;

    // Walk scene.guiElements to find the main Panel, then bind each child button.
    void wireCallbacks() {
        if (!ctx->scene) return;
        for (auto& elem : ctx->scene->guiElements) {
            if (elem->getType() == "Panel") {
                auto* panel = static_cast<Gui::Panel*>(elem.get());
                for (auto& child : panel->getChildrenMutable()) {
                    if (child->getType() == "Button") {
                        auto* btn = static_cast<Gui::Button*>(child.get());
                        bindButton(btn);
                    } else if (child->getType() == "LineEdit") {
                        ctx->playerName = static_cast<Gui::LineEdit*>(child.get());
                    } else if (child->getType() == "SpinBox") {
                        ctx->difficulty = static_cast<Gui::SpinBox*>(child.get());
                    }
                }
            }
        }
    }

    void bindButton(Gui::Button* btn) {
        const std::string& label = btn->getText();
        if (label == "Play Game") {
            ctx->btnPlay = btn;
            btn->onClicked = [this]() {
                if (ctx->playerName)
                    ctx->pendingPlayerName = ctx->playerName->getText();
                if (ctx->difficulty)
                    ctx->pendingDifficulty = ctx->difficulty->getValue();
                SDL_Log("[MainMenu] Play clicked — player='%s' difficulty=%.0f",
                        ctx->pendingPlayerName.c_str(), ctx->pendingDifficulty);
                ctx->requestPlay = true;
            };
        } else if (label == "Options") {
            ctx->btnOptions = btn;
            btn->onClicked = [this]() {
                ctx->showOptions = !ctx->showOptions;
                SDL_Log("[MainMenu] Options toggled: %s", ctx->showOptions ? "open" : "closed");
            };
        } else if (label == "Load Game") {
            ctx->btnLoad = btn;
            btn->onClicked = [this]() {
                SDL_Log("[MainMenu] Load Game clicked (stub)");
            };
        } else if (label == "Credits") {
            ctx->btnCredits = btn;
            btn->onClicked = [this]() {
                SDL_Log("[MainMenu] Credits clicked (stub)");
            };
        } else if (label == "Quit") {
            ctx->btnQuit = btn;
            btn->onClicked = [this]() {
                SDL_Log("[MainMenu] Quit clicked");
                ctx->requestQuit = true;
            };
        }
    }
};