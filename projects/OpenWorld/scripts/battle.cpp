#include "battle.h"

// Registers BattleScript under the key "battle", matching the stem of
// battle.json's script_attached ("OpenWorld/scripts/battle.cpp" -> "battle")
// — the same convention GameScript ("game") and MainMenuScript ("main_menu")
// already use. This is what lets battle.json resolve to this class purely
// through ScriptRegistry, with nothing in engine.h needing to know
// BattleScript exists.
REGISTER_SCRIPT(BattleScript, "battle")

namespace {
    // Battle-arena layout constants. Kept local to this file since nothing
    // outside battle.cpp needs them (mirrors how game.h only exposes the
    // *shared* bar constants — healthbarOffsetY, barOffsetX, etc. — that
    // GameContext/other scripts actually reach into).
    constexpr float kBarWidth   = 220.0f;
    constexpr float kBarHeight  = 22.0f;
    constexpr float kBarYFrac   = 0.6f;   // bar sits near the center of the screen
    constexpr float kSideMargin = 350.0f;  // distance from the left/right screen edges
    constexpr float kNameGap    = 26.0f;   // vertical space for the name label above the bar
}


std::array<Entity, 3> powerupEntities;
bool bluePulseCreated = false;
bool damagePlayer = false;


void BattleScript::onStart() {
    if (!ctx) return;

    elapsed = 0.0f;
    SDL_Log("[BattleScript] onStart");

    // bluePulseCreated/powerupEntities are file-scope globals (not
    // BattleScript members), so without this reset they'd stay set from
    // the first battle ever fought and every subsequent battle would find
    // bluePulseCreated already true, permanently skipping pulse creation.
    bluePulseCreated = false;
    powerupEntities[0] = (Entity)-1;
    powerupEntities[1] = (Entity)-1;
    powerupEntities[2] = (Entity)-1;

    // Defaults in case GameContext::battleEncounter wasn't populated (e.g.
    // this scene got loaded directly rather than via GameScript's
    // MathHorseMen encounter — shouldn't normally happen, but don't crash).
    leftName  = "Destined";
    rightName = "Enemy";
    backdropBiomeTexture = "grassBackground";
    leftHealthbar  = Tools::Healthbar(100.0f, kBarWidth, kBarHeight);
    rightHealthbar = Tools::Healthbar(100.0f, kBarWidth, kBarHeight);

    

    // ----------------------------------------------------------------
    // Pull the encounter info GameScript stashed in GameContext right
    // before triggering this scene change (see GameScript::onUpdate()'s
    // enemyCutScenesFinished switch in game.cpp). GameContext is the only
    // safe channel for this: change_scene() destroyed the previous scene's
    // ECSWorld — and the GameScript instance itself — the moment it ran,
    // so approacherEntity/challengerEntity (raw indices into that now-gone
    // world) could never have been used here even if we still had them.
    // ----------------------------------------------------------------
    if (ctx->battleEncounter.valid) {
        const auto& info = ctx->battleEncounter;

        if (!info.approacherName.empty()) leftName  = info.approacherName;
        if (!info.challengerName.empty()) rightName = info.challengerName;

        // set initial health by percentage and using damage() to set ratio

        float approacherPct = std::clamp(info.approacherHealthPct, 0.0f, 1.0f);
        float challengerPct = std::clamp(info.challengerHealthPct, 0.0f, 1.0f);

        leftHealthbar.damage((1.0f - approacherPct) * 100.0f);
        rightHealthbar.damage((1.0f - challengerPct) * 100.0f);

        if (!info.biomeTexture.empty()) backdropBiomeTexture = info.biomeTexture;

        // Consumed — clear so a later, unrelated scene load into this same
        // GameContext (e.g. leaving battle back to the main menu) doesn't
        // accidentally reuse stale encounter data.
        ctx->battleEncounter.valid = false;
    }

    ECSWorld* cutsceneWorld = &ctx->scene->world;
    enemyPowerupTracks.world = cutsceneWorld;

    // NOTE: we do NOT select the "idle" clip here. onStart() runs from
    // inside change_scene(), which is BEFORE GameScript (game.cpp) gets a
    // chance to overwrite ctx->battleEncounter.approacherEntity/
    // challengerEntity with the ids the copied-over entities actually got
    // in THIS (battle) world -- at this point those fields still hold the
    // stale overworld indices. Calling .play("idle") against those would
    // silently no-op (index out of range for this near-empty world), which
    // is exactly why the combatants render but never animate. See
    // onUpdate() below, which does this once the real ids are in place.

    g_resources.AudioManager.PlaySong("fight_theme", 0.65f, 1.0f);
    hasShownClashText = false;
}

