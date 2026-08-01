
#include "game.h"
#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif
 
static SDL_Renderer *g_renderer = nullptr;
static SDL_Window *g_window = nullptr;
TTF_TextEngine *g_textEngine = nullptr;
static GameScript *g_script = nullptr;
static GameContext *g_context = nullptr;
static bool g_running = true;
static Uint64 g_lastTime = 0;
static constexpr float dtStep = 1000.0f;
static constexpr float minDt = 0.1f;
 
// ---------------------------------------------------------------------------
// Shared per-frame helpers
//
// Both the native (SDL) loop and the Emscripten loop must step physics,
// sync the ECS from Box2D, and render identically. Previously this logic
// was duplicated in two places and the Emscripten copy silently fell out
// of sync (it never called physicsWorld->Step() or movement_system() at
// all). Keeping a single implementation here means that class of bug
// can't happen again.
// ---------------------------------------------------------------------------
 
// Steps the physics world, syncs the resulting Box2D transforms back into
// the ECS position pool, and clamps the player back inside the camera
// bounds if the physics step let it drift out.
static void StepPhysicsAndSync(GameContext& ctx, GameScript& script, float dt) {
    if (!ctx.physicsWorld) return;
 
    // Fixed 60 Hz step keeps the simulation stable regardless of frame rate
    ctx.physicsWorld->Step(1.0f / 60.0f, 4);
    movement_system(ctx.scene->world, dt);
 
    // Keep the player inside the world bounds by teleporting the body back
    // if movement_system let it drift past the camera bounds.
    if (script.player != (Entity)-1 &&
        ctx.scene->world.has_physics_body[script.player] &&
        b2Body_IsValid(ctx.scene->world.physics_body_pool[script.player].bodyId) &&
        ctx.camera.boundsEnabled)
    {
        auto& pos = ctx.scene->world.position_pool[script.player];
        float w = ctx.scene->world.has_rectangle_shape[script.player]
                ? ctx.scene->world.rectangle_shape_pool[script.player].w : 50.0f;
        float h = ctx.scene->world.has_rectangle_shape[script.player]
                ? ctx.scene->world.rectangle_shape_pool[script.player].h : 50.0f;
        float minX = ctx.camera.boundsMinX;
        float maxX = ctx.camera.boundsMaxX - w;
        float minY = ctx.camera.boundsMinY;
        float maxY = ctx.camera.boundsMaxY - h;
        if (minX > maxX) maxX = minX;
        if (minY > maxY) maxY = minY;
 
        float clampedX = std::clamp(pos.x, minX, maxX);
        float clampedY = std::clamp(pos.y, minY, maxY);
        if (clampedX != pos.x || clampedY != pos.y) {
            pos.x = clampedX;
            pos.y = clampedY;
            // Teleport the Box2D body to match and kill any residual velocity
            // so the player doesn't "bounce" off the wall next frame.
            b2BodyId bid = ctx.scene->world.physics_body_pool[script.player].bodyId;
            b2Body_SetTransform(bid,
                Physics::PxToM(b2Vec2{ pos.x + w * 0.5f, pos.y + h * 0.5f }),
                b2Body_GetRotation(bid));
            b2Body_SetLinearVelocity(bid, b2Vec2_zero);
        }
    }
}
 
