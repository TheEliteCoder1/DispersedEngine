#include "game.h"
#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif
// Registers GameScript with the engine's ScriptRegistry under the key
// "GameScript", matching the stem of whatever script_attached path scene
// JSON files use (e.g. "OpenWorld/scripts/GameScript.cpp"). This is what
// lets SceneParser::loadFromFile()/ProjectScript_loadFromFile() and
// change_scene() instantiate a GameScript polymorphically through nothing
// but a ScriptBase* — no code in engine.h needs to know GameScript exists.
REGISTER_SCRIPT(GameScript, "game");



static SDL_Renderer *g_renderer = nullptr;



static SDL_Window *g_window = nullptr;



TTF_TextEngine *g_textEngine = nullptr;



static GameContext *g_context = nullptr;



static bool g_running = true;



static Uint64 g_lastTime = 0;



static constexpr float dtStep = 1000.0f;



static constexpr float minDt = 0.1f;



static int reason = 0;

SDL_FRect GamepadCursorIconSrcRect =  {0.0f, 0.0f, 32.0f, 32.0f};
SDL_FRect GamepadCursorIconDestRect = {0.0f, 0.0f, 1600.0f, 900.0f};

// The active scene's script is polymorphic (ScriptBase*, owned by
// GameContext::scene->script) so that different scenes can run entirely
// different ScriptBase subclasses. Generic dispatch (onStart/onUpdate/
// onDraw/onEnd) goes straight through that pointer. The rendering/physics
// helpers below additionally need GameScript-specific data (player,
// tileMap, minimap, ySort settings, ...) that isn't part of ScriptBase,
// so they recover it with a dynamic_cast and simply skip that extra
// behavior when the active scene's script isn't a GameScript at all
// (e.g. a menu or losing-screen scene driven by some other script type).
static GameScript* ActiveGameScript(GameContext& ctx) {
    if (!ctx.scene || !ctx.scene->script) return nullptr;
    return dynamic_cast<GameScript*>(ctx.scene->script.get());
}

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
// bounds if the physics step let it drift out. The player-clamp step is
// GameScript-specific (it needs a `player` entity handle), so it's skipped
// entirely when the active scene isn't running a GameScript.
static void StepPhysicsAndSync(GameContext& ctx, float dt) {
    if (!ctx.physicsWorld) return;



 
    // Fixed 60 Hz step keeps the simulation stable regardless of frame rate
    ctx.physicsWorld->Step(1.0f / 60.0f, 4);



    movement_system(ctx.scene->world, dt);



 
    GameScript* script = ActiveGameScript(ctx);



    if (!script) return;




    // Keep the player inside the world bounds by teleporting the body back
    // if movement_system let it drift past the camera bounds.
    if (script->player != (Entity)-1 &&
        ctx.scene->world.has_physics_body[script->player] &&
        b2Body_IsValid(ctx.scene->world.physics_body_pool[script->player].bodyId) &&
        ctx.camera.boundsEnabled)
    {
        auto& pos = ctx.scene->world.position_pool[script->player];



        float w = ctx.scene->world.has_rectangle_shape[script->player]
                ? ctx.scene->world.rectangle_shape_pool[script->player].w : 50.0f;



        float h = ctx.scene->world.has_rectangle_shape[script->player]
                ? ctx.scene->world.rectangle_shape_pool[script->player].h : 50.0f;



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
            b2BodyId bid = ctx.scene->world.physics_body_pool[script->player].bodyId;



            b2Body_SetTransform(bid,
                Physics::PxToM(b2Vec2{ pos.x + w * 0.5f, pos.y + h * 0.5f }),
                b2Body_GetRotation(bid));



            b2Body_SetLinearVelocity(bid, b2Vec2_zero);



        }
    }
}
 
// Clears the screen, renders every visible entity (Y-sorted, if a
// GameScript is active), calls the active script's onDraw() polymorphically,
// then layers on GameScript-specific extras (tilemap, physics debug,
// minimap, healthbar) when the active scene's script actually is one.
static void RenderFrame(SDL_Renderer* renderer, SDL_Window* window, GameContext& ctx) {
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);



    SDL_SetRenderDrawColor(renderer, 10, 10, 20, 255);



    SDL_RenderClear(renderer);



 
    int winW, winH;



    SDL_GetWindowSize(ctx.window, &winW, &winH);



 
    // 1. Compute visible world rect for culling
    SDL_FRect visibleWorld = Tools::computeVisibleWorldRect(ctx.camera, winW, winH);




    GameScript* script = ActiveGameScript(ctx);




    // 2. Render the tiled biome backgrounds (GameScript-specific)
    if (script) {
        script->bgMap.render(renderer, ctx.camera, visibleWorld);



    }
 
    // 3. Build render order. If a GameScript is active, exclude the main
    //    background entity (handled by bgMap above) and use its Y-sort
    //    settings; player and decorations share the same z tier (see
    //    GameScript::onStart), so this Y-sort actually determines who
    //    draws in front of whom instead of z_index deciding it outright.
    //    Scenes with no GameScript (e.g. a menu) just get sensible
    //    defaults with nothing excluded.
    std::vector<Entity> exclude;



    bool ySortEnabled = true;



    Tools::YSortAnchor ySortAnchor = Tools::YSortAnchor::Bottom;



    if (script) {
        exclude = { script->background, script->puddle };



        ySortEnabled = script->ySortEnabled;



        ySortAnchor = script->ySortAnchor;



    }
    std::vector<Entity> renderOrder = Tools::buildVisibleRenderOrder(
        ctx.scene->world, visibleWorld, exclude,
        ySortEnabled, ySortAnchor);



 
    // 4. Render visible entities
    for (Entity i : renderOrder) {
        // Entities mid-vanish are drawn entirely by GameScript::onDraw()
        // via vanishEffects.render() (fading sprite strips + black particle
        // trail) instead of their normal sprite, so the two don't overlap.
        if (script && script->vanishEffects.isActive(i)) continue;

        
        SDL_FPoint screenPos = ctx.camera.worldToScreen(
            ctx.scene->world.position_pool[i].x,
            ctx.scene->world.position_pool[i].y);


        if (ctx.scene->world.has_animation_state[i])
            render_entity_animation(renderer, ctx.scene->world, i, screenPos.x, screenPos.y, ctx.camera.zoom);



        else if (ctx.scene->world.has_texture_ref[i])
            render_entity_texture(renderer, ctx.scene->world, i, screenPos.x, screenPos.y, ctx.camera.zoom);

        
        if (ctx.scene->world.has_tilemap[i]) {
            render_entity_tilemap(renderer, ctx.scene->world, i, screenPos.x, screenPos.y, ctx.camera.zoom);
        
        }

    }
 
    // Polymorphic dispatch: whatever ScriptBase subclass is actually
    // driving this scene gets to draw, regardless of concrete type.
    if (ctx.scene->script) {
        ctx.scene->script->onDraw();



    }

    for (auto& elem : ctx.scene->guiElements) {
        elem->render(0.0f, 0.0f);



    }
 
    if (script && script->drawPhysicsDebug) {
        draw_physics_debug_overlay(renderer, ctx.scene->world, ctx.camera);



    }
 
    // 5. Render the minimap/healthbar (GameScript-specific; minimap already
    //    guards internally on player == (Entity)-1).
    if (script) {

        script->minimap.render(renderer, ctx.scene->world, script->player, script->importantEntities, winW, winH);




        // Player-anchored healthbar. Guarded because change_scene() can swap
        // in a scene (e.g. the losing screen) with no player entity at all,
        // in which case script->player has been reset to (Entity)-1, or
        // with an entirely different script type, in which case `script`
        // itself is null and this whole block is skipped.
        if (script->player != (Entity)-1 && ctx.scene->world.has_position[script->player]) {
            SDL_FPoint playerScreen = ctx.camera.worldToScreen(
                ctx.scene->world.position_pool[script->player].x,
                ctx.scene->world.position_pool[script->player].y
            );




            script->healthbar.render(renderer,
                playerScreen.x,
                playerScreen.y + healthbarOffsetY
            );



        }
    }

    
    if (ctx.gamepad && !ActiveGameScript(ctx)) {
        // gamepad cursor rendering
        GamepadCursorIconSrcRect.x = ctx.gamepadCursorX;
        GamepadCursorIconSrcRect.y = ctx.gamepadCursorY;
        GamepadCursorIconDestRect.w = winW;
        GamepadCursorIconDestRect.h = winH;
        SDL_RenderTexture(renderer, ProjectScript_IMG_GetTexture("GamePadCursorIcon"), &GamepadCursorIconSrcRect, &GamepadCursorIconDestRect);
    }

    

    SDL_RenderPresent(renderer);



}
 
