#pragma once
#include "engine.h"

// Forward-declared so MainMenuScript.cpp can set it after scene load.
struct MainMenuContext {
    SDL_Renderer*   renderer    = nullptr;
    TTF_TextEngine* textEngine  = nullptr;
    TTF_Font*       titleFont   = nullptr;
    TTF_Font*       bodyFont    = nullptr;
    SDL_Window*     window      = nullptr;
    Scene*          scene       = nullptr;

    Gui::Button*   btnPlay    = nullptr;
    Gui::Button*   btnOptions = nullptr;
    Gui::Button*   btnLoad    = nullptr;
    Gui::Button*   btnCredits = nullptr;
    Gui::Button*   btnQuit    = nullptr;
    Gui::LineEdit* playerName = nullptr;
    Gui::SpinBox*  difficulty = nullptr;

    bool requestQuit    = false;
    bool requestPlay    = false;
    bool showOptions    = false;
    std::string pendingPlayerName;
    float       pendingDifficulty = 3.0f;
};

class MainMenuScript : public ScriptBase {
public:
    std::string getName() const override { return "MainMenuScript"; }
    MainMenuContext* ctx = nullptr;

    void onStart() override {
        if (!ctx) return;
        elapsed = 0.0f;
        SDL_Log("[MainMenuScript] onStart");

        wireCallbacks();
        updatePanelPosition(); // Center panel on start
    }
 
    void onUpdate(float dt) override {
        elapsed += dt;
        updatePanelPosition(); // Keep panel centered on resize
    }

    void onDraw() override {
        if (!ctx || !ctx->renderer || !ctx->textEngine || !ctx->window) return;

        // Get dynamic window size
        int winW = 1600, winH = 900;
        SDL_GetWindowSize(ctx->window, &winW, &winH);
        float w = (float)winW;
        float h = (float)winH;

        // ── Title bar background
        SDL_SetRenderDrawBlendMode(ctx->renderer, SDL_BLENDMODE_BLEND);
        SDL_FRect titleBg = { 0, 0, w, 110 };
        SDL_SetRenderDrawColor(ctx->renderer, 12, 12, 22, 220);
        SDL_RenderFillRect(ctx->renderer,  &titleBg);

        // ── Game title ─────────────────────────────────────────────────────
        TTF_Font* tf = ctx->titleFont ? ctx->titleFont : ctx->bodyFont;
        if (tf && ctx->textEngine) {
            TTF_Text* title = TTF_CreateText(ctx->textEngine, tf,  "DISPERSED ENGINE ", 0);
            if (title) {
                TTF_SetTextColor(title, 200, 210, 255, 255);
                int tw = 0, th = 0;
                TTF_GetTextSize(title,  &tw,  &th);
                TTF_DrawRendererText(title, (w - tw) * 0.5f, 18.0f);
                TTF_DestroyText(title);
            }
        }

        // ── Subtitle with pulse ────────────────────────────────────────────
        if (ctx->bodyFont && ctx->textEngine) {
            float alpha = 160.0f + 80.0f * std::sin(elapsed * 2.0f);
            TTF_Text* sub = TTF_CreateText(ctx->textEngine, ctx->bodyFont,
                                            "Select an option below ", 0);
            if (sub) {
                TTF_SetTextColor(sub, 140, 150, 200, (Uint8)alpha);
                int sw = 0, sh = 0;
                TTF_GetTextSize(sub,  &sw,  &sh);
                TTF_DrawRendererText(sub, (w - sw) * 0.5f, 72.0f);
                TTF_DestroyText(sub);
            }
        }

        // ── Player name / difficulty readout (bottom-left) ─────────────────
        if (ctx->bodyFont && ctx->textEngine && ctx->playerName && ctx->difficulty) {
            std::string info =  "Player:  " + ctx->playerName->getText() +
                                "   Difficulty:  " +
                               std::to_string((int)ctx->difficulty->getValue());
            TTF_Text* infoT = TTF_CreateText(ctx->textEngine, ctx->bodyFont, info.c_str(), 0);
            if (infoT) {
                TTF_SetTextColor(infoT, 120, 130, 160, 200);
                TTF_DrawRendererText(infoT, 20.0f, h - 40.0f);
                TTF_DestroyText(infoT);
            }
        }

        // ── Options overlay ─────────────────────────────────────────────────
        if (ctx->showOptions) {
            float overlayW = 840.0f;
            float overlayH = 460.0f;
            // Center the overlay dynamically
            SDL_FRect overlay = { (w - overlayW) * 0.5f, (h - overlayH) * 0.5f, overlayW, overlayH };
            SDL_SetRenderDrawColor(ctx->renderer, 18, 18, 30, 240);
            SDL_RenderFillRect(ctx->renderer,  &overlay);
            SDL_SetRenderDrawColor(ctx->renderer, 80, 90, 140, 255);
            SDL_RenderRect(ctx->renderer,  &overlay);
            
            if (ctx->bodyFont) {
                TTF_Text* ot = TTF_CreateText(ctx->textEngine, ctx->bodyFont,
                     "Options (placeholder) — press Esc to close ", 0);
                if (ot) {
                    int otw = 0, oth = 0;
                    TTF_GetTextSize(ot, &otw, &oth);
                    TTF_SetTextColor(ot, 200, 210, 255, 255);
                    TTF_DrawRendererText(ot, overlay.x + (overlayW - otw) * 0.5f, overlay.y + (overlayH - oth) * 0.5f);
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

    // Helper to keep the main menu panel centered on window resize
    void updatePanelPosition() {
        if (!ctx || !ctx->scene || !ctx->window) return;
        int w, h;
        SDL_GetWindowSize(ctx->window, &w, &h);
        for (auto & elem : ctx->scene->guiElements) {
            if (elem->getType() == "Panel") {
                auto* panel = static_cast<Gui::Panel*>(elem.get());
                float pw = panel->getWidth();
                float ph = panel->getHeight();
                panel->setRect({ ((float)w - pw) * 0.5f, ((float)h - ph) * 0.5f, pw, ph });
            }
        }
    }

    void wireCallbacks() {
        if (!ctx->scene) return;
        for (auto & elem : ctx->scene->guiElements) {
            if (elem->getType() == "Panel") {
                auto* panel = static_cast<Gui::Panel*>(elem.get());
                for (auto & child : panel->getChildrenMutable()) {
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
        const std::string & label = btn->getText();
        if (label == "Play Game") {
            ctx->btnPlay = btn;
            btn->onClicked = [this]() {
                if (ctx->playerName) ctx->pendingPlayerName = ctx->playerName->getText();
                if (ctx->difficulty) ctx->pendingDifficulty = ctx->difficulty->getValue();
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
            btn->onClicked = [this]() { SDL_Log("[MainMenu] Load Game clicked (stub)"); };
        } else if (label == "Credits") {
            ctx->btnCredits = btn;
            btn->onClicked = [this]() { SDL_Log("[MainMenu] Credits clicked (stub)"); };
        } else if (label == "Quit") {
            ctx->btnQuit = btn;
            btn->onClicked = [this]() {
                SDL_Log("[MainMenu] Quit clicked");
                ctx->requestQuit = true;
            };
        }
    }
};