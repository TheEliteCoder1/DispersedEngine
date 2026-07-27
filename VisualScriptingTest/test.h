#pragma once
#include "engine.h"

struct GameContext {
    SDL_Renderer*   renderer    = nullptr;
    TTF_TextEngine* textEngine  = nullptr;
    SDL_Window*     window      = nullptr;
    Scene*          scene       = nullptr;
};

class GameScript : public ScriptBase {
public:
    std::string getName() const override { return "GameScript"; }
    GameContext* ctx = nullptr;
    void onStart() override;
    void onUpdate(float dt) override;
    void onDraw() override;
    void onEnd() override;
private:
};