#ifdef __EMSCRIPTEN__
void main_loop_callback() {
    if (!g_running) { emscripten_cancel_main_loop();


 return;


 }
    double now = emscripten_get_now();



    float dt = (float)(now - g_lastTime) / dtStep;



    g_lastTime = (Uint64)now;



    if (dt > minDt) dt = minDt;



 
    SDL_Event e;



    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_EVENT_QUIT) g_running = false;



        if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) g_running = false;



        if (g_context->scene && g_context->scene->script) {
            g_context->scene->script->onEvent(e);



        }
        for (auto& elem : g_context->scene->guiElements) {
            elem->handleEvent(e, g_window, 0.0f, 0.0f);



        }
        if (e.type == SDL_EVENT_GAMEPAD_ADDED && !g_context->gamepad)
            g_context->gamepad = SDL_OpenGamepad(e.gdevice.which);



        else if (e.type == SDL_EVENT_GAMEPAD_REMOVED && g_context->gamepad &&
                 SDL_GetGamepadID(g_context->gamepad) == e.gdevice.which) {
            SDL_CloseGamepad(g_context->gamepad);



            g_context->gamepad = nullptr;



        }
    }
 
    // Polymorphic dispatch: onUpdate() runs on whatever ScriptBase subclass
    // is attached to the *currently active* scene, which may have changed
    // (via change_scene()) since last frame.
    if (g_context->scene->script) {
        g_context->scene->script->onUpdate(dt);



    }
 
    // Same physics step + ECS sync the native loop uses — this was
    // previously missing here, which meant the player's position_pool
    // never advanced on web at all once the direct-position fallback
    // was removed from onUpdate().
    StepPhysicsAndSync(*g_context, dt);



 
    animation_system(g_context->scene->world, dt);



    if (g_context->gamepad) {
        GameScript* gs = ActiveGameScript(*g_context);



        if (!gs) {
            float deadzone = 0.2f;



            float ax = SDL_GetGamepadAxis(g_context->gamepad, SDL_GAMEPAD_AXIS_LEFTX) / 32767.0f;



            float ay = SDL_GetGamepadAxis(g_context->gamepad, SDL_GAMEPAD_AXIS_LEFTY) / 32767.0f;



            if (std::fabs(ax) < deadzone) ax = 0.0f;



            if (std::fabs(ay) < deadzone) ay = 0.0f;




            const float cursorSpeed = 500.0f;



            g_context->gamepadCursorX += ax * cursorSpeed * dt;



            g_context->gamepadCursorY += ay * cursorSpeed * dt;




            int winW, winH;



            SDL_GetWindowSize(g_window, &winW, &winH);



            g_context->gamepadCursorX = std::clamp(g_context->gamepadCursorX, 0.0f, (float)winW);



            g_context->gamepadCursorY = std::clamp(g_context->gamepadCursorY, 0.0f, (float)winH);




            bool confirmDown = SDL_GetGamepadButton(g_context->gamepad, SDL_GAMEPAD_BUTTON_SOUTH) != 0;



            for (auto& elem : g_context->scene->guiElements) {
                elem->handleGamepad(g_context->gamepadCursorX, g_context->gamepadCursorY,
                                    0.0f, 0.0f, g_window,
                                    confirmDown, g_context->confirmDownLastFrame);



            }
            g_context->confirmDownLastFrame = confirmDown;



        } else {
            g_context->confirmDownLastFrame = false;



        };


    } else {
        g_context->confirmDownLastFrame = false;



    };


    RenderFrame(g_renderer, g_window, *g_context);



}
#endif
 
void GameScript::buildBiomeGrid() {
    biomes.clear();



    auto addBiome = [&](const std::string& tex, int gx, int gy, int id) {
        biomes.push_back({tex, gx, gy, id});
    };



    addBiome("snowBackground",   0, 0, 2);


     addBiome("snowBackground",   1, 0, 2);


     addBiome("snowBackground",   2, 0, 2);



    addBiome("grassBackground",  0, 1, 0);


     addBiome("snowBackground",   1, 1, 2);


     addBiome("grassBackground",  2, 1, 0);



    addBiome("grassBackground",  0, 2, 0);


     addBiome("grassBackground",  1, 2, 0);


     addBiome("grassBackground",  2, 2, 0);



    addBiome("grassBackground",  0, 3, 0);


     addBiome("desertBackground", 1, 3, 1);


     addBiome("grassBackground",  2, 3, 0);
    


    addBiome("desertBackground", 0, 4, 1);


     addBiome("desertBackground", 1, 4, 1);


     addBiome("desertBackground", 2, 4, 1);



}
 
