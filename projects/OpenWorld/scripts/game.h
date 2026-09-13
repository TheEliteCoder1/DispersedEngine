#pragma once
#include "engine.h"
#include "particles.h"

constexpr float healthbarOffsetY = -50.0f;



constexpr float waterbarOffsetY = -80.0f;



constexpr float foodbarOffsetY = -110.0f;



constexpr SDL_FRect barIconsrcRect = {0,0,26,26};



constexpr float barOffsetX = 30.0f;



constexpr float WaterNFoodMaxValue = 200.0f;



constexpr float WaterNFoodBarHeight = 12.0f;



constexpr float WaterNFoodBarWidth = 100.0f;




// Snapshot of one MathHorseMen encounter, copied out of GameScript's
// ECSWorld into GameContext right before GameScript triggers the switch
// to battle.json (see GameScript::onUpdate() in game.cpp). Plain data
// only -- no Entity handles, no ECSWorld pointers -- since everything
// about the old scene (including the GameScript instance itself) is
// destroyed by change_scene() before BattleScript ever runs.
struct BattleEncounterInfo {
    // False until GameScript populates it, and reset to false again once
    // BattleScript::onStart() has consumed it, so a later, unrelated
    // scene load into this same GameContext never reads stale data.
    bool valid = false;

    // GameScript::approacherEntity's metadata name -- drawn on the LEFT
    // side of the battle scene.
    std::string approacherName;

    // GameScript::challengerEntity's metadata name -- drawn on the RIGHT
    // side of the battle scene.
    std::string challengerName;

    Entity approacherEntity = (Entity)-1;
    Entity challengerEntity = (Entity)-1;

    // 0..1 health fractions at hand-off time. approacherHealthPct comes
    // straight from GameScript::healthbar; challengerHealthPct defaults to
    // full since the enemy has no health system of its own yet -- this
    // script doesn't invent one.
    float approacherHealthPct = 1.0f;
    float challengerHealthPct = 1.0f;

    // textureName of the Biome (see GameScript::getBiomeAt()) the
    // approacher was standing in when the encounter started, e.g.
    // "grassBackground". Lets the battle scene pick a matching backdrop
    // (see BattleScript::onDraw() in battle.cpp).
    std::string biomeTexture;
};



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


  
    SDL_Texture* healthbarIcon;



    SDL_Texture* waterbarIcon;



    SDL_Texture* foodbarIcon;



    // Handoff channel from GameScript to BattleScript for a single
    // encounter (see BattleEncounterInfo above for why GameContext, and
    // not an Entity handle, is used to carry this across the scene
    // change).
    BattleEncounterInfo battleEncounter;



};





struct Biome {
    std::string textureName;



    int gridX = 0;



    int gridY = 0;



    int id = 0;



};




class WaterBar {
public:
    WaterBar(float max = 100.0f, float width = 100.0f, float height = 12.0f);



    // Virtual destructor ensures proper cleanup of derived classes
    virtual~ WaterBar() = default;


 
    void damage(float amount);



    void heal(float amount);



    float getCurrentVal() const;



    float getPercentage() const;



    virtual void render(SDL_Renderer* renderer, float screenX, float screenY);




protected:
    float maxValue, currentValue;



    float barWidth, barHeight;



};




class FoodBar : public WaterBar {
    public:
        using WaterBar::WaterBar;


 // inherits constructor
        // virtual destructor is already used behind the scenes
        // render method is overrided
        void render(SDL_Renderer* renderer, float screenX, float screenY) override;



};




// Forward declaration for MathDialog (defined in game.cpp)
class MathDialog;




class GameScript : public ScriptBase {
public:
    std::string getName() const override { return "GameScript";


 };
    GameContext* ctx = nullptr;




    // engine.h's ScriptRegistry/SceneParser/change_scene machinery only
    // knows ScriptBase, not GameContext (a project-defined type), so the
    // context is handed over as void* and cast back here. Called once
    // right after ScriptRegistry creates this instance for a scene, and
    // again by change_scene() whenever this script becomes the active one
    // for a newly-loaded scene.
    void setContext(void* context) override { ctx = static_cast<GameContext*>(context);


 };

    // your default speed is 300.0f, and slow peed is 150.0f
    float normalSpeed = 2000.0f;
    float moveSpeed = 2000.0f;
    float slowSpeed = 2000.0f;




    int puddleContactCount = 0;


    int mathHorseMenContactCount = 0;



    bool isInPuddle = false;


    bool isInCombat = false;

    
    Tools::CutsceneTrack playerCutsceneTracks;

    Tools::CutsceneTrack enemyCutsceneTracks;

    int playerCutScenesFinished = 0;
    
    int enemyCutScenesFinished = 0;

    void handleSensorTouch(b2ShapeId sensorShape, b2ShapeId visitorShape, bool isBegin);

    // Biome grid configuration
    std::vector<Biome> biomes;



    int biomeGridW = 3;



    int biomeGridH = 5;



    int biomeTileCountX = 5;



    int biomeTileCountY = 5;




    // Water
    WaterBar waterbar;



    float waterDepletionRate = 0.3f;


        // units per second
    bool isDrinking = false;



    float drinkTimer = 0.0f;



    float drinkDuration = 2.0f;

    // Food
    FoodBar foodbar;



    float foodDepletionRate = 0.1f;



    bool isEating = false;



    float eatTimer = 0.0f;



    float eatDuration = 2.0f;






    bool movementLocked = false;


    // blocks player input

    // Math dialog
    std::unique_ptr<MathDialog> mathDialog;



    bool isMathDialogActive = false;




    // Methods
    void showMathDialog();



    void onMathAnswer(bool correct);




    void startDrinking();



    void finishDrinking();




    void startEating();



    void finishEating();

    Entity challengerEntity;

    Entity approacherEntity;


    void Attack();




    Entity lastHitEntity = (Entity)-1;



    float hitCooldown = 0.0f;




    Tools::Healthbar healthbar;




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
    Tools::BackgroundMap bgMap;



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



    Entity fruitTree = (Entity)-1;



    Entity puddle = (Entity)-1;


    Entity MathHorseMen = (Entity)-1;


    std::vector<Entity> decorationEntities;



    std::vector<Entity> importantEntities;




    // Controls whether the one-time biome/decoration generation guard
    // (Scene::areBiomesAndEntitiesGenerated) is honored:
    //   true  -> once biomes/decorations are generated for a scene, later
    //            onStart() calls (e.g. after a reload) skip generation.
    //   false -> ignore the saved flag and always (re)generate biomes and
    //            decoration entities on every onStart(), useful for
    //            iterating on generation logic during development.
    bool shouldStopRegeneration = true;




    void buildBiomeGrid();



    void rebuildBgMap();



    // Returns a pointer into `biomes` for whichever cell contains the
    // given world-space point, or nullptr if it falls outside every
    // configured cell. Used to tell the battle scene which backdrop
    // matches where the encounter started (see the enemyCutScenesFinished
    // switch in onUpdate(), game.cpp).
    const Biome* getBiomeAt(float worldX, float worldY) const;



    void spawnRandomDecorations();



    void checkContacts(float dt);



    float puddleCooldown = 0.0f;


   // prevents multiple triggers per frame

    Entity player = (Entity)-1;

    Entity background = (Entity)-1;

    Entity path0deg = (Entity)-1;

    Entity path90deg = (Entity)-1;

    void onStart() override;



    void onUpdate(float dt) override;



    void onDraw() override;



    void onEnd() override;



    void onEvent(const SDL_Event& e) override;




private:
    float elapsed = 0.0f;



};