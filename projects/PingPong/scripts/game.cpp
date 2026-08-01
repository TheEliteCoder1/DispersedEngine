#include "game.h"
#include <cmath>
#include <ctime>          // for seeding rand()
#include <cstdlib>        // for rand(), srand()

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

static SDL_Renderer* g_renderer = nullptr;
static SDL_Window* g_window = nullptr;
static TTF_TextEngine* g_textEngine = nullptr;
static PingPongScript* g_script = nullptr;
static PingPongContext* g_ctx = nullptr;
static bool g_running = true;
static Uint64 g_lastTime = 0;

#ifdef __EMSCRIPTEN__
void main_loop_callback() {
    if (!g_running) { emscripten_cancel_main_loop(); return; }

    double now = emscripten_get_now();
    float dt = (float)(now - g_lastTime) / 1000.0f;
    g_lastTime = now;

    if (dt > 0.1f) dt = 0.1f;  // clamp to avoid spiral of death

    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_EVENT_QUIT) g_running = false;
        if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) g_running = false;
    }

    g_script->onUpdate(dt);

    SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(g_renderer, 10, 10, 20, 255);
    SDL_RenderClear(g_renderer);

    for (Entity i = 0; i < g_ctx->scene->world.entity_count; ++i) {
        if (!g_ctx->scene->world.has_position[i]) continue;
        render_entity_texture(g_renderer, g_ctx->scene->world, i,
                              g_ctx->scene->world.position_pool[i].x,
                              g_ctx->scene->world.position_pool[i].y,
                              1.0f);
    }


    g_script->onDraw();
    SDL_RenderPresent(g_renderer);
}
#endif

void PingPongScript::resetBall(int servingDirection) {
    if (!ctx || !ctx->scene || ball == (Entity)-1) return;
    ECSWorld& world = ctx->scene->world;
    auto& phys = world.physics_body_pool[ball];
    if (!b2Body_IsValid(phys.bodyId)) return;

    int winW = 1600, winH = 900;
    if (ctx->window) SDL_GetWindowSize(ctx->window, &winW, &winH);

    b2Vec2 centerM = Physics::PxToM({ (float)winW * 0.5f, (float)winH * 0.5f });
    b2Body_SetTransform(phys.bodyId, centerM, b2Body_GetRotation(phys.bodyId));

    b2Vec2 vel = {
        ballSpeed * (float)servingDirection,
        ballSpeed
    };
    b2Body_SetLinearVelocity(phys.bodyId, vel);
}

void PingPongScript::onStart() {
    if (!ctx || !ctx->scene) return;
    elapsed = 0.0f;

    pad1 = findEntityByName(ctx->scene, "Pad1");
    pad2 = findEntityByName(ctx->scene, "Pad2");
    ball = findEntityByName(ctx->scene, "Ball");

    if (pad1 == (Entity)-1 || pad2 == (Entity)-1 || ball == (Entity)-1) {
        SDL_Log("[PingPongScript] Could not find Pad1/Pad2/Ball in the scene!");
    }

    ctx->scene->world.texture_ref_pool[pad1].resourceName = "paddleTex";
    ctx->scene->world.texture_ref_pool[pad2].resourceName = "paddleTex";
    ctx->scene->world.texture_ref_pool[ball].resourceName = "ballTex";

    physics_sync_system(ctx->scene->world, physWorld);

    // ---- Set paddle restitution to 1 (no speed loss) ----
    auto setPaddleRestitution = [&](Entity pad) {
        auto& phys = ctx->scene->world.physics_body_pool[pad];
        if (b2Body_IsValid(phys.bodyId)) {
            b2ShapeId shapes[8];
            int count = b2Body_GetShapes(phys.bodyId, shapes, 8);
            for (int i = 0; i < count; ++i) {
                b2Shape_SetRestitution(shapes[i], 1.0f);
                b2Shape_SetFriction(shapes[i], 0.0f);
            }
        }
    };
    setPaddleRestitution(pad1);
    setPaddleRestitution(pad2);

    // ---- static walls (top and bottom) ----
    int winW = 1600, winH = 900;
    if (ctx->window) SDL_GetWindowSize(ctx->window, &winW, &winH);

    auto addWall = [&](float x, float y, float w, float h) {
        b2BodyDef wallDef = b2DefaultBodyDef();
        wallDef.type = b2_staticBody;
        wallDef.position = Physics::PxToM({ x, y });
        b2BodyId wallId = b2CreateBody(physWorld.GetHandle(), &wallDef);

        b2Polygon box = b2MakeBox(Physics::PxToM(w) * 0.5f, Physics::PxToM(h) * 0.5f);
        b2ShapeDef shapeDef = b2DefaultShapeDef();
        shapeDef.density = 0.0f;
        shapeDef.material.friction = 0.0f;
        shapeDef.material.restitution = 1.0f;
        b2CreatePolygonShape(wallId, &shapeDef, &box);
    };

    addWall(winW * 0.5f, 0.0f, winW, 20.0f);           // top
    addWall(winW * 0.5f, (float)winH, winW, 20.0f);    // bottom

    score1 = 0;
    score2 = 0;
    resetBall(1);

    SDL_Log("[PingPongScript] onStart");
}

