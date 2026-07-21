#include "engine.h"
#include <iostream>
#include <cmath>

struct PingPongContext {
    SDL_Renderer*   renderer    = nullptr;
    TTF_TextEngine* textEngine  = nullptr;
    TTF_Font*       titleFont   = nullptr;
    TTF_Font*       bodyFont    = nullptr;
    SDL_Window*     window      = nullptr;
    Scene*          scene       = nullptr;
};

class PingPongScript : public ScriptBase {
public:
    std::string getName() const override { return "ProgramScript"; }
    PingPongContext* ctx = nullptr;
    void onStart() override;
    void onUpdate(float dt) override;
    void onDraw() override;
    void onEnd() override;
private:
    float elapsed = 0.0f;
};