// Clears the screen, renders the tilemap, builds the (z-index + Y-sorted)
// draw order, renders every visible entity, then the physics debug overlay
// and minimap, and presents.
static void RenderFrame(SDL_Renderer* renderer, SDL_Window* window, GameContext& ctx, GameScript& script) {
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 10, 10, 20, 255);
    SDL_RenderClear(renderer);
 
    int winW, winH;
    SDL_GetWindowSize(window, &winW, &winH);
 
    // 1. Compute visible world rect for culling
    SDL_FRect visibleWorld = Tools::computeVisibleWorldRect(ctx.camera, winW, winH);
 
    // 2. Render the tiled biome backgrounds
    script.tileMap.render(renderer, ctx.camera, visibleWorld);
 
    // 3. Build render order (excluding the main background entity, handled by tileMap).
    //    Player and decorations share the same z tier (see onStart), so this Y-sort
    //    actually determines who draws in front of whom instead of z_index deciding
    //    it outright.
    std::vector<Entity> exclude = { script.background };
    std::vector<Entity> renderOrder = Tools::buildVisibleRenderOrder(
        ctx.scene->world, visibleWorld, exclude,
        script.ySortEnabled, script.ySortAnchor);
 
    // 4. Render visible entities
    for (Entity i : renderOrder) {
        SDL_FPoint screenPos = ctx.camera.worldToScreen(
            ctx.scene->world.position_pool[i].x,
            ctx.scene->world.position_pool[i].y);
 
        if (ctx.scene->world.has_animation_state[i])
            render_entity_animation(renderer, ctx.scene->world, i, screenPos.x, screenPos.y, ctx.camera.zoom);
        else if (ctx.scene->world.has_texture_ref[i])
            render_entity_texture(renderer, ctx.scene->world, i, screenPos.x, screenPos.y, ctx.camera.zoom);
    }
 
    script.onDraw();
 
    if (script.drawPhysicsDebug) {
        draw_physics_debug_overlay(renderer, ctx.scene->world, ctx.camera);
    }
 
    // 5. Render the minimap
    script.minimap.render(renderer, ctx.scene->world, script.player, winW, winH);

    SDL_FPoint playerScreen = ctx.camera.worldToScreen(
        ctx.scene->world.position_pool[script.player].x,
        ctx.scene->world.position_pool[script.player].y
    );

    // Offset above the player (e.g., 20 pixels up) and center horizontally
    float barOffsetY = -30.0f; // above player's top

    script.healthbar.render(renderer,
        playerScreen.x,
        playerScreen.y + barOffsetY
    );
 
    SDL_RenderPresent(renderer);
}
 
#ifdef __EMSCRIPTEN__
void main_loop_callback() {
    if (!g_running) { emscripten_cancel_main_loop(); return; }
    double now = emscripten_get_now();
    float dt = (float)(now - g_lastTime) / dtStep;
    g_lastTime = (Uint64)now;
    if (dt > minDt) dt = minDt;
 
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_EVENT_QUIT) g_running = false;
        if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) g_running = false;
        if (e.type == SDL_EVENT_GAMEPAD_ADDED && !g_context->gamepad)
            g_context->gamepad = SDL_OpenGamepad(e.gdevice.which);
        else if (e.type == SDL_EVENT_GAMEPAD_REMOVED && g_context->gamepad &&
                 SDL_GetGamepadID(g_context->gamepad) == e.gdevice.which) {
            SDL_CloseGamepad(g_context->gamepad);
            g_context->gamepad = nullptr;
        }
    }
 
    g_script->onUpdate(dt);
 
    // Same physics step + ECS sync the native loop uses — this was
    // previously missing here, which meant the player's position_pool
    // never advanced on web at all once the direct-position fallback
    // was removed from onUpdate().
    StepPhysicsAndSync(*g_context, *g_script, dt);
 
    animation_system(g_context->scene->world, dt);
 
    RenderFrame(g_renderer, g_window, *g_context, *g_script);
}
#endif
 
void GameScript::buildBiomeGrid() {
    biomes.clear();
    auto addBiome = [&](const std::string& tex, int gx, int gy, int id) {
        biomes.push_back({tex, gx, gy, id});
    };
    addBiome("snowBackground",   0, 0, 2); addBiome("snowBackground",   1, 0, 2); addBiome("snowBackground",   2, 0, 2);
    addBiome("grassBackground",  0, 1, 0); addBiome("snowBackground",   1, 1, 2); addBiome("grassBackground",  2, 1, 0);
    addBiome("grassBackground",  0, 2, 0); addBiome("grassBackground",  1, 2, 0); addBiome("grassBackground",  2, 2, 0);
    addBiome("grassBackground",  0, 3, 0); addBiome("desertBackground", 1, 3, 1); addBiome("grassBackground",  2, 3, 0);
    addBiome("desertBackground", 0, 4, 1); addBiome("desertBackground", 1, 4, 1); addBiome("desertBackground", 2, 4, 1);
}
 