void GameScript::rebuildBgMap() {
    if (!(ctx && ctx->scene) || background == (Entity)-1) return;



    if (!ctx->scene->world.has_rectangle_shape[background] || !ctx->scene->world.has_position[background]) return;



 
    bgMap.tileWidth  = ctx->scene->world.rectangle_shape_pool[background].w;



    bgMap.tileHeight = ctx->scene->world.rectangle_shape_pool[background].h;



 
    float originX = ctx->scene->world.position_pool[background].x;



    float originY = ctx->scene->world.position_pool[background].y;



    bgMap.clear();



 
    for (const Biome& biome : biomes) {
        float biomeOriginX = originX + biome.gridX * biomeTileCountX * bgMap.tileWidth;



        float biomeOriginY = originY + biome.gridY * biomeTileCountY * bgMap.tileHeight;



        bgMap.addRegion(biomeOriginX, biomeOriginY, biomeTileCountX * bgMap.tileWidth, biomeTileCountY * bgMap.tileHeight, biome.textureName);



    }
 
    float totalW = biomeGridW * biomeTileCountX * bgMap.tileWidth;



    float totalH = biomeGridH * biomeTileCountY * bgMap.tileHeight;



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
        if (b.textureName.find("grass") != std::string::npos)       { mb.colorG = 140;


 }
        else if (b.textureName.find("snow") != std::string::npos)  { mb.colorR = 200;


 mb.colorG = 200;


 mb.colorB = 220;


 }
        else if (b.textureName.find("desert") != std::string::npos){ mb.colorR = 200;


 mb.colorG = 170;


 mb.colorB = 80;


 }
        miniBiomes.push_back(mb);



    }
    minimap.setBiomeGrid(miniBiomes, bgMap.tileWidth, bgMap.tileHeight, biomeTileCountX, biomeTileCountY);



}
 
void GameScript::spawnRandomDecorations() {
    if (!(ctx && ctx->scene)) return;



 
    static std::mt19937 rng(std::random_device{}());



    decorationEntities.clear();



 
    auto scatterForBiome = [&](const Biome& biome, Entity firstSrc, Entity secondSrc, int count1, int count2) {
        float biomeOriginX = ctx->camera.boundsMinX + biome.gridX * biomeTileCountX * bgMap.tileWidth;



        float biomeOriginY = ctx->camera.boundsMinY + biome.gridY * biomeTileCountY * bgMap.tileHeight;



        float biomeW = biomeTileCountX * bgMap.tileWidth;



        float biomeH = biomeTileCountY * bgMap.tileHeight;



 
        Tools::scatterDecorations(ctx->scene->world, firstSrc, count1, biomeOriginX, biomeOriginX + biomeW, biomeOriginY, biomeOriginY + biomeH, rng, decorationEntities);



        Tools::scatterDecorations(ctx->scene->world, secondSrc, count2, biomeOriginX, biomeOriginX + biomeW, biomeOriginY, biomeOriginY + biomeH, rng, decorationEntities);



    };




    auto scatterForBiomeExtraSources = [&](const Biome& biome, const std::vector<Entity>& sources, const std::vector<int>& counts) {
        float biomeOriginX = ctx->camera.boundsMinX + biome.gridX * biomeTileCountX * bgMap.tileWidth;



        float biomeOriginY = ctx->camera.boundsMinY + biome.gridY * biomeTileCountY * bgMap.tileHeight;



        float biomeW = biomeTileCountX * bgMap.tileWidth;



        float biomeH = biomeTileCountY * bgMap.tileHeight;




        for (size_t i = 0; i < sources.size(); i++) {
            Tools::scatterDecorations(ctx->scene->world, sources[i], counts[i], biomeOriginX, biomeOriginX + biomeW, biomeOriginY, biomeOriginY + biomeH, rng, decorationEntities);



        }
    };



 
    for (const Biome& biome : biomes) {
        switch (biome.id) {
            case 0: {
                // use scope brackets for creating vectors during runtime
                std::vector<Entity> sources = {flower, grass, fruitTree, puddle};



                std::vector<int> counts = {75, 100, 100, 100};



                scatterForBiomeExtraSources(biome, sources, counts);



                break;



            };


            case 1:  {
                std::vector<Entity> sources = {catcus, emptyTree, fruitTree, puddle};



                std::vector<int> counts = {75, 100, 50, 100};



                scatterForBiomeExtraSources(biome, sources, counts);



                break;



            };


            case 2: {
                std::vector<Entity> sources = {iceCrystal, snowyEmptyTree, fruitTree, puddle};



                std::vector<int> counts = {75, 100, 50, 100};



                scatterForBiomeExtraSources(biome, sources, counts);



                break;



            };


        }
    }

    // populate important entities after generation
    // importantEntities.clear();
    // for (Entity e : decorationEntities)
    // {
    //     if (ctx->scene->world.has_metadata[e])
    //     {
    //         if (Tools::contains(ctx->scene->world.metadata_pool[e].name, "puddle"))
    //         {
    //             importantEntities.push_back(e);
    //         }
    //     }
    // }
}

struct UserProgress {
    int checkpoints_completed = 0;



};




UserProgress userProgress;




void loadUserProgress(std::string filepath)
{
                
    std::ifstream file((getProjectsRootForScripts() / std::filesystem::path(filepath)).string());



    nlohmann::json j;



    file >> j;



    userProgress.checkpoints_completed = j.value("checkpoints_completed", 0);



}

void saveUserProgress(UserProgress& userProgress, const std::string& filepath) 
{
    nlohmann::json j;



    j["checkpoints_completed"] = userProgress.checkpoints_completed;



    std::ofstream outFile((getProjectsRootForScripts() / std::filesystem::path(filepath)).string());



    outFile << j.dump(4);



}
 