void BattleScript::onUpdate(float dt) {
    if (!(ctx && ctx->renderer && ctx->window)) return;

    int winW, winH;
    SDL_GetWindowSize(ctx->window, &winW, &winH);
    float w = (float)winW, h = (float)winH;

    if (elapsed <= 0.0f) {
        // --- Spawn "Contender's Clash!" Text and Particles ---
        if (!hasShownClashText) {
            hasShownClashText = true;
            animatedText.spawnContenderClash(w, h);
            
            // Configure golden spark particles
            Particles::EmitterDef sparkDef;
            sparkDef.colorStart = {255, 220, 100, 255}; 
            sparkDef.colorEnd = {255, 100, 0, 0};       
            sparkDef.speedMin = 80.0f;
            sparkDef.speedMax = 200.0f;
            sparkDef.lifetimeMin = 0.6f;
            sparkDef.lifetimeMax = 1.2f;
            sparkDef.sizeMin = 3.0f;
            sparkDef.sizeMax = 6.0f;
            sparkDef.endSize = 0.0f;
            sparkDef.gravity = 150.0f;

            // Spawn burst at the center of the screen
            particleBursts.spawnBurst(w * 0.5f, h * 0.5f, sparkDef, 60);
        }
        
    }

    if (animatedText.instances.size() == 0 && particleBursts.bursts.size() == 0 && hasShownClashText)
    {
        if (ctx->battleEncounter.challengerEntity != (Entity)-1 && ctx->battleEncounter.challengerEntity < ctx->scene->world.entity_count && ctx->scene->world.has_animation_state[ctx->battleEncounter.challengerEntity]) {
            ctx->scene->world.animation_state_pool[ctx->battleEncounter.challengerEntity].play("attack", false, false);
        }

        if (!ctx->scene->world.animation_state_pool[ctx->battleEncounter.challengerEntity].active().isPlaying && !bluePulseCreated)
        {
            // NOTE: the challenger's on-screen position is NOT
            // position_pool[challengerEntity] -- that component still holds
            // whatever overworld coordinates were carried over by
            // copy_entity() in change_scene(). onDraw() never writes the
            // battle-layout screen position back into it; it computes
            // rightX/barY2 fresh each frame purely from window size and
            // draws the challenger sprite there directly. Anchoring the
            // pulse off position_pool would place it near the stale
            // overworld coordinates -- almost always off the visible
            // window -- even with a valid, correctly-loaded texture. So we
            // recompute the same rightX/barY2 anchor onDraw() uses, here,
            // and offset the pulse from that instead.
            float rightX = w - kSideMargin - kBarWidth;
            float barY2  = h * 0.5f;

            Entity e = ctx->scene->world.create_entity();
            ctx->scene->world.add_z_index(e);
            ctx->scene->world.add_metadata(e);
            ctx->scene->world.add_position(e);
            ctx->scene->world.add_rotation(e); // needed for useExplicitRotations to have anywhere to write to -- both CutsceneTrack::writeRotation() and render_entity_animation() gate on has_rotation[e]
            ctx->scene->world.z_index_pool[e].z = 0;
            ctx->scene->world.metadata_pool[e].name = "MathHorseMan_BluePulse";
            ctx->scene->world.position_pool[e] = {rightX - 57.0f, barY2 + 87.0f};
            ctx->scene->world.add_animation_state(e);
            std::string fp = "C:/Users/daeli/Documents/DispersedEngine/projects/OpenWorld/resources/MathHorseMen_BluePulse.animres";
            ctx->scene->world.assign_animation_resource(e, fp);
            powerupEntities[0] = e; 
            ctx->scene->world.animation_state_pool[e].play("MathHorseMen_BluePulse");
            enemyPowerupTracks.target = e;
            // The approacher's actual on-screen position is NOT
            // position_pool[approacherEntity] -- same trap as the
            // challenger/pulse-anchor issue earlier: that component still
            // holds whatever overworld coordinates were carried over by
            // copy_entity(). onDraw() never writes the battle-layout
            // screen position back into it; it draws the approacher at a
            // hardcoded {leftX, barY + 50.0f} computed fresh each frame.
            // Aiming the cutscene at position_pool sent the pulse flying
            // toward wherever the approacher happened to be standing on
            // the overworld map instead. Recompute the same anchor here.
            float leftX = kSideMargin;
            float barY  = h * kBarYFrac;
            enemyPowerupTracks.points = {
                {ctx->scene->world.position_pool[e].x, ctx->scene->world.position_pool[e].y},
                {ctx->scene->world.position_pool[e].x-100.0f, ctx->scene->world.position_pool[e].y-100.0f},
                {ctx->scene->world.position_pool[e].x-100.0f, ctx->scene->world.position_pool[e].y+50.0f},
                {leftX + 25.0f, barY + 100.0f}
            };
            // durations.size() must equal points.size() - 1 (one entry per
            // segment). This was missing entirely, which left durations
            // empty -- Update()'s guard `segIdx >= (int)durations.size()`
            // (0 >= 0) then tripped on the very first tick after Play(),
            // marking the track finished before it ever moved a single
            // frame. That's why the pulse just sat at its spawn position.
            enemyPowerupTracks.durations = {
                0.5f,
                0.5f,
                1.0f
            };
            enemyPowerupTracks.useExplicitRotations = true;
            // rotations needs one entry per point (4), not per segment (3)
            // -- as written, the last segment (points[2] -> points[3]) had
            // no rotations[3] to ease toward, so the bounds check in
            // Update() (segIdx + 1 < rotations.size()) silently skipped
            // rotating for that whole final leg.
            enemyPowerupTracks.rotations = {
                -45,
                45,
                0,
                0
            };
            enemyPowerupTracks.Play();
            bluePulseCreated = true;
        }

    } else {
        if (ctx->battleEncounter.approacherEntity != (Entity)-1 && ctx->battleEncounter.approacherEntity < ctx->scene->world.entity_count && ctx->scene->world.has_animation_state[ctx->battleEncounter.approacherEntity])
            ctx->scene->world.animation_state_pool[ctx->battleEncounter.approacherEntity].play("idle");
        if (ctx->battleEncounter.challengerEntity != (Entity)-1 && ctx->battleEncounter.challengerEntity < ctx->scene->world.entity_count && ctx->scene->world.has_animation_state[ctx->battleEncounter.challengerEntity])
            ctx->scene->world.animation_state_pool[ctx->battleEncounter.challengerEntity].play("idle");
    }
    
    elapsed += dt;
    
    // Tick the  systems
    leftHealthbar.update(dt);
    rightHealthbar.update(dt);
    animatedText.update(dt);
    particleBursts.update(dt);
    enemyPowerupTracks.Update(dt);

    if (enemyPowerupTracks.finished && !enemyPowerupTracks.wasFinished)
    {
        enemyPowerupTracks.wasFinished = true;
        enemyPowerupTracksFinished++;
        switch(enemyPowerupTracksFinished)
        {
            case 1:
                damagePlayer = true;
                auto& approacherAnim = ctx->scene->world.animation_state_pool[ctx->battleEncounter.approacherEntity];
                
                approacherAnim.play("hurt");
                approacherAnim.active().limitLoopsTo(3); 

                approacherAnim.active().onLoopCompleted = [this]() {
                    leftHealthbar.triggerShake(10.0f, 1.0f);
                    leftHealthbar.damage(10.0f);
                    Tools::ActivateQuickShock(ctx->gamepad);
                };

                break;
        }
    }
}