void GameScript::rebuildTileMap() {
    if (!(ctx && ctx->scene) || background == (Entity)-1) return;
    if (!ctx->scene->world.has_rectangle_shape[background] || !ctx->scene->world.has_position[background]) return;
 
    tileMap.tileWidth  = ctx->scene->world.rectangle_shape_pool[background].w;
    tileMap.tileHeight = ctx->scene->world.rectangle_shape_pool[background].h;
 
    float originX = ctx->scene->world.position_pool[background].x;
    float originY = ctx->scene->world.position_pool[background].y;
    tileMap.clear();
 
    for (const Biome& biome : biomes) {
        float biomeOriginX = originX + biome.gridX * biomeTileCountX * tileMap.tileWidth;
        float biomeOriginY = originY + biome.gridY * biomeTileCountY * tileMap.tileHeight;
        tileMap.addRegion(biomeOriginX, biomeOriginY, biomeTileCountX * tileMap.tileWidth, biomeTileCountY * tileMap.tileHeight, biome.textureName);
    }
 
    float totalW = biomeGridW * biomeTileCountX * tileMap.tileWidth;
    float totalH = biomeGridH * biomeTileCountY * tileMap.tileHeight;
    ctx->camera.setBounds(originX, originX + totalW, originY, originY + totalH, true);
    minimap.setWorldBounds(originX, originX + totalW, originY, originY + totalH);
 
    // --- NEW: Configure minimap biome grid ---
    std::vector<Tools::MinimapBiome> miniBiomes;
    for (const Biome& b : biomes) {
        Tools::MinimapBiome mb;
        mb.textureName = b.textureName;
        mb.gridX = b.gridX;
        mb.gridY = b.gridY;
        mb.id = b.id;
        // Fallback colors matching the original renderMinimap()
        if (b.textureName.find("grass") != std::string::npos)       { mb.colorG = 140; }
        else if (b.textureName.find("snow") != std::string::npos)  { mb.colorR = 200; mb.colorG = 200; mb.colorB = 220; }
        else if (b.textureName.find("desert") != std::string::npos){ mb.colorR = 200; mb.colorG = 170; mb.colorB = 80; }
        miniBiomes.push_back(mb);
    }
    minimap.setBiomeGrid(miniBiomes, tileMap.tileWidth, tileMap.tileHeight, biomeTileCountX, biomeTileCountY);
}
 
void GameScript::spawnRandomDecorations() {
    if (!(ctx && ctx->scene)) return;
 
    static std::mt19937 rng(std::random_device{}());
    decorationEntities.clear();
 
    auto scatterForBiome = [&](const Biome& biome, Entity firstSrc, Entity secondSrc, int count1, int count2) {
        float biomeOriginX = ctx->camera.boundsMinX + biome.gridX * biomeTileCountX * tileMap.tileWidth;
        float biomeOriginY = ctx->camera.boundsMinY + biome.gridY * biomeTileCountY * tileMap.tileHeight;
        float biomeW = biomeTileCountX * tileMap.tileWidth;
        float biomeH = biomeTileCountY * tileMap.tileHeight;
 
        Tools::scatterDecorations(ctx->scene->world, firstSrc, count1, biomeOriginX, biomeOriginX + biomeW, biomeOriginY, biomeOriginY + biomeH, rng, decorationEntities);
        Tools::scatterDecorations(ctx->scene->world, secondSrc, count2, biomeOriginX, biomeOriginX + biomeW, biomeOriginY, biomeOriginY + biomeH, rng, decorationEntities);
    };
 
    for (const Biome& biome : biomes) {
        switch (biome.id) {
            case 0: scatterForBiome(biome, flower, grass, 75, 100); break;
            case 1: scatterForBiome(biome, catcus, emptyTree, 75, 100); break;
            case 2: scatterForBiome(biome, iceCrystal, snowyEmptyTree, 75, 100); break;
        }
    }
}
 