void GameScript::onStart() {
    if (!(ctx && ctx->scene)) return;



    deathState = false;



    elapsed = 0.0f;



    ctx->camera.zoom = 1.45f;



 
    ySortEnabled = true;



    ySortAnchor  = Tools::YSortAnchor::Bottom;




    loadUserProgress("OpenWorld/userdata/userdata.json");



 
    player = findEntityByName(ctx->scene, "Player");

    background = findEntityByName(ctx->scene, "Background");


    if (player == (Entity)-1 || background == (Entity)-1) return;

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



    rebuildBgMap();



 
    flower = findEntityByName(ctx->scene, "flower");


    grass  = findEntityByName(ctx->scene, "grass");



    catcus = findEntityByName(ctx->scene, "catcus");


    emptyTree = findEntityByName(ctx->scene, "emptyTree");



    iceCrystal = findEntityByName(ctx->scene, "iceCrystal");


    snowyEmptyTree = findEntityByName(ctx->scene, "snowyEmptyTree");



    puddle = findEntityByName(ctx->scene, "puddle");


    fruitTree = findEntityByName(ctx->scene, "fruitTree");

    path0deg = findEntityByName(ctx->scene, "path0deg");

    path90deg = findEntityByName(ctx->scene, "path90deg");


    auto setZ = [&](Entity e, int z) {
        if (e != (Entity)-1) {
            if (!ctx->scene->world.has_z_index[e]) ctx->scene->world.add_z_index(e);
                ctx->scene->world.z_index_pool[e].z = z;
        }
    };

    setZ(flower, 1);


    setZ(grass, 1);



    setZ(emptyTree, 1);


    setZ(iceCrystal, 1);


 
    setZ(snowyEmptyTree, 1);


    setZ(puddle, 0);

    setZ(path0deg, 0);

    setZ(path90deg, 0);

    setZ(catcus, 1);

    setZ(fruitTree, 1);

 
    switch (userProgress.checkpoints_completed)
    {
        case 0:
            ctx->scene->world.position_pool[player].x = 7470;



            ctx->scene->world.position_pool[player].y = 7435.5;



            break;



    }
    // set player to center of generated biome based map
    // if (player != (Entity)-1) {
    //     int midCol = biomeGridW / 2, midRow = biomeGridH / 2;
    //     for (const Biome& b : biomes) {
    //         if (b.gridX == midCol && b.gridY == midRow) {
    //             float originX = ctx->scene->world.position_pool[background].x;
    //             float originY = ctx->scene->world.position_pool[background].y;
    //             float biomeOriginX = originX + b.gridX * biomeTileCountX * tileMap.tileWidth;
    //             float biomeOriginY = originY + b.gridY * biomeTileCountY * tileMap.tileHeight;
    //             float pw = ctx->scene->world.rectangle_shape_pool[player].w;
    //             float ph = ctx->scene->world.rectangle_shape_pool[player].h;
    //             ctx->scene->world.position_pool[player].x = biomeOriginX + (biomeTileCountX * tileMap.tileWidth - pw) * 0.5f;
    //             ctx->scene->world.position_pool[player].y = biomeOriginY + (biomeTileCountY * tileMap.tileHeight - ph) * 0.5f;
    //             // SDL_Log("%f%f", ctx->scene->world.position_pool[player].x, ctx->scene->world.position_pool[player].y);
    //             break;
    //         }
    //     }
    // }
 
    // --- NEW: Binary .world file loading/saving ---
    std::string worldFilePath = ctx->sceneFilePath;



    size_t dot = worldFilePath.rfind('.');



    if (dot != std::string::npos) {
        worldFilePath = worldFilePath.substr(0, dot) + ".world";



    } else {
        worldFilePath += ".world";



    };


 
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




            // Puddles are sensors: they should trigger the slow effect
            // instead of physically blocking the player.
            bool isPuddle = ctx->scene->world.has_metadata[e] &&
                            Tools::contains(ctx->scene->world.metadata_pool[e].name, "puddle");



            phys.isSensor = isPuddle;



        }
    }
    
    if (ctx->physicsWorld) {
        physics_sync_system(ctx->scene->world, *ctx->physicsWorld, player);



        b2Body_SetFixedRotation(ctx->scene->world.physics_body_pool[player].bodyId, true);



        b2Body_SetLinearDamping(ctx->scene->world.physics_body_pool[player].bodyId, 8.0f);



    }

    waterbar = WaterBar(WaterNFoodMaxValue, WaterNFoodBarWidth, WaterNFoodBarHeight);



    isDrinking = false;



    drinkTimer = 0.0f;




    foodbar = FoodBar(WaterNFoodMaxValue, WaterNFoodBarWidth, WaterNFoodBarHeight);



    isEating = false;



    eatTimer = 0.0f;



    

    movementLocked = false;



    isMathDialogActive = false;




    // Create math dialog (will be opened later)
    mathDialog = std::make_unique<MathDialog>(
        ctx->renderer,
        ctx->textEngine,
        ProjectScript_TTF_GetFont("mathFont"),
        ctx->window,
        ctx->gamepad,
        [this](bool correct) { onMathAnswer(correct); }
    );



 
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



 
    if (knockbackTimer > 0.0f) {
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




            for (Entity e = 0; e < ctx->scene->world.entity_count; e++) {
                if (ctx->scene->world.has_physics_body[e]) {
                    auto& phys = ctx->scene->world.physics_body_pool[e];



                    if (b2Body_IsValid(phys.bodyId) && 
                        phys.bodyId.index1 == otherBodyId.index1 && 
                        phys.bodyId.generation == otherBodyId.generation) {
                         // Check if this specific entity is a cactus using your custom Tools::contains
                        if (ctx->scene->world.has_metadata[e]) {

                            if (Tools::contains(ctx->scene->world.metadata_pool[e].name, "catcus")) {
                                // 1. Reduce HP
                                healthbar.damage(10.0f);



                                ProjectScript_MIXER_PlaySound("punch", 0.7, 1.0);


    
                                ProjectScript_MIXER_PlaySound("pain");




                                b2Vec2 normal = contactData[i].manifold.normal;



                                b2Vec2 bounceDir = isPlayerA ? b2Vec2{-normal.x, -normal.y} : normal;




                                // 3. Apply velocity ONCE
                                constexpr float ppm = Physics::PIXELS_PER_METER;



                                b2Body_SetLinearVelocity(
                                    playerBodyId, 
                                    b2Vec2{ bounceDir.x * knockbackSpeed / ppm, bounceDir.y * knockbackSpeed / ppm }
                                );




                                // 4. Set timer to ignore input and prevent re-triggering
                                knockbackTimer = 0.25f;


 // Slides for ~0.25 seconds

                                // extra effects
                                SDL_RumbleGamepad(ctx->gamepad, 0xC000, 0xFFFF, 200);




                                if (healthbar.getCurrentVal() <= 0) {
                                    deathState = true;



                                    if (!vanishEffects.isActive(player)) {
                                        vanishEffects.start(player);



                                    }
                                }
                                return;


                            // Exit after first trigger to avoid multiple HP drains/bounces per frame

                            } else if (Tools::contains(ctx->scene->world.metadata_pool[e].name, "fruitTree")) {

                                if (foodbar.getPercentage() <= 0.3f && !isMathDialogActive) {
                                    reason = 1;



                                    showMathDialog();



                                    movementLocked = true;



                                    return;



                                }
                            }
                        }
                        break;


 // Found the entity, no need to keep searching decorations
                    }
                }
            }
        }
    }
}

void GameScript::checkContacts(float dt) {
    if (!ctx || !ctx->scene || player == (Entity)-1) return;



    if (!ctx->scene->world.has_physics_body[player]) return;




    // Get the Box2D world ID (adjust to your actual variable name)
    Physics::PhysicsWorld* pw = ctx->physicsWorld;



    b2WorldId worldId = pw->GetHandle();




    // Fetch all sensor events that happened since last call
    b2SensorEvents sensorEvents = b2World_GetSensorEvents(worldId);




    // Process begin events
    for (int i = 0; i < sensorEvents.beginCount; ++i) {
        b2SensorBeginTouchEvent* event = sensorEvents.beginEvents + i;



        handleSensorTouch(event->sensorShapeId, event->visitorShapeId, true);



    }

    // Process end events
    for (int i = 0; i < sensorEvents.endCount; ++i) {
        b2SensorEndTouchEvent* event = sensorEvents.endEvents + i;



        handleSensorTouch(event->sensorShapeId, event->visitorShapeId, false);



    }
}