void PingPongScript::onUpdate(float dt) {
    elapsed += dt;
    if (!ctx || !ctx->scene) return;
    if (pad1 == (Entity)-1 || pad2 == (Entity)-1 || ball == (Entity)-1) return;

    ECSWorld& world = ctx->scene->world;
    int winW = 1600, winH = 900;
    if (ctx->window) SDL_GetWindowSize(ctx->window, &winW, &winH);

    const bool* keys = SDL_GetKeyboardState(nullptr);

    // --- Drive paddles ---
    auto setPaddleVelocity = [&](Entity pad, bool up, bool down) {
        auto& phys = world.physics_body_pool[pad];
        if (!b2Body_IsValid(phys.bodyId)) return;
        float vy = 0.0f;
        if (up)   vy -= paddleSpeed;
        if (down) vy += paddleSpeed;
        b2Body_SetLinearVelocity(phys.bodyId, { 0.0f, vy });
    };
    setPaddleVelocity(pad1, keys[SDL_SCANCODE_W], keys[SDL_SCANCODE_S]);
    setPaddleVelocity(pad2, keys[SDL_SCANCODE_UP], keys[SDL_SCANCODE_DOWN]);

    // --- Step physics on a fixed timestep ----
    // Native (Windows) frame timing is fairly stable, so stepping physics
    // directly with the render dt looked fine there. Browser rAF timing is
    // noisier (JS/GC/compositor overhead), so feeding Box2D a jittery dt
    // produces visibly choppy motion even at a similar average FPS.
    // Decoupling physics from render dt fixes that on both platforms.
    static float physicsAccumulator = 0.0f;
    const float FIXED_DT = 1.0f / 60.0f;
    physicsAccumulator += dt;
    // Clamp so a hitch (tab backgrounded, asset load, etc.) doesn't cause
    // a spiral of death trying to catch up.
    const float MAX_ACCUMULATED = FIXED_DT * 5.0f;
    if (physicsAccumulator > MAX_ACCUMULATED) physicsAccumulator = MAX_ACCUMULATED;
    while (physicsAccumulator >= FIXED_DT) {
        b2World_Step(physWorld.GetHandle(), FIXED_DT, 4);
        physicsAccumulator -= FIXED_DT;
    }
    movement_system(world, dt);

    // --- Clamp paddles inside screen ---
    auto clampPaddle = [&](Entity pad) {
        auto& phys = world.physics_body_pool[pad];
        float w = world.has_rectangle_shape[pad] ? world.rectangle_shape_pool[pad].w : 30.0f;
        float h = world.has_rectangle_shape[pad] ? world.rectangle_shape_pool[pad].h : 100.0f;
        float& y = world.position_pool[pad].y;
        float clampedY = std::clamp(y, 0.0f, (float)winH - h);
        if (clampedY != y && b2Body_IsValid(phys.bodyId)) {
            b2Vec2 correctedM = Physics::PxToM({ world.position_pool[pad].x + w * 0.5f, clampedY + h * 0.5f });
            b2Body_SetTransform(phys.bodyId, { correctedM.x, correctedM.y }, b2Body_GetRotation(phys.bodyId));
            y = clampedY;
        }
    };
    clampPaddle(pad1);
    clampPaddle(pad2);

    // --- Paddle collision: diagonal reflection (reverse x, keep y) ---
    auto& ballPhys = world.physics_body_pool[ball];
    if (b2Body_IsValid(ballPhys.bodyId)) {
        float ballRadius = world.physics_body_pool[ball].radius;
        float bx = world.position_pool[ball].x;
        float by = world.position_pool[ball].y;
        float ballCenterX = bx + ballRadius * 0.5f;
        float ballCenterY = by + ballRadius * 0.5f;

        Entity paddles[2] = { pad1, pad2 };
        for (Entity pad : paddles) {
            float pw = world.has_rectangle_shape[pad] ? world.rectangle_shape_pool[pad].w : 30.0f;
            float ph = world.has_rectangle_shape[pad] ? world.rectangle_shape_pool[pad].h : 100.0f;
            float px = world.position_pool[pad].x;
            float py = world.position_pool[pad].y;
            float padCenterX = px + pw * 0.5f;
            float padCenterY = py + ph * 0.5f;

            // AABB overlap test using diameter (ballRadius is radius, so diameter = 2*radius)
            float ballDiameter = ballRadius * 2.0f;
            if (!(bx < px + pw && bx + ballDiameter > px &&
                by < py + ph && by + ballDiameter > py)) continue;

            // Get current velocity
            b2Vec2 vel = b2Body_GetLinearVelocity(ballPhys.bodyId);

            // Only react if moving toward the paddle
            float dirX = (ballCenterX > padCenterX) ? 1.0f : -1.0f;
            if (vel.x * dirX <= 0.0f) continue;

            // ---- Play impact sound ----
            float speed = b2Length(vel);

            // ---- Pure diagonal reflection ----
            float newX = -vel.x;          // reverse horizontal direction
            float newY = vel.y;           // keep vertical unchanged

            // Accelerate uniformly (no angle change)
            const float maxSpeed = 30.0f;
            if (speed < maxSpeed) {
                float factor = 1.02f;     // 2% speed increase per hit
                newX *= factor;
                newY *= factor;
                speed = b2Length({newX, newY});
                if (speed > maxSpeed) {
                    newX = newX / speed * maxSpeed;
                    newY = newY / speed * maxSpeed;
                }
            }

            // Apply the new velocity
            b2Body_SetLinearVelocity(ballPhys.bodyId, { newX, newY });
            break; // only process one paddle per frame
        }
    }

    // --- Scoring ---
    float bw = world.has_rectangle_shape[ball] ? world.rectangle_shape_pool[ball].w : 25.0f;
    float bx = world.position_pool[ball].x;
    if (bx + bw < 0.0f)         { score2++; resetBall(+1); }
    else if (bx > (float)winW)  { score1++; resetBall(-1); }
}