void GameScript::onStart() {
    if (!(ctx && ctx->scene)) return;
    elapsed = 0.0f;
    ctx->camera.zoom = 1.45f;
 
    ySortEnabled = true;
    ySortAnchor  = Tools::YSortAnchor::Bottom;
 
    player     = findEntityByName(ctx->scene, "Player");
    background = findEntityByName(ctx->scene, "Background");
    if (player == (Entity)-1 || background == (Entity)-1) return;

    healthbar = Healthbar(100.0f, 100.0f, 12.0f); 
 
    ctx->scene->world.add_texture_ref(background);
    ctx->scene->world.texture_ref_pool[background].resourceName = "grassBackground";
    ctx->scene->world.add_z_index(background);
    ctx->scene->world.z_index_pool[background].z = 0; 
 
    if (!ctx->scene->world.has_physics_body[player]) {
        ctx->scene->world.add_physics_body(player);
    }
    auto& playerPhys = ctx->scene->world.physics_body_pool[player];
    playerPhys.bodyType  = b2_dynamicBody;
    playerPhys.bodyId    = b2_nullBodyId;
    playerPhys.density   = 1.0f;
    playerPhys.isSensor  = false;
    playerPhys.category  = Physics::LAYER_1;
    playerPhys.mask      = Physics::LAYER_ALL;
 
    if (player != (Entity)-1) {
        ctx->scene->world.add_z_index(player);
        ctx->scene->world.z_index_pool[player].z = 1;
    }
 
    buildBiomeGrid();
    rebuildTileMap();
 
    flower = findEntityByName(ctx->scene, "flower"); grass  = findEntityByName(ctx->scene, "grass");
    catcus = findEntityByName(ctx->scene, "catcus"); emptyTree = findEntityByName(ctx->scene, "emptyTree");
    iceCrystal = findEntityByName(ctx->scene, "iceCrystal"); snowyEmptyTree = findEntityByName(ctx->scene, "snowyEmptyTree");
 
    auto setZ = [&](Entity e, int z) {
        if (e != (Entity)-1) {
            if (!ctx->scene->world.has_z_index[e]) ctx->scene->world.add_z_index(e);
            ctx->scene->world.z_index_pool[e].z = z;
        }
    };
    setZ(flower, 1); setZ(grass, 1); setZ(catcus, 1);
    setZ(emptyTree, 1); setZ(iceCrystal, 1); setZ(snowyEmptyTree, 1);
 
    if (player != (Entity)-1) {
        int midCol = biomeGridW / 2, midRow = biomeGridH / 2;
        for (const Biome& b : biomes) {
            if (b.gridX == midCol && b.gridY == midRow) {
                float originX = ctx->scene->world.position_pool[background].x;
                float originY = ctx->scene->world.position_pool[background].y;
                float biomeOriginX = originX + b.gridX * biomeTileCountX * tileMap.tileWidth;
                float biomeOriginY = originY + b.gridY * biomeTileCountY * tileMap.tileHeight;
                float pw = ctx->scene->world.rectangle_shape_pool[player].w;
                float ph = ctx->scene->world.rectangle_shape_pool[player].h;
                ctx->scene->world.position_pool[player].x = biomeOriginX + (biomeTileCountX * tileMap.tileWidth - pw) * 0.5f;
                ctx->scene->world.position_pool[player].y = biomeOriginY + (biomeTileCountY * tileMap.tileHeight - ph) * 0.5f;
                break;
            }
        }
    }
 
    // --- NEW: Binary .world file loading/saving ---
    std::string worldFilePath = ctx->sceneFilePath;
    size_t dot = worldFilePath.rfind('.');
    if (dot != std::string::npos) {
        worldFilePath = worldFilePath.substr(0, dot) + ".world";
    } else {
        worldFilePath += ".world";
    }
 
    // Try to load previously generated world data
    bool worldLoaded = WorldBinary::load(worldFilePath, ctx->scene->world, decorationEntities, 
                                         ctx->scene->areBiomesAndEntitiesGenerated, ctx->scene->projectRoot);
 
    // If it doesn't exist, or we are forcing regeneration, generate and save it
    if (!worldLoaded || !shouldStopRegeneration) {
        spawnRandomDecorations();
        Tools::cleanupOutOfBoundsDecorations(ctx->scene->world, decorationEntities,
            ctx->camera.boundsMinX, ctx->camera.boundsMaxX,
            ctx->camera.boundsMinY, ctx->camera.boundsMaxY);
        
        if (shouldStopRegeneration) {
            ctx->scene->areBiomesAndEntitiesGenerated = true;
        }
        
        // Save the newly generated world immediately
        WorldBinary::save(worldFilePath, ctx->scene->world, decorationEntities, 
                          ctx->scene->areBiomesAndEntitiesGenerated, ctx->scene->projectRoot);
    }
 
    // Configure physics for all decoration entities (whether loaded from disk or just generated)
    for (Entity e : decorationEntities) {
        if (ctx->scene->world.has_physics_body[e]) {
            auto& phys = ctx->scene->world.physics_body_pool[e];
            phys.bodyType = b2_staticBody;
            phys.category = Physics::LAYER_2;
            phys.mask = Physics::LAYER_1;
            phys.isSensor = false;
        }
    }
 
    if (ctx->physicsWorld) {
        physics_sync_system(ctx->scene->world, *ctx->physicsWorld);
        b2Body_SetFixedRotation(ctx->scene->world.physics_body_pool[player].bodyId, true);
        b2Body_SetLinearDamping(ctx->scene->world.physics_body_pool[player].bodyId, 8.0f);
    }
 
    int winW, winH;
    SDL_GetWindowSize(ctx->window, &winW, &winH);
    ctx->camera.setupForCanvas(0.0f, 0.0f, (float)winW, (float)winH);
    if (player != (Entity)-1) {
        float w = ctx->scene->world.has_rectangle_shape[player] ? ctx->scene->world.rectangle_shape_pool[player].w : 50;
        float h = ctx->scene->world.has_rectangle_shape[player] ? ctx->scene->world.rectangle_shape_pool[player].h : 50;
        ctx->camera.centerOnEntity(ctx->scene->world.position_pool[player], w, h);
        ctx->camera.clampToBounds(winW, winH);
    }
}