// Helper method (can be a private lambda inside the function)
void GameScript::handleSensorTouch(b2ShapeId sensorShape, b2ShapeId visitorShape, bool isBegin) {

    Entity sensorEntity = (Entity)(intptr_t)b2Shape_GetUserData(sensorShape);



    Entity visitorEntity = (Entity)(intptr_t)b2Shape_GetUserData(visitorShape);





    if (sensorEntity == (Entity)-1)
        return;





    // Verify puddle
    if (!ctx->scene->world.has_metadata[sensorEntity])
        return;





    if (isBegin)
    {
        // Only react if the player entered
        if (visitorEntity != player)
            return;




        if (Tools::contains(ctx->scene->world.metadata_pool[sensorEntity].name,"puddle"))
        {
            if (puddleContactCount == 0)
            {
                moveSpeed = slowSpeed;



                isInPuddle = true;



                ProjectScript_MIXER_PlaySound("water", 1.0, 1.0);


    
            }
            puddleContactCount++;



        }
    }
    else
    {
        // Do not check visitorEntity here.
        // Box2D may report a different player shape on exit.
        if (Tools::contains(ctx->scene->world.metadata_pool[sensorEntity].name, "puddle")) {
            if (puddleContactCount > 0)
            {
                puddleContactCount--;




                if (puddleContactCount == 0)
                {
                    moveSpeed = normalSpeed;



                    isInPuddle = false;



                }
            }
        }
    };


}

WaterBar::WaterBar(float max, float width, float height)
    : maxValue(max), currentValue(max), barWidth(width), barHeight(height) {}

void WaterBar::damage(float amount) {
    currentValue = std::max(0.0f, currentValue - amount);



}

void WaterBar::heal(float amount) {
    currentValue = std::min(maxValue, currentValue + amount);



}

float WaterBar::getCurrentVal() const { return currentValue;


 };



float WaterBar::getPercentage() const { return currentValue / maxValue;


 };



void WaterBar::render(SDL_Renderer* renderer, float screenX, float screenY) {
    if (currentValue <= 0) return;



    SDL_FRect bgRect = { screenX, screenY, barWidth, barHeight };



    SDL_SetRenderDrawColor(renderer, 40, 40, 40, 255);



    SDL_RenderRect(renderer, &bgRect);



    float fillWidth = getPercentage() * barWidth;



    SDL_FRect fillRect = { screenX, screenY, fillWidth, barHeight };



    SDL_SetRenderDrawColor(renderer, 50, 150, 255, 255);


 // blue
    SDL_RenderFillRect(renderer, &fillRect);



}

void FoodBar::render(SDL_Renderer* renderer, float screenX, float screenY)
{
    if (currentValue <= 0) return;



    SDL_FRect bgRect = { screenX, screenY, barWidth, barHeight };



    SDL_SetRenderDrawColor(renderer, 40, 40, 40, 255);



    SDL_RenderRect(renderer, &bgRect);



    float fillWidth = getPercentage() * barWidth;



    SDL_FRect fillRect = { screenX, screenY, fillWidth, barHeight };



    SDL_SetRenderDrawColor(renderer, 255, 255, 0, 255);


 // yellow
    SDL_RenderFillRect(renderer, &fillRect);



}


class MathDialog : public Gui::Dialog {
public:
    MathDialog(SDL_Renderer* renderer, TTF_TextEngine* textEngine, TTF_Font* font,
               SDL_Window* window, SDL_Gamepad*& gamepadRef, std::function<void(bool)> callback)
        : Gui::Dialog(renderer, textEngine, font, window, {0,0,600,350},
                      "", "", ""),   // empty strings = no base UI
          gamepad(gamepadRef),
          onAnswer(std::move(callback)) {
        setShowDefaultButtons(false);


 // hide built-in X/Confirm/Cancel; MathDialog draws its own answer buttons
    }

    void onOpen() override {
        int winW, winH;



        SDL_GetWindowSize(window, &winW, &winH);




        // Position dialog at bottom‑centre
        const float dlgW = 600.0f, dlgH = 350.0f;



        const float margin = 40.0f;



        logicalRect = { (winW - dlgW) * 0.5f, winH - dlgH - margin, dlgW, dlgH };




        // Generate random equation: a * x + b = c, with a,b ∈ [1,12], x ∈ [1,10]
        std::random_device rd;



        std::mt19937 gen(rd());



        std::uniform_int_distribution<int> distA(1, 12), distB(1, 12), distX(1, 10);



        a = distA(gen);


 
        b = distB(gen);


 
        x = distX(gen);


 
        c = a * x + b;


   // compute correct result

        // Pick a random letter from 'a' to 'z'
        std::uniform_int_distribution<int> distLetter(0, 25);



        letter = static_cast<char>('a' + distLetter(gen));




        // Build 4 answer options (one correct, three random wrong)
        std::vector<int> options;



        options.push_back(x);



        while (options.size() < 4) {
            int wrong = x + (std::uniform_int_distribution<int>(-5,5)(gen));



            if (wrong != x && wrong >= 1 && wrong <= 12 &&
                std::find(options.begin(), options.end(), wrong) == options.end())
                options.push_back(wrong);



        }
        std::shuffle(options.begin(), options.end(), gen);




        // Create answer buttons (larger)
        answerButtons.clear();



        for (int val : options) {
            auto btn = std::make_unique<Gui::Button>(
                renderer, font, std::to_string(val),
                SDL_FPoint{0,0}, 160, 60);



            btn->onClicked = [this, val]() {
                if (onAnswer) onAnswer(val == x);



            };



            answerButtons.push_back(std::move(btn));



        }

        selectedButtonIndex = 0;



        state = DialogState::Opened;



        progress = 1.0f;



    };



    bool onHandleEvent(const SDL_Event& ev) override {
        if (state != DialogState::Opened) return false;




        // ---- Keyboard navigation ----
        if (ev.type == SDL_EVENT_KEY_DOWN) {
            switch (ev.key.key) {
                case SDLK_LEFT:  moveSelection(-1, false);


 return true;



                case SDLK_RIGHT: moveSelection(+1, false);


 return true;



                case SDLK_UP:    moveSelection(-2, false);


 return true;



                case SDLK_DOWN:  moveSelection(+2, false);


 return true;



                case SDLK_RETURN:
                case SDLK_KP_ENTER:
                    if (selectedButtonIndex >= 0 && selectedButtonIndex < (int)answerButtons.size())
                        if (answerButtons[selectedButtonIndex]->onClicked)
                            answerButtons[selectedButtonIndex]->onClicked();



                    return true;



                default: break;



            }
        }

        // ---- Gamepad D‑pad (only if a gamepad is connected) ----
        if (gamepad && ev.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
            switch (ev.gbutton.button) {
                case SDL_GAMEPAD_BUTTON_DPAD_LEFT:  moveSelection(-1, true);


 return true;



                case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: moveSelection(+1, true);


 return true;



                case SDL_GAMEPAD_BUTTON_DPAD_UP:    moveSelection(-2, true);


 return true;



                case SDL_GAMEPAD_BUTTON_DPAD_DOWN:  moveSelection(+2, true);


 return true;



                case SDL_GAMEPAD_BUTTON_SOUTH:      // A on Xbox, Cross on PS
                    if (selectedButtonIndex >= 0 && selectedButtonIndex < (int)answerButtons.size())
                        if (answerButtons[selectedButtonIndex]->onClicked)
                            answerButtons[selectedButtonIndex]->onClicked();



                    return true;



                default: break;



            }
        }

        // Also forward mouse events to buttons (for mouse users)
        for (auto& btn : answerButtons) {
            if (btn->handleEvent(ev, window, 0, 0)) return true;



        }

        // Escape closes dialog (counts as wrong answer)
        if (ev.type == SDL_EVENT_KEY_DOWN && ev.key.key == SDLK_ESCAPE) {
            if (onAnswer) onAnswer(false);



            return true;



        }
        return false;



    };