void BattleScript::onDraw() {
    if (!(ctx && ctx->renderer && ctx->window)) return;

    int winW, winH;
    SDL_GetWindowSize(ctx->window, &winW, &winH);
    float w = (float)winW, h = (float)winH;

    // --- Backdrop --------------------------------------------------------
    // Simple flat color for now — deliberately as blank as possible, per
    // the scene being "as blank as possible" otherwise. Swap in the
    // commented-out stretched-texture version below once real backdrop art
    // exists.
    SDL_SetRenderDrawColor(ctx->renderer, 30, 20, 40, 255);
    SDL_FRect backdrop = {0.0f, 0.0f, w, h};
    SDL_RenderFillRect(ctx->renderer, &backdrop);

    /*
    // --- Full-window backdrop image (fills the window, even on resize) ---
    // Not a tiled/biome background like GameScript's bgMap — just one flat
    // image stretched to cover the whole window every frame.
    //
    // 1. Make the art in Inkscape and load it once, e.g. alongside the
    //    other ProjectScript_IMG_LoadTexture() calls in main() (game.cpp):
    //
    //        ProjectScript_IMG_LoadTexture("battleBackdropGrass",
    //            "OpenWorld/assets/textures/battleBackdropGrass.svg");
    //        ProjectScript_IMG_LoadTexture("battleBackdropDefault",
    //            "OpenWorld/assets/textures/battleBackdropDefault.svg");
    //
    // 2. Pick which one to use from backdropBiomeTexture, which was cached
    //    in onStart() from ctx->battleEncounter.biomeTexture (itself set by
    //    GameScript::getBiomeAt() before the scene change — see game.cpp).
    //    For now this only distinguishes "grass" vs. everything else; add
    //    more textures/branches here as more backdrop art exists.
    //
    // 3. Stretch it to the CURRENT window size every frame (not just once),
    //    so a live resize is handled for free — SDL_GetWindowSize() above
    //    already re-queries this every single onDraw() call, so the dest
    //    rect below always matches the current window, no resize event
    //    handling required.
    */
    
    if (backdropBiomeTexture.find("grass") != std::string::npos)
    {
        SDL_Texture* backdropTex = ProjectScript_IMG_GetTexture("battleBackgroundGrass");
        if (backdropTex) {
            SDL_FRect dest = {0.0f, 0.0f, w, h};
            SDL_RenderTexture(ctx->renderer, backdropTex, nullptr, &dest);
        }
    }


    // --- Health bars: approacher on the left, challenger on the right ----
    float barY   = h * kBarYFrac;
    float barY2 = h * 0.5f;
    float leftX  = kSideMargin;
    float rightX = w - kSideMargin - kBarWidth;

    leftHealthbar.render(ctx->renderer, leftX, barY);
    rightHealthbar.render(ctx->renderer, rightX, barY2);

    // approacherEntity/challengerEntity are ids GameScript's battle-transition
    // branch had change_scene() copy over into THIS (battle) scene's world
    // (see the carryOverEntities argument in game.cpp's case 1) -- they are
    // NOT the original indices from the overworld scene, those died with
    // that scene's ECSWorld. Bounds-check anyway: battleEncounter.valid can
    // be true with no combatant actually carried (e.g. this scene was loaded
    // directly rather than via that transition), in which case both ids are
    // (Entity)-1 and world.entity_count is 0.

    // creating reference of ecsworld is efficient as compiler treats it as direct access
    // so you get performance and reliability
    auto& world = ctx->scene->world;
    // creating copies of primitive data types for readability is also efficient
    Entity approacher = ctx->battleEncounter.approacherEntity;
    Entity challenger = ctx->battleEncounter.challengerEntity;

    if (approacher != (Entity)-1 && approacher < world.entity_count && world.has_animation_state[approacher])
    {
        Components::Position screenPos = {leftX, barY + 50.0f};
        render_entity_animation(ctx->renderer, world, approacher, screenPos.x, screenPos.y, ctx->camera.zoom);
    }

    if (challenger != (Entity)-1 && challenger < world.entity_count && world.has_animation_state[challenger])
    {
        Components::Position screenPos = {rightX, barY2 + 50.0f};
        render_entity_animation(ctx->renderer, world, challenger, screenPos.x, screenPos.y, ctx->camera.zoom);
    }

    // for each loops is as efficient as iterator based with size_t
    for (Entity e : powerupEntities)
    {
        if (e != (Entity)-1)
            render_entity_animation(ctx->renderer, world, e, world.position_pool[e].x, world.position_pool[e].y, ctx->camera.zoom);
    }

    // if (ctx->scene->world.animation_state_pool[ctx->battleEncounter.approacherEntity].active().loopsCompleted <= 3 && damagePlayer)
    // {
        // leftHealthbar.damage(10.0f);
        // Tools::ActivateQuickShock(ctx->gamepad);
    // }      


    // --- Name labels above each bar ---------------------------------------
    // Reuses "gameFont", already loaded once in main() (game.cpp) — no need
    // to load it again here.
    TTF_Font* font = ProjectScript_TTF_GetFont("gameFont");
    if (font && ctx->textEngine) {
        auto drawLabel = [&](const std::string& text, float x, float y) {
            TTF_Text* t = TTF_CreateText(ctx->textEngine, font, text.c_str(), 0);
            if (!t) return;
            TTF_SetTextColor(t, 235, 235, 245, 255);
            TTF_DrawRendererText(t, x, y);
            TTF_DestroyText(t);
        };
        drawLabel(leftName,  leftX,  barY - kNameGap);
        drawLabel(rightName, rightX, barY2 - kNameGap);
    }

    particleBursts.render(ctx->renderer, ctx->camera, true); 
    animatedText.render(ctx->renderer, ctx->textEngine, font);
    
}

void BattleScript::onEnd() {
    SDL_Log("[BattleScript] onEnd");
}


void BattleScript::showAttackResultText(const std::string& text) {
    if (!ctx || !ctx->window) return;
    int winW, winH;
    SDL_GetWindowSize(ctx->window, &winW, &winH);
    
    // Spawns a static bar at the bottom with the text centered
    animatedText.spawnBottomBarText((float)winW, (float)winH, text, 2.5f, {255,255,255,255}, {0,0,0,180});
}