void GameScript::checkCollisions(float dt) {
    if (!ctx || !ctx->scene || player == (Entity)-1) return;
    if (!ctx->scene->world.has_physics_body[player]) return;
 
    auto& playerPhys = ctx->scene->world.physics_body_pool[player];
    if (!b2Body_IsValid(playerPhys.bodyId)) return;
 
    if (knockbackTimer > 0.0f || deathState) {
        return;
    }
 
    b2BodyId playerBodyId = playerPhys.bodyId;
    
    // --- Box2D v3.0 Data-Oriented Contact API ---
    int capacity = b2Body_GetContactCapacity(playerBodyId);
    if (capacity > 0) {
        std::vector<b2ContactData> contactData(capacity);
        int count = b2Body_GetContactData(playerBodyId, contactData.data(), capacity);
 
        for (int i = 0; i < count; ++i) {
            b2ShapeId shapeA = contactData[i].shapeIdA;
            b2ShapeId shapeB = contactData[i].shapeIdB;
 
            b2BodyId bodyA = b2Shape_GetBody(shapeA);
            b2BodyId bodyB = b2Shape_GetBody(shapeB);
 
            // Safely compare b2BodyId structs to find the "other" body
            bool isPlayerA = (bodyA.index1 == playerBodyId.index1 && 
                              bodyA.generation == playerBodyId.generation);
            b2BodyId otherBodyId = isPlayerA ? bodyB : bodyA;
 
            // Map the Box2D body back to an ECS Entity
            for (Entity e : decorationEntities) {
                if (ctx->scene->world.has_physics_body[e]) {
                    auto& phys = ctx->scene->world.physics_body_pool[e];
                    if (b2Body_IsValid(phys.bodyId) && 
                        phys.bodyId.index1 == otherBodyId.index1 && 
                        phys.bodyId.generation == otherBodyId.generation) {
                         // Check if this specific entity is a cactus using your custom Tools::contains
                        if (ctx->scene->world.has_metadata[e]) {
                            if (Tools::contains(ctx->scene->world.metadata_pool[e].name, "catcus")) {
                                // 1. Reduce HP
                                if (healthbar.getCurrentVal() > 0) {
                                    healthbar.damage(10.0f);
                                    ProjectScript_MIXER_PlaySound("punch", 0.7, 1.0);    
 
                                    b2Vec2 normal = contactData[i].manifold.normal;
                                    b2Vec2 bounceDir = isPlayerA ? b2Vec2{-normal.x, -normal.y} : normal;
    
                                    // 3. Apply velocity ONCE
                                    constexpr float ppm = Physics::PIXELS_PER_METER;
                                    b2Body_SetLinearVelocity(
                                        playerBodyId, 
                                        b2Vec2{ bounceDir.x * knockbackSpeed / ppm, bounceDir.y * knockbackSpeed / ppm }
                                    );
    
                                    // 4. Set timer to ignore input and prevent re-triggering
                                    knockbackTimer = 0.25f; // Slides for ~0.25 seconds
    
                                    // extra effects
                                    SDL_RumbleGamepad(ctx->gamepad, 0xC000, 0xFFFF, 200);

                                } else {

                                    ctx->scene->world.animation_state_pool[player].play("death", true, false);
                                    deathState = true;

                                }
                                return; // Exit after first trigger to avoid multiple HP drains/bounces per frame
                            }
                        }
                        break; // Found the entity, no need to keep searching decorations
                    }
                }
            }
        }
    }
}
 
