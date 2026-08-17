#pragma once
#include "game.h"

class MainMenuScript : public ScriptBase {
public:
    std::string getName() const override { return "MainMenuScript"; }

    GameContext* ctx = nullptr;

    void setContext(void* context) override {
        ctx = static_cast<GameContext*>(context);
    }

    void onStart() override;
    void onUpdate(float dt) override;
    void onDraw() override;
    void onEnd() override;

private:
    float elapsed = 0.0f;

    TTF_Font*       titleFont   = nullptr;
    TTF_Font*       bodyFont    = nullptr;
    Gui::Button*    playBtn     = nullptr;
    bool            requestPlay = false;

    void centerButton();
    void wireCallbacks();
};