void PingPongScript::onDraw() {
    if (!ctx || !ctx->renderer || !ctx->textEngine || !ctx->window) return;
    int winW = 1600, winH = 900; SDL_GetWindowSize(ctx->window, &winW, &winH);
    SDL_SetRenderDrawBlendMode(ctx->renderer, SDL_BLENDMODE_BLEND);

    // Dashed center line
    SDL_SetRenderDrawColor(ctx->renderer, 80, 80, 95, 255);
    for (float y = 0.0f; y < (float)winH; y += 24.0f) {
        SDL_FRect dash = { winW * 0.5f - 2.0f, y, 4.0f, 14.0f };
        SDL_RenderFillRect(ctx->renderer, &dash);
    }

    // Score (centered)
    TTF_Font* font = ctx->titleFont;
    if (font)
    {
        std::string score =
            std::to_string(score1) + "    " +
            std::to_string(score2);

        TTF_Text* text =
            TTF_CreateText(ctx->textEngine, font, score.c_str(), 0);

        if (text)
        {
            TTF_SetTextColor(text,0,255,0,255);

            int tw = 0, th = 0;
            TTF_GetTextSize(text, &tw, &th);

            float x = winW * 0.5f - tw * 0.5f;
            float y = 40.0f;

            TTF_DrawRendererText(text, x, y);
            TTF_DestroyText(text);
        }
    }
}