void GameScript::onUpdate(float dt) {
    if (!(ctx && ctx->scene) || player == (Entity)-1) return;
    if (deathState) return;
 
    // Detect cactus contact and, if not already sliding from a previous hit,
    // apply the knockback velocity + start the knockback timer.
    checkCollisions(dt);
 
    float dx = 0.0f, dy = 0.0f;
 
    // --- KNOCKBACK STATE ---
    if (knockbackTimer > 0.0f) {
        knockbackTimer -= dt; // Count down the timer
        
        // Player input is ignored, and velocity is NOT overwritten below.
        // The b2Body_SetLinearDamping(8.0f) set in onStart() will naturally 
        // and smoothly slide the player to a halt over this duration.
    } 
    // --- NORMAL MOVEMENT STATE ---
    else {
        const bool* keys = SDL_GetKeyboardState(nullptr);
        if (keys[SDL_SCANCODE_A]) dx -= 1.0f;
        if (keys[SDL_SCANCODE_D]) dx += 1.0f;
        if (keys[SDL_SCANCODE_W]) dy -= 1.0f;
        if (keys[SDL_SCANCODE_S]) dy += 1.0f;
        
        if (ctx->gamepad) {
            const float stickDeadzone = 0.2f;
            float gx = SDL_GetGamepadAxis(ctx->gamepad, SDL_GAMEPAD_AXIS_LEFTX) / 32767.0f;
            float gy = SDL_GetGamepadAxis(ctx->gamepad, SDL_GAMEPAD_AXIS_LEFTY) / 32767.0f;
            if (std::fabs(gx) > stickDeadzone) dx += gx;
            if (std::fabs(gy) > stickDeadzone) dy += gy;
        }
 
        // Normalize so diagonal movement isn't faster
        if (dx != 0.0f || dy != 0.0f) {
            float len = std::sqrt(dx * dx + dy * dy);
            if (len > 1.0f) { dx /= len; dy /= len; }
        }
    }
 
    // --- Animation State Update ---
    if (ctx->scene->world.has_animation_state[player]) {
        auto& anim = ctx->scene->world.animation_state_pool[player];
        // Only change animation if we are NOT currently knocking back
        if (knockbackTimer <= 0.0f) {
            if (dx != 0.0f || dy != 0.0f) {
                if (anim.activeName() != "walk") {
                    anim.play("walk", true);
                }
            } else {
                if (anim.activeName() != "idle") {
                    anim.play("idle", true);
                }
            }
        }
    }
 
    // --- Physics Velocity Application ---
    if (ctx->physicsWorld &&
        ctx->scene->world.has_physics_body[player] &&
        b2Body_IsValid(ctx->scene->world.physics_body_pool[player].bodyId))
    {
        // ONLY overwrite velocity if we are NOT in a knockback state
        if (knockbackTimer <= 0.0f) {
            constexpr float ppm = Physics::PIXELS_PER_METER;
            b2Body_SetLinearVelocity(
                ctx->scene->world.physics_body_pool[player].bodyId,
                b2Vec2{ dx * moveSpeed / ppm, dy * moveSpeed / ppm });
        }
        // If knockbackTimer > 0.0f, we do NOTHING here. 
        // The velocity set in checkCollisions is preserved, and the physics 
        // solver's damping will smoothly slide the player to a halt.
    }
 
    // Camera follow
    ctx->camera.centerOnEntity(ctx->scene->world.position_pool[player],
        ctx->scene->world.rectangle_shape_pool[player].w,
        ctx->scene->world.rectangle_shape_pool[player].h);
    
    int winW, winH;
    SDL_GetWindowSize(ctx->window, &winW, &winH);
    ctx->camera.setupForCanvas(0.0f, 0.0f, (float)winW, (float)winH);
    ctx->camera.clampToBounds(winW, winH);
}
 



