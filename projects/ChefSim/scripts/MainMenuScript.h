#pragma once
#include "engine.h"

// Bare-bones context holding just what's needed for rendering
struct MainMenuContext {
    SDL_Renderer*   renderer    = nullptr;
    TTF_TextEngine* textEngine  = nullptr;
    TTF_Font*       titleFont   = nullptr;
    TTF_Font*       bodyFont    = nullptr;
    SDL_Window*     window      = nullptr;
};

class ProgramScript : public ScriptBase {
public:
    std::string getName() const override { return "ProgramScript"; }
    MainMenuContext* ctx = nullptr;

    void onStart() override;
    void onUpdate(float dt) override;
    void onDraw() override;
    void onEnd() override;

private:
    float elapsed = 0.0f;
};