void PingPongScript::onEnd() { SDL_Log("[PingPongScript] onEnd"); }

int main(int argc, char* argv[]) {
    // Seed the random number generator once at startup
    srand((unsigned)time(nullptr));

    const float windowWidth = 1600.0f, windowHeight = 900.0f;
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) { std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n"; return -1; }
    if (!TTF_Init()) { std::cerr << "TTF_Init failed: " << SDL_GetError() << "\n"; SDL_Quit(); return -1; }

    SDL_Window* window = SDL_CreateWindow("PingPong", windowWidth, windowHeight, SDL_WINDOW_RESIZABLE);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    if (!window || !renderer) { std::cerr << "Window/Renderer creation failed\n"; TTF_Quit(); SDL_Quit(); return -1; }
    // Cap the native loop to the display's refresh rate. Without this the
    // loop runs uncapped (hundreds/thousands of fps), which causes visible
    // tearing and can itself look "laggy" from the CPU/GPU contention of
    // presenting far faster than the monitor can show anything.
    if (!SDL_SetRenderVSync(renderer, 1)) {
        std::cerr << "[Warning] SDL_SetRenderVSync failed: " << SDL_GetError() << "\n";
    }
    g_resources.TextureManager.SetRenderer(renderer);
    TTF_TextEngine* textEngine = TTF_CreateRendererTextEngine(renderer);

    ProjectScript_TTF_LoadFont("titleFont", "PingPong/assets/fonts/fredoka.ttf", 45);
    ProjectScript_TTF_LoadFont("bodyFont", "PingPong/assets/fonts/fredoka.ttf", 22);

    ProjectScript_IMG_LoadTexture("paddleTex", "PingPong/assets/textures/paddle.svg");
    ProjectScript_IMG_LoadTexture("ballTex",   "PingPong/assets/textures/ball.svg");

    TTF_Font* titleFont = ProjectScript_TTF_GetFont("titleFont");
    TTF_Font* bodyFont = ProjectScript_TTF_GetFont("bodyFont");

    SceneParser parser(renderer, textEngine, bodyFont, window);
    Scene scene = parser.ProjectScript_loadFromFile("PingPong/scenes/game.json");
    if (!scene.scriptValid)
        std::cerr << "[Warning] Scene script validation failed — running anyway.\n";
    PingPongContext ctx; 
    ctx.renderer = renderer; 
    ctx.textEngine = textEngine;
    ctx.window = window;
    ctx.scene = &scene;
    ctx.titleFont = titleFont;
    ctx.bodyFont  = bodyFont;
    PingPongScript script; 
    script.ctx = &ctx; 
    script.onStart();
    g_renderer = renderer; 
    g_window = window; 
    g_textEngine = textEngine;
    g_script = &script; 
    g_ctx = &ctx; 
    g_lastTime = SDL_GetTicks();

#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop(main_loop_callback, 0, 1);
    emscripten_set_main_loop_timing(EM_TIMING_RAF, 1);
#else
    bool running = true; 
    Uint64 lastTime = SDL_GetTicks();
    while (running) {
        Uint64 now = SDL_GetTicks(); 
        float dt = (float)(now - lastTime) / 1000.0f; 
        lastTime = now;
        SDL_Event e;
        while (SDL_PollEvent(&e)) { if (e.type == SDL_EVENT_QUIT) running = false; 
        if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) running = false; }
        script.onUpdate(dt);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 10, 10, 20, 255);
        SDL_RenderClear(renderer);
        for (Entity i = 0; i < scene.world.entity_count; ++i) {
            if (!scene.world.has_position[i]) continue;
            render_entity_texture(renderer, scene.world, i,
                                   scene.world.position_pool[i].x,
                                   scene.world.position_pool[i].y,
                                   1.0f);
        }
        script.onDraw(); 
        SDL_RenderPresent(renderer);
    }
#endif
    script.onEnd();
    if (textEngine) TTF_DestroyRendererTextEngine(textEngine);
    ProjectScript_TTF_Clear();
    ProjectScript_IMG_Clear();
    TTF_Quit();
    SDL_DestroyRenderer(renderer); 
    SDL_DestroyWindow(window); 
    SDL_Quit();
    return 0;
}