void GameScript::onDraw() {
}
 
void GameScript::onEnd() {
    if (!(ctx && ctx->scene)) return;
 
    // Derive the .world file path from the scene file path
    std::string worldFilePath = ctx->sceneFilePath;
    size_t dot = worldFilePath.rfind('.');
    if (dot != std::string::npos) {
        worldFilePath = worldFilePath.substr(0, dot) + ".world";
    } else {
        worldFilePath += ".world";
    }
 
    // Save the generated entities to the fast binary .world file
    // (We no longer call saveToFile on the scene JSON, keeping it clean)
    WorldBinary::save(worldFilePath, ctx->scene->world, decorationEntities, 
                      ctx->scene->areBiomesAndEntitiesGenerated, ctx->scene->projectRoot);
}
 
// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main(int argc, char *argv[])
{
    const float windowWidth = 1600.0f;
    const float windowHeight = 900.0f;
 
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMEPAD)) {
        std::cerr << "SDL_Init failed:" << SDL_GetError() << "\n";
        return -1;
    }
    if (!TTF_Init()) {
        std::cerr << "TTF_Init failed: " << SDL_GetError() << "\n";
        SDL_Quit();
        return -1;
    }
    if (!MIX_Init()) {
        std::cerr << "MIX_Init failed: " << SDL_GetError() << "\n";
        SDL_Quit();
        return -1;
    }
 
    SDL_Window *window = SDL_CreateWindow("Open World", windowWidth, windowHeight,
                                          SDL_WINDOW_RESIZABLE);
    SDL_Renderer *renderer = SDL_CreateRenderer(window, nullptr);
    if (!window || !renderer) {
        std::cerr << "Window/Renderer creation failed\n";
        TTF_Quit();
        SDL_Quit();
        return -1;
    }
    if (!SDL_SetRenderVSync(renderer, 1)) {
        std::cerr << "[Warning] SDL_SetRenderVSync failed: " << SDL_GetError() << "\n";
    }
 
    g_resources.TextureManager.SetRenderer(renderer);
    TTF_TextEngine *textEngine = TTF_CreateRendererTextEngine(renderer);
    ProjectScript_TTF_LoadFont("gameFont", "OpenWorld/assets/fonts/fredoka.ttf", 30);
    TTF_Font *gameFont = ProjectScript_TTF_GetFont("gameFont");
 
    const std::string sceneFilePath = "OpenWorld/scenes/world1.json";
    SceneParser parser(renderer, textEngine, gameFont, window);
    Scene scene = parser.ProjectScript_loadFromFile(sceneFilePath);
    if (!scene.scriptValid)
        std::cerr << "[Warning] Scene script validation failed - running anyway.\n";
 
    // ---- Load ALL biome textures ----
    // The background entity uses "grassBackground" by default, but we load
    // all biome textures so the tile system can reference them by name.
    ProjectScript_IMG_LoadTexture("grassBackground",  "OpenWorld/assets/textures/grassBackground.svg");
    ProjectScript_IMG_LoadTexture("snowBackground",   "OpenWorld/assets/textures/snowBackground.svg");
    ProjectScript_IMG_LoadTexture("desertBackground", "OpenWorld/assets/textures/dessertBackground.svg");
    g_resources.AudioManager.CreateMixerDevice();
    ProjectScript_MIXER_LoadSound("punch", "OpenWorld/assets/audio/Punch.wav", false);
 
    GameContext ctx;
    ctx.renderer      = renderer;
    ctx.textEngine    = textEngine;
    ctx.window        = window;
    ctx.scene         = &scene;
    ctx.sceneParser   = &parser;
    ctx.sceneFilePath = sceneFilePath;
 
    {
        int numGamepads = 0;
        SDL_JoystickID* gamepadIDs = SDL_GetGamepads(&numGamepads);
        if (gamepadIDs && numGamepads > 0)
            ctx.gamepad = SDL_OpenGamepad(gamepadIDs[0]);
        SDL_free(gamepadIDs);
    }
 
    GameScript script;
    script.ctx = &ctx;
    g_renderer  = renderer;
    g_window    = window;
    g_textEngine = textEngine;
    g_script    = &script;
    g_context   = &ctx;
    g_lastTime  = SDL_GetTicks();
 
    ctx.physicsWorld = new Physics::PhysicsWorld(0.0f);
 
    script.onStart();
 
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
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) running = false;
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) running = false;
            if (e.type == SDL_EVENT_GAMEPAD_ADDED && !ctx.gamepad)
                ctx.gamepad = SDL_OpenGamepad(e.gdevice.which);
            else if (e.type == SDL_EVENT_GAMEPAD_REMOVED && ctx.gamepad &&
                     SDL_GetGamepadID(ctx.gamepad) == e.gdevice.which) {
                SDL_CloseGamepad(ctx.gamepad);
                ctx.gamepad = nullptr;
            }
        }
 
        script.onUpdate(dt);
        StepPhysicsAndSync(ctx, script, dt);
        animation_system(ctx.scene->world, dt);
 
        RenderFrame(renderer, window, ctx, script);
    }
 
#endif
 
    script.onEnd();
    if (ctx.gamepad) SDL_CloseGamepad(ctx.gamepad);
    if (textEngine) TTF_DestroyRendererTextEngine(textEngine);
    ProjectScript_TTF_Clear();
    ProjectScript_IMG_Clear();
    if (ctx.physicsWorld) { delete ctx.physicsWorld; ctx.physicsWorld = nullptr; }
    TTF_Quit();
    MIX_Quit();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    return 0;
}