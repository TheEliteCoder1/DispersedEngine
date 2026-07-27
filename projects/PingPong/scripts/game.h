#include "engine.h"

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
    std::string getName() const override { return "PingPongScript"; }
    PingPongContext* ctx = nullptr;
    void onStart() override;
    void onUpdate(float dt) override;
    void onDraw() override;
    void onEnd() override;
private:
    float elapsed = 0.0f;

    // --- Pong game state ---
    Entity pad1 = (Entity)-1;   // "Pad1" from game.json (left paddle, now kinematic)
    Entity pad2 = (Entity)-1;   // "Pad2" from game.json (right paddle, now kinematic)
    Entity ball = (Entity)-1;   // "Ball" from game.json (stays dynamic)

    // Owns the box2d simulation for this scene.
    // NOTE: haven't seen physics.h, so I don't know its exact constructor.
    // If Physics::PhysicsWorld takes gravity args, construct it with zero
    // gravity explicitly (e.g. Physics::PhysicsWorld{0.0f, 0.0f};) so the
    // ball/paddles don't drift downward - check physics.h for the signature.
    Physics::PhysicsWorld physWorld;

    const float ballSpeed   = 10.0f;  // box2d units are meters, not pixels
    const float paddleSpeed = 10.0f;  // m/s

    int score1 = 0;
    int score2 = 0;

    void   resetBall(int servingDirection); // +1 = serve toward Pad2, -1 = serve toward Pad1
};