    void onRender(SDL_FRect win) override {
        // Semi‑transparent background overlay
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);



        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 180);



        SDL_RenderFillRect(renderer, &win);




        // Border
        SDL_SetRenderDrawColor(renderer, 100, 100, 150, 255);



        SDL_RenderRect(renderer, &win);




        // Question: "a*letter + b = c, letter = ?"
        char question[128];



        snprintf(question, sizeof(question), "%d%c + %d = %d, %c = ?", a, letter, b, c, letter);



        TTF_Text* text = TTF_CreateText(textEngine, font, question, 0);



        if (text) {
            TTF_SetTextColor(text, 255, 255, 255, 255);



            int tw, th;



            TTF_GetTextSize(text, &tw, &th);



            TTF_DrawRendererText(text, win.x + (win.w - tw) / 2, win.y + 20);



            TTF_DestroyText(text);



        }

        // Layout buttons in a 2×2 grid, larger
        float btnW = 160, btnH = 60;



        float gapX = 30, gapY = 20;



        float totalW = 2 * btnW + gapX;



        float totalH = 2 * btnH + gapY;



        float startX = win.x + (win.w - totalW) / 2;



        float startY = win.y + 70;




        for (int i = 0; i < 4; ++i) {
            int row = i / 2, col = i % 2;



            float x = startX + col * (btnW + gapX);



            float y = startY + row * (btnH + gapY);




            // Draw yellow highlight around the selected button if gamepad connected
            if (i == selectedButtonIndex && gamepad) {
                SDL_SetRenderDrawColor(renderer, 255, 255, 0, 200);



                SDL_FRect highlight = { x - 4, y - 4, btnW + 8, btnH + 8 };



                SDL_RenderRect(renderer, &highlight);



            }

            answerButtons[i]->setRect({x, y, btnW, btnH});



            answerButtons[i]->render(0, 0);



        }
    };



    void onReset() override {
        answerButtons.clear();



        selectedButtonIndex = 0;



        state = DialogState::Closed;



        progress = 0.0f;



    };



private:
    void moveSelection(int delta, bool wrap) {
        if (answerButtons.empty()) return;



        int newIdx = selectedButtonIndex + delta;



        if (wrap) {
            newIdx = (newIdx % 4 + 4) % 4;



        } else {
            newIdx = std::clamp(newIdx, 0, 3);



        };


        selectedButtonIndex = newIdx;



    }

    SDL_Gamepad*& gamepad;



    std::function<void(bool)> onAnswer;



    std::vector<std::unique_ptr<Gui::Button>> answerButtons;



    int selectedButtonIndex = 0;



    int a, b, c, x;



    char letter;



};





void GameScript::showMathDialog() {
    if (mathDialog) {
        mathDialog->open();



        isMathDialogActive = true;



        movementLocked = true;



    }
}


void GameScript::onMathAnswer(bool correct) {
    // Close the dialog and reset its state so it can be reopened later
    if (mathDialog) {
        mathDialog->reset();


   // state -> Closed
    }
    isMathDialogActive = false;



    movementLocked = false;


   // allow movement again

    if (correct) {
        switch (reason)
        {
            case 0:
                startDrinking();



                break;



            case 1: 
                startEating();



                break;



        }
    }
    // Wrong: water continues to deplete (no refill)
    // The player must re‑enter the puddle to trigger another question
}

void GameScript::startDrinking() {
    if (ctx && ctx->scene && player != (Entity)-1) {
        auto& anim = ctx->scene->world.animation_state_pool[player];



        if (anim.find("drink")) {
            ProjectScript_MIXER_PlaySound("slurp");



            anim.play("drink", true, false);



        }
    }
    isDrinking = true;



    drinkTimer = 0.0f;



    movementLocked = true;



}

void GameScript::startEating() {
    if (ctx && ctx->scene && player != (Entity)-1) {
        auto& anim = ctx->scene->world.animation_state_pool[player];



        if (anim.find("eat")) { // change to eat once we have the animation
            ProjectScript_MIXER_PlaySound("bite");


 // change to munch once we have the sound
            anim.play("eat", true, false);



        }
    }
    isEating = true;



    eatTimer = 0.0f;



    movementLocked = true;



}

void GameScript::finishEating() {
    isEating = false;



    movementLocked = false;



    foodbar.heal(WaterNFoodMaxValue);


   // fully refill

    if (ctx && ctx->scene && player != (Entity)-1) {
        auto& anim = ctx->scene->world.animation_state_pool[player];



        anim.play("idle", true, true);



    }
}


void GameScript::finishDrinking() {
    isDrinking = false;



    movementLocked = false;



    waterbar.heal(WaterNFoodMaxValue);


   // fully refill

    if (ctx && ctx->scene && player != (Entity)-1) {
        auto& anim = ctx->scene->world.animation_state_pool[player];



        anim.play("idle", true, true);



    }
}

