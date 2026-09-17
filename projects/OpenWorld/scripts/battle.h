#pragma once
#include "game.h"   // GameContext + BattleEncounterInfo live here
#include "particles.h" 

// ============================================================
// BattleScript — script attached to battle.json.
//
// A header isn't actually *mandatory* here: engine.h's ScriptRegistry only
// needs a ScriptBase-derived class to exist somewhere and be registered via
// REGISTER_SCRIPT (see the macro in engine.h) — nothing about scene loading
// or change_scene() requires a header to go with it. RenderFrame()'s
// GameScript-specific extras (tilemap, minimap, player healthbar) are the
// one place that reaches for a concrete subclass, and that's hard-coded to
// GameScript specifically via dynamic_cast, so BattleScript doesn't need to
// be visible there either.
//
// This header exists purely to match the project's existing convention
// (GameScript/game.h, MainMenuScript/main_menu.h) so the class declaration
// isn't buried inside battle.cpp — not because engine.h forces it.
// ============================================================
class BattleScript : public ScriptBase {
public:
    std::string getName() const override { return "BattleScript"; }

    GameContext* ctx = nullptr;

    // Called once right after ScriptRegistry creates this instance (see
    // instantiateScriptForScene() in engine.h), and again by change_scene()
    // whenever this script becomes the active one for a newly-loaded scene.
    void setContext(void* context) override {
        ctx = static_cast<GameContext*>(context);
    }

    void onStart() override;
    void onUpdate(float dt) override;
    void onDraw() override;
    void onEnd() override;

    void showAttackResultText(const std::string& text);

private:
    float elapsed = 0.0f;

    Tools::AnimatedTextSystem animatedTextSystem;
    Particles::ParticleBurstSystem particleBursts;
    bool hasShownClashText = false;

    // Left = approacher (player), right = challenger (enemy). Populated in
    // onStart() from ctx->battleEncounter — see GameScript::onUpdate()'s
    // enemyCutScenesFinished switch in game.cpp, where that struct gets
    // filled in immediately before the change_scene() call that brought us
    // here. No combat moves live here — this scene only displays the two
    // combatants and their current health.
    Tools::Healthbar leftHealthbar;
    Tools::Healthbar rightHealthbar;

    std::string leftName  = "Player";
    std::string rightName = "Enemy";

    Tools::CutsceneTrack playerPowerupTracks;
    Tools::CutsceneTrack enemyPowerupTracks;
    Tools::CutsceneTrack playerDecisionTracks;

    int playerPowerupTracksFinished = 0;
    int enemyPowerupTracksFinished = 0;
    int playerDecisionTracksFinished = 0;

    void spawnContenderClash(float screenW, float screenH, const std::string& text = "Contender's Clash!"); 

    void spawnBottomBarText(float screenW, float screenH, const std::string& text, float duration = 2.0f, SDL_Color textColor = {255,255,255,255}, SDL_Color barColor = {0,0,0,160});


    // Cached from ctx->battleEncounter.biomeTexture in onStart(), since that
    // struct is cleared (battleEncounter.valid = false) right after being
    // read so a later, unrelated scene load doesn't pick up stale data.
    // Currently only used to choose between the "grass" backdrop and the
    // generic one — see the commented-out texture backdrop in onDraw().
    std::string backdropBiomeTexture = "grassBackground";
};
