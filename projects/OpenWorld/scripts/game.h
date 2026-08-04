#pragma once
#include "engine.h"
#include "particles.h"
#include <vector>
#include <string>
#include <functional>
#include <random>

struct GameContext {
    SDL_Renderer*       renderer        = nullptr;
    TTF_TextEngine*     textEngine      = nullptr;
    SDL_Window*         window          = nullptr;
    Scene*              scene           = nullptr;
    Tools::Camera       camera;
    SDL_Gamepad*        gamepad         = nullptr;
    Physics::PhysicsWorld* physicsWorld = nullptr;  
    SceneParser*        sceneParser     = nullptr;   
    std::string         sceneFilePath;   
    float gamepadCursorX = 0.0f;
    float gamepadCursorY = 0.0f;
    bool confirmDownLastFrame = false;  
};


struct Biome {
    std::string textureName;
    int gridX = 0;
    int gridY = 0;
    int id = 0;
};

class Healthbar {
public:
    Healthbar(float maxHealth = 100.0f, float barWidth = 100.0f, float barHeight = 12.0f)
        : maxHealth(maxHealth), currentHealth(maxHealth), barWidth(barWidth), barHeight(barHeight) {}

    void damage(float amount) { currentHealth = std::max(0.0f, currentHealth - amount); }
    void heal(float amount)   { currentHealth = std::min(maxHealth, currentHealth + amount); }
    float getCurrentVal() const { return currentHealth; }

    void render(SDL_Renderer* renderer, float screenX, float screenY) {
        if (currentHealth <= 0.0f) return; // optional

        // Background (grey outline)
        SDL_FRect bgRect = { screenX, screenY, barWidth, barHeight };
        SDL_SetRenderDrawColor(renderer, 40, 40, 40, 255);
        SDL_RenderRect(renderer, &bgRect); // optional border

        // Fill (colored portion)
        float fillWidth = (currentHealth / maxHealth) * barWidth;
        SDL_FRect fillRect = { screenX, screenY, fillWidth, barHeight };
        SDL_Color color = getHealthColor(currentHealth / maxHealth);
        SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, 255);
        SDL_RenderFillRect(renderer, &fillRect);
    }

private:
    float maxHealth;
    float currentHealth;
    float barWidth;
    float barHeight;

    SDL_Color getHealthColor(float ratio) const {
        if (ratio >= 0.6f)  return { 0, 200, 0, 255 };       // green
        if (ratio >= 0.4f)  return { 200, 200, 0, 255 };     // yellow
        if (ratio >= 0.2f)  return { 255, 165, 0, 255 };     // orange
        return { 200, 0, 0, 255 };                           // red
    }
};
 

class GameScript : public ScriptBase {
public:
    std::string getName() const override { return "GameScript"; }
    GameContext* ctx = nullptr;

    // engine.h's ScriptRegistry/SceneParser/change_scene machinery only
    // knows ScriptBase, not GameContext (a project-defined type), so the
    // context is handed over as void* and cast back here. Called once
    // right after ScriptRegistry creates this instance for a scene, and
    // again by change_scene() whenever this script becomes the active one
    // for a newly-loaded scene.
    void setContext(void* context) override { ctx = static_cast<GameContext*>(context); }

    float moveSpeed = 300.0f;

    // Biome grid configuration
    std::vector<Biome> biomes;
    int biomeGridW = 3;
    int biomeGridH = 5;
    int biomeTileCountX = 5;
    int biomeTileCountY = 5;

    Entity lastHitEntity = (Entity)-1;
    float hitCooldown = 0.0f;

    Healthbar healthbar;

    // Drives the black-particle "vanish" effect (see particles.h). Started
    // once when the player dies (see checkCollisions), ticked every frame
    // from onUpdate(), and drawn from onDraw(). A vanish's own progress and
    // particles live entirely inside this object, so nothing else on
    // GameScript needs to track fade state by hand.
    Particles::VanishSystem vanishEffects;

    void checkCollisions(float dt);
    
    float knockbackTimer = 0.0f;
    float knockbackSpeed = 500.0f;
    bool deathState = false;

    // Engine APIs
    Tools::TileMap tileMap;
    Tools::Minimap minimap;

    bool drawPhysicsDebug = false;

    // --- Y-Sorting ---
    bool ySortEnabled = true;

    // Which point of each entity's rect is used for depth sorting.
    // Bottom = feet-based sorting (recommended for top-down games with
    // mixed-height entities like a tall player and short grass).
    Tools::YSortAnchor ySortAnchor = Tools::YSortAnchor::Bottom;

    Entity flower = (Entity)-1;
    Entity grass  = (Entity)-1;
    Entity catcus = (Entity)-1;
    Entity emptyTree = (Entity)-1;
    Entity iceCrystal = (Entity)-1;
    Entity snowyEmptyTree = (Entity)-1;

    std::vector<Entity> decorationEntities;

    // Controls whether the one-time biome/decoration generation guard
    // (Scene::areBiomesAndEntitiesGenerated) is honored:
    //   true  -> once biomes/decorations are generated for a scene, later
    //            onStart() calls (e.g. after a reload) skip generation.
    //   false -> ignore the saved flag and always (re)generate biomes and
    //            decoration entities on every onStart(), useful for
    //            iterating on generation logic during development.
    bool shouldStopRegeneration = true;

    void buildBiomeGrid();
    void rebuildTileMap();
    void spawnRandomDecorations();

    Entity player = (Entity)-1;
    Entity background = (Entity)-1;

    void onStart() override;
    void onUpdate(float dt) override;
    void onDraw() override;
    void onEnd() override;

private:
    float elapsed = 0.0f;
};