void GameScript::onUpdate(float dt) {
    if (!(ctx && ctx->scene) || player == (Entity)-1) return;




    // Update vanish effects and healthbar (heartbeat audio) – always run
    vanishEffects.update(ctx->scene->world, dt);



    healthbar.update(
        dt,
        [&]() { ProjectScript_MIXER_PlayLoopingSound("heartbeat", 0.7f, 1.0f); },
        [&]() { ProjectScript_MIXER_StopSound("heartbeat"); }
    );




    // Water depletion and health damage – ONLY when NOT in dialog, NOT drinking, NOT dead
    if (!isMathDialogActive && !isDrinking && !deathState) {
        waterbar.damage(waterDepletionRate * dt);



        if (waterbar.getCurrentVal() <= 0) {
            healthbar.damage(8.0f * dt);



        }
    }

    // Food depletion and health damage - ONLY when NOT in dialog, NOT eating, NOT dead
    if (!isMathDialogActive && !isEating && !deathState) {
        foodbar.damage(foodDepletionRate * dt);



        if (foodbar.getCurrentVal() <= 0) {
            healthbar.damage(8.0f * dt);



        }
    }


    // Check death (health <= 0)
    if (healthbar.getCurrentVal() <= 0 && !deathState) {
        deathState = true;



        movementLocked = true;



        if (!vanishEffects.isActive(player)) {
            vanishEffects.start(player);



        }
        return;


 // no further processing this frame
    }

    // 4. If already dead, wait for vanish to finish, then change scene
    if (deathState) {
        if (vanishEffects.activeCount() == 0) {
            change_scene(*ctx->sceneParser, *ctx->scene, ctx->sceneFilePath,
                         "OpenWorld/scenes/losing_screen.json",
                         static_cast<void*>(ctx), ctx->physicsWorld);



        }
        return;



    }

    // 5. Drinking animation – locks movement, water is already paused
    if (isDrinking) {
        drinkTimer += dt;



        if (drinkTimer >= drinkDuration) {
            finishDrinking();



        }
        return;


 // movement locked during drinking
    }

    if (isEating) {
        eatTimer += dt;



        if (eatTimer >= eatDuration) {
            finishEating();



        }
        return;



    }

    // 6. Math dialog trigger – only when in a puddle, water ≤ 30%, and dialog not already open
    if (isInPuddle && waterbar.getPercentage() <= 0.3f && !isMathDialogActive) {
        reason = 0;



        showMathDialog();



        movementLocked = true;


   // freeze player while answering
        return;


                  // wait for the user's answer
    }
    

    // 7. Normal gameplay – only if movement is not locked
    if (!movementLocked) {
        // ---- Collisions and input (existing code) ----
        checkCollisions(dt);



        checkContacts(dt);




        float dx = 0.0f, dy = 0.0f;




        if (knockbackTimer > 0.0f) {
            knockbackTimer -= dt;


   // sliding – no input
        } else {
            const bool* keys = SDL_GetKeyboardState(nullptr);



            if (keys[SDL_SCANCODE_A]) dx -= 1.0f;



            if (keys[SDL_SCANCODE_D]) dx += 1.0f;



            if (keys[SDL_SCANCODE_W]) dy -= 1.0f;



            if (keys[SDL_SCANCODE_S]) dy += 1.0f;




            if (ctx->gamepad) {
                float deadzone = 0.2f;



                float gx = SDL_GetGamepadAxis(ctx->gamepad, SDL_GAMEPAD_AXIS_LEFTX) / 32767.0f;



                float gy = SDL_GetGamepadAxis(ctx->gamepad, SDL_GAMEPAD_AXIS_LEFTY) / 32767.0f;



                if (std::fabs(gx) > deadzone) dx += gx;



                if (std::fabs(gy) > deadzone) dy += gy;



            }

            // Normalise diagonal speed
            if (dx != 0.0f || dy != 0.0f) {
                float len = std::sqrt(dx * dx + dy * dy);



                if (len > 1.0f) { dx /= len;


 dy /= len;


 }
            }
        };



        // Animation state
        if (ctx->scene->world.has_animation_state[player]) {
            auto& anim = ctx->scene->world.animation_state_pool[player];



            if (knockbackTimer <= 0.0f) {
                if (dx != 0.0f || dy != 0.0f) {
                    if (dx > 0.0f) {
                        if (anim.activeName() != "walk") anim.play("walk", true);



                    } else if (dx < 0.0f) {
                        if (anim.activeName() != "walk-flip") anim.play("walk-flip", true);



                    } else {
                        if (anim.activeName() != "walk" && anim.activeName() != "walk-flip")
                            anim.play("walk", true);



                    };


                } else {
                    if (anim.activeName() != "idle") anim.play("idle", true);



                };


            }
        }

        // Apply velocity to physics body
        if (ctx->physicsWorld &&
            ctx->scene->world.has_physics_body[player] &&
            b2Body_IsValid(ctx->scene->world.physics_body_pool[player].bodyId))
        {
            if (knockbackTimer <= 0.0f) {
                constexpr float ppm = Physics::PIXELS_PER_METER;



                b2Body_SetLinearVelocity(
                    ctx->scene->world.physics_body_pool[player].bodyId,
                    b2Vec2{ dx * moveSpeed / ppm, dy * moveSpeed / ppm });



            }
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
}



void GameScript::onDraw() {
    // Draws the fading sprite strips + batched black-particle trail for any
    // entity currently vanishing (see RenderFrame(), which skips their
    // normal sprite so this fully replaces it instead of drawing under it).
    if (ctx) {
        vanishEffects.render(ctx->renderer, ctx->scene->world, ctx->camera);



    }
    if (ctx && player != (Entity)-1 && ctx->scene->world.has_position[player]) {
        SDL_FPoint playerScreen = ctx->camera.worldToScreen(
            ctx->scene->world.position_pool[player].x,
            ctx->scene->world.position_pool[player].y
        );



        if (!vanishEffects.isActive(player)) {
            
            waterbar.render(ctx->renderer, playerScreen.x, playerScreen.y + waterbarOffsetY);



            foodbar.render(ctx->renderer, playerScreen.x, playerScreen.y + foodbarOffsetY);




            
            SDL_FRect destRect1 = {playerScreen.x - barOffsetX, playerScreen.y + waterbarOffsetY, 26,26};



            SDL_RenderTexture(ctx->renderer, ctx->waterbarIcon, &barIconsrcRect, &destRect1);




            SDL_FRect destRect2 = {playerScreen.x - barOffsetX, playerScreen.y + foodbarOffsetY, 26,26};



            SDL_RenderTexture(ctx->renderer, ctx->foodbarIcon, &barIconsrcRect, &destRect2);




            SDL_FRect destRect3 = {playerScreen.x - barOffsetX, playerScreen.y + healthbarOffsetY, 26,26};



            SDL_RenderTexture(ctx->renderer, ctx->healthbarIcon, &barIconsrcRect, &destRect3);



        }
    }

    // Render math dialog if active
    if (mathDialog && isMathDialogActive) {
        mathDialog->render();



    }
}

void GameScript::onEvent(const SDL_Event& e) {
    // Forward events to the math dialog when active
    if (isMathDialogActive && mathDialog) {
        // The dialog's buttons handle answer via onClicked, but we still
        // need to process mouse/keyboard events so buttons work.
        mathDialog->handleEvent(e);



    }
    // No need to forward to other GUI elements unless you have them
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



    };


 
    // Save the generated entities to the fast binary .world file
    // (We no longer call saveToFile on the scene JSON, keeping it clean)
    WorldBinary::save(worldFilePath, ctx->scene->world, decorationEntities, 
                      ctx->scene->areBiomesAndEntitiesGenerated, ctx->scene->projectRoot);




    mathDialog.reset();




    saveUserProgress(userProgress, "OpenWorld/userdata/userdata.json");



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



    TTF_TextEngine* textEngine = TTF_CreateRendererTextEngine(renderer);



    ProjectScript_TTF_LoadFont("gameFont", "OpenWorld/assets/fonts/fredoka.ttf", 30);



    TTF_Font* gameFont = ProjectScript_TTF_GetFont("gameFont");




    ProjectScript_TTF_LoadFont("mathFont", "OpenWorld/assets/fonts/cmu.ttf", 40);



    TTF_Font* mathFont = ProjectScript_TTF_GetFont("mathFont");



 
    const std::string sceneFilePath = "OpenWorld/scenes/main_menu.json";



    SceneParser parser(renderer, textEngine, gameFont, window);



    Scene scene = parser.ProjectScript_loadFromFile(sceneFilePath);



    if (!scene.scriptValid)
        std::cerr << "[Warning] Scene script validation failed - running anyway.\n";



 
    // load textures
    ProjectScript_IMG_LoadTexture("grassBackground",  "OpenWorld/assets/textures/grassBackground.svg");



    ProjectScript_IMG_LoadTexture("snowBackground",   "OpenWorld/assets/textures/snowBackground.svg");



    ProjectScript_IMG_LoadTexture("desertBackground", "OpenWorld/assets/textures/dessertBackground.svg");



    ProjectScript_IMG_LoadTexture("healthbarIcon", "OpenWorld/assets/textures/healthbarIcon.svg");



    ProjectScript_IMG_LoadTexture("foodbarIcon", "OpenWorld/assets/textures/foodbarIcon.svg");



    ProjectScript_IMG_LoadTexture("waterbarIcon", "OpenWorld/assets/textures/waterbarIcon.svg");

    ProjectScript_IMG_LoadTexture("path", "OpenWorld/assets/textures/path.svg");

    ProjectScript_IMG_LoadTexture("path90deg", "OpenWorld/assets/textures/path90deg.svg");

    ProjectScript_IMG_LoadTexture("GamePadCursorIcon", "OpenWorld/assets/textures/cup.svg");

    // load audio 
    g_resources.AudioManager.CreateMixerDevice();



    ProjectScript_MIXER_LoadSound("punch", "OpenWorld/assets/audio/Punch.wav", false);



    ProjectScript_MIXER_LoadSound("water", "OpenWorld/assets/audio/water.wav", false);



    ProjectScript_MIXER_LoadSound("heartbeat", "OpenWorld/assets/audio/heartbeat.wav", false);



    ProjectScript_MIXER_LoadSound("slurp", "OpenWorld/assets/audio/slurp.wav", false);



    ProjectScript_MIXER_LoadSound("pain", "OpenWorld/assets/audio/pain.wav", false);



    ProjectScript_MIXER_LoadSound("bite", "OpenWorld/assets/audio/bite.wav", false);




 
    GameContext ctx;



    ctx.renderer      = renderer;



    ctx.textEngine    = textEngine;



    ctx.window        = window;



    ctx.scene         = &scene;



    ctx.sceneParser   = &parser;



    ctx.sceneFilePath = sceneFilePath;



    ctx.healthbarIcon = ProjectScript_IMG_GetTexture("healthbarIcon");



    ctx.foodbarIcon = ProjectScript_IMG_GetTexture("foodbarIcon");



    ctx.waterbarIcon = ProjectScript_IMG_GetTexture("waterbarIcon");



 
    {
        int numGamepads = 0;



        SDL_JoystickID* gamepadIDs = SDL_GetGamepads(&numGamepads);



        if (gamepadIDs && numGamepads > 0)
            ctx.gamepad = SDL_OpenGamepad(gamepadIDs[0]);



        SDL_free(gamepadIDs);



    }

 
    // The scene's script instance was already created polymorphically back
    // in parser.ProjectScript_loadFromFile() above (SceneParser resolves
    // scene.script_attached through ScriptRegistry — see
    // instantiateScriptForScene() in engine.h). main() no longer hard-codes
    // a concrete GameScript here at all: it just hands this GameContext to
    // whatever script the scene actually resolved, then drives it purely
    // through the ScriptBase interface. If a scene names a different
    // script class, or change_scene() later swaps in one, this code below
    // doesn't need to change at all — that's the whole point of the
    // registry + polymorphic dispatch.
    if (!scene.script) {
        std::cerr << "[Warning] Scene '" << scene.name
                   << "' has no active script (script_attached='" << scene.scriptAttached
                   << "') -- running with no game logic.\n";



    } else {
        scene.script->setContext(&ctx);



    };



    g_renderer   = renderer;



    g_window     = window;



    g_textEngine = textEngine;



    g_context    = &ctx;



    g_lastTime   = SDL_GetTicks();



 
    ctx.physicsWorld = new Physics::PhysicsWorld(0.0f);



 
    if (scene.script) scene.script->onStart();



 
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
if (ctx.scene && ctx.scene->script) {
    ctx.scene->script->onEvent(e);
}
for (auto& elem : ctx.scene->guiElements) {
    elem->handleEvent(e, window, 0.0f, 0.0f);
}
if (e.type == SDL_EVENT_GAMEPAD_ADDED && !ctx.gamepad)
    ctx.gamepad = SDL_OpenGamepad(e.gdevice.which);
else if (e.type == SDL_EVENT_GAMEPAD_REMOVED && ctx.gamepad &&
         SDL_GetGamepadID(ctx.gamepad) == e.gdevice.which) {
    SDL_CloseGamepad(ctx.gamepad);
    ctx.gamepad = nullptr;
}
        }
 
        // Polymorphic dispatch: ctx.scene->script may point at a completely
        // different ScriptBase subclass than it did last frame if
        // change_scene() swapped scenes in the meantime.
        if (ctx.scene->script) ctx.scene->script->onUpdate(dt);



        StepPhysicsAndSync(ctx, dt);



        animation_system(ctx.scene->world, dt);



        if (ctx.gamepad) {
            GameScript* gs = ActiveGameScript(ctx);



            if (!gs) {
                // Menu mode: update cursor and forward GUI events
                float deadzone = 0.2f;



                float ax = SDL_GetGamepadAxis(ctx.gamepad, SDL_GAMEPAD_AXIS_LEFTX) / 32767.0f;



                float ay = SDL_GetGamepadAxis(ctx.gamepad, SDL_GAMEPAD_AXIS_LEFTY) / 32767.0f;



                if (std::fabs(ax) < deadzone) ax = 0.0f;



                if (std::fabs(ay) < deadzone) ay = 0.0f;




                const float cursorSpeed = 500.0f;


 // pixels per second
                ctx.gamepadCursorX += ax * cursorSpeed * dt;



                ctx.gamepadCursorY += ay * cursorSpeed * dt;




                int winW, winH;



                SDL_GetWindowSize(ctx.window, &winW, &winH);



                ctx.gamepadCursorX = std::clamp(ctx.gamepadCursorX, 0.0f, (float)winW);



                ctx.gamepadCursorY = std::clamp(ctx.gamepadCursorY, 0.0f, (float)winH);




                bool confirmDown = SDL_GetGamepadButton(ctx.gamepad, SDL_GAMEPAD_BUTTON_SOUTH) != 0;



                for (auto& elem : ctx.scene->guiElements) {
                    elem->handleGamepad(ctx.gamepadCursorX, ctx.gamepadCursorY,
                                        0.0f, 0.0f, ctx.window,
                                        confirmDown, ctx.confirmDownLastFrame);



                }
                ctx.confirmDownLastFrame = confirmDown;



            } else {
                // Gameplay mode: do nothing with cursor/GUI
                ctx.confirmDownLastFrame = false;



            };


        } else {
            ctx.confirmDownLastFrame = false;



        };


        RenderFrame(renderer, window, ctx);



    }
 
#endif
 
    if (ctx.scene->script) ctx.scene->script->onEnd();



    if (ctx.gamepad) SDL_CloseGamepad(ctx.gamepad);



    if (textEngine) TTF_DestroyRendererTextEngine(textEngine);



    ProjectScript_TTF_Clear();



    ProjectScript_IMG_Clear();



    if (ctx.physicsWorld) { delete ctx.physicsWorld;


 ctx.physicsWorld = nullptr;


 }
    TTF_Quit();



    MIX_Quit();
SDL_DestroyRenderer(renderer);
SDL_DestroyWindow(window);
    return 0;



}