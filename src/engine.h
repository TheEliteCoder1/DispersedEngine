#pragma once
#include <iostream>
#include <string>
#include <cstdlib>
#include <filesystem> 
#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <json.hpp>
#include <fstream>
#include <memory>
#include <cstdio>
#include <stdexcept>
#include <array>
#include <tuple>
#include <stdio.h>
#include <sstream>
#include <string_view>
#include <SDL3/SDL.h>
#include <random>
#include "physics.h"
#include "music.h"
#include "texture.h"
#include "font.h"
#include "transpile.h"

// ============================================================
// Platform Detection and Path Helpers
// ============================================================
#ifdef __EMSCRIPTEN__
#define PLATFORM_EMSCRIPTEN 1
#ifdef PROJECT_SCRIPT_BUILD
    // Project scripts on web: mounted at /FirstProject/
    const std::string PATH_PREFIX_PROJECTS = "/";
    const std::string PATH_PREFIX_ASSETS = "/";
    const std::string PATH_PREFIX_ENGINE = "/";
#else
    // Editor on web: projects mounted at /projects/, assets at /assets/
    const std::string PATH_PREFIX_PROJECTS = "/projects/";
    const std::string PATH_PREFIX_ASSETS = "/assets/";
    const std::string PATH_PREFIX_ENGINE = "/";
#endif

#elif defined(_WIN32) || defined(_WIN64)
#define PLATFORM_WINDOWS 1
#ifdef PROJECT_SCRIPT_BUILD
    // Project executable has the folder copied next to it, so use ./
    const std::string PATH_PREFIX_PROJECTS = "./";
    const std::string PATH_PREFIX_ASSETS = "./";
    const std::string PATH_PREFIX_ENGINE = "./";
#else
    // Editor needs to reach the source 'projects/' folder from build_windows/src/Release/
    const std::string PATH_PREFIX_PROJECTS = "../../../projects/";
    const std::string PATH_PREFIX_ASSETS = "../../../src/assets/";
    const std::string PATH_PREFIX_ENGINE = "../../../";
#endif

#elif defined(__linux__)
#define PLATFORM_LINUX 1
#ifdef PROJECT_SCRIPT_BUILD
    const std::string PATH_PREFIX_PROJECTS = "./";
    const std::string PATH_PREFIX_ASSETS = "./";
    const std::string PATH_PREFIX_ENGINE = "./";
#else
    const std::string PATH_PREFIX_PROJECTS = "./projects/"; // Adjust if your Linux build dir is different
    const std::string PATH_PREFIX_ASSETS = "./assets/";
    const std::string PATH_PREFIX_ENGINE = "./";
#endif

#elif defined(__APPLE__)
#define PLATFORM_MACOS 1
#ifdef PROJECT_SCRIPT_BUILD
    const std::string PATH_PREFIX_PROJECTS = "./";
    const std::string PATH_PREFIX_ASSETS = "./";
    const std::string PATH_PREFIX_ENGINE = "./";
#else
    const std::string PATH_PREFIX_PROJECTS = "./projects/"; // Adjust if your Mac build dir is different
    const std::string PATH_PREFIX_ASSETS = "./assets/";
    const std::string PATH_PREFIX_ENGINE = "./";
#endif

#else
#error "Unsupported platform"
#endif

// Update the helper functions to handle Emscripten's virtual FS better:
inline std::string getProjectsPath(const std::string& relativePath = "") {
#ifdef EMSCRIPTEN
    return PATH_PREFIX_PROJECTS + relativePath;
#else
    return PATH_PREFIX_PROJECTS + relativePath;
#endif
}

inline const std::filesystem::path getProjectsRootForScripts() {
#ifdef __EMSCRIPTEN__
    // On Emscripten, project scripts use root paths
    return std::filesystem::path("/");
#else
    return std::filesystem::path(PATH_PREFIX_PROJECTS);
#endif
}

inline std::string getAssetsPath(const std::string& relativePath = "") {
#ifdef EMSCRIPTEN
    // On Emscripten, assets are preloaded to /assets/
    if (!relativePath.empty() && relativePath[0] == '/') {
        return "/assets/" + relativePath.substr(1);
    }
    return "/assets/" + relativePath;
#else
    return PATH_PREFIX_ASSETS + relativePath;
#endif
}

inline std::string getEnginePath(const std::string& relativePath = "") {
#ifdef EMSCRIPTEN
    if (!relativePath.empty() && relativePath[0] == '/') {
        return "/" + relativePath.substr(1);
    }
    return "/" + relativePath;
#else
    return PATH_PREFIX_ENGINE + relativePath;
#endif
}

struct EngineResources {
    Font::Manager FontManager;
    Texture::Manager TextureManager;
    MusicAndSfx::Manager AudioManager;
    // Maps animation resource name to a list of loaded texture names (frames)
    std::unordered_map<std::string, std::vector<std::string>> animations; 
};

inline EngineResources g_resources;

inline std::filesystem::path getProjectsPathFS(const std::string& relativePath = "") {
    return std::filesystem::path(getProjectsPath(relativePath));
}

// Resolves a .animres/.physicsres path as stored in scene.json into an
// actual filesystem path. Absolute paths (e.g. a file picked from
// somewhere outside the project) are used as-is; everything else is
// treated as relative to the project root, mirroring how scriptAttached
// is resolved elsewhere in the engine.
inline std::string resolveComponentResourcePath(const std::string& projectsRoot, const std::string& storedPath) {
    if (storedPath.empty()) return storedPath;

    std::string path = storedPath;

#ifdef __EMSCRIPTEN__
    // Normalise backslashes to forward slashes
    std::replace(path.begin(), path.end(), '\\', '/');

    // If path starts with the project name (last component of projectsRoot), strip it
    std::string projectName = std::filesystem::path(projectsRoot).filename().string();
    if (!projectName.empty() && path.find(projectName + "/") == 0) {
        path = path.substr(projectName.size() + 1);
    }

    // If path is now absolute (starts with '/'), keep it; otherwise prepend projectsRoot
    if (!path.empty() && path[0] == '/') {
        return path;
    }
#endif

    std::filesystem::path p(path);
    if (p.is_absolute()) {
        return path;
    }
    return (std::filesystem::path(projectsRoot) / p).lexically_normal().generic_string();
}

using Entity = uint32_t;
inline const size_t MAX_ENTITIES = 1000000;
inline Entity lastSelectedEntity = (Entity)-1;

enum EditMode {
    Select = 0,
    MoveWithMouse,
    Delete,
    Dialog
};

inline EditMode currentEditMode = EditMode::Select;
// Remembers the mode that was active before a Dialog was opened, so it can be
// restored once the dialog closes (otherwise the editor gets stuck in Dialog
// mode forever, and edit_object_with_editor_mouse/gamepad both bail out early
// whenever currentEditMode == Dialog, silently disabling select/move/delete).
inline EditMode modeBeforeDialog = EditMode::Select;

enum SelectionMode {
    SingleSelect = 0,
    MultiSelect
};
inline SelectionMode currentSelectionmode = SelectionMode::SingleSelect;

// Editor scroll offsets and viewport (updated each frame in main.cpp)
inline float editorScrollX = 0.0f;
inline float editorScrollY = 0.0f;
inline bool inspectorVisible = true;

inline float canvasViewX = 105.0f;
inline float canvasViewY = 105.0f;
inline float canvasViewW = 1390.0f;
inline float canvasViewH = 690.0f;
inline bool editor_showGrid = false;
inline float editor_cellW = 32.0f;
inline float editor_cellH = 32.0f;
inline bool editor_isDrawingPolygon = false;

inline bool isInsideCanvas(float x, float y) {
    return x >= canvasViewX && x <= canvasViewX + canvasViewW &&
           y >= canvasViewY && y <= canvasViewY + canvasViewH;
}

inline bool isDraggingLeftMouse = false;
inline int lastDragX = 0;
inline int lastDragY = 0;

inline bool isGamepadDragging = false;
inline bool gamepadDidDrag = false;
inline float lastGamepadCursorX = 0.0f;
inline float lastGamepadCursorY = 0.0f;

struct VertexDragState {
    Entity target = (Entity)-1;
    int vertexIndex = -1;
    float startMouseX = 0.0f, startMouseY = 0.0f;
    float startVertexX = 0.0f, startVertexY = 0.0f;
};
inline VertexDragState vertexDrag;
inline bool isDraggingVertex = false;

// --- Tilemap cell move (hold S + right-click drag on a painted cell) ---
struct TileCellDragState {
    bool active = false;
    Entity target = (Entity)-1;
    int cellIndex = -1;       // index into tm.cells / tm.cellOffsets being moved
    float grabDX = 0.0f;      // cursor position relative to the cell's top-left
    float grabDY = 0.0f;      // at the moment the drag started, in world units
};
inline TileCellDragState tileCellDrag;

static constexpr float LOW_HEALTH_THRESHOLD = 0.3f;

namespace Components {
    struct Metadata { std::string name; };
    struct Position { float x = 0.0f, y = 0.0f; };
    struct RectangleShape { float w = 50.0f, h = 50.0f; };
    struct ZIndex { int z = 0; };
    struct Selection {
        bool isSelected = false;
        SDL_Color selectionColor = {0, 255, 0, 255};
    };

    struct TextureRef {
        std::string resourceName;
        SDL_FRect sourceRect = {0, 0, 0, 0};
    };

    struct SfxEmitter {
        std::string sfxName;
        float volume = 1.0f;
        float pitch = 1.0f;   // Added for runtime pitch shifting
        float speed = 1.0f;   // Added for runtime speed shifting
        bool playOnCollision = false;
    };

    struct PhysicsBodyDef {
        Physics::ShapeType shapeType = Physics::ShapeType::Rectangle;
        b2BodyType bodyType  = b2_dynamicBody;
        float width = 50.0f;
        float height = 50.0f;
        float radius = 25.0f;
        std::vector<b2Vec2> polygonPoints;
        float density = 1.0f;
        bool isSensor = false;
        uint16_t category = Physics::LAYER_1;
        uint16_t mask = Physics::LAYER_ALL;
        b2BodyId bodyId = b2_nullBodyId;
    };

    struct Rotation {
        float degrees = 0.0f;
    };

    struct Scale {
        float x = 1.0f;
        float y = 1.0f;
    };

    struct Position3D {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };


    // ================================================================
    // 3D-mode-only components (Engine3D::CanvasMode::Mode3D).
    // Mirror the 2D RectangleShape/TextureRef pattern: the entity is
    // "just data" here, Engine3D + Physics3D do the actual GPU/physics
    // work driven by this data. Harmless to have on an entity in a 2D
    // build too (never read), so no #ifdef needed around the struct
    // definitions themselves -- only around where they're *used* for
    // rendering (main.cpp already gates all 3D rendering on
    // `if constexpr (g_canvasMode == Engine3D::CanvasMode::Mode3D)`).
    // ================================================================

    // Third extent to pair with RectangleShape's w/h when an entity is
    // being edited in 3D mode -- w=X size, h=Y size, depth=Z size.
    struct Depth3D {
        float depth = 50.0f;
    };

    // Points at a 3D model on disk (.obj/.fbx/.gltf/.glb) that
    // Engine3D::ModelCache3D will load (and de-duplicate by path) on
    // first use. Leave modelPath empty to fall back to the default
    // white wireframe cube outline (see render_entities_3d in
    // engine3d.h) instead of a real mesh -- this is the "basic 3D
    // entity" look, same role the empty rectangle outline plays for
    // un-textured 2D entities.
    struct Mesh3DRef {
        std::string modelPath;
        // Which sub-mesh's material to override, or -1 to leave every
        // submesh using whatever materials the model file itself
        // defined (typical case). Sized for the (currently rare) case
        // of wanting one flat tint across the whole model.
        int materialOverrideSubmesh = -1;
        // Separate dimensions for the mesh itself (0 = use native model size).
        // These are independent from the outline dimensions (width/height/depth).
        float meshW = 0.0f;
        float meshH = 0.0f;
        float meshD = 0.0f;
    };

    // Optional flat-color/texture override, applied on top of (or
    // instead of, if the model had none) whatever Engine3D::Material3D
    // came from the model file.
    struct Material3DRef {
        float r = 0.8f, g = 0.8f, b = 0.8f, a = 1.0f;
        std::string diffuseTexturePath; // empty = keep the model's own texture
        bool overrideColor = false;     // false = use the model's own diffuseColor
    };

    // Declarative 3D collider setup for Physics3D -- mirrors
    // PhysicsBodyDef's role on the 2D side, minus the runtime JPH
    // handle (that lives in a Physics3D::PhysicsBody3D owned by
    // Physics3D::PhysicsWorld3D, looked up by entity id, same split
    // physics_sync_system already uses for Box2D).
    struct Collider3DRef {
        enum class Shape { Box, Sphere, Capsule, ConvexHull, Mesh, RiggedApprox } shape = Shape::Box;
        enum class Motion { Static, Kinematic, Dynamic } motion = Motion::Static;
        // Plain floats, not glm::vec3: engine.h intentionally stays
        // glm-independent (glm is only pulled in by engine3d.h /
        // physics3d.h / main.cpp for actual 3D math). Physics3D::
        // BuildColliderFromRef() converts these to glm on the way in.
        float halfExtentsX = 0.5f, halfExtentsY = 0.5f, halfExtentsZ = 0.5f; // Box
        float radius = 0.5f;                     // Sphere / Capsule
        float halfHeight = 0.5f;                 // Capsule
        float density = 1000.0f;
        bool isSensor = false;
        uint16_t category = 0x0001;
        uint16_t mask = 0xFFFF;
        // For Shape::ConvexHull/Mesh/RiggedApprox: build the collider
        // from the entity's own Mesh3DRef model instead of a primitive.
        // RiggedApprox additionally decimates + re-attaches a capsule
        // chain per bone if the model is skinned -- see
        // Physics3D::BuildRiggedApproxCollider in physics3d.h.
        int maxHullPoints = 64; // decimation target for ConvexHull/RiggedApprox
    };

    // A single named animation (e.g. "idle", "walk", "attack"). Each clip owns
    // its own frame list and playback state so switching clips doesn't stomp
    // on another clip's progress.
    struct AnimationClip {
        std::string name = "default";
        float speed = 10.0f;
        float timer = 0.0f;
        int currentFrame = 0;
        bool isPlaying = true;
        bool loop = true; // when false, the clip halts on its last frame instead of wrapping

        // Only image frames are supported now
        std::vector<std::string> imageFrameResources;

        float frameWidth = 0.0f;
        float frameHeight = 0.0f;
    };

    // Holds one or more named AnimationClips and tracks which one is
    // currently active/playing. Kept backwards-compatible in spirit with the
    // old single-animation component: active() gives direct access to the
    // clip that's currently playing, so `.speed`, `.isPlaying`,
    // `.imageFrameResources` etc. still work by going through active().
    struct AnimationState {
        std::vector<AnimationClip> clips = { AnimationClip{} };
        int activeClipIndex = 0;

        AnimationClip& active() {
            if (clips.empty()) clips.push_back(AnimationClip{});
            if (activeClipIndex < 0 || activeClipIndex >= (int)clips.size()) activeClipIndex = 0;
            return clips[activeClipIndex];
        }
        const AnimationClip& active() const {
            static const AnimationClip fallback{};
            if (clips.empty()) return fallback;
            size_t idx = (activeClipIndex < 0 || activeClipIndex >= (int)clips.size()) ? 0 : (size_t)activeClipIndex;
            return clips[idx];
        }

        AnimationClip* find(const std::string& name) {
            for (auto& c : clips) if (c.name == name) return &c;
            return nullptr;
        }
        const AnimationClip* find(const std::string& name) const {
            for (auto& c : clips) if (c.name == name) return &c;
            return nullptr;
        }

        // Finds or creates a clip with the given name.
        AnimationClip& getOrCreate(const std::string& name) {
            if (auto* c = find(name)) return *c;
            clips.push_back(AnimationClip{});
            clips.back().name = name;
            return clips.back();
        }

        // Switches playback to the named clip. Returns false if not found.
        // Resets the clip's frame/timer by default so it starts clean.
        // `loop` controls whether animation_system() wraps back to frame 0
        // after the last frame (true, the default) or halts on the last
        // frame and stops playing (false).
        bool play(const std::string& name, bool resetFrame = true, bool loop = true) {
            for (size_t i = 0; i < clips.size(); ++i) {
                if (clips[i].name == name) {
                    activeClipIndex = (int)i;
                    if (resetFrame) { clips[i].currentFrame = 0; clips[i].timer = 0.0f; }
                    clips[i].isPlaying = true;
                    clips[i].loop = loop;
                    return true;
                }
            }
            return false;
        }

        std::string activeName() const { return active().name; }

        std::pair<std::string, SDL_FRect> getCurrentFrame() const {
            const AnimationClip& c = active();
            if (c.imageFrameResources.empty()) return {"", {0,0,0,0}};
            size_t idx = (size_t)c.currentFrame % c.imageFrameResources.size();
            return {c.imageFrameResources[idx], {0,0,0,0}};
        }
    };

    struct ColorMod {
        float alpha = 1.0f;          // 0.0 .. 1.0
        SDL_Color tint = {255,255,255,255};   // (not used yet)
    };

    struct TileMap {
        std::string textureName;
        float cellWidth = 32.0f;
        float cellHeight = 32.0f;
        std::vector<std::pair<int,int>> cells;
        std::vector<SDL_FPoint> cellOffsets;
        bool snapEnabled = true;
    };

    struct GridCell3D {
        int x = 0, y = 0, z = 0;
        float offX = 0.0f, offY = 0.0f, offZ = 0.0f;
    };

    struct GridMap3D {
        std::string materialName;  // texture or material resource
        float cellWidth = 1.0f;
        float cellHeight = 1.0f;
        float cellDepth = 1.0f;
        std::vector<GridCell3D> cells;
        bool snapEnabled = true;
    };
}

// The "Shape" spinbox in the inspector uses a fixed UI order (0=Rectangle,
// 1=Circle, 2=Polygon) that is intentionally decoupled from
// Physics::ShapeType's own enum ordering (Circle=0, Triangle=1,
// Rectangle=2, Polygon=3, Chain=4). Previously the spinbox value was cast
// straight to Physics::ShapeType, so 0 showed Circle, 1 showed Triangle
// (nothing was drawn for it), and 2 showed Rectangle instead of Polygon.
// Always go through these two helpers instead of casting directly.
inline Physics::ShapeType SpinboxIndexToShapeType(int idx) {
    switch (idx) {
        case 0:  return Physics::ShapeType::Rectangle;
        case 1:  return Physics::ShapeType::Circle;
        case 2:  return Physics::ShapeType::Polygon;
        default: return Physics::ShapeType::Rectangle;
    }
}
inline int ShapeTypeToSpinboxIndex(Physics::ShapeType t) {
    switch (t) {
        case Physics::ShapeType::Rectangle: return 0;
        case Physics::ShapeType::Circle:    return 1;
        case Physics::ShapeType::Polygon:   return 2;
        default:                            return 0;
    }
}

// Default triangle used when a shape is switched to Polygon with no points
// yet defined. Points are stored TOP-LEFT relative (range [0,w] x [0,h]),
// matching both the mouse-click polygon editor and render_physics_shape_overlay,
// which subtract width*0.5/height*0.5 from each stored point before drawing.
inline std::vector<b2Vec2> MakeDefaultTrianglePoints(float w, float h) {
    return { { w * 0.5f, 0.0f }, { w, h }, { 0.0f, h } };
}

inline float g_canvasLogicalWidth = 1390.0f;
inline float g_canvasLogicalHeight = 690.0f;



// NOTE: Entities live in an "infinite" world-space canvas driven entirely by
// g_editorCamera (pan/zoom/rotation) and the ruler/axis system built on top
// of it. There is no reason a world position should ever be capped to the
// size of the default canvas viewport (previously 0..g_canvasLogicalWidth /
// 0..g_canvasLogicalHeight) -- that rectangle only happened to match the
// on-screen canvas size at zoom=1 with no panning, and broke down completely
// as soon as you panned or zoomed, making it impossible to move an entity to
// any position the camera could actually see. This is intentionally a no-op
// now; entities are free to be placed anywhere in world-space. Kept as a
// function (rather than removed) so existing call sites don't need to
// change, and so a future, genuinely camera-aware bound (e.g. clamping to
// the *currently visible* world-space rect via camera.getVisibleWorldBounds)
// can be reintroduced here later if ever desired.
inline void clamp_entity_position_to_canvas(Components::Position& pos, float entityWidth = 50.0f, float entityHeight = 50.0f) {
    (void)pos; (void)entityWidth; (void)entityHeight;
}


inline void clamp_guiElem_position_to_canvas(SDL_FPoint& pos, float guiElemWidth, float guiElemHeight) {
    float minX = 0.0f;
    float minY = 0.0f;
    float maxX = g_canvasLogicalWidth - guiElemWidth;
    float maxY = g_canvasLogicalHeight - guiElemHeight;
    
    if (maxX >= minX) pos.x = std::clamp(pos.x, minX, maxX);
    if (maxY >= minY) pos.y = std::clamp(pos.y, minY, maxY);
}

// Normalizes a filepath so "./x.animres" and "x.animres" (etc.) compare
// equal. Falls back to the raw string if the path doesn't resolve yet
// (e.g. about to be written for the first time). Shared by
// ComponentResourceManager's cache keys and ECSWorld's broadcast helpers
// so both agree on when two entities are "pointing at the same file".
inline std::string canonicalComponentResourcePath(const std::string& filepath) {
    std::error_code ec;
    auto canon = std::filesystem::weakly_canonical(filepath, ec);
    return ec ? filepath : canon.string();
}

// Resolves every per-frame image path inside an AnimationState's clips
// against the project root, same convention as .animres/.physicsres/
// TextureRef paths. Call this right after parsing an .animres file (or a
// scene's inline legacy AnimationState block) so the in-memory frame paths
// are always real, loadable filesystem paths -- TextureManager loads them
// directly with no resolution step of its own.
inline void resolveAnimationFramePaths(Components::AnimationState& anim, const std::string& projectRoot) {
    for (auto& clip : anim.clips)
        for (auto& frame : clip.imageFrameResources)
            frame = resolveComponentResourcePath(projectRoot, frame);
}

// Inverse of resolveAnimationFramePaths: turns each frame's absolute
// in-memory path back into one relative to the project root before it's
// written out to disk, so .animres files (and scenes using the legacy
// inline format) stay portable instead of baking in one machine's absolute
// folder layout. Operates on a copy the caller passes in -- never call
// this on the entity's live AnimationState, since rendering needs the
// absolute in-memory paths to keep working.
inline void relativizeAnimationFramePaths(Components::AnimationState& anim, const std::string& projectRoot) {
    for (auto& clip : anim.clips) {
        for (auto& frame : clip.imageFrameResources) {
            if (frame.empty()) continue;
            std::string rel = std::filesystem::relative(frame, projectRoot).string();
            if (!rel.empty()) frame = rel; // fallback: keep absolute if relative() fails
        }
    }
}

// ============================================================
// ComponentResourceManager
// ------------------------------------------------------------
// .animres and .physicsres are just JSON files holding exactly the same
// fields the old inline "AnimationState": { ... } / "PhysicsBody": { ... }
// blocks used to hold directly in scene.json. This manager is a flyweight
// cache keyed by filepath: entities that reference the same resource FILE
// only pay the disk-read + JSON-parse cost once (on the first load), and
// only pay the disk-write cost once per save pass, no matter how many
// entities point at that file.
//
// A note on pointer type: multiple entities sharing one object cannot be
// modeled with std::unique_ptr — by definition only one unique_ptr can own
// an object at a time, so handing the "same" unique_ptr to a second entity
// would either fail to transpile (it's move-only) or leave the first entity
// holding a null pointer. std::shared_ptr is the smart pointer built for
// "N owners, one object, freed automatically once the last owner lets go"
// — exactly this situation — so that's what load*() returns below.
// Nothing is ever manually deleted; in C++17 the shared_ptr's control
// block frees the resource automatically once the last reference
// (including the one held in this manager's own cache) goes away.
// ============================================================
class ComponentResourceManager {
public:
    // Loads (or returns the already-cached) AnimationState for a given
    // .animres filepath. A second/third/Nth caller with the same filepath
    // gets the exact same shared_ptr instance back — no disk I/O, no
    // re-parse.
    std::shared_ptr<Components::AnimationState> loadAnimation(const std::string& filepath, const std::string& projectRoot = "") {
        std::string key = canonicalKey(filepath);
        auto it = animationCache.find(key);
        if (it != animationCache.end()) return it->second;

        std::ifstream in(filepath);
        if (!in.is_open())
            throw std::runtime_error("ComponentResourceManager: cannot open animation resource '" + filepath + "'");
        nlohmann::json j;
        in >> j;

        auto anim = std::make_shared<Components::AnimationState>(parseAnimationJson(j));
        resolveAnimationFramePaths(*anim, projectRoot);
        animationCache[key] = anim;
        return anim;
    }

    // Loads (or returns the already-cached) PhysicsBodyDef for a given
    // .physicsres filepath. Same de-dupe behavior as loadAnimation().
    std::shared_ptr<Components::PhysicsBodyDef> loadPhysics(const std::string& filepath) {
        std::string key = canonicalKey(filepath);
        auto it = physicsCache.find(key);
        if (it != physicsCache.end()) return it->second;

        std::ifstream in(filepath);
        if (!in.is_open())
            throw std::runtime_error("ComponentResourceManager: cannot open physics resource '" + filepath + "'");
        nlohmann::json j;
        in >> j;

        auto phys = std::make_shared<Components::PhysicsBodyDef>(parsePhysicsJson(j));
        physicsCache[key] = phys;
        return phys;
    }

    // Writes `data` to `filepath` as a .animres file, but only the FIRST
    // time it's asked to for that path during the current save pass (see
    // beginSavePass()). Every later entity sharing the same path is a
    // no-op write, since the file on disk would end up byte-identical
    // anyway — this is where "reduced save times" actually comes from.
    void saveAnimation(const std::string& filepath, const Components::AnimationState& data, const std::string& projectRoot = "") {
        std::string key = canonicalKey(filepath);
        if (writtenThisPass.count(key)) return;
        writtenThisPass.insert(key);

        // Relativize a COPY for the on-disk JSON; the cache (and thus every
        // entity sharing this resource) keeps the absolute in-memory paths
        // TextureManager needs to actually load the frames.
        Components::AnimationState toSave = data;
        relativizeAnimationFramePaths(toSave, projectRoot);

        std::ofstream out(filepath);
        if (!out.is_open())
            throw std::runtime_error("ComponentResourceManager: cannot write animation resource '" + filepath + "'");
        out << serializeAnimationJson(toSave).dump(4);

        // Refresh the cache so a load() later in the same run sees the
        // freshly saved data instead of a stale copy.
        animationCache[key] = std::make_shared<Components::AnimationState>(data);
    }

    // Writes `data` to `filepath` as a .physicsres file. Same
    // one-write-per-save-pass de-dupe as saveAnimation().
    void savePhysics(const std::string& filepath, const Components::PhysicsBodyDef& data) {
        std::string key = canonicalKey(filepath);
        if (writtenThisPass.count(key)) return;
        writtenThisPass.insert(key);

        std::ofstream out(filepath);
        if (!out.is_open())
            throw std::runtime_error("ComponentResourceManager: cannot write physics resource '" + filepath + "'");
        out << serializePhysicsJson(data).dump(4);

        physicsCache[key] = std::make_shared<Components::PhysicsBodyDef>(data);
    }

    // Call once at the start of a save pass (top of saveToFile) so each
    // unique resource path gets exactly one disk write during that pass,
    // instead of being permanently "already written" forever.
    void beginSavePass() { writtenThisPass.clear(); }

    // Drops all cached resources. Call when closing/switching projects so
    // stale data from the old project can't leak into the new one.
    void clear() {
        animationCache.clear();
        physicsCache.clear();
        writtenThisPass.clear();
    }

    size_t animationCacheSize() const { return animationCache.size(); }
    size_t physicsCacheSize()   const { return physicsCache.size(); }

private:
    std::unordered_map<std::string, std::shared_ptr<Components::AnimationState>> animationCache;
    std::unordered_map<std::string, std::shared_ptr<Components::PhysicsBodyDef>> physicsCache;
    std::unordered_set<std::string> writtenThisPass;

    // Normalizes a filepath so "./x.animres" and "x.animres" (etc.) hit the
    // same cache entry. Falls back to the raw string if the path doesn't
    // resolve yet (e.g. about to be written for the first time).
    static std::string canonicalKey(const std::string& filepath) {
        std::error_code ec;
        auto canon = std::filesystem::weakly_canonical(filepath, ec);
        return ec ? filepath : canon.string();
    }

public:

    static Components::AnimationState parseAnimationJson(const nlohmann::json& j) {
        Components::AnimationState anim;
        anim.clips.clear();
        if (j.contains("clips") && j["clips"].is_array()) {
            for (const auto& clipJson : j["clips"]) {
                Components::AnimationClip clip;
                clip.name = clipJson.value("name", "default");
                clip.speed = clipJson.value("speed", 10.0f);
                clip.isPlaying = clipJson.value("isPlaying", true);
                clip.loop = clipJson.value("loop", true);
                clip.imageFrameResources = clipJson.value("imageFrames", std::vector<std::string>());
                clip.frameWidth = clipJson.value("frameWidth", 0.0f);
                clip.frameHeight = clipJson.value("frameHeight", 0.0f);
                anim.clips.push_back(std::move(clip));
            }
        }
        if (anim.clips.empty()) anim.clips.push_back(Components::AnimationClip{});
        anim.activeClipIndex = j.value("activeClipIndex", 0);
        if (anim.activeClipIndex < 0 || anim.activeClipIndex >= (int)anim.clips.size())
            anim.activeClipIndex = 0;
        return anim;
    }

    static nlohmann::json serializeAnimationJson(const Components::AnimationState& data) {
        nlohmann::json j;
        nlohmann::json clipsJson = nlohmann::json::array();
        for (const auto& clip : data.clips) {
            nlohmann::json clipJson;
            clipJson["name"] = clip.name;
            clipJson["speed"] = clip.speed;
            clipJson["isPlaying"] = clip.isPlaying;
            clipJson["loop"] = clip.loop;
            clipJson["imageFrames"] = clip.imageFrameResources;
            // Per-clip frame size override. parseAnimationJson() already reads
            // these back (defaulting to 0.0f i.e. "use the texture's native
            // size" when absent) -- they just weren't being written here, so
            // any width/height set in the Inspector was silently lost on the
            // next save/load round-trip through the shared .animres cache.
            clipJson["frameWidth"] = clip.frameWidth;
            clipJson["frameHeight"] = clip.frameHeight;
            clipsJson.push_back(std::move(clipJson));
        }
        j["clips"] = std::move(clipsJson);
        j["activeClipIndex"] = data.activeClipIndex;
        return j;
    }

    static Components::PhysicsBodyDef parsePhysicsJson(const nlohmann::json& j) {
        Components::PhysicsBodyDef phys;
        phys.shapeType = (Physics::ShapeType)j.value("shapeType", (int)Physics::ShapeType::Rectangle);
        phys.bodyType  = (b2BodyType)j.value("bodyType", (int)b2_dynamicBody);
        phys.width     = j.value("width", 50.0f);
        phys.height    = j.value("height", 50.0f);
        phys.radius    = j.value("radius", 25.0f);
        phys.density   = j.value("density", 1.0f);
        phys.isSensor  = j.value("isSensor", false);
        phys.category  = j.value("category", (uint16_t)Physics::LAYER_1);
        phys.mask      = j.value("mask", (uint16_t)Physics::LAYER_ALL);
        phys.polygonPoints.clear();
        if (j.contains("polygonPoints") && j["polygonPoints"].is_array()) {
            for (const auto& pt : j["polygonPoints"])
                phys.polygonPoints.push_back({ pt.value("x", 0.0f), pt.value("y", 0.0f) });
        }
        // bodyId is intentionally NOT part of the resource file: it's a
        // runtime Box2D handle, not saved data, and physics_sync_system
        // re-creates it fresh for every entity regardless of where the
        // rest of the def came from.
        phys.bodyId = b2_nullBodyId;
        return phys;
    }

    static nlohmann::json serializePhysicsJson(const Components::PhysicsBodyDef& data) {
        nlohmann::json j;
        j["shapeType"] = (int)data.shapeType;
        j["bodyType"]  = (int)data.bodyType;
        j["width"]     = data.width;
        j["height"]    = data.height;
        j["radius"]    = data.radius;
        j["density"]   = data.density;
        j["isSensor"]  = data.isSensor;
        j["category"]  = data.category;
        j["mask"]      = data.mask;
        nlohmann::json pts = nlohmann::json::array();
        for (const auto& p : data.polygonPoints)
            pts.push_back({ {"x", p.x}, {"y", p.y} });
        j["polygonPoints"] = pts;
        return j;
    }
};



// Single shared instance, engine-wide. Declared `inline` (a C++17 feature)
// so this header can be included from multiple translation units without
// duplicate-definition linker errors — the same trick used for the other
// `inline` free functions in this file.
inline ComponentResourceManager g_componentResources;

struct ECSWorld {
    std::vector<uint8_t> has_metadata;
    std::vector<Components::Metadata> metadata_pool;
    std::vector<uint8_t> has_position;
    std::vector<Components::Position> position_pool;
    std::vector<uint8_t> has_rectangle_shape;
    std::vector<Components::RectangleShape> rectangle_shape_pool;
    std::vector<uint8_t> has_z_index;
    std::vector<Components::ZIndex> z_index_pool;
    std::vector<uint8_t> has_selection;
    std::vector<Components::Selection> selection_pool;
    std::vector<uint8_t> has_texture_ref;
    std::vector<Components::TextureRef> texture_ref_pool;
    std::vector<uint8_t> has_animation_state;
    std::vector<Components::AnimationState> animation_state_pool;
    // Path (relative to the project root, or absolute) of the .animres
    // file this entity's AnimationState was loaded from / should be saved
    // to. Empty means the component has no resource file yet (e.g. it was
    // just added in-editor); saveToFile() will generate one the first time
    // it's saved.
    std::vector<std::string> animation_resource_path;
    std::vector<uint8_t> has_sfx_emitter;
    std::vector<Components::SfxEmitter> sfx_emitter_pool;
    std::vector<uint8_t> has_physics_body;
    std::vector<Components::PhysicsBodyDef> physics_body_pool;
    // Same idea as animation_resource_path, but for .physicsres files.
    std::vector<std::string> physics_resource_path;
    std::vector<uint8_t> has_rotation;
    std::vector<Components::Rotation> rotation_pool;
    std::vector<uint8_t> has_scale;
    std::vector<Components::Scale> scale_pool;
    std::vector<uint8_t> has_tilemap;
    std::vector<Components::TileMap> tilemap_pool;

    // ── 3D-mode-only component pools (see Components::Depth3D etc.) ──
    std::vector<uint8_t> has_position3d;
    std::vector<Components::Position3D> position3d_pool;
    std::vector<uint8_t> has_depth3d;
    std::vector<Components::Depth3D> depth3d_pool;
    std::vector<uint8_t> has_mesh3d;
    std::vector<Components::Mesh3DRef> mesh3d_pool;
    std::vector<uint8_t> has_material3d;
    std::vector<Components::Material3DRef> material3d_pool;
    std::vector<uint8_t> has_collider3d;
    std::vector<Components::Collider3DRef> collider3d_pool;
    std::vector<uint8_t> has_gridmap3d;
    std::vector<Components::GridMap3D> gridmap3d_pool;

    size_t entity_count = 0;

    ECSWorld() {
        has_metadata.reserve(MAX_ENTITIES);
        metadata_pool.reserve(MAX_ENTITIES);
        has_position.reserve(MAX_ENTITIES);
        position_pool.reserve(MAX_ENTITIES);
        has_rectangle_shape.reserve(MAX_ENTITIES);
        rectangle_shape_pool.reserve(MAX_ENTITIES);
        has_z_index.reserve(MAX_ENTITIES);
        z_index_pool.reserve(MAX_ENTITIES);
        has_selection.reserve(MAX_ENTITIES);
        selection_pool.reserve(MAX_ENTITIES);
        has_texture_ref.reserve(MAX_ENTITIES);
        texture_ref_pool.reserve(MAX_ENTITIES);
        has_animation_state.reserve(MAX_ENTITIES);
        animation_state_pool.reserve(MAX_ENTITIES);
        animation_resource_path.reserve(MAX_ENTITIES);
        has_sfx_emitter.reserve(MAX_ENTITIES);
        sfx_emitter_pool.reserve(MAX_ENTITIES);
        has_physics_body.reserve(MAX_ENTITIES);
        physics_body_pool.reserve(MAX_ENTITIES);
        physics_resource_path.reserve(MAX_ENTITIES);
        has_rotation.reserve(MAX_ENTITIES);
        rotation_pool.reserve(MAX_ENTITIES);
        has_scale.reserve(MAX_ENTITIES);
        scale_pool.reserve(MAX_ENTITIES);
        has_tilemap.reserve(MAX_ENTITIES);
        tilemap_pool.reserve(MAX_ENTITIES);
        has_depth3d.reserve(MAX_ENTITIES);
        depth3d_pool.reserve(MAX_ENTITIES);
        has_mesh3d.reserve(MAX_ENTITIES);
        mesh3d_pool.reserve(MAX_ENTITIES);
        has_material3d.reserve(MAX_ENTITIES);
        material3d_pool.reserve(MAX_ENTITIES);
        has_collider3d.reserve(MAX_ENTITIES);
        collider3d_pool.reserve(MAX_ENTITIES);
        has_position3d.reserve(MAX_ENTITIES);
        position3d_pool.reserve(MAX_ENTITIES);
        has_gridmap3d.reserve(MAX_ENTITIES);
        gridmap3d_pool.reserve(MAX_ENTITIES);
    }

    Entity create_entity() {
        Entity id = entity_count++;
        has_metadata.push_back(0);
        metadata_pool.emplace_back();
        has_position.push_back(0);
        position_pool.emplace_back();
        has_rectangle_shape.push_back(0);
        rectangle_shape_pool.emplace_back();
        has_z_index.push_back(0);
        z_index_pool.emplace_back();
        has_selection.push_back(0);
        selection_pool.emplace_back();
        has_texture_ref.push_back(0);
        texture_ref_pool.emplace_back();
        has_animation_state.push_back(0);
        animation_state_pool.emplace_back();
        animation_resource_path.emplace_back();
        has_sfx_emitter.push_back(0);
        sfx_emitter_pool.emplace_back();
        has_physics_body.push_back(0);
        physics_body_pool.emplace_back();
        physics_resource_path.emplace_back();
        has_rotation.push_back(0);
        rotation_pool.emplace_back();
        has_scale.push_back(0);
        scale_pool.emplace_back();
        has_tilemap.push_back(0);
        tilemap_pool.emplace_back();
        has_depth3d.push_back(0);
        depth3d_pool.emplace_back();
        has_mesh3d.push_back(0);
        mesh3d_pool.emplace_back();
        has_material3d.push_back(0);
        material3d_pool.emplace_back();
        has_collider3d.push_back(0);
        collider3d_pool.emplace_back();
        has_position3d.push_back(0);
        position3d_pool.emplace_back();
        has_gridmap3d.push_back(0);
        gridmap3d_pool.emplace_back();
        return id;
    }

    void delete_entity(Entity id) {
        if (id >= entity_count) return;
        Entity last = entity_count - 1;
        if (id != last) {
            has_metadata[id] = has_metadata[last];
            metadata_pool[id] = metadata_pool[last];
            has_position[id] = has_position[last];
            position_pool[id] = position_pool[last];
            has_rectangle_shape[id] = has_rectangle_shape[last];
            rectangle_shape_pool[id] = rectangle_shape_pool[last];
            has_z_index[id] = has_z_index[last];
            z_index_pool[id] = z_index_pool[last];
            has_selection[id] = has_selection[last];
            selection_pool[id] = selection_pool[last];
            has_texture_ref[id] = has_texture_ref[last];
            texture_ref_pool[id] = texture_ref_pool[last];
            has_animation_state[id] = has_animation_state[last];
            animation_state_pool[id] = animation_state_pool[last];
            animation_resource_path[id] = animation_resource_path[last];
            has_sfx_emitter[id] = has_sfx_emitter[last];
            sfx_emitter_pool[id] = sfx_emitter_pool[last];
            has_physics_body[id] = has_physics_body[last];
            physics_body_pool[id] = physics_body_pool[last];
            physics_resource_path[id] = physics_resource_path[last];
            has_rotation[id] = has_rotation[last];
            rotation_pool[id] = rotation_pool[last];
            has_scale[id] = has_scale[last];
            scale_pool[id] = scale_pool[last];
            has_tilemap[id] = has_tilemap[last];
            tilemap_pool[id] = tilemap_pool[last];
            has_depth3d[id] = has_depth3d[last];
            depth3d_pool[id] = depth3d_pool[last];
            has_mesh3d[id] = has_mesh3d[last];
            mesh3d_pool[id] = mesh3d_pool[last];
            has_material3d[id] = has_material3d[last];
            material3d_pool[id] = material3d_pool[last];
            has_collider3d[id] = has_collider3d[last];
            collider3d_pool[id] = collider3d_pool[last];
            has_position3d[id] = has_position3d[last];
            position3d_pool[id] = position3d_pool[last];
            has_gridmap3d[id] = has_gridmap3d[last];
            gridmap3d_pool[id] = gridmap3d_pool[last];
        }
        has_metadata.pop_back();
        metadata_pool.pop_back();
        has_position.pop_back();
        position_pool.pop_back();
        has_rectangle_shape.pop_back();
        rectangle_shape_pool.pop_back();
        has_z_index.pop_back();
        z_index_pool.pop_back();
        has_selection.pop_back();
        selection_pool.pop_back();
        has_texture_ref.pop_back();
        texture_ref_pool.pop_back();
        has_animation_state.pop_back();
        animation_state_pool.pop_back();
        animation_resource_path.pop_back();
        has_sfx_emitter.pop_back();
        sfx_emitter_pool.pop_back();
        has_physics_body.pop_back();
        physics_body_pool.pop_back();
        physics_resource_path.pop_back();
        has_rotation.pop_back();
        rotation_pool.pop_back();
        has_scale.pop_back();
        scale_pool.pop_back();
        has_tilemap.pop_back();
        tilemap_pool.pop_back();
        has_depth3d.pop_back();
        depth3d_pool.pop_back();
        has_mesh3d.pop_back();
        mesh3d_pool.pop_back();
        has_material3d.pop_back();
        material3d_pool.pop_back();
        has_collider3d.pop_back();
        collider3d_pool.pop_back();
        has_position3d.pop_back();
        position3d_pool.pop_back();
        has_gridmap3d.pop_back();
        gridmap3d_pool.pop_back();
        entity_count--;
    }

    void add_metadata(Entity id) { if (id < entity_count) has_metadata[id] = 1; }
    void add_position(Entity id) { if (id < entity_count) has_position[id] = 1; }
    void add_position3d(Entity id) { if (id < entity_count) has_position3d[id] = 1; }
    void add_rectangle_shape(Entity id) { if (id < entity_count) has_rectangle_shape[id] = 1; }
    void add_z_index(Entity id) { if (id < entity_count) has_z_index[id] = 1; }
    void add_selection(Entity id) { if (id < entity_count) has_selection[id] = 1; }
    void add_texture_ref(Entity id) { if (id < entity_count) has_texture_ref[id] = 1; }
    void add_animation_state(Entity id) { if (id < entity_count) has_animation_state[id] = 1; }
    void add_sfx_emitter(Entity id) { if (id < entity_count) has_sfx_emitter[id] = 1; }
    void add_physics_body(Entity id) { if (id < entity_count) has_physics_body[id] = 1; }
    void add_rotation(Entity id) { if (id < entity_count) has_rotation[id] = 1; }
    void add_scale(Entity id)   { if (id < entity_count) has_scale[id] = 1; }
    void add_tilemap(Entity id) { if (id < entity_count) has_tilemap[id] = 1; }
    void add_gridmap3d(Entity id) { if (id < entity_count) has_gridmap3d[id] = 1; }
    void add_depth3d(Entity id)    { if (id < entity_count) has_depth3d[id] = 1; }
    void add_mesh3d(Entity id)     { if (id < entity_count) has_mesh3d[id] = 1; }
    void add_material3d(Entity id) { if (id < entity_count) has_material3d[id] = 1; }
    void add_collider3d(Entity id) { if (id < entity_count) has_collider3d[id] = 1; }

    // Points this entity's AnimationState at the resource loaded from
    // `filepath`. If some other entity already loaded that exact file, the
    // disk read + JSON parse is skipped entirely (ComponentResourceManager
    // returns its cached shared_ptr); only a cheap in-memory copy into this
    // entity's pool slot happens here. `filepath` should already be a
    // resolved filesystem path (see resolveComponentResourcePath).
    void assign_animation_resource(Entity id, const std::string& filepath, const std::string& projectRoot = "") {
        if (id >= entity_count) return;
        auto shared = g_componentResources.loadAnimation(filepath, projectRoot);
        animation_state_pool[id] = *shared;
        animation_resource_path[id] = filepath;
        has_animation_state[id] = 1;
    }

    // Same idea as assign_animation_resource(), but for a .physicsres file
    // and this entity's PhysicsBodyDef.
    void assign_physics_resource(Entity id, const std::string& filepath) {
        if (id >= entity_count) return;
        auto shared = g_componentResources.loadPhysics(filepath);
        physics_body_pool[id] = *shared;
        physics_body_pool[id].bodyId = b2_nullBodyId; // always a fresh per-entity runtime handle
        physics_resource_path[id] = filepath;
        has_physics_body[id] = 1;
    }
};

namespace Tools {

    inline bool contains(std::string_view haystack, std::string_view needle) {
        return haystack.find(needle) != std::string_view::npos;
    }


    // void _DrawCrossHair(SDL_Renderer* renderer, float crossHairX, float crossHairY) 
    // {
    //     SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    //     SDL_FRect crosshair_firstrect = { crossHairX - 10.0f, crossHairY - 1.0f, 20.0f, 2.0f };
    //     SDL_FRect crosshair_secondrect = { crossHairX - 1.0f, crossHairY - 10.0f, 2.0f, 20.0f };
    //     SDL_RenderFillRect(renderer, &crosshair_firstrect); 
    //     SDL_RenderFillRect(renderer, &crosshair_secondrect);
    // }

    class Timer {
    public:

        Timer(float duration = 1.0f, bool loop = false)
            :
            duration(duration),
            elapsed(0.0f),
            running(false),
            loop(loop)
        {}



        void start(float newDuration = -1.0f)
        {
            if(running)
                return;


            if(newDuration > 0)
                duration = newDuration;


            elapsed = 0.0f;
            running = true;
        }



        void stop()
        {
            elapsed = 0.0f;
            running = false;
        }



        void update(float delta)
        {
            if(!running)
                return;


            elapsed += delta;


            if(elapsed >= duration)
            {
                if(loop)
                {
                    elapsed = 0.0f;
                    loopTriggered = true;
                }
                else
                {
                    elapsed = duration;
                    running = false;
                }
            }
        }



        void setLoop(bool value)
        {
            loop = value;
        }



        bool isRunning() const
        {
            return running;
        }



        float progress() const
        {
            if(duration <= 0)
                return 1.0f;

            return elapsed / duration;
        }



        // 0 -> 1 -> 0
        float pulse() const
        {
            return sin(progress() * 3.14159265f);
        }

        bool consumeLoop()
        {
            if(loopTriggered)
            {
                loopTriggered = false;
                return true;
            }

            return false;
        }


    private:

        float duration;
        float elapsed;
        bool running;
        bool loop;
        bool loopTriggered = false;

    };

    class Healthbar {

    public:

        Healthbar(
            float maxHealth = 100.0f,
            float barWidth = 100.0f,
            float barHeight = 12.0f
        )
            :
            maxHealth(maxHealth),
            currentHealth(maxHealth),
            barWidth(barWidth),
            barHeight(barHeight),
            pulseTimer(1.0f, true),
            heartbeatTimer(1.0f, true)
        {}



        void damage(float amount)
        {
            currentHealth =
                std::max(0.0f, currentHealth - amount);
        }



        void heal(float amount)
        {
            currentHealth =
                std::min(maxHealth, currentHealth + amount);
        }



        void update(
            float delta,
            std::function<void()> startHeartbeat = nullptr,
            std::function<void()> stopHeartbeat = nullptr
        )
        {
            float ratio = currentHealth / maxHealth;


            bool shouldHeartbeat =
                ratio <= LOW_HEALTH_THRESHOLD &&
                currentHealth > 0;



            if(shouldHeartbeat && !lowHealthActive)
            {
                lowHealthActive = true;


                pulseTimer.start();


                if(startHeartbeat)
                    startHeartbeat();
            }



            if(!shouldHeartbeat && lowHealthActive)
            {
                lowHealthActive = false;


                pulseTimer.stop();


                if(stopHeartbeat)
                    stopHeartbeat();
            }


            pulseTimer.update(delta);
        }


        float getCurrentVal() const
        {
            return currentHealth;
        }



        float getPercentage() const
        {
            return currentHealth / maxHealth;
        }



        void render(
            SDL_Renderer* renderer,
            float screenX,
            float screenY
        )
        {

            if(currentHealth <= 0)
                return;



            SDL_FRect bgRect =
            {
                screenX,
                screenY,
                barWidth,
                barHeight
            };


            SDL_SetRenderDrawColor(
                renderer,
                40,
                40,
                40,
                255
            );


            SDL_RenderRect(
                renderer,
                &bgRect
            );



            float fillWidth =
                getPercentage() * barWidth;



            SDL_FRect fillRect =
            {
                screenX,
                screenY,
                fillWidth,
                barHeight
            };



            SDL_Color color =
                getHealthColor(
                    getPercentage()
                );



            // Low health red pulse
            if(
                pulseTimer.isRunning() &&
                getPercentage() <= LOW_HEALTH_THRESHOLD
            )
            {

                float pulse =
                    pulseTimer.pulse();



                SDL_Color darkRed =
                {
                    120,
                    0,
                    0,
                    255
                };


                SDL_Color lightRed =
                {
                    255,
                    70,
                    70,
                    255
                };



                color.r =
                    darkRed.r +
                    (lightRed.r - darkRed.r) * pulse;


                color.g =
                    darkRed.g +
                    (lightRed.g - darkRed.g) * pulse;


                color.b =
                    darkRed.b +
                    (lightRed.b - darkRed.b) * pulse;
            }



            SDL_SetRenderDrawColor(
                renderer,
                color.r,
                color.g,
                color.b,
                255
            );


            SDL_RenderFillRect(
                renderer,
                &fillRect
            );
        }



    private:


        static constexpr float LOW_HEALTH_THRESHOLD = 0.30f;
        bool lowHealthActive = false; 


        float maxHealth;
        float currentHealth;


        float barWidth;
        float barHeight;



        Tools::Timer pulseTimer;

        // controls heartbeat timing
        Tools::Timer heartbeatTimer;



        SDL_Color getHealthColor(
            float ratio
        ) const
        {

            if(ratio >= 0.6f)
                return {0,200,0,255};


            if(ratio >= 0.4f)
                return {200,200,0,255};


            if(ratio >= 0.2f)
                return {255,165,0,255};


            return {200,0,0,255};
        }

    };

    struct Camera {
        float targetX  = 0.0f;
        float targetY  = 0.0f;
        float offsetX  = 0.0f;
        float offsetY  = 0.0f;
        float zoom     = 1.0f;
        float rotation = 0.0f;   // degrees, clockwise-positive (raylib convention)

        // ── Coordinate conversion ────────────────────────────────────────
        SDL_FPoint worldToScreen(float wx, float wy) const {
            float dx = wx - targetX;
            float dy = wy - targetY;
            if (rotation != 0.0f) {
                float rad = rotation * 3.14159265f / 180.0f;
                float c = std::cos(rad), s = std::sin(rad);
                float rx = dx * c - dy * s;
                float ry = dx * s + dy * c;
                dx = rx; dy = ry;
            }
            return { dx * zoom + offsetX, dy * zoom + offsetY };
        }

        SDL_FPoint screenToWorld(float sx, float sy) const {
            float dx = (sx - offsetX) / zoom;
            float dy = (sy - offsetY) / zoom;
            if (rotation != 0.0f) {
                float rad = -rotation * 3.14159265f / 180.0f;
                float c = std::cos(rad), s = std::sin(rad);
                float rx = dx * c - dy * s;
                float ry = dx * s + dy * c;
                dx = rx; dy = ry;
            }
            return { dx + targetX, dy + targetY };
        }

        // ── Camera operations ────────────────────────────────────────────
        // Pin the camera's target on the centre of an entity.  Used by game
        // scripts to follow the player, and by the editor to focus on a pick.
        void centerOnEntity(const Components::Position& pos,
                            float entityW = 0.0f, float entityH = 0.0f) {
            targetX = pos.x + entityW * 0.5f;
            targetY = pos.y + entityH * 0.5f;
        }

        // Place the camera so that its `offset` sits at the centre of the
        // given canvas rect.  Call this every frame after the canvas size is
        // known so the camera stays glued to the canvas when the window is
        // resized.
        void setupForCanvas(float canvasX, float canvasY,
                            float canvasW, float canvasH) {
            offsetX = canvasX + canvasW * 0.5f;
            offsetY = canvasY + canvasH * 0.5f;
        }

        // Zoom toward a screen point (keeps the world point under the cursor
        // stable, exactly like raylib's scroll-wheel zoom in CanvasView).
        void zoomToward(float screenX, float screenY, float factor) {
            SDL_FPoint wb = screenToWorld(screenX, screenY);
            zoom = std::clamp(zoom * factor, 0.25f, 10.0f);  // Adjust these values as needed
            SDL_FPoint wa = screenToWorld(screenX, screenY);
            targetX += wb.x - wa.x;
            targetY += wb.y - wa.y;
        }

        // Pan the camera by a screen-space delta (e.g. mouse movement while
        // P is held).  Divides by zoom so the pan speed feels the same at
        // any zoom level.
        void pan(float deltaX, float deltaY) {
            targetX -= deltaX / zoom;
            targetY -= deltaY / zoom;
        }

        // Compute the world-space AABB currently visible inside the canvas.
        // Used by the ruler/grid to know which tick marks to draw.
        void getVisibleWorldBounds(float canvasX, float canvasY,
                                   float canvasW, float canvasH,
                                   float& outL, float& outT,
                                   float& outR, float& outB) const {
            SDL_FPoint tl = screenToWorld(canvasX,        canvasY);
            SDL_FPoint br = screenToWorld(canvasX + canvasW, canvasY + canvasH);
            outL = std::min(tl.x, br.x);
            outR = std::max(tl.x, br.x);
            outT = std::min(tl.y, br.y);
            outB = std::max(tl.y, br.y);
        }

        bool boundsEnabled = false;
        float boundsMinX = 0.0f, boundsMaxX = 0.0f;
        float boundsMinY = 0.0f, boundsMaxY = 0.0f;

        void setBounds(float minX, float maxX, float minY, float maxY, bool enabled = true) {
            boundsMinX = minX; boundsMaxX = maxX;
            boundsMinY = minY; boundsMaxY = maxY;
            boundsEnabled = enabled;
        }

        void clampToBounds(int windowW, int windowH) {
            if (!boundsEnabled || zoom <= 0.0f) return;
            float halfViewW = (windowW * 0.5f) / zoom;
            float halfViewH = (windowH * 0.5f) / zoom;
            float minCenterX = boundsMinX + halfViewW;
            float maxCenterX = boundsMaxX - halfViewW;
            float minCenterY = boundsMinY + halfViewH;
            float maxCenterY = boundsMaxY - halfViewH;
            if (minCenterX > maxCenterX) targetX = (boundsMinX + boundsMaxX) * 0.5f;
            else targetX = std::clamp(targetX, minCenterX, maxCenterX);
            if (minCenterY > maxCenterY) targetY = (boundsMinY + boundsMaxY) * 0.5f;
            else targetY = std::clamp(targetY, minCenterY, maxCenterY);
        }
    };

    // --- Minimap ---
    struct MinimapBiome {
        std::string textureName;
        int gridX = 0;
        int gridY = 0;
        int id = 0;
        // Fallback color if texture not loaded
        Uint8 colorR = 60, colorG = 60, colorB = 60;
    };

    class Minimap {
    public:
        bool show = true;
        float radius = 110.0f;
        float margin = 25.0f;
        float borderWidth = 3.0f;
        
        float worldMinX = 0, worldMaxX = 0, worldMinY = 0, worldMaxY = 0;
        
        // Biome grid support
        std::vector<MinimapBiome> biomes;
        float tileWidth = 0.0f;
        float tileHeight = 0.0f;
        int biomeTileCountX = 1;
        int biomeTileCountY = 1;
        
        void setWorldBounds(float minX, float maxX, float minY, float maxY) {
            worldMinX = minX; worldMaxX = maxX;
            worldMinY = minY; worldMaxY = maxY;
        }
        
        void setBiomeGrid(const std::vector<MinimapBiome>& biomeList,
                        float tW, float tH, int btX, int btY) {
            biomes = biomeList;
            tileWidth = tW;
            tileHeight = tH;
            biomeTileCountX = btX;
            biomeTileCountY = btY;
        }
        
        SDL_FRect getMapRect(int windowW, int windowH) const {
            float mapSize = radius * 2.0f;
            return { (float)windowW - mapSize - margin, (float)windowH - mapSize - margin, mapSize, mapSize };
        }
        
        void render(SDL_Renderer* renderer, const ECSWorld& world, Entity playerEntity, const std::vector<Entity>& importantEntities, int windowW, int windowH) {
            if (!show) return;
            SDL_FRect map = getMapRect(windowW, windowH);
            float worldW = worldMaxX - worldMinX;
            float worldH = worldMaxY - worldMinY;
            if (worldW <= 0.0f || worldH <= 0.0f) return;
            
            float scaleX = map.w / worldW;
            float scaleY = map.h / worldH;
            float scale = std::min(scaleX, scaleY);
            
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(renderer, 20, 20, 30, 200);
            SDL_RenderFillRect(renderer, &map);
            
            // --- Draw biome grid ---
            for (const MinimapBiome& biome : biomes) {
                float bx = worldMinX + biome.gridX * biomeTileCountX * tileWidth;
                float by = worldMinY + biome.gridY * biomeTileCountY * tileHeight;
                float bw = biomeTileCountX * tileWidth;
                float bh = biomeTileCountY * tileHeight;
                
                float mx = map.x + (bx - worldMinX) * scale;
                float my = map.y + (by - worldMinY) * scale;
                float mw = bw * scale;
                float mh = bh * scale;
                
                SDL_Texture* tex = g_resources.TextureManager.Get(biome.textureName);
                if (tex) {
                    SDL_FRect dst = { mx, my, mw, mh };
                    SDL_RenderTexture(renderer, tex, nullptr, &dst);
                } else {
                    SDL_SetRenderDrawColor(renderer, biome.colorR, biome.colorG, biome.colorB, 255);
                    SDL_FRect dst = { mx, my, mw, mh };
                    SDL_RenderFillRect(renderer, &dst);
                }
                
                // Thin border around each biome region
                SDL_SetRenderDrawColor(renderer, 80, 80, 90, 180);
                SDL_FRect border = { mx, my, mw, mh };
                SDL_RenderRect(renderer, &border);
            }
            
            // --- Draw player as red dot ---
            if (playerEntity != (Entity)-1 && world.has_position[playerEntity]) {
                float px = world.position_pool[playerEntity].x;
                float py = world.position_pool[playerEntity].y;
                float pw = world.has_rectangle_shape[playerEntity] ? world.rectangle_shape_pool[playerEntity].w : 0.0f;
                float ph = world.has_rectangle_shape[playerEntity] ? world.rectangle_shape_pool[playerEntity].h : 0.0f;
                float pcx = px + pw * 0.5f;
                float pcy = py + ph * 0.5f;
                float mpx = map.x + (pcx - worldMinX) * scale;
                float mpy = map.y + (pcy - worldMinY) * scale;
                SDL_SetRenderDrawColor(renderer, 255, 50, 50, 255);
                SDL_FRect dot = { mpx - 4.0f, mpy - 4.0f, 8.0f, 8.0f };
                SDL_RenderFillRect(renderer, &dot);
            }

            // Draw important entities as grey dots
            for (Entity e : importantEntities) {
                float px = world.position_pool[e].x;
                float py = world.position_pool[e].y;
                float pw = world.has_rectangle_shape[e] ? world.rectangle_shape_pool[e].w : 0.0f;
                float ph = world.has_rectangle_shape[e] ? world.rectangle_shape_pool[e].h : 0.0f;
                float pcx = px + pw * 0.5f;
                float pcy = py + ph * 0.5f;
                float mpx = map.x + (pcx - worldMinX) * scale;
                float mpy = map.y + (pcy - worldMinY) * scale;
                SDL_SetRenderDrawColor(renderer, 150, 150, 150, 255);
                SDL_FRect dot = { mpx - 2.0f, mpy - 2.0f, 4.0f, 4.0f };
                SDL_RenderFillRect(renderer, &dot);
            }

            // --- Outer border ---
            SDL_SetRenderDrawColor(renderer, 200, 200, 220, 255);
            SDL_RenderRect(renderer, &map);
        }
    };

    // --- TileMap / Background ---
    struct BackgroundTile {
        SDL_FRect rect;
        std::string textureName;
    };

    // --- Culling & Render Order ---
    inline bool aabbOverlap(const SDL_FRect& a, const SDL_FRect& b) {
        return a.x < b.x + b.w && a.x + a.w > b.x &&
            a.y < b.y + b.h && a.y + a.h > b.y;
    }

    class BackgroundMap {
    public:
        std::vector<BackgroundTile> tiles;
        float tileWidth = 0.0f;
        float tileHeight = 0.0f;
        float overlap = 6.0f;
        
        void clear() { tiles.clear(); }
        
        void addRegion(float originX, float originY, float regionW, float regionH, const std::string& textureName) {
            if (tileWidth <= 0 || tileHeight <= 0) return;
            int tilesX = (int)std::ceil(regionW / tileWidth);
            int tilesY = (int)std::ceil(regionH / tileHeight);
            for (int ty = 0; ty < tilesY; ++ty) {
                for (int tx = 0; tx < tilesX; ++tx) {
                    BackgroundTile tile;
                    tile.rect = {
                        originX + tx * tileWidth - overlap * 0.5f,
                        originY + ty * tileHeight - overlap * 0.5f,
                        tileWidth + overlap,
                        tileHeight + overlap
                    };
                    tile.textureName = textureName;
                    tiles.push_back(tile);
                }
            }
        }
        
        void render(SDL_Renderer* renderer, const Camera& camera, const SDL_FRect& visibleWorld) {
            for (const BackgroundTile& tile : tiles) {
                if (!aabbOverlap(tile.rect, visibleWorld)) continue;
                SDL_Texture* tex = g_resources.TextureManager.Get(tile.textureName);
                if (!tex) continue;
                SDL_FPoint screenPos = camera.worldToScreen(tile.rect.x, tile.rect.y);
                SDL_FRect dst = {
                    SDL_floorf(screenPos.x),
                    SDL_floorf(screenPos.y),
                    SDL_ceilf(tile.rect.w * camera.zoom),
                    SDL_ceilf(tile.rect.h * camera.zoom)
                };
                SDL_RenderTexture(renderer, tex, nullptr, &dst);
            }
        }
    };

    inline SDL_FRect computeVisibleWorldRect(const Camera& camera, int windowW, int windowH, float cullMargin = 100.0f) {
        float vl, vt, vr, vb;
        camera.getVisibleWorldBounds(0.0f, 0.0f, (float)windowW, (float)windowH, vl, vt, vr, vb);
        return SDL_FRect{
            vl - cullMargin, vt - cullMargin,
            (vr - vl) + cullMargin * 2.0f, (vb - vt) + cullMargin * 2.0f
        };
    }

    enum class YSortAnchor {
        Top,    // position.y (default, top of sprite)
        Center, // position.y + h*0.5 (middle of sprite)
        Bottom  // position.y + h (feet / bottom of sprite)
    };

    inline std::vector<Entity> buildVisibleRenderOrder(
        const ECSWorld& world,
        const SDL_FRect& visibleWorld,
        const std::vector<Entity>& excludeEntities = {},
        bool ySortEnabled = false,
        YSortAnchor ySortAnchor = YSortAnchor::Top)
    {
        std::vector<Entity> renderOrder;
        renderOrder.reserve(world.entity_count);

        for (Entity i = 0; i < world.entity_count; ++i) {
            if (!world.has_position[i]) continue;

            bool excluded = false;
            for (Entity ex : excludeEntities) {
                if (i == ex) { excluded = true; break; }
            }
            if (excluded) continue;

            // Compute the actual bounding box of the entity.
            // For tilemaps, use the union of all tile cells; otherwise use the rectangle shape.
            SDL_FRect box{0,0,0,0};

            if (world.has_tilemap[i] && !world.tilemap_pool[i].cells.empty()) {
                const auto& tm = world.tilemap_pool[i];
                float minX = FLT_MAX, maxX = -FLT_MAX;
                float minY = FLT_MAX, maxY = -FLT_MAX;

                for (size_t idx = 0; idx < tm.cells.size(); ++idx) {
                    float offX = (idx < tm.cellOffsets.size()) ? tm.cellOffsets[idx].x : 0.0f;
                    float offY = (idx < tm.cellOffsets.size()) ? tm.cellOffsets[idx].y : 0.0f;

                    float cellX = tm.cells[idx].first * tm.cellWidth + offX;
                    float cellY = tm.cells[idx].second * tm.cellHeight + offY;
                    float cellX2 = cellX + tm.cellWidth;
                    float cellY2 = cellY + tm.cellHeight;

                    minX = std::min(minX, cellX);
                    maxX = std::max(maxX, cellX2);
                    minY = std::min(minY, cellY);
                    maxY = std::max(maxY, cellY2);
                }

                // Add entity position offset (tile coords are local to entity origin)
                box.x = world.position_pool[i].x + minX;
                box.y = world.position_pool[i].y + minY;
                box.w = maxX - minX;
                box.h = maxY - minY;
            } else {
                // Fallback to rectangle shape if present, otherwise use zero size (will be skipped)
                float w = world.has_rectangle_shape[i] ? world.rectangle_shape_pool[i].w : 0.0f;
                float h = world.has_rectangle_shape[i] ? world.rectangle_shape_pool[i].h : 0.0f;
                box = { world.position_pool[i].x, world.position_pool[i].y, w, h };
            }

            // Skip culling for zero‑sized boxes (rare, but include them to be safe)
            if (box.w > 0.0f && box.h > 0.0f && !aabbOverlap(box, visibleWorld))
                continue;

            renderOrder.push_back(i);
        }

        if (ySortEnabled) {
            std::sort(renderOrder.begin(), renderOrder.end(), [&](Entity a, Entity b) {
                int za = world.has_z_index[a] ? world.z_index_pool[a].z : 0;
                int zb = world.has_z_index[b] ? world.z_index_pool[b].z : 0;
                if (za != zb) return za < zb;

                auto getY = [&](Entity e) -> float {
                    float y = world.position_pool[e].y;
                    float h = world.has_rectangle_shape[e] ? world.rectangle_shape_pool[e].h : 0.0f;
                    switch (ySortAnchor) {
                        case YSortAnchor::Top:    return y;
                        case YSortAnchor::Center: return y + h * 0.5f;
                        case YSortAnchor::Bottom: return y + h;
                    }
                    return y;
                };

                float ya = getY(a);
                float yb = getY(b);
                return ya < yb;
            });
        } else {
            std::sort(renderOrder.begin(), renderOrder.end(), [&](Entity a, Entity b) {
                int za = world.has_z_index[a] ? world.z_index_pool[a].z : 0;
                int zb = world.has_z_index[b] ? world.z_index_pool[b].z : 0;
                return za < zb;
            });
        }

        return renderOrder;
    }


    // --- Decoration Spawner ---
        inline void scatterDecorations(ECSWorld& world, Entity sourceEntity, int count, 
                                   float minX, float maxX, float minY, float maxY, 
                                   std::mt19937& rng, std::vector<Entity>& outEntities) {
        if (sourceEntity == (Entity)-1) return;
        if (!world.has_position[sourceEntity] || !world.has_rectangle_shape[sourceEntity]) return;
        float w = world.rectangle_shape_pool[sourceEntity].w;
        float h = world.rectangle_shape_pool[sourceEntity].h;
        float effMaxX = maxX - w;
        float effMaxY = maxY - h;
        if (minX > effMaxX) effMaxX = minX;
        if (minY > effMaxY) effMaxY = minY;
        std::uniform_real_distribution<float> distX(minX, effMaxX);
        std::uniform_real_distribution<float> distY(minY, effMaxY);

        // Track positions of entities spawned in this batch to enforce minimum distance
        std::vector<SDL_FPoint> spawnedPositions;
        const float minDistance = w*6;
        const int maxAttempts = 100; // Prevent infinite loops in crowded areas

        for (int n = 0; n < count; ++n) {
            float px = 0.0f, py = 0.0f;
            bool placed = false;
            
            // Try to find a valid spot
            for (int attempt = 0; attempt < maxAttempts; ++attempt) {
                px = distX(rng);
                py = distY(rng);
                
                bool tooClose = false;
                for (const auto& pos : spawnedPositions) {
                    float dx = px - pos.x;
                    float dy = py - pos.y;
                    // Squared distance check is faster and avoids sqrt
                    if (dx * dx + dy * dy < minDistance * minDistance) {
                        tooClose = true;
                        break;
                    }
                }
                
                if (!tooClose) {
                    placed = true;
                    break;
                }
            }
            
            // If the area is too crowded, skip spawning this entity
            if (!placed) continue; 
            
            spawnedPositions.push_back({px, py});

            Entity copy = world.create_entity();
            world.add_position(copy);
            world.position_pool[copy].x = px;
            world.position_pool[copy].y = py;
            world.add_rectangle_shape(copy);
            world.rectangle_shape_pool[copy] = world.rectangle_shape_pool[sourceEntity];
            if (world.has_z_index[sourceEntity]) {
                world.add_metadata(copy);
                world.metadata_pool[copy] = world.metadata_pool[sourceEntity];
                world.metadata_pool[copy].name = world.metadata_pool[sourceEntity].name;
            }
            if (world.has_z_index[sourceEntity]) {
                world.add_z_index(copy);
                world.z_index_pool[copy] = world.z_index_pool[sourceEntity];
            }
            if (world.has_animation_state[sourceEntity]) {
                world.add_animation_state(copy);
                world.animation_state_pool[copy] = world.animation_state_pool[sourceEntity];
                // must do this so all copies only use the same file
                world.animation_resource_path[copy] = world.animation_resource_path[sourceEntity];
            }
            if (world.has_texture_ref[sourceEntity]) {
                world.add_texture_ref(copy);
                world.texture_ref_pool[copy] = world.texture_ref_pool[sourceEntity];
            }
            if (world.has_physics_body[sourceEntity]) {
                world.add_physics_body(copy);
                world.physics_body_pool[copy] = world.physics_body_pool[sourceEntity];
                world.physics_body_pool[copy].bodyId = b2_nullBodyId; // Will be recreated by sync system
                world.physics_resource_path[copy] = world.physics_resource_path[sourceEntity];
            }
            outEntities.push_back(copy);
        }
    }

    inline void cleanupOutOfBoundsDecorations(ECSWorld& world, std::vector<Entity>& entities,
                                          float minX, float maxX, float minY, float maxY) {
        std::vector<Entity> toDestroy;
        for (Entity e : entities) {
            if (e == (Entity)-1 || !world.has_position[e]) continue;
            float px = world.position_pool[e].x;
            float py = world.position_pool[e].y;
            float w = world.has_rectangle_shape[e] ? world.rectangle_shape_pool[e].w : 0.0f;
            float h = world.has_rectangle_shape[e] ? world.rectangle_shape_pool[e].h : 0.0f;
            if (px + w < minX || px > maxX || py + h < minY || py > maxY) {
                toDestroy.push_back(e);
            }
        }

        // world.delete_entity() swaps the last entity into the deleted slot and
        // shrinks the array. That silently reassigns whatever handle equaled
        // `last` to now mean the entity at `id` — any handle still holding the
        // old `last` value is now stale. Rewrite every remaining handle (both
        // in the caller's list and our own destroy queue) immediately after
        // each delete so nothing downstream ever operates on a stale index.
        for (Entity e : toDestroy) {
            Entity last = world.entity_count - 1;
            world.delete_entity(e);
            if (e != last) {
                for (Entity& other : entities)   if (other == last) other = e;
                for (Entity& other : toDestroy)  if (other == last) other = e;
            }
        }

        std::vector<Entity> remaining;
        remaining.reserve(entities.size());
        for (Entity e : entities) {
            bool destroyed = false;
            for (Entity d : toDestroy) { if (e == d) { destroyed = true; break; } }
            if (!destroyed) remaining.push_back(e);
        }
        entities = std::move(remaining);
    }
}


inline void physics_sync_system(ECSWorld& world, Physics::PhysicsWorld& physWorld, Entity player) {
    for (Entity i = 0; i < world.entity_count; ++i) {

        if (world.has_physics_body[i] && world.has_position[i]) {

            auto& def = world.physics_body_pool[i];

            // Detect puddle before creating physics shape
            bool isPuddle = false;

            if (world.has_metadata[i]) {
                isPuddle = Tools::contains(
                    world.metadata_pool[i].name,
                    "puddle"
                );
            }

            // Force sensor flag for puddles
            if (isPuddle) {
                def.isSensor = true;
            }


            if (!b2Body_IsValid(def.bodyId)) {

                float cx = world.position_pool[i].x +
                    (world.has_rectangle_shape[i] ?
                        world.rectangle_shape_pool[i].w :
                        50.0f) * 0.5f;

                float cy = world.position_pool[i].y +
                    (world.has_rectangle_shape[i] ?
                        world.rectangle_shape_pool[i].h :
                        50.0f) * 0.5f;


                auto body = std::make_unique<Physics::PhysicsBody>(
                    physWorld.GetHandle(),
                    def.bodyType,
                    cx,
                    cy
                );


                if (def.shapeType == Physics::ShapeType::Rectangle) {

                    body->AddRectangle(
                        def.width,
                        def.height,
                        def.density,
                        def.category,
                        def.mask,
                        def.isSensor
                    );

                }
                else if (def.shapeType == Physics::ShapeType::Circle) {

                    body->AddCircle(
                        def.radius,
                        def.density,
                        def.category,
                        def.mask,
                        def.isSensor
                    );

                }
                else if (def.shapeType == Physics::ShapeType::Polygon &&
                         !def.polygonPoints.empty()) {


                    float hw = def.width * 0.5f;
                    float hh = def.height * 0.5f;


                    std::vector<b2Vec2> centerRelativePoints;

                    for (const auto& pt : def.polygonPoints) {

                        centerRelativePoints.push_back({
                            pt.x - hw,
                            pt.y - hh
                        });

                    }


                    body->AddConvexPolygon(
                        centerRelativePoints,
                        def.density,
                        def.category,
                        def.mask,
                        def.isSensor
                    );
                }


                def.bodyId = body->GetHandle();

                physWorld.AddOwned(std::move(body));


                //
                // Enable sensor events and attach ECS entity IDs
                //
                b2ShapeId shapes[8];

                int count = b2Body_GetShapes(
                    def.bodyId,
                    shapes,
                    8
                );


                for (int j = 0; j < count; j++) {

                    // Give shape identity for sensor callbacks
                    b2Shape_SetUserData(
                        shapes[j],
                        (void*)(intptr_t)i
                    );


                    // Enable events for sensors
                    if (def.isSensor || i == player) {

                        b2Shape_EnableSensorEvents(
                            shapes[j],
                            true
                        );
                    }
                }
            }
        }
    }
}


inline Tools::Camera g_editorCamera;

enum DialogState { Closed = 0, Opening, Opened, Closing };

namespace Gui {
    class IGuiElement {
    public:
        virtual ~IGuiElement() = default;
        
        // Serialization & Geometry
        virtual std::string getType() const = 0;
        virtual float getX() const = 0;
        virtual float getY() const = 0;
        virtual float getWidth() const = 0;
        virtual float getHeight() const = 0;
        virtual void setPos(SDL_Point p) = 0;
        virtual void setRect(SDL_FRect r) = 0;


        // Editor selection
        bool editorSelected = false;

        // Unified Lifecycle (Signatures must match exactly)
        virtual void render(float offsetX = 0.0f, float offsetY = 0.0f) = 0;
        virtual bool handleEvent(const SDL_Event& e, SDL_Window* window, float offsetX = 0.0f, float offsetY = 0.0f) = 0;
        virtual void handleGamepad(float cursorX, float cursorY, float offsetX, float offsetY, SDL_Window* window, bool confirmDown, bool confirmDownLastFrame) = 0;

        // Draw a selection highlight around this element (called by editor)
        void renderSelectionOutline(SDL_Renderer* renderer, float offsetX = 0.0f, float offsetY = 0.0f) const {
            if (!editorSelected) return;
            SDL_FRect r = { getX() - offsetX - 2, getY() - offsetY - 2, getWidth() + 4, getHeight() + 4 };
            SDL_SetRenderDrawColor(renderer, 255, 200, 0, 255);
            SDL_RenderRect(renderer, &r);
            // corner handles
            const float hs = 6.0f;
            SDL_FRect corners[4] = {
                {r.x - hs*0.5f,           r.y - hs*0.5f,           hs, hs},
                {r.x + r.w - hs*0.5f,     r.y - hs*0.5f,           hs, hs},
                {r.x - hs*0.5f,           r.y + r.h - hs*0.5f,     hs, hs},
                {r.x + r.w - hs*0.5f,     r.y + r.h - hs*0.5f,     hs, hs},
            };
            for (auto& c : corners) SDL_RenderFillRect(renderer, &c);
        }
    };
    struct ITextInput {
        virtual void appendText(const std::string& str) = 0;
        virtual void removeLastChar() = 0;
        virtual bool isActive() const = 0;
        virtual void setActive(bool a) = 0;
        virtual const SDL_FRect& getRect() const = 0;
        virtual ~ITextInput() = default;
        virtual bool isNumericOnly() const { return false; }
    };

    // ----------------------------------------------------------------
    // Scrollbar — supports both vertical and horizontal orientation
    // ----------------------------------------------------------------
    enum class ScrollOrientation { Vertical, Horizontal };

    class Scrollbar {
    public:
        float trackX = 0, trackY = 0, trackW = 10, trackH = 100;
        float contentSize = 0;   // total scrollable content size (width or height)
        float viewSize    = 0;   // visible area size
        float offset      = 0;   // current scroll offset
        ScrollOrientation orientation = ScrollOrientation::Vertical;
        std::function<void(float)> onChange;

        void setOrientation(ScrollOrientation o) { orientation = o; }

        void setOffsetNoCallback(float v) {
            offset = std::clamp(v, 0.0f, maxOffset());
        }

        void setGeometry(float x, float y, float w, float h,
                        float contentSz, float viewSz) {
            trackX = x; trackY = y; trackW = w; trackH = h;
            contentSize = contentSz; viewSize = viewSz;
            offset = std::clamp(offset, 0.0f, maxOffset());
        }

        float maxOffset() const {
            return std::max(0.0f, contentSize - viewSize);
        }

        bool handleEvent(const SDL_Event& ev) {
            if (maxOffset() <= 0) return false;
            if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN && ev.button.button == SDL_BUTTON_LEFT) {
                float mx = ev.button.x, my = ev.button.y;
                SDL_FRect thumb = thumbRect();
                if (inRect(mx, my, thumb)) {
                    dragging = true;
                    dragStart = (orientation == ScrollOrientation::Vertical) ? my : mx;
                    dragStartOffset = offset;
                    return true;
                }
            }
            if (ev.type == SDL_EVENT_MOUSE_BUTTON_UP)   { dragging = false; }
            if (ev.type == SDL_EVENT_MOUSE_MOTION && dragging) {
                float d = (orientation == ScrollOrientation::Vertical)
                        ? ev.motion.y - dragStart
                        : ev.motion.x - dragStart;
                float trackSize = (orientation == ScrollOrientation::Vertical)
                                ? trackH - thumbSize()
                                : trackW - thumbSize();
                float ratio = trackSize > 0 ? d / trackSize : 0;
                setOffset(dragStartOffset + ratio * maxOffset());
                return true;
            }
            if (ev.type == SDL_EVENT_MOUSE_WHEEL) {
                float mx, my; SDL_GetMouseState(&mx, &my);
                SDL_FRect track = { trackX, trackY, trackW, trackH };
                if (!inRect(mx, my, track)) return false;
                float delta = (orientation == ScrollOrientation::Vertical)
                            ? (ev.wheel.y > 0 ? -40.f : 40.f)
                            : (ev.wheel.x > 0 ? -40.f : 40.f);
                setOffset(offset + delta);
                return true;
            }
            return false;
        }

        void render(SDL_Renderer* renderer, float offsetX = 0.0f, float offsetY = 0.0f) {
            SDL_FRect track = { trackX - offsetX, trackY - offsetY, trackW, trackH };
            // Track background – slightly lighter so it stands out
            SDL_SetRenderDrawColor(renderer, 80, 80, 90, 255);
            SDL_RenderFillRect(renderer, &track);
            // Track border – a thin line for visibility
            SDL_SetRenderDrawColor(renderer, 120, 120, 140, 255);
            SDL_RenderRect(renderer, &track);

            if (maxOffset() <= 0) return;
            SDL_FRect thumb = thumbRect();
            thumb.x -= offsetX;
            thumb.y -= offsetY;
            // Thumb – bright and with a slight border
            SDL_SetRenderDrawColor(renderer, 180, 180, 200, 255);
            SDL_RenderFillRect(renderer, &thumb);
            SDL_SetRenderDrawColor(renderer, 220, 220, 240, 255);
            SDL_RenderRect(renderer, &thumb);
        }

            
        void setOffset(float v) {
            offset = std::clamp(v, 0.0f, maxOffset());
            if (onChange) onChange(offset);
        }

    private:
        bool  dragging = false;
        float dragStart = 0, dragStartOffset = 0;

        float thumbSize() const {
            if (contentSize <= 0) return (orientation == ScrollOrientation::Vertical) ? trackH : trackW;
            float trackSize = (orientation == ScrollOrientation::Vertical) ? trackH : trackW;
            return std::max(20.0f, trackSize * (viewSize / contentSize));
        }

        SDL_FRect thumbRect() const {
            float ratio = (maxOffset() > 0) ? offset / maxOffset() : 0;
            float trackSize = (orientation == ScrollOrientation::Vertical) ? trackH : trackW;
            float pos = ratio * (trackSize - thumbSize());
            if (orientation == ScrollOrientation::Vertical)
                return { trackX, trackY + pos, trackW, thumbSize() };
            else
                return { trackX + pos, trackY, thumbSize(), trackH };
        }

        static bool inRect(float x, float y, SDL_FRect r) {
            return x >= r.x && x <= r.x+r.w && y >= r.y && y <= r.y+r.h;
        }
    };

    class SpinBox : public ITextInput, public IGuiElement {
    public:
        SpinBox(SDL_Renderer* renderer, TTF_TextEngine* textEngine, TTF_Font* font,
                SDL_FRect rect, float minVal = 1.0f, float maxVal = 9999.0f,
                float initial = 50.0f, float step = 0.5f)
            : renderer(renderer), textEngine(textEngine), font(font),
              rect(rect), minVal(minVal), maxVal(maxVal), value(initial), step(step) {
            value = snapToDecimal(value);
            rebuildDisplayStr();
        }

        std::string getType() const override { return "SpinBox"; }
        float getX() const override { return rect.x; }
        float getY() const override { return rect.y; }
        float getWidth() const override { return rect.w; }
        float getHeight() const override { return rect.h; }
        void setRect(SDL_FRect r) override { rect = r; }
        void setPos(SDL_Point p) override {rect.x = p.x; rect.y = p.y;};
        float getMin() const { return minVal; }
        float getMax() const { return maxVal; }
        float getStep() const { return step; }
        void setMin(float v)  { minVal = v; value = std::clamp(value, minVal, maxVal); rebuildDisplayStr(); }
        void setMax(float v)  { maxVal = v; value = std::clamp(value, minVal, maxVal); rebuildDisplayStr(); }
        void setStep(float v) { step = v; }
        void setValue(float v){ value = snapToDecimal(std::clamp(v, minVal, maxVal)); rebuildDisplayStr(); }

        SpinBox(const SpinBox&) = delete;
        SpinBox& operator=(const SpinBox&) = delete;
        SpinBox(SpinBox&&) = default;
        SpinBox& operator=(SpinBox&&) = default;
        ~SpinBox() = default;

        std::function<void(float)> onChange;

        void appendText(const std::string& str) override {
            for (char c : str) {
                if (std::isdigit(c)) editBuf += c;
                else if (c == '.' && editBuf.find('.') == std::string::npos) editBuf += c;
            }
        }
        void removeLastChar() override { if (!editBuf.empty()) editBuf.pop_back(); }
        bool isActive() const override { return active; }
        void setActive(bool a) override { active = a; }
        const SDL_FRect& getRect() const override { return rect; }
        bool isNumericOnly() const override { return true; }

        bool handleEvent(const SDL_Event& ev, SDL_Window* window, float offsetX, float offsetY) override {
            SDL_FRect originalRect = rect;
            rect.x -= offsetX;
            rect.y -= offsetY;
            auto restore = [&]() { rect = originalRect; };

            if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN && ev.button.button == SDL_BUTTON_LEFT) {
                float mx = ev.button.x, my = ev.button.y;
                if (inRect(mx, my, decBtn())) { restore(); adjust(-step); return true; }
                if (inRect(mx, my, incBtn())) { restore(); adjust(+step); return true; }
                SDL_FRect textArea = fieldTextArea();
                if (inRect(mx, my, textArea)) { if (!active) { active = true; SDL_StartTextInput(window); } restore(); return true; }
                if (active) commitEdit(window);
                restore(); return false;
            }
            if (!active) { restore(); return false; }
            if (ev.type == SDL_EVENT_TEXT_INPUT) {
                for (char c : std::string(ev.text.text)) {
                    if (std::isdigit(c)) editBuf += c;
                    else if (c == '.' && editBuf.find('.') == std::string::npos) editBuf += c;
                }
                restore(); return true;
            }
            if (ev.type == SDL_EVENT_KEY_DOWN) {
                switch (ev.key.key) {
                    case SDLK_BACKSPACE: if (!editBuf.empty()) editBuf.pop_back(); restore(); return true;
                    case SDLK_RETURN: commitEdit(window); restore(); return true;
                    case SDLK_ESCAPE: cancelEdit(window); restore(); return true;
                    default: break;
                }
                restore(); return false;
            }
            restore(); return false;
        }

        void handleGamepad(float cursorX, float cursorY, float offsetX, float offsetY, SDL_Window* window, bool confirmDown, bool confirmDownLastFrame) override {
            SDL_FRect dec = decBtn(); dec.x -= offsetX; dec.y -= offsetY;
            SDL_FRect inc = incBtn(); inc.x -= offsetX; inc.y -= offsetY;
            SDL_FRect ta = fieldTextArea(); ta.x -= offsetX; ta.y -= offsetY;
            bool inDec = inRect(cursorX, cursorY, dec);
            bool inInc = inRect(cursorX, cursorY, inc);
            bool inTa = inRect(cursorX, cursorY, ta);
            if (confirmDown && !confirmDownLastFrame) {
                if (inDec) adjust(-step);
                else if (inInc) adjust(+step);
                else if (inTa) {
                    if (!active) { active = true; }
                } else {
                    if (active) commitEdit(window);
                }
            }
        }

        void render(float offsetX, float offsetY) override {
            SDL_FRect originalRect = rect;
            rect.x -= offsetX; // ADD THIS
            rect.y -= offsetY;
            float mx, my;
            SDL_GetMouseState(&mx, &my);
            SDL_FRect dec = decBtn(), inc = incBtn();
            auto btnCol = [&](SDL_FRect b) -> SDL_Color {
                return inRect(mx, my, b) ? SDL_Color{80,80,80,255} : SDL_Color{60,60,60,255};
            };
            SDL_SetRenderDrawColor(renderer, btnCol(dec).r, btnCol(dec).g, btnCol(dec).b, 255);
            SDL_RenderFillRect(renderer, &dec);
            SDL_SetRenderDrawColor(renderer, btnCol(inc).r, btnCol(inc).g, btnCol(inc).b, 255);
            SDL_RenderFillRect(renderer, &inc);
            drawCentredText("-", dec, {220,220,220,255});
            drawCentredText("+", inc, {220,220,220,255});
            SDL_FRect ta = fieldTextArea();
            SDL_Color bg = active ? SDL_Color{255,255,255,255} : SDL_Color{245,245,245,255};
            SDL_Color brd = active ? SDL_Color{52,110,235,255} : SDL_Color{130,130,130,255};
            SDL_SetRenderDrawColor(renderer, bg.r, bg.g, bg.b, 255);
            SDL_RenderFillRect(renderer, &ta);
            SDL_SetRenderDrawColor(renderer, brd.r, brd.g, brd.b, 255);
            SDL_RenderRect(renderer, &ta);
            std::string display = active ? editBuf : displayStr;
            if (active) {
                Uint64 ticks = SDL_GetTicks();
                if ((ticks / 500) % 2 == 0) display += "|";
            }
            SDL_Color tc = active ? SDL_Color{20,20,20,255} : SDL_Color{30,30,30,255};
            TTF_Text* t = TTF_CreateText(textEngine, font, display.c_str(), 0);
            if (t) {
                TTF_SetTextColor(t, tc.r, tc.g, tc.b, tc.a);
                TTF_DrawRendererText(t, ta.x + 6.0f, ta.y + (ta.h - 20.0f) * 0.5f);
                TTF_DestroyText(t);
            }
            rect = originalRect;
        }

        float getValue() const { return value; }
        void deactivate(SDL_Window* window) { if (active) commitEdit(window); }

    private:
        SDL_Renderer* renderer;
        TTF_TextEngine* textEngine;
        TTF_Font* font;
        SDL_FRect rect;
        float minVal, maxVal, value, step;
        bool active = false;
        std::string editBuf, displayStr;

        static float snapToDecimal(float v) { return std::round(v * 10.0f) / 10.0f; }
        void rebuildDisplayStr() {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.1f", value);
            displayStr = buf;
        }
        void adjust(float delta) {
            value = snapToDecimal(std::clamp(value + delta, minVal, maxVal));
            rebuildDisplayStr();
            if (onChange) onChange(value);
        }
        void commitEdit(SDL_Window* window) {
            if (!editBuf.empty()) {
                try {
                    float parsed = snapToDecimal(std::stof(editBuf));
                    value = std::clamp(parsed, minVal, maxVal);
                } catch (...) {}
            }
            rebuildDisplayStr();
            editBuf.clear();
            active = false;
            SDL_StopTextInput(window);
            if (onChange) onChange(value); 
        }
        void cancelEdit(SDL_Window* window) {
            editBuf.clear();
            active = false;
            SDL_StopTextInput(window);
        }

        
        SDL_FRect decBtn() const { return { rect.x, rect.y, rect.h, rect.h }; }
        SDL_FRect incBtn() const { return { rect.x + rect.w - rect.h, rect.y, rect.h, rect.h }; }
        SDL_FRect fieldTextArea() const {
            float bw = rect.h;
            return { rect.x + bw, rect.y, rect.w - bw * 2.0f, rect.h };
        }
        static bool inRect(float x, float y, SDL_FRect r) {
            return x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h;
        }
        void drawCentredText(const char* str, SDL_FRect r, SDL_Color c) {
            TTF_Text* t = TTF_CreateText(textEngine, font, str, 0);
            if (!t) return;
            TTF_SetTextColor(t, c.r, c.g, c.b, c.a);
            TTF_DrawRendererText(t, r.x + r.w * 0.35f, r.y + (r.h - 20.0f) * 0.5f);
            TTF_DestroyText(t);
        }
    };
    
    class OptionBox : public IGuiElement {
        private:
            SDL_Renderer* renderer;
            TTF_TextEngine* textEngine;
            TTF_Font* font;
            SDL_FRect optionRect;
            std::vector<std::string> options;
            int currentOption = 0;
            
            // Dropdown state
            bool isDropdownOpen = false;
            float dropdownScrollOffset = 0.0f;
            Gui::Scrollbar dropdownScrollbar;
            float maxDropdownHeight = 150.0f;

        public:
            OptionBox(SDL_Renderer* r, TTF_TextEngine* te, TTF_Font* f, 
                    SDL_FRect rect, const std::vector<std::string>& opts) 
                : renderer(r), textEngine(te), font(f), optionRect(rect), options(opts) 
            {
                dropdownScrollbar.setOrientation(Gui::ScrollOrientation::Vertical);
                dropdownScrollbar.onChange = [this](float v){ dropdownScrollOffset = v; };
            }

            std::string getType() const override { return "OptionBox"; }
            float getX() const override { return optionRect.x; }
            float getY() const override { return optionRect.y; }
            float getWidth() const override { return optionRect.w; }
            float getHeight() const override { return optionRect.h; }
            void setRect(SDL_FRect r) override { optionRect = r; }
            void setPos(SDL_Point p) override { optionRect.x = p.x; optionRect.y = p.y; }
            std::function<void(int)> onChange; 
            SDL_FRect decBtn() const { return { optionRect.x, optionRect.y, optionRect.h, optionRect.h }; }
            SDL_FRect incBtn() const { return { optionRect.x + optionRect.w - optionRect.h, optionRect.y, optionRect.h, optionRect.h }; }

            void setOptions(const std::vector<std::string>& opts) {
                options = opts;
                currentOption = 0;
                isDropdownOpen = false;
            }


            SDL_FRect fieldLabel() const {
                float bw = optionRect.h;
                return { optionRect.x + bw, optionRect.y, optionRect.w - bw * 2.0f, optionRect.h };
            }

            static bool inRect(float x, float y, SDL_FRect r) {
                return x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h;
            }

            void drawCentredText(const char* str, SDL_FRect r, SDL_Color c) {
                TTF_Text* t = TTF_CreateText(textEngine, font, str, 0);
                if (!t) return;
                TTF_SetTextColor(t, c.r, c.g, c.b, c.a);
                TTF_DrawRendererText(t, r.x + r.w * 0.35f, r.y + (r.h - 20.0f) * 0.5f);
                TTF_DestroyText(t);
            }

            bool handleEvent(const SDL_Event& ev, SDL_Window* window, float offsetX, float offsetY) override {
                if (ev.type != SDL_EVENT_MOUSE_BUTTON_DOWN && ev.type != SDL_EVENT_MOUSE_MOTION) return false;
                
                float mx = (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN) ? ev.button.x : ev.motion.x;
                float my = (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN) ? ev.button.y : ev.motion.y;

                SDL_FRect shiftedMain = { optionRect.x - offsetX, optionRect.y - offsetY, optionRect.w, optionRect.h };
                
                // Handle Dropdown interactions
                if (isDropdownOpen) {
                    float totalH = options.size() * 28.0f;
                    float visH = std::min(totalH, maxDropdownHeight);
                    SDL_FRect dropRect = { shiftedMain.x, shiftedMain.y + shiftedMain.h, shiftedMain.w, visH };
                    if (dropdownScrollbar.handleEvent(ev)) return true;
                    if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN && ev.button.button == SDL_BUTTON_LEFT) {
                        if (inRect(mx, my, dropRect)) {
                            int idx = (int)((my - dropRect.y + dropdownScrollOffset) / 28.0f);
                            if (idx >= 0 && idx < (int)options.size()) {
                                currentOption = idx;
                                isDropdownOpen = false;
                                if (onChange) onChange(currentOption); // <-- ADD THIS LINE
                                return true;
                            }
                        } else if (!inRect(mx, my, shiftedMain)) {
                            isDropdownOpen = false; // Clicked outside
                            return true;
                        }
                    }
                }

                // Handle Main Box interactions
                if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN && ev.button.button == SDL_BUTTON_LEFT) {
                    if (inRect(mx, my, shiftedMain)) {
                        isDropdownOpen = !isDropdownOpen;
                        dropdownScrollOffset = 0.0f;
                        return true;
                    } else {
                        isDropdownOpen = false;
                    }
                }
                return false;
            }

            void handleGamepad(float cursorX, float cursorY, float offsetX, float offsetY, SDL_Window* window, bool confirmDown, bool confirmDownLastFrame) override {
                SDL_FRect shiftedMain = { optionRect.x - offsetX, optionRect.y - offsetY,
                                        optionRect.w, optionRect.h };

                if (isDropdownOpen) {
                    float totalH = options.size() * 28.0f;
                    float visH = std::min(totalH, maxDropdownHeight);
                    SDL_FRect dropRect = { shiftedMain.x, shiftedMain.y + shiftedMain.h,
                                        shiftedMain.w, visH };

                    // Check if cursor is over the dropdown area
                    if (cursorX >= dropRect.x && cursorX <= dropRect.x + dropRect.w &&
                        cursorY >= dropRect.y && cursorY <= dropRect.y + dropRect.h) {
                        int idx = (int)((cursorY - dropRect.y + dropdownScrollOffset) / 28.0f);
                        if (idx >= 0 && idx < (int)options.size()) {
                            if (confirmDown && !confirmDownLastFrame) {
                                currentOption = idx;
                                isDropdownOpen = false;
                                if (onChange) onChange(currentOption);
                                return;
                            }
                            // Prevent toggling the main box while hovering the dropdown
                            return;
                        }
                    }

                    // Close dropdown if confirm is pressed and cursor is outside it
                    if (confirmDown && !confirmDownLastFrame) {
                        isDropdownOpen = false;
                        return;
                    }
                }

                // Main box toggling
                if (confirmDown && !confirmDownLastFrame) {
                    if (cursorX >= shiftedMain.x && cursorX <= shiftedMain.x + shiftedMain.w &&
                        cursorY >= shiftedMain.y && cursorY <= shiftedMain.y + shiftedMain.h) {
                        isDropdownOpen = !isDropdownOpen;
                        dropdownScrollOffset = 0.0f;
                    }
                }
            }
            
            void render(float offsetX, float offsetY) override {
                SDL_FRect shiftedMain = { optionRect.x - offsetX, optionRect.y - offsetY, optionRect.w, optionRect.h };

                // Draw Main Box
                SDL_Color bg = {255,255,255,255};
                SDL_Color brd = {130,130,130,255};
                SDL_SetRenderDrawColor(renderer, bg.r, bg.g, bg.b, bg.a);
                SDL_RenderFillRect(renderer, &shiftedMain);
                SDL_SetRenderDrawColor(renderer, brd.r, brd.g, brd.b, brd.a);
                SDL_RenderRect(renderer, &shiftedMain);

                std::string displayText = options.empty() ? "[Empty]" : options[currentOption];
                SDL_Color tc = {20,20,20,255};
                TTF_Text* t = TTF_CreateText(textEngine, font, displayText.c_str(), 0);
                if (t) {
                    TTF_SetTextColor(t, tc.r, tc.g, tc.b, tc.a);
                    TTF_DrawRendererText(t, shiftedMain.x + 6.0f, shiftedMain.y + (shiftedMain.h - 20.0f) * 0.5f);
                    TTF_DestroyText(t);
                }

                // Draw Dropdown if open
                if (isDropdownOpen && !options.empty()) {
                    float totalH = options.size() * 28.0f;
                    float visH = std::min(totalH, maxDropdownHeight);
                    SDL_FRect dropRect = { shiftedMain.x, shiftedMain.y + shiftedMain.h, shiftedMain.w, visH };

                    // Background and border
                    SDL_SetRenderDrawColor(renderer, 240, 240, 240, 255);
                    SDL_RenderFillRect(renderer, &dropRect);
                    SDL_SetRenderDrawColor(renderer, 100, 100, 100, 255);
                    SDL_RenderRect(renderer, &dropRect);

                    // Manually cull each item to the visible area – no clip state changes
                    for (size_t i = 0; i < options.size(); ++i) {
                        SDL_FRect itemRect = {
                            dropRect.x,
                            dropRect.y + (i * 28.0f) - dropdownScrollOffset,
                            dropRect.w,
                            28.0f
                        };

                        // Simple intersection test with dropRect
                        if (itemRect.y + itemRect.h < dropRect.y || itemRect.y > dropRect.y + dropRect.h)
                            continue;

                        SDL_Color itemBg = (i == currentOption) ? SDL_Color{200, 220, 255, 255} : SDL_Color{240, 240, 240, 255};
                        SDL_SetRenderDrawColor(renderer, itemBg.r, itemBg.g, itemBg.b, itemBg.a);
                        SDL_RenderFillRect(renderer, &itemRect);

                        TTF_Text* it = TTF_CreateText(textEngine, font, options[i].c_str(), 0);
                        if (it) {
                            TTF_SetTextColor(it, 20, 20, 20, 255);
                            TTF_DrawRendererText(it, itemRect.x + 6.0f, itemRect.y + 4.0f);
                            TTF_DestroyText(it);
                        }
                    }

                    // Draw scrollbar if needed
                    if (totalH > maxDropdownHeight) {
                        dropdownScrollbar.setGeometry(dropRect.x + dropRect.w - 10, dropRect.y, 10, dropRect.h, totalH, visH);
                        dropdownScrollbar.render(renderer, 0.0f, 0.0f);
                    }
                }
            }

            std::string getCurrentOption() const { 
                return options.empty() ? "" : options[currentOption]; 
            }
            int getCurrentIndex() const { return currentOption; }
            void setCurrentIndex(int idx) { if(idx >= 0 && idx < (int)options.size()) currentOption = idx; }

            // New methods for inspector scrollbar adaptation
            bool isOpen() const { return isDropdownOpen; }
            float getDropdownHeight() const {
                if (!isDropdownOpen || options.empty()) return 0.0f;
                float totalH = options.size() * 28.0f;
                return std::min(totalH, maxDropdownHeight);
            }
    };

    // Replace the LineEdit class with this improved version
    class LineEdit : public ITextInput, public IGuiElement {
    public:
        LineEdit(SDL_Renderer* renderer, TTF_TextEngine* textEngine, TTF_Font* font,
                SDL_FRect rect, const std::string& placeholder = "Type here...")
            : renderer(renderer), textEngine(textEngine), font(font),
            rect(rect), placeholder(placeholder), cursorPos(0), scrollOffset(0.0f) {}
        
        // --- ITextInput ---
        void appendText(const std::string& str) override {
            if (hasSelection) deleteSelection();
            text.insert(cursorPos, str);
            cursorPos += (int)str.size();
            pushUndoState();
            ensureCursorVisible();
        }
        
        void removeLastChar() override {
            if (!active || text.empty() || cursorPos == 0) return;
            if (hasSelection) {
                deleteSelection();
                return;
            }
            text.erase(cursorPos - 1, 1);
            --cursorPos;
            pushUndoState();
            ensureCursorVisible();
        }
        
        bool isActive() const override { return active; }
        
        void setActive(bool a) override {
            active = a;
            if (!active) {
                cursorPos = (int)text.size();
                hasSelection = false;
            }
        }
        
        const SDL_FRect& getRect() const override { return rect; }
        
        // --- IGuiElement ---
        std::string getType() const override { return "LineEdit"; }
        float getX() const override { return rect.x; }
        float getY() const override { return rect.y; }
        float getWidth() const override { return rect.w; }
        float getHeight() const override { return rect.h; }
        
        void setRect(SDL_FRect r) override {
            rect = r;
            ensureCursorVisible();
        }
        
        void setPos(SDL_Point p) override {
            rect.x = (float)p.x;
            rect.y = (float)p.y;
        }
        
        const std::string& getPlaceholder() const { return placeholder; }
        void setPlaceholder(const std::string& p) { placeholder = p; }
        
        const std::string& getText() const { return text; }
        
        void clear() {
            text.clear();
            cursorPos = 0;
            scrollOffset = 0.0f;
            hasSelection = false;
            undoStack.clear();
            redoStack.clear();
        }
        
        void setText(const std::string& t) {
            text = t;
            cursorPos = (int)text.size();
            scrollOffset = 0.0f;
            hasSelection = false;
            pushUndoState();
            ensureCursorVisible();
        }
        
        // --- Event handling ---
        bool handleEvent(const SDL_Event& ev, SDL_Window* window, float offsetX, float offsetY) override {
            SDL_FRect originalRect = rect;
            rect.x -= offsetX;
            rect.y -= offsetY;
            auto restore = [&]() { rect = originalRect; };
            
            if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN && ev.button.button == SDL_BUTTON_LEFT) {
                float mx = ev.button.x, my = ev.button.y;
                bool hit = mx >= rect.x && mx <= rect.x + rect.w &&
                        my >= rect.y && my <= rect.y + rect.h;
                if (hit) {
                    if (!active) {
                        active = true;
                        SDL_StartTextInput(window);
                    }
                    bool shift = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
                    float textX = mx - (rect.x + 6.0f) + scrollOffset;
                    int newCol = xToCol(text, textX);
                    
                    if (shift) {
                        if (!hasSelection) {
                            selAnchor = cursorPos;
                            hasSelection = true;
                        }
                        cursorPos = newCol;
                    } else {
                        cursorPos = newCol;
                        hasSelection = false;
                    }
                    mouseSelecting = true;
                    ensureCursorVisible();
                    restore();
                    return true;
                } else {
                    if (active) {
                        active = false;
                        SDL_StopTextInput(window);
                        hasSelection = false;
                    }
                    restore();
                    return false;
                }
            }
            
            if (ev.type == SDL_EVENT_MOUSE_MOTION && mouseSelecting && active) {
                float mx = ev.motion.x, my = ev.motion.y;
                float textX = mx - (rect.x + 6.0f) + scrollOffset;
                cursorPos = xToCol(text, textX);
                ensureCursorVisible();
                restore();
                return true;
            }
            
            if (ev.type == SDL_EVENT_MOUSE_BUTTON_UP && ev.button.button == SDL_BUTTON_LEFT) {
                mouseSelecting = false;
            }
            
            if (!active) { restore(); return false; }
            
            if (ev.type == SDL_EVENT_TEXT_INPUT) {
                if (hasSelection) deleteSelection();
                text.insert(cursorPos, ev.text.text);
                cursorPos += (int)strlen(ev.text.text);
                pushUndoState();
                ensureCursorVisible();
                restore();
                return true;
            }
            
            if (ev.type == SDL_EVENT_KEY_DOWN) {
                bool shift = (ev.key.mod & SDL_KMOD_SHIFT) != 0;
                bool ctrl = (ev.key.mod & SDL_KMOD_CTRL) != 0;
                
                // Ctrl+A: Select all
                if (ctrl && ev.key.key == SDLK_A) {
                    selAnchor = 0;
                    cursorPos = (int)text.size();
                    hasSelection = true;
                    ensureCursorVisible();
                    restore();
                    return true;
                }
                
                // Ctrl+C: Copy
                if (ctrl && ev.key.key == SDLK_C) {
                    if (hasSelection) {
                        auto [start, end] = getSelectionRange();
                        std::string selected = text.substr(start, end - start);
                        SDL_SetClipboardText(selected.c_str());
                    }
                    restore();
                    return true;
                }
                
                // Ctrl+X: Cut
                if (ctrl && ev.key.key == SDLK_X) {
                    if (hasSelection) {
                        auto [start, end] = getSelectionRange();
                        std::string selected = text.substr(start, end - start);
                        SDL_SetClipboardText(selected.c_str());
                        deleteSelection();
                        pushUndoState();
                    }
                    restore();
                    return true;
                }
                
                // Ctrl+V: Paste
                if (ctrl && ev.key.key == SDLK_V) {
                    char* clipText = SDL_GetClipboardText();
                    if (clipText) {
                        if (hasSelection) deleteSelection();
                        text.insert(cursorPos, clipText);
                        cursorPos += (int)strlen(clipText);
                        SDL_free(clipText);
                        pushUndoState();
                        ensureCursorVisible();
                    }
                    restore();
                    return true;
                }
                
                // Ctrl+Z: Undo
                if (ctrl && ev.key.key == SDLK_Z) {
                    undo();
                    restore();
                    return true;
                }
                
                // Ctrl+Y or Ctrl+Shift+Z: Redo
                if (ctrl && (ev.key.key == SDLK_Y || (shift && ev.key.key == SDLK_Z))) {
                    redo();
                    restore();
                    return true;
                }
                
                switch (ev.key.key) {
                    case SDLK_BACKSPACE:
                        if (hasSelection) {
                            deleteSelection();
                            pushUndoState();
                        } else if (cursorPos > 0) {
                            text.erase(cursorPos - 1, 1);
                            --cursorPos;
                            pushUndoState();
                        }
                        ensureCursorVisible();
                        restore();
                        return true;
                        
                    case SDLK_DELETE:
                        if (hasSelection) {
                            deleteSelection();
                            pushUndoState();
                        } else if (cursorPos < (int)text.size()) {
                            text.erase(cursorPos, 1);
                            pushUndoState();
                        }
                        ensureCursorVisible();
                        restore();
                        return true;
                        
                    case SDLK_LEFT:
                        if (cursorPos > 0) {
                            if (shift) {
                                if (!hasSelection) {
                                    selAnchor = cursorPos;
                                    hasSelection = true;
                                }
                                --cursorPos;
                            } else {
                                if (hasSelection) {
                                    auto [start, end] = getSelectionRange();
                                    cursorPos = start;
                                    hasSelection = false;
                                } else {
                                    --cursorPos;
                                }
                            }
                        }
                        ensureCursorVisible();
                        restore();
                        return true;
                        
                    case SDLK_RIGHT:
                        if (cursorPos < (int)text.size()) {
                            if (shift) {
                                if (!hasSelection) {
                                    selAnchor = cursorPos;
                                    hasSelection = true;
                                }
                                ++cursorPos;
                            } else {
                                if (hasSelection) {
                                    auto [start, end] = getSelectionRange();
                                    cursorPos = end;
                                    hasSelection = false;
                                } else {
                                    ++cursorPos;
                                }
                            }
                        }
                        ensureCursorVisible();
                        restore();
                        return true;
                        
                    case SDLK_HOME:
                        if (shift) {
                            if (!hasSelection) {
                                selAnchor = cursorPos;
                                hasSelection = true;
                            }
                            cursorPos = 0;
                        } else {
                            cursorPos = 0;
                            hasSelection = false;
                        }
                        ensureCursorVisible();
                        restore();
                        return true;
                        
                    case SDLK_END:
                        if (shift) {
                            if (!hasSelection) {
                                selAnchor = cursorPos;
                                hasSelection = true;
                            }
                            cursorPos = (int)text.size();
                        } else {
                            cursorPos = (int)text.size();
                            hasSelection = false;
                        }
                        ensureCursorVisible();
                        restore();
                        return true;
                        
                    case SDLK_RETURN:
                    case SDLK_ESCAPE:
                        active = false;
                        SDL_StopTextInput(window);
                        hasSelection = false;
                        restore();
                        return true;
                        
                    default:
                        break;
                }
            }
            
            restore();
            return false;
        }
        
        void handleGamepad(float cursorX, float cursorY, float offsetX, float offsetY,
                        SDL_Window* window, bool confirmDown, bool confirmDownLastFrame) override {
            SDL_FRect r = rect;
            r.x -= offsetX;
            r.y -= offsetY;
            bool hit = cursorX >= r.x && cursorX <= r.x + r.w &&
                    cursorY >= r.y && cursorY <= r.y + r.h;
            if (confirmDown && !confirmDownLastFrame) {
                if (hit) {
                    if (!active) { 
                        active = true; 
                        SDL_StartTextInput(window); 
                        cursorPos = (int)text.size(); 
                        ensureCursorVisible(); 
                    }
                } else {
                    if (active) { 
                        active = false; 
                        SDL_StopTextInput(window); 
                        hasSelection = false;
                    }
                }
            }
        }
        
        // --- Rendering ---
        void render(float offsetX, float offsetY) override {
            SDL_FRect originalRect = rect;
            rect.x -= offsetX;
            rect.y -= offsetY;
            
            // Background & border
            SDL_Color bg = active ? SDL_Color{255, 255, 255, 255} : SDL_Color{245, 245, 255, 255};
            SDL_SetRenderDrawColor(renderer, bg.r, bg.g, bg.b, bg.a);
            SDL_RenderFillRect(renderer, &rect);
            
            SDL_Color border = active ? SDL_Color{52, 110, 235, 255} : SDL_Color{130, 130, 130, 255};
            SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
            SDL_RenderRect(renderer, &rect);
            
            if (!font || !textEngine) { rect = originalRect; return; }
            
            bool showPlaceholder = text.empty() && !active;
            const std::string& display = showPlaceholder ? placeholder : text;
            SDL_Color textColor = showPlaceholder ? SDL_Color{150, 150, 150, 255} : SDL_Color{20, 20, 20, 255};
            
            // Clip to the interior
            SDL_Rect clipRect = { (int)rect.x + 4, (int)rect.y + 2,
                                (int)rect.w - 8, (int)rect.h - 4 };
            SDL_SetRenderClipRect(renderer, &clipRect);
            
            const float availableWidth = rect.w - 12.0f;
            const float textStartX = rect.x + 6.0f;
            
            // Draw selection highlight
            if (hasSelection && !showPlaceholder) {
                auto [start, end] = getSelectionRange();
                float selStartX = textStartX + getTextWidth(text.substr(0, start)) - scrollOffset;
                float selEndX = textStartX + getTextWidth(text.substr(0, end)) - scrollOffset;
                
                if (selEndX > selStartX) {
                    SDL_SetRenderDrawColor(renderer, 51, 153, 255, 100);
                    SDL_FRect selRect = { selStartX, rect.y + 4.0f, selEndX - selStartX, rect.h - 8.0f };
                    SDL_RenderFillRect(renderer, &selRect);
                }
            }
            
            // Draw text
            if (!display.empty()) {
                float drawX = textStartX - scrollOffset;
                TTF_Text* t = TTF_CreateText(textEngine, font, display.c_str(), 0);
                if (t) {
                    TTF_SetTextColor(t, textColor.r, textColor.g, textColor.b, textColor.a);
                    TTF_DrawRendererText(t, drawX, rect.y + (rect.h - 20.0f) * 0.5f);
                    TTF_DestroyText(t);
                }
            }
            
            // Draw cursor
            if (active) {
                Uint64 ticks = SDL_GetTicks();
                if ((ticks / 500) % 2 == 0) {
                    float cursorX = textStartX + getTextWidth(text.substr(0, cursorPos)) - scrollOffset;
                    float minX = rect.x + 4.0f;
                    float maxX = rect.x + rect.w - 4.0f;
                    
                    if (cursorX >= minX && cursorX <= maxX) {
                        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
                        SDL_FRect cursorRect = { cursorX, rect.y + 4.0f, 2.0f, rect.h - 8.0f };
                        SDL_RenderFillRect(renderer, &cursorRect);
                    }
                }
            }
            
            SDL_SetRenderClipRect(renderer, nullptr);
            rect = originalRect;
        }
        
        void deactivate(SDL_Window* window) {
            active = false;
            SDL_StopTextInput(window);
            hasSelection = false;
        }

    private:
        SDL_Renderer* renderer;
        TTF_TextEngine* textEngine;
        TTF_Font* font;
        SDL_FRect rect;
        std::string placeholder, text;
        bool active = false;
        int cursorPos = 0;
        float scrollOffset = 0.0f;
        
        // Selection
        int selAnchor = 0;
        bool hasSelection = false;
        bool mouseSelecting = false;
        
        // Undo/Redo
        struct UndoState {
            std::string text;
            int cursorPos;
        };
        std::vector<UndoState> undoStack;
        std::vector<UndoState> redoStack;
        static constexpr size_t MAX_UNDO_STATES = 100;
        
        void pushUndoState() {
            undoStack.push_back({text, cursorPos});
            if (undoStack.size() > MAX_UNDO_STATES) {
                undoStack.erase(undoStack.begin());
            }
            redoStack.clear();
        }
        
        void undo() {
            if (undoStack.empty()) return;
            redoStack.push_back({text, cursorPos});
            auto state = undoStack.back();
            undoStack.pop_back();
            text = state.text;
            cursorPos = state.cursorPos;
            hasSelection = false;
            ensureCursorVisible();
        }
        
        void redo() {
            if (redoStack.empty()) return;
            undoStack.push_back({text, cursorPos});
            auto state = redoStack.back();
            redoStack.pop_back();
            text = state.text;
            cursorPos = state.cursorPos;
            hasSelection = false;
            ensureCursorVisible();
        }
        
        std::pair<int, int> getSelectionRange() const {
            int start = std::min(selAnchor, cursorPos);
            int end = std::max(selAnchor, cursorPos);
            return {start, end};
        }
        
        void deleteSelection() {
            if (!hasSelection) return;
            auto [start, end] = getSelectionRange();
            text.erase(start, end - start);
            cursorPos = start;
            hasSelection = false;
        }
        
        float getTextWidth(const std::string& str) const {
            if (str.empty() || !font) return 0.0f;
            int w = 0, h = 0;
            TTF_GetStringSize(font, str.c_str(), str.size(), &w, &h);
            return (float)w;
        }
        
        int xToCol(const std::string& line, float localX) const {
            if (localX <= 0.0f || line.empty()) return 0;
            
            float prevW = 0.0f;
            for (size_t i = 1; i <= line.size(); ++i) {
                float currW = getTextWidth(line.substr(0, i));
                float mid = (prevW + currW) * 0.5f;
                if (localX < mid) return (int)i - 1;
                prevW = currW;
            }
            return (int)line.size();
        }
        
        void ensureCursorVisible() {
            if (text.empty()) { scrollOffset = 0.0f; return; }
            
            const float availableWidth = rect.w - 12.0f;
            float cursorPixel = getTextWidth(text.substr(0, cursorPos));
            
            if (cursorPixel < scrollOffset) {
                scrollOffset = cursorPixel;
            } else if (cursorPixel > scrollOffset + availableWidth) {
                scrollOffset = cursorPixel - availableWidth;
            }
            
            float maxScroll = std::max(0.0f, getTextWidth(text) - availableWidth);
            scrollOffset = std::clamp(scrollOffset, 0.0f, maxScroll);
        }
    };

    class TextArea : public IGuiElement {
    public:
        TextArea(SDL_Renderer* renderer, TTF_TextEngine* textEngine, TTF_Font* font,
                SDL_FRect rect)
            : renderer(renderer), textEngine(textEngine), font(font),
            rect(rect), lines(1, ""), cursorRow(0), cursorCol(0),
            scrollX(0), scrollY(0) {
            vScrollbar.setOrientation(Gui::ScrollOrientation::Vertical);
            hScrollbar.setOrientation(Gui::ScrollOrientation::Horizontal);
            vScrollbar.onChange = [this](float v) { scrollY = v / m_zoom; };
            hScrollbar.onChange = [this](float v) { scrollX = v / m_zoom; };
            refreshScrollbars();
        }

        // IGuiElement overrides
        std::string getType() const override { return "TextArea"; }
        float getX() const override { return rect.x; }
        float getY() const override { return rect.y; }
        float getWidth() const override { return rect.w; }
        float getHeight() const override { return rect.h; }
        void setRect(SDL_FRect r) override { rect = r; refreshScrollbars(); }
        void setPos(SDL_Point p) override { rect.x = p.x; rect.y = p.y; refreshScrollbars(); }

    private:
    
        struct TextToken { size_t start, len; bool isKeyword; };

        // Lightweight tokenizer for C++17 keywords typically found inside
        // function bodies. Deliberately excludes preprocessor directives
        // (#include, #define, etc.) and namespace/class/struct declarations
        // since those live outside method bodies.
        static std::vector<TextToken> tokenizeCppBody(const std::string& line) {
            static const std::unordered_set<std::string> keywords = {
                "if", "else", "for", "while", "do", "return", "break", "continue",
                "switch", "case", "default", "goto", "throw", "try", "catch",
                "new", "delete", "this", "sizeof", "alignof", "decltype", "noexcept",
                "static_cast", "dynamic_cast", "const_cast", "reinterpret_cast",
                "true", "false", "nullptr",
                "auto", "void", "int", "float", "double", "bool", "char",
                "const", "static", "virtual", "override", "final",
                "long", "short", "unsigned", "signed",
                "co_await", "co_return", "co_yield"
            };

            std::vector<TextToken> tokens;
            size_t i = 0;
            while (i < line.size()) {
                if (std::isalpha((unsigned char)line[i]) || line[i] == '_') {
                    size_t j = i;
                    while (j < line.size() && (std::isalnum((unsigned char)line[j]) || line[j] == '_')) j++;
                    std::string word = line.substr(i, j - i);
                    bool isKw = keywords.count(word) > 0;
                    tokens.push_back({i, j - i, isKw});
                    i = j;
                } else if (std::isdigit((unsigned char)line[i])) {
                    size_t j = i;
                    while (j < line.size() && (std::isalnum((unsigned char)line[j]) || line[j] == '.')) j++;
                    tokens.push_back({i, j - i, false});
                    i = j;
                } else if (line[i] == '/' && i + 1 < line.size() && line[i+1] == '/') {
                    tokens.push_back({i, line.size() - i, false});
                    break;
                } else if (line[i] == '"' || line[i] == '\'') {
                    char quote = line[i];
                    size_t j = i + 1;
                    while (j < line.size() && line[j] != quote) {
                        if (line[j] == '\\' && j + 1 < line.size()) j++;
                        j++;
                    }
                    if (j < line.size()) j++;
                    tokens.push_back({i, j - i, false});
                    i = j;
                } else {
                    tokens.push_back({i, 1, false});
                    i++;
                }
            }
            return tokens;
        }

    private:
        float m_zoom = 1.0f;

        struct UndoState {
            std::vector<std::string> lines;
            int cursorRow;
            int cursorCol;
        };
        std::vector<UndoState> undoStack;
        std::vector<UndoState> redoStack;
        static constexpr size_t MAX_UNDO_STATES = 100;

        void pushUndoState() {
            undoStack.push_back({lines, cursorRow, cursorCol});
            if (undoStack.size() > MAX_UNDO_STATES) {
                undoStack.erase(undoStack.begin());
            }
            redoStack.clear();
        }

        void undo() {
            if (undoStack.empty()) return;
            redoStack.push_back({lines, cursorRow, cursorCol});
            auto state = undoStack.back();
            undoStack.pop_back();
            lines = state.lines;
            cursorRow = state.cursorRow;
            cursorCol = state.cursorCol;
            ensureCursorVisible();
        }

        void redo() {
            if (redoStack.empty()) return;
            undoStack.push_back({lines, cursorRow, cursorCol});
            auto state = redoStack.back();
            redoStack.pop_back();
            lines = state.lines;
            cursorRow = state.cursorRow;
            cursorCol = state.cursorCol;
            ensureCursorVisible();
        }

    public:
        void setZoom(float z) { m_zoom = z; refreshScrollbars(); }

    

        void setText(const std::string& text) {
            lines.clear();
            std::stringstream ss(text);
            std::string line;
            while (std::getline(ss, line)) lines.push_back(line);
            if (lines.empty()) lines.push_back("");
            cursorRow = 0; cursorCol = 0;  // <-- start at top
            scrollX = scrollY = 0;
            refreshScrollbars();
            ensureCursorVisible();
        }

        std::string getText() const {
            std::string result;
            for (size_t i = 0; i < lines.size(); ++i) {
                result += lines[i];
                if (i + 1 < lines.size()) result += '\n';
            }
            return result;
        }

        bool isActive() const { return active; }
        void setActive(bool a) { active = a; }

        bool handleEvent(const SDL_Event& ev, SDL_Window* window, float offsetX, float offsetY) override {
            // Scrollbars first
            if (vScrollbar.handleEvent(ev)) return true;
            if (hScrollbar.handleEvent(ev)) return true;

            if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN && ev.button.button == SDL_BUTTON_LEFT) {
                float mx = ev.button.x, my = ev.button.y;
                SDL_FRect shifted = { rect.x - offsetX, rect.y - offsetY, rect.w, rect.h };
                if (inRect(mx, my, shifted)) {
                    // Activate if not already
                    if (!active) {
                        active = true;
                        SDL_StartTextInput(window);
                    }
                    // Always reposition cursor
                    float localX = (mx - shifted.x) + scrollX * m_zoom;
                    float localY = (my - shifted.y) + scrollY * m_zoom;
                    int row = (int)(localY / (LINE_HEIGHT * m_zoom));
                    if (row < 0) row = 0;
                    if (row >= (int)lines.size()) row = (int)lines.size() - 1;
                    int col = xToCol(lines[row], localX / m_zoom);
                    cursorRow = row;
                    cursorCol = col;
                    ensureCursorVisible();
                    return true;
                } else {
                    // Clicked outside -> deactivate
                    if (active) {
                        active = false;
                        SDL_StopTextInput(window);
                    }
                    return false;
                }
            }

            // ---- If not active, ignore everything else ----
            if (!active) return false;

            // Keyboard input while active
            if (ev.type == SDL_EVENT_KEY_DOWN) {
                bool shift = (ev.key.mod & SDL_KMOD_SHIFT) != 0;
                bool ctrl = (ev.key.mod & SDL_KMOD_CTRL) != 0;

                if (ctrl && ev.key.key == SDLK_C) {
                    // TextArea doesn't have selection yet, so copy current line
                    if (cursorRow >= 0 && cursorRow < (int)lines.size()) {
                            SDL_SetClipboardText(lines[cursorRow].c_str());
                        }
                        return true;
                }

                // Ctrl+V: Paste
                if (ctrl && ev.key.key == SDLK_V) {
                    char* clipText = SDL_GetClipboardText();
                    if (clipText) {
                        std::string text(clipText);
                        SDL_free(clipText);
                        for (char c : text) {
                            if (c == '\n') {
                                newline();
                            } else if (c == '\r') {
                                continue;
                            } else if (c == '\t') {
                                insertChar('\t');
                            } else if (c >= 32 && c < 127) {
                                insertChar(c);
                            }
                        }
                        pushUndoState();
                    }
                    return true;
                }

                // Ctrl+Z: Undo
                if (ctrl && ev.key.key == SDLK_Z) {
                    undo();
                    return true;
                }

                // Ctrl+Y or Ctrl+Shift+Z: Redo
                if (ctrl && (ev.key.key == SDLK_Y || (shift && ev.key.key == SDLK_Z))) {
                    redo();
                    return true;
                }
                switch (ev.key.key) {
                    case SDLK_LEFT:  moveCursor(cursorRow, cursorCol - 1, shift); return true;
                    case SDLK_RIGHT: moveCursor(cursorRow, cursorCol + 1, shift); return true;
                    case SDLK_UP:    moveCursor(cursorRow - 1, cursorCol, shift); return true;
                    case SDLK_DOWN:  moveCursor(cursorRow + 1, cursorCol, shift); return true;
                    case SDLK_HOME:  moveCursor(cursorRow, 0, shift); return true;
                    case SDLK_END:   moveCursor(cursorRow, getLineLength(cursorRow), shift); return true;
                    case SDLK_PAGEUP: {
                        int rows = (int)(rect.h / LINE_HEIGHT) - 1;
                        moveCursor(cursorRow - rows, cursorCol, shift);
                        return true;
                    }
                    case SDLK_PAGEDOWN: {
                        int rows = (int)(rect.h / LINE_HEIGHT) - 1;
                        moveCursor(cursorRow + rows, cursorCol, shift);
                        return true;
                    }
                    case SDLK_BACKSPACE: backspace(); return true;
                    case SDLK_DELETE:    deleteChar(); return true;
                    case SDLK_RETURN:    newline(); return true;
                    case SDLK_TAB:       insertChar('\t'); return true;
                    case SDLK_ESCAPE:
                        active = false;
                        SDL_StopTextInput(window);
                        return true;
                    default:
                        break;
                }
            }
            if (ev.type == SDL_EVENT_TEXT_INPUT) {
                for (const char* c = ev.text.text; *c; ++c) {
                    if (*c >= 32 && *c < 127) insertChar(*c);
                }
                return true;
            }
            return false;
        }

        void handleGamepad(float cursorX, float cursorY, float offsetX, float offsetY,
                        SDL_Window* window, bool confirmDown, bool confirmDownLastFrame) override {
            // Not implemented for simplicity – gamepad can focus but text input is keyboard-driven.
            if (confirmDown && !confirmDownLastFrame) {
                SDL_FRect shifted = { rect.x - offsetX, rect.y - offsetY, rect.w, rect.h };
                if (inRect(cursorX, cursorY, shifted)) {
                    active = true;
                    SDL_StartTextInput(window);
                } else {
                    active = false;
                    SDL_StopTextInput(window);
                }
            }
        }

        void render(float offsetX, float offsetY) override {
            SDL_FRect shifted = { rect.x - offsetX, rect.y - offsetY, rect.w, rect.h };
            // Background
            SDL_SetRenderDrawColor(renderer, 40, 40, 48, 255);
            SDL_RenderFillRect(renderer, &shifted);
            SDL_SetRenderDrawColor(renderer, 70, 70, 90, 255);
            SDL_RenderRect(renderer, &shifted);
            // Clip
            SDL_Rect clip = { (int)shifted.x, (int)shifted.y, (int)shifted.w, (int)shifted.h };
            SDL_SetRenderClipRect(renderer, &clip);

            // Use actual font height to prevent vertical drift after newlines
            float lineHeight = (float)TTF_GetFontHeight(font);
            if (lineHeight <= 0.0f) lineHeight = 22.0f;

            const SDL_Color defaultColor = {220, 220, 240, 255};
            const SDL_Color keywordColor = {110, 160, 230, 255};
            const SDL_Color commentColor = {90, 140, 90, 255};
            const SDL_Color stringColor  = {210, 150, 90, 255};

            // Draw text lines with scrolling
            float y = shifted.y + 6.0f - scrollY;
            for (int row = 0; row < (int)lines.size(); ++row) {
                float yy = y + row * lineHeight;
                if (yy + lineHeight < shifted.y || yy > shifted.y + shifted.h) continue;
                const std::string& line = lines[row];

                // Horizontal scroll: find start index
                int startIdx = 0;
                if (scrollX > 0 && !line.empty()) {
                    int accumulated = 0;
                    for (size_t i = 0; i < line.size(); ++i) {
                        int cw = 0, ch = 0;
                        std::string chStr = line.substr(i, 1);
                        TTF_GetStringSize(font, chStr.c_str(), 1, &cw, &ch);
                        if (accumulated + cw > scrollX) {
                            startIdx = (int)i;
                            break;
                        }
                        accumulated += cw;
                    }
                }

                // Draw cursor for active line — uses TTF_GetStringSize which now
                // matches the rendered glyph positions exactly, so the caret stays
                // glued to the real glyph boundary even after indent or newline.
                if (row == cursorRow && active) {
                    std::string prefix = line.substr(0, cursorCol);
                    int pw = 0, ph = 0;
                    TTF_GetStringSize(font, prefix.c_str(), prefix.size(), &pw, &ph);
                    float cursorX = shifted.x + 6.0f + (pw - scrollX) * m_zoom;
                    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
                    SDL_FRect cursorRect = { cursorX, yy, 2.0f, lineHeight };
                    SDL_RenderFillRect(renderer, &cursorRect);
                }

                // Tokenize and render with syntax highlighting
                auto tokens = tokenizeCppBody(line);
                float drawX = shifted.x + 6.0f - scrollX * m_zoom;

                for (const auto& tok : tokens) {
                    if ((int)(tok.start + tok.len) <= startIdx) continue;
                    std::string sub = line.substr(tok.start, tok.len);

                    // Calculate token position using the same TTF_GetStringSize
                    // call that the cursor uses — this is what fixes the drift.
                    int prefixW = 0;
                    if (tok.start > 0) {
                        std::string prefix = line.substr(0, tok.start);
                        TTF_GetStringSize(font, prefix.c_str(), prefix.size(), &prefixW, nullptr);
                    }
                    float tokenX = drawX + prefixW * m_zoom;

                    // Determine color
                    SDL_Color color = defaultColor;
                    if (tok.isKeyword) {
                        color = keywordColor;
                    } else if (tok.start < line.size() && line[tok.start] == '/' &&
                            tok.start + 1 < line.size() && line[tok.start+1] == '/') {
                        color = commentColor;
                    } else if (tok.start < line.size() &&
                            (line[tok.start] == '"' || line[tok.start] == '\'')) {
                        color = stringColor;
                    }

                    // Render text with zoom scaling via texture (since TTF_SetTextScale isn't available)
                    SDL_Surface* surf = TTF_RenderText_Blended(font, sub.c_str(), (int)sub.size(), color);
                    if (surf) {
                        SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
                        if (tex) {
                            float scaledW = surf->w * m_zoom;
                            float scaledH = surf->h * m_zoom;
                            SDL_FRect dst = { tokenX, yy, scaledW, scaledH };
                            SDL_RenderTexture(renderer, tex, nullptr, &dst);
                            SDL_DestroyTexture(tex);
                        }
                        SDL_DestroySurface(surf);
                    }
                }
            }
            SDL_SetRenderClipRect(renderer, nullptr);
            // Scrollbars
            vScrollbar.render(renderer, offsetX, offsetY);
            hScrollbar.render(renderer, offsetX, offsetY);
        }

    private:
        SDL_Renderer* renderer;
        TTF_TextEngine* textEngine;
        TTF_Font* font;
        SDL_FRect rect;
        std::vector<std::string> lines;
        int cursorRow = 0, cursorCol = 0;
        float scrollX = 0, scrollY = 0;
        bool active = false;

        Gui::Scrollbar vScrollbar, hScrollbar;

        static constexpr float LINE_HEIGHT = 22.0f;
        static constexpr float PADDING = 6.0f;

        void refreshScrollbars() {
            float maxLineWidth = 0;
            for (const auto& line : lines) {
                int w=0,h=0;
                TTF_GetStringSize(font, line.c_str(), line.size(), &w, &h);
                maxLineWidth = std::max(maxLineWidth, (float)w);
            }
            float contentW = (maxLineWidth + 2*PADDING) * m_zoom;
            float contentH = (lines.size() * LINE_HEIGHT + 2*PADDING) * m_zoom;
            float viewW = rect.w - 12.0f - 2*PADDING;
            float viewH = rect.h - 12.0f - 2*PADDING;
            vScrollbar.setGeometry(rect.x + rect.w - 12, rect.y, 12, rect.h, contentH, viewH);
            hScrollbar.setGeometry(rect.x, rect.y + rect.h - 12, rect.w - 12, 12, contentW, viewW);
            vScrollbar.setOffsetNoCallback(scrollY * m_zoom);
            hScrollbar.setOffsetNoCallback(scrollX * m_zoom);
        }

        void ensureCursorVisible() {
            float viewH = (rect.h - 12.0f) / m_zoom;
            float viewW = (rect.w - 12.0f) / m_zoom;

            // Vertical
            float cursorY = cursorRow * LINE_HEIGHT;
            if (cursorY < scrollY) {
                scrollY = cursorY;
            } else if (cursorY + LINE_HEIGHT > scrollY + viewH) {
                scrollY = cursorY + LINE_HEIGHT - viewH;
            }

            // Horizontal
            std::string prefix = lines[cursorRow].substr(0, cursorCol);
            int pw = 0, ph = 0;
            TTF_GetStringSize(font, prefix.c_str(), prefix.size(), &pw, &ph);
            float fw = (float)pw;
            if (fw < scrollX) {
                scrollX = fw;
            } else if (fw > scrollX + viewW) {
                scrollX = fw - viewW;
            }

            refreshScrollbars();
        }

        int getLineLength(int row) const {
            if (row < 0 || row >= (int)lines.size()) return 0;
            return (int)lines[row].size();
        }

        int xToCol(const std::string& line, float localX) const {
            if (localX <= 0 || line.empty()) return 0;
            // Binary search or linear scan using exact prefix widths
            for (size_t i = 1; i <= line.size(); ++i) {
                std::string prefix = line.substr(0, i);
                int w = 0, h = 0;
                TTF_GetStringSize(font, prefix.c_str(), prefix.size(), &w, &h);
                if (localX < w) return (int)i - 1;
            }
            return (int)line.size();
        }

        void moveCursor(int row, int col, bool extend) {
            // For simplicity, we don't support selection, just move.
            (void)extend;
            if (row < 0) row = 0;
            if (row >= (int)lines.size()) row = (int)lines.size() - 1;
            int maxCol = getLineLength(row);
            if (col < 0) col = 0;
            if (col > maxCol) col = maxCol;
            cursorRow = row;
            cursorCol = col;
            ensureCursorVisible();
        }

        void insertChar(char ch) {
            if (cursorRow < 0 || cursorRow >= (int)lines.size()) return;
            std::string& line = lines[cursorRow];
            if (ch == '\t') {
                line.insert(cursorCol, 4, ' ');
                cursorCol += 4;
            } else {
                line.insert(cursorCol, 1, ch);
                cursorCol++;
            }
            pushUndoState();
            ensureCursorVisible();
        }

        void deleteChar() {
            if (cursorRow < 0 || cursorRow >= (int)lines.size()) return;
            std::string& line = lines[cursorRow];
            if (cursorCol < (int)line.size()) {
                line.erase(cursorCol, 1);
            } else if (cursorRow + 1 < (int)lines.size()) {
                // Merge with next line
                std::string& next = lines[cursorRow + 1];
                line += next;
                lines.erase(lines.begin() + cursorRow + 1);
            }
            pushUndoState();
            ensureCursorVisible();
        }

        void backspace() {
            if (cursorRow < 0 || cursorRow >= (int)lines.size()) return;
            if (cursorCol == 0 && cursorRow > 0) {
                // Merge with previous line
                std::string& prev = lines[cursorRow - 1];
                std::string& curr = lines[cursorRow];
                int oldLen = (int)prev.size();
                prev += curr;
                lines.erase(lines.begin() + cursorRow);
                cursorRow--;
                cursorCol = oldLen;
            } else if (cursorCol > 0) {
                std::string& line = lines[cursorRow];
                line.erase(cursorCol - 1, 1);
                cursorCol--;
            }
            pushUndoState();
            ensureCursorVisible();
        }

        void newline() {
            if (cursorRow < 0 || cursorRow >= (int)lines.size()) return;
            std::string& line = lines[cursorRow];
            std::string rest = line.substr(cursorCol);
            line.erase(cursorCol);
            lines.insert(lines.begin() + cursorRow + 1, rest);
            cursorRow++;
            cursorCol = 0;
            pushUndoState();
            ensureCursorVisible();
        }

        static bool inRect(float x, float y, SDL_FRect r) {
            return x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h;
        }
    };

    class VirtualKeyboard {
    private:
        struct VKey {
            std::string label, lower, upper;
            bool isShift = false, isBack = false, isSpace = false, isClose = false;
            SDL_FRect rect = {0,0,0,0};
            bool hovered = false;
            TTF_Text* textObj = nullptr;
            VKey() = default;
            ~VKey() { if (textObj) TTF_DestroyText(textObj); }
            VKey(const VKey&) = delete;
            VKey& operator=(const VKey&) = delete;
            VKey(VKey&& o) noexcept { *this = std::move(o); }
            VKey& operator=(VKey&& o) noexcept {
                if (this != &o) {
                    if (textObj) TTF_DestroyText(textObj);
                    label = std::move(o.label);
                    lower = std::move(o.lower);
                    upper = std::move(o.upper);
                    isShift = o.isShift; isBack = o.isBack; isSpace = o.isSpace; isClose = o.isClose;
                    rect = o.rect; hovered = o.hovered;
                    textObj = o.textObj;
                    o.textObj = nullptr;
                }
                return *this;
            }
        };

        SDL_Renderer* renderer;
        TTF_TextEngine* textEngine;
        TTF_Font* font;
        SDL_FRect rect = {0,0,0,0};
        std::vector<VKey> keys;
        bool isUpper = false;
        bool closeRequested = false;

    public:
        VirtualKeyboard(SDL_Renderer* renderer, TTF_TextEngine* textEngine, TTF_Font* font)
            : renderer(renderer), textEngine(textEngine), font(font) {}

        ~VirtualKeyboard() = default;

        void layoutKeys(SDL_FRect targetRect, float windowWidth, float windowHeight, bool numericOnly = false) {
            for (auto& k : keys) {
                if (k.textObj) {
                    TTF_DestroyText(k.textObj);
                    k.textObj = nullptr; // prevent double-free in destructor
                }
            }
            keys.clear();

            float kbY = targetRect.y + targetRect.h;
            rect = {0.0f, kbY, windowWidth, windowHeight - kbY};

            std::vector<std::vector<std::string>> labels;
            if (numericOnly) {
                labels = {
                    {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0"},
                    {".", "BACK", "CLOSE"}
                };
            } else {
                labels = {
                    {"!", "@", "#", "$", "%", "^", "&", "*", "(", ")"},
                    {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0"},
                    {"q", "w", "e", "r", "t", "y", "u", "i", "o", "p"},
                    {"a", "s", "d", "f", "g", "h", "j", "k", "l"},
                    {"SHIFT", "z", "x", "c", "v", "b", "n", "m", "BACK"},
                    {"SPACE", "CLOSE"}
                };
            }

            float rowH = rect.h / labels.size();
            for (size_t r = 0; r < labels.size(); ++r) {
                float colW = rect.w / labels[r].size();
                for (size_t c = 0; c < labels[r].size(); ++c) {
                    VKey k;
                    k.label = labels[r][c];
                    k.lower = k.label;
                    k.upper = k.label;

                    if (k.label == "BACK") { k.isBack = true; }
                    else if (k.label == "CLOSE") { k.isClose = true; }
                    else if (k.label == "SPACE") { k.isSpace = true; k.lower = " "; k.upper = " "; }
                    else if (k.label == "SHIFT") { k.isShift = true; }
                    else if (k.label.length() == 1 && std::isalpha(k.label[0])) {
                        k.upper = std::string(1, std::toupper(k.label[0]));
                    }

                    k.rect = { c * colW, kbY + r * rowH, colW, rowH };
                    std::string disp = (isUpper && !k.isShift && !k.isBack && !k.isSpace && !k.isClose && !numericOnly)
                                       ? k.upper : k.label;
                    if (k.isShift && isUpper) disp = "SHIFT (ON)";
                    k.textObj = TTF_CreateText(textEngine, font, disp.c_str(), 0);
                    keys.push_back(std::move(k));
                }
            }
        }

        void updateLabels() {
            for (auto& k : keys) {
                if (k.textObj) {
                    std::string disp = (isUpper && !k.isShift && !k.isBack && !k.isSpace && !k.isClose)
                                       ? k.upper : k.label;
                    if (k.isShift && isUpper) disp = "SHIFT (ON)";
                    TTF_SetTextString(k.textObj, disp.c_str(), 0);
                }
            }
        }

        void processKey(VKey& k, ITextInput* target) {
            if (k.isShift) {
                isUpper = !isUpper;
                updateLabels();
            } else if (k.isBack) {
                if (target) target->removeLastChar();
            } else if (k.isSpace) {
                if (target) target->appendText(" ");
            } else if (k.isClose) {
                closeRequested = true;
            } else {
                if (target) target->appendText(isUpper ? k.upper : k.lower);
            }
        }

        void handleGamepad(float cursorX, float cursorY, bool confirmDown, bool confirmDownLastFrame, ITextInput* target) {
            for (auto& k : keys) {
                bool hit = cursorX >= k.rect.x && cursorX <= k.rect.x + k.rect.w &&
                           cursorY >= k.rect.y && cursorY <= k.rect.y + k.rect.h;
                k.hovered = hit;
                if (hit && confirmDown && !confirmDownLastFrame) {
                    processKey(k, target);
                }
            }
        }

        // --- New gamepad helpers ---
        void toggleShift() {
            isUpper = !isUpper;
            updateLabels();
        }

        void requestClose() {
            closeRequested = true;
        }

        bool isCloseRequested() const { return closeRequested; }
        void resetCloseRequest() { closeRequested = false; }

        void handleMouse(const SDL_Event& ev, ITextInput* target) {
            if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN && ev.button.button == SDL_BUTTON_LEFT) {
                float mx = ev.button.x, my = ev.button.y;
                for (auto& k : keys) {
                    bool hit = mx >= k.rect.x && mx <= k.rect.x + k.rect.w &&
                               my >= k.rect.y && my <= k.rect.y + k.rect.h;
                    if (hit) { processKey(k, target); break; }
                }
            }
        }

        void render() {
            SDL_SetRenderDrawColor(renderer, 40, 40, 40, 240);
            SDL_RenderFillRect(renderer, &rect);
            for (const auto& k : keys) {
                SDL_Color bg = k.hovered ? SDL_Color{100,100,150,255} : SDL_Color{60,60,60,255};
                if (k.isShift && isUpper) bg = {150,150,50,255};
                SDL_SetRenderDrawColor(renderer, bg.r, bg.g, bg.b, bg.a);
                SDL_RenderFillRect(renderer, &k.rect);
                SDL_SetRenderDrawColor(renderer, 200,200,200,255);
                SDL_RenderRect(renderer, &k.rect);
                if (k.textObj) {
                    TTF_SetTextColor(k.textObj, 255,255,255,255);
                    int tw = 0, th = 0;
                    TTF_GetTextSize(k.textObj, &tw, &th);
                    TTF_DrawRendererText(k.textObj,
                        k.rect.x + (k.rect.w - tw) * 0.5f,
                        k.rect.y + (k.rect.h - th) * 0.5f);
                }
            }
        }
    };

    class Button : public IGuiElement {
    public:
        Button(SDL_Renderer* renderer, TTF_Font* font, const std::string& text, SDL_FPoint pos, float w, float h)
            : renderer(renderer), font(font), textStr(text), position(pos), width(w), height(h) {
            idleColor = {100,100,200,255}; 
            hoverColor = {150,150,250,255}; 
            pressColor = {50,50,150,255};
            textColor = {255,255,255,255};
            currentColor = idleColor;
            updateTextTexture();
        }

        ~Button() { if (textTexture) SDL_DestroyTexture(textTexture); }

        std::string getType() const override { return "Button"; }
        float getX() const override { return position.x; }
        float getY() const override { return position.y; }
        float getWidth() const override { return width; }
        float getHeight() const override { return height; }
        void setPos(SDL_Point p) override {position.x = p.x; position.y = p.y;};
        void setRect(SDL_FRect r) override { position = {r.x, r.y}; width = r.w; height = r.h; }
        const std::string& getText() const { return textStr; }
        void setText(const std::string& t) { textStr = t; updateTextTexture(); }

        // IGuiElement pure virtual overrides — delegate to Impl helpers
        void render(float offsetX = 0.0f, float offsetY = 0.0f) override { renderImpl(offsetX, offsetY); }
        void handleGamepad(float cursorX, float cursorY, float offsetX, float offsetY, SDL_Window* /*window*/,
                        bool confirmDown, bool confirmDownLastFrame) override {
            handleGamepadImpl(cursorX, cursorY, offsetX, offsetY, confirmDown, confirmDownLastFrame);
        }

        void handleGamepad(float cursorX, float cursorY, float offsetX, float offsetY, bool confirmDown, bool confirmDownLastFrame) {
            handleGamepadImpl(cursorX, cursorY, offsetX, offsetY, confirmDown, confirmDownLastFrame);
        }
        void render(float offsetX, float offsetY, float /*cursorX*/, float /*cursorY*/) { renderImpl(offsetX, offsetY); }

        std::function<void()> onClicked;

        SDL_FPoint getPosition() { return position; }

    private:
        SDL_Renderer* renderer;
        TTF_Font* font;
        std::string textStr;
        SDL_FPoint position;
        float width, height;
        float textWidth = 0, textHeight = 0;
        SDL_Color idleColor, hoverColor, pressColor, textColor, currentColor;
        bool isHovered = false, isPressed = false;
        SDL_Texture* textTexture = nullptr;

    public:

        void handleEventImpl(const SDL_Event& e, float offsetX, float offsetY) {
            if (e.type == SDL_EVENT_MOUSE_MOTION || e.type == SDL_EVENT_MOUSE_BUTTON_DOWN || e.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                float mouseX, mouseY;
                if (e.type == SDL_EVENT_MOUSE_MOTION) { mouseX = e.motion.x; mouseY = e.motion.y; }
                else { mouseX = e.button.x; mouseY = e.button.y; }
                float visualX = position.x - offsetX;
                float visualY = position.y - offsetY;
                bool inside = (mouseX >= visualX) && (mouseX <= visualX + width) &&
                            (mouseY >= visualY) && (mouseY <= visualY + height);
                if (!inside) {
                    isHovered = false; isPressed = false; currentColor = idleColor;
                } else {
                    isHovered = true;
                    if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT) {
                        isPressed = true; currentColor = pressColor;
                    } else if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_LEFT) {
                        isPressed = false;
                        if (onClicked) onClicked();
                    }
                    if (!isPressed) currentColor = hoverColor;
                }
            }
        }

        bool handleEvent(const SDL_Event & ev, SDL_Window* window, float offsetX, float offsetY) override {
            handleEventImpl(ev, offsetX, offsetY);
            if (ev.type == SDL_EVENT_MOUSE_MOTION || ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN || ev.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                float mouseX = (ev.type == SDL_EVENT_MOUSE_MOTION) ? ev.motion.x : ev.button.x;
                float mouseY = (ev.type == SDL_EVENT_MOUSE_MOTION) ? ev.motion.y : ev.button.y;
                float visualX = position.x - offsetX;
                float visualY = position.y - offsetY;
                bool inside = (mouseX >= visualX) && (mouseX <= visualX + width) &&
                            (mouseY >= visualY) && (mouseY <= visualY + height);
                if (inside) return true;
            }
            return false;
        }

        void handleGamepadImpl(float cursorX, float cursorY, float offsetX, float offsetY, bool confirmDown, bool confirmDownLastFrame) {
            float visualX = position.x - offsetX;
            float visualY = position.y - offsetY;
            bool inside = (cursorX >= visualX) && (cursorX <= visualX + width) &&
                        (cursorY >= visualY) && (cursorY <= visualY + height);
            if (!inside) {
                if (!isHovered) { isPressed = false; currentColor = idleColor; }
                return;
            }
            isHovered = true;
            if (confirmDown && !confirmDownLastFrame) {
                isPressed = true; currentColor = pressColor;
            } else if (!confirmDown && confirmDownLastFrame && isPressed) {
                isPressed = false;
                if (onClicked) onClicked();
            }
            if (!isPressed) currentColor = hoverColor;
        }

        void renderImpl(float offsetX, float offsetY) {
            SDL_FRect fillRect = { position.x - offsetX, position.y - offsetY, width, height };
            SDL_SetRenderDrawColor(renderer, currentColor.r, currentColor.g, currentColor.b, currentColor.a);
            SDL_RenderFillRect(renderer, &fillRect);
            if (textTexture) {
                SDL_FRect renderQuad = {
                    (position.x - offsetX) + (width - textWidth) / 2.0f,
                    (position.y - offsetY) + (height - textHeight) / 2.0f,
                    textWidth, textHeight
                };
                SDL_RenderTexture(renderer, textTexture, nullptr, &renderQuad);
            }
        }

        void updateTextTexture() {
            if (textTexture) SDL_DestroyTexture(textTexture);
            SDL_Surface* surf = TTF_RenderText_Blended(font, textStr.c_str(), 0, textColor);
            if (surf) {
                textWidth = (float)surf->w; textHeight = (float)surf->h;
                textTexture = SDL_CreateTextureFromSurface(renderer, surf);
                SDL_DestroySurface(surf);
            }
        }
    };

    // ----------------------------------------------------------------
    // Panel — coloured rectangle that owns child IGuiElements and an
    // internal Scrollbar.  The panel clips all child hit-testing and
    // rendering to its own bounds; it does NOT scroll with the editor
    // scrollbar (offsetY is always ignored for its own position and
    // passed as 0 to its children so they render relative to the panel).
    // ----------------------------------------------------------------
    class Panel : public IGuiElement {
    public:
        SDL_Color bgColor   = { 40,  40,  48, 230 };
        SDL_Color borderColor = { 80, 80, 100, 255 };
        bool      showBorder  = true;

        Panel(SDL_Renderer* renderer, TTF_TextEngine* textEngine, TTF_Font* font,
              SDL_FRect rect, SDL_Color bg = {40,40,48,230})
            : renderer(renderer), textEngine(textEngine), font(font),
              rect(rect), bgColor(bg)
        {
            const float sbW = 10.0f;
            scrollbar.setGeometry(rect.x + rect.w - sbW - 2,
                                  rect.y + 2, sbW, rect.h - 4,
                                  computeContentHeight(), rect.h - 4);
            scrollbar.onChange = [this](float v){ panelScrollOffset = v; };
        }

        // IGuiElement interface — Panel lives in scene-space (no editor scroll)
        std::string getType()   const override { return "Panel"; }
        float getX()            const override { return rect.x; }
        float getY()            const override { return rect.y; }
        float getWidth()        const override { return rect.w; }
        float getHeight()       const override { return rect.h; }
        void  setRect(SDL_FRect r) override {
            const float dx = r.x - rect.x;
            const float dy = r.y - rect.y;
            rect = r;
            if (dx != 0.0f || dy != 0.0f) shiftChildren(dx, dy);
            refreshScrollbarGeometry();
        }
        void setPos(SDL_Point p) override {
            const float dx = (float)p.x - rect.x;
            const float dy = (float)p.y - rect.y;
            rect.x = p.x; rect.y = p.y;
            if (dx != 0.0f || dy != 0.0f) shiftChildren(dx, dy);
            refreshScrollbarGeometry();
        };

        // Children are owned by the Panel
        void addChild(std::unique_ptr<IGuiElement> child) {
            children.push_back(std::move(child));
            refreshScrollbarGeometry();
        }
        const std::vector<std::unique_ptr<IGuiElement>>& getChildren() const { return children; }
        std::vector<std::unique_ptr<IGuiElement>>& getChildrenMutable() { return children; }

        // Returns the child (or this panel) that the screen-space point hits, for editor picking.
        // Children are tested first (they're on top visually).
        IGuiElement* hitTest(float screenX, float screenY) {
            if (!inRect(screenX, screenY, rect)) return nullptr;
            // Test children in reverse order (last added = topmost visually)
            for (int i = (int)children.size() - 1; i >= 0; --i) {
                auto* c = children[i].get();
                // Child positions are absolute screen coords; their Y is unscrolled here
                // because we want editor picking to always work regardless of panel scroll.
                SDL_FRect cr = { c->getX(), c->getY() - panelScrollOffset,
                                 c->getWidth(), c->getHeight() };
                if (inRect(screenX, screenY, cr)) return c;
            }
            return this; // hit the panel itself
        }

        // ------------------------------------------------------------------
        // render — Panel ignores the caller's offsetY completely; it is always
        // drawn at its scene-space position.  Children are scrolled by the
        // panel's own panelScrollOffset.
        // ------------------------------------------------------------------
        void render(float offsetX = 0.0f, float editorOffsetY = 0.0f) override {
            SDL_FRect originalRect = rect;
            rect.x -= offsetX;
            rect.y -= editorOffsetY;
            // ... Background and border drawing ...
            SDL_Rect clip = { (int)rect.x, (int)rect.y, (int)rect.w, (int)rect.h };
            SDL_SetRenderClipRect(renderer, &clip);
            for (auto& child : children)
                child->render(offsetX, editorOffsetY + panelScrollOffset);
            SDL_SetRenderClipRect(renderer, nullptr);
            scrollbar.render(renderer, offsetX, editorOffsetY);
            //for (auto& child : children)
                //child->renderSelectionOutline(renderer, offsetX, editorOffsetY + panelScrollOffset);
            renderSelectionOutline(renderer, offsetX, editorOffsetY);
            rect = originalRect; // Restore
        }

        // ------------------------------------------------------------------
        // handleEvent — only processes events whose mouse position is within
        // the panel.  Wheel events inside the panel scroll the panel, not
        // the editor.
        // ------------------------------------------------------------------
        bool handleEvent(const SDL_Event& ev, SDL_Window* window, float offsetX = 0.0f, float editorOffsetY = 0.0f) override {
            float mx = 0, my = 0;
            bool hasPos = getEventPos(ev, mx, my);
            SDL_FRect shiftedRect = { rect.x - offsetX, rect.y - editorOffsetY, rect.w, rect.h };

            if (ev.type == SDL_EVENT_MOUSE_WHEEL) {
                if (hasPos && inRect(mx, my, shiftedRect)) {
                    float origTX = scrollbar.trackX, origTY = scrollbar.trackY;
                    scrollbar.trackX -= offsetX; scrollbar.trackY -= editorOffsetY;
                    bool res = scrollbar.handleEvent(ev);
                    scrollbar.trackX = origTX; scrollbar.trackY = origTY;
                    return res;
                }
                return false;
            }
            if (hasPos && !inRect(mx, my, shiftedRect)) return false;
            
            float origTX = scrollbar.trackX, origTY = scrollbar.trackY;
            scrollbar.trackX -= offsetX; scrollbar.trackY -= editorOffsetY;
            bool sbRes = scrollbar.handleEvent(ev);
            scrollbar.trackX = origTX; scrollbar.trackY = origTY;
            if (sbRes) return true;

            for (auto& child : children)
                if (child->handleEvent(ev, window, offsetX, editorOffsetY + panelScrollOffset)) return true;
            return inRect(mx, my, shiftedRect);
        }

        void handleGamepad(float cursorX, float cursorY, float offsetX, float editorOffsetY, SDL_Window* window, bool confirmDown, bool confirmDownLastFrame) override {
            SDL_FRect shiftedRect = { rect.x - offsetX, rect.y - editorOffsetY, rect.w, rect.h };
            if (!inRect(cursorX, cursorY, shiftedRect)) return;
            for (auto& child : children)
                child->handleGamepad(cursorX, cursorY, offsetX, editorOffsetY + panelScrollOffset, window, confirmDown, confirmDownLastFrame);
        }

        float getPanelScrollOffset() const { return panelScrollOffset; }
        Scrollbar& getScrollbar() { return scrollbar; }

    private:
        SDL_Renderer*    renderer;
        TTF_TextEngine*  textEngine;
        TTF_Font*        font;
        SDL_FRect        rect;
        float            panelScrollOffset = 0.0f;
        Scrollbar        scrollbar;
        std::vector<std::unique_ptr<IGuiElement>> children;

        void refreshScrollbarGeometry() {
            const float sbW = 10.0f;
            scrollbar.setGeometry(rect.x + rect.w - sbW - 2,
                                  rect.y + 2, sbW, rect.h - 4,
                                  computeContentHeight(), rect.h - 4);
        }

        // Moves every child by (dx, dy) so they stay glued to the panel
        // whenever the panel itself is repositioned (e.g. re-centered on
        // window resize). Without this, children keep their old baked
        // absolute coordinates and visually detach from the panel.
        void shiftChildren(float dx, float dy) {
            for (auto& c : children) {
                c->setPos(SDL_Point{
                    (int)std::lround(c->getX() + dx),
                    (int)std::lround(c->getY() + dy)
                });
            }
        }

        float computeContentHeight() const {
            // NOTE: child Y is absolute screen-space; measure relative to
            // the panel's own rect.y, not the raw absolute value, or the
            // "does this content need to scroll" result (and therefore
            // scrollbar visibility) ends up depending on where the panel
            // happens to sit on screen instead of actual content size.
            float bottom = 0;
            for (auto& c : children)
                bottom = std::max(bottom, (c->getY() - rect.y) + c->getHeight());
            return bottom + 8.0f; // small padding
        }

        static bool inRect(float x, float y, SDL_FRect r) {
            return x >= r.x && x <= r.x+r.w && y >= r.y && y <= r.y+r.h;
        }
        // Extract pointer position from various SDL event types
        static bool getEventPos(const SDL_Event& ev, float& x, float& y) {
            if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN || ev.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                x = ev.button.x; y = ev.button.y; return true;
            }
            if (ev.type == SDL_EVENT_MOUSE_MOTION) {
                x = ev.motion.x; y = ev.motion.y; return true;
            }
            if (ev.type == SDL_EVENT_MOUSE_WHEEL) {
                SDL_GetMouseState(&x, &y); return true;
            }
            return false;
        }
    };

    // ----------------------------------------------------------------
    // Base Container – manages children layout
    // ----------------------------------------------------------------
    class Container : public IGuiElement {
    public:
        virtual ~Container() = default;

        void addChild(std::unique_ptr<IGuiElement> child) {
            children.push_back(std::move(child));
            layoutChildren();
        }

        void setPadding(float p) { padding = p; layoutChildren(); }
        void setSpacing(float s) { spacing = s; layoutChildren(); }

        float getPadding() const { return padding; }
        float getSpacing() const { return spacing; }

        std::string getType() const override { return "Container"; }
        float getX() const override { return rect.x; }
        float getY() const override { return rect.y; }
        float getWidth() const override { return rect.w; }
        float getHeight() const override { return rect.h; }
        void setRect(SDL_FRect r) override {
            rect = r;
            layoutChildren();
        }
        void setPos(SDL_Point p) override {
            rect.x = p.x; 
            rect.y = p.y;
            layoutChildren();
        }

        void render(float offsetX, float offsetY) override {
            for (auto& child : children) child->render(offsetX, offsetY);
        }

        bool handleEvent(const SDL_Event& e, SDL_Window* window, float offsetX, float offsetY) override {
            bool consumed = false;
            for (auto& child : children)
                if (child->handleEvent(e, window, offsetX, offsetY)) consumed = true;
            return consumed;
        }

        void handleGamepad(float cursorX, float cursorY, float offsetX, float offsetY, SDL_Window* window, bool confirmDown, bool confirmDownLastFrame) override {
            for (auto& child : children)
                child->handleGamepad(cursorX, cursorY, offsetX, offsetY, window, confirmDown, confirmDownLastFrame);
        }

        const std::vector<std::unique_ptr<IGuiElement>>& getChildren() const { return children; }

    protected:
        std::vector<std::unique_ptr<IGuiElement>> children;
        SDL_FRect rect = {0, 0, 0, 0};
        float padding = 5.0f;
        float spacing = 5.0f;

        virtual void layoutChildren() = 0;
    };

    class ScrollableContainer : public IGuiElement {
    public:
        ScrollableContainer(SDL_Renderer* renderer, TTF_TextEngine* textEngine, TTF_Font* font,
                            ScrollOrientation orientation = ScrollOrientation::Vertical)
            : renderer(renderer), textEngine(textEngine), font(font), orientation(orientation)
        {
            scrollbar.setOrientation(orientation);
            scrollbar.onChange = [this](float v) { scrollOffset = v; };
        }

        // Set the single child that will be scrolled.
        void setChild(std::unique_ptr<IGuiElement> child) {
            this->child = std::move(child);
            recalcContentSize();
        }

        // IGuiElement overrides
        std::string getType() const override { return "ScrollableContainer"; }
        float getX() const override { return rect.x; }
        float getY() const override { return rect.y; }
        float getWidth() const override { return rect.w; }
        float getHeight() const override { return rect.h; }

        void setRect(SDL_FRect r) override {
            rect = r;
            recalcContentSize();
        }
        void setPos(SDL_Point p) override {
            rect.x = (float)p.x;
            rect.y = (float)p.y;
            recalcContentSize();
        }

        void render(float offsetX, float offsetY) override {
            if (child) {
                float childOffX = offsetX + (orientation == ScrollOrientation::Horizontal ? scrollOffset : 0.0f);
                float childOffY = offsetY + (orientation == ScrollOrientation::Vertical   ? scrollOffset : 0.0f);
                child->render(childOffX, childOffY);
            }
            scrollbar.render(renderer, offsetX, offsetY);
        }

        bool handleEvent(const SDL_Event& ev, SDL_Window* window, float offsetX, float offsetY) override {
            if (scrollbar.handleEvent(ev)) return true;
            if (child) {
                float childOffX = offsetX + (orientation == ScrollOrientation::Horizontal ? scrollOffset : 0.0f);
                float childOffY = offsetY + (orientation == ScrollOrientation::Vertical   ? scrollOffset : 0.0f);
                return child->handleEvent(ev, window, childOffX, childOffY);
            }
            return false;
        }

        void handleGamepad(float cursorX, float cursorY, float offsetX, float offsetY,
                        SDL_Window* window, bool confirmDown, bool confirmDownLastFrame) override {
            if (child) {
                float childOffX = offsetX + (orientation == ScrollOrientation::Horizontal ? scrollOffset : 0.0f);
                float childOffY = offsetY + (orientation == ScrollOrientation::Vertical   ? scrollOffset : 0.0f);
                child->handleGamepad(cursorX, cursorY, childOffX, childOffY,
                                    window, confirmDown, confirmDownLastFrame);
            }
        }

        // Expose the scrollbar if needed (e.g. for custom styling).
        Scrollbar& getScrollbar() { return scrollbar; }

    private:
        SDL_Renderer* renderer;
        TTF_TextEngine* textEngine;
        TTF_Font* font;
        SDL_FRect rect;
        ScrollOrientation orientation;
        Scrollbar scrollbar;
        float scrollOffset = 0.0f;
        std::unique_ptr<IGuiElement> child;

        void recalcContentSize() {
            if (!child) return;

            float contentSize = 0.0f;
            float viewSize = 0.0f;
            const float sbThickness = 12.0f;

            if (orientation == ScrollOrientation::Vertical) {
                contentSize = child->getHeight();
                viewSize = rect.h;
                scrollbar.setGeometry(rect.x + rect.w - sbThickness, rect.y,
                                    sbThickness, rect.h,
                                    contentSize, viewSize);
            } else { // Horizontal
                contentSize = child->getWidth();
                viewSize = rect.w;
                scrollbar.setGeometry(rect.x, rect.y + rect.h - sbThickness,
                                    rect.w, sbThickness,
                                    contentSize, viewSize);
            }

            // Clamp the scroll offset to the new range.
            scrollOffset = std::clamp(scrollOffset, 0.0f, scrollbar.maxOffset());
        }
    };

    // ----------------------------------------------------------------
    // HBoxContainer – arranges children horizontally without resizing them
    // ----------------------------------------------------------------
    class HBoxContainer : public Container {
    public:
        std::string getType() const override { return "HBox"; }

    protected:
        void layoutChildren() override {
            if (children.empty()) return;
            float x = rect.x + padding;
            float y = rect.y + padding;
            float maxHeight = 0;
            for (auto& child : children)
                maxHeight = std::max(maxHeight, child->getHeight());
            for (auto& child : children) {
                float childW = child->getWidth();
                float childH = child->getHeight();
                float childY = y + (maxHeight - childH) * 0.5f;
                child->setRect({ x, childY, childW, childH });
                x += childW + spacing;
            }
        }
    };

    class RightAlignedHBoxContainer : public Container {
    public:
        std::string getType() const override { return "RightAlignedHBox"; }

    protected:
        void layoutChildren() override {
            if (children.empty()) return;
            float totalWidth = padding;
            for (auto& child : children) totalWidth += child->getWidth() + spacing;
            totalWidth -= spacing; // remove last spacing
            float x = rect.x + rect.w - totalWidth;
            float y = rect.y + padding;
            float maxHeight = 0;
            for (auto& child : children) maxHeight = std::max(maxHeight, child->getHeight());
            for (auto& child : children) {
                float childH = child->getHeight();
                float childY = y + (maxHeight - childH) * 0.5f;
                child->setRect({ x, childY, child->getWidth(), childH });
                x += child->getWidth() + spacing;
            }
        }
    };

    // ----------------------------------------------------------------
    // VBoxContainer – arranges children vertically without resizing them
    // ----------------------------------------------------------------
    class VBoxContainer : public Container {
    public:
        std::string getType() const override { return "VBox"; }

    protected:
        void layoutChildren() override {
            if (children.empty()) return;
            float x = rect.x + padding;
            float y = rect.y + padding;
            float maxWidth = 0;
            for (auto& child : children)
                maxWidth = std::max(maxWidth, child->getWidth());
            for (auto& child : children) {
                float childW = child->getWidth();
                float childH = child->getHeight();
                float childX = x + (maxWidth - childW) * 0.5f;
                child->setRect({ childX, y, childW, childH });
                y += childH + spacing;
            }
        }
    };
    
    class Dialog {
    public:
        enum class Action { None, Confirm, Cancel };

        virtual ITextInput* getCurrentTextInput() { return nullptr; }

        Dialog(const Dialog&) = delete;
        Dialog& operator=(const Dialog&) = delete;
        virtual ~Dialog() = default;

        float progress = 0.0f;
        DialogState state = DialogState::Closed;

        void open() {
            progress = 0.0f;
            state = DialogState::Opening;
            onOpen();
        }

        bool isOpen() const {
            return state == DialogState::Opening || state == DialogState::Opened;
        }

        virtual void tick(float dt) {
            if (state == DialogState::Opening) {
                progress += dt * 4.0f;
                if (progress >= 1.0f) { progress = 1.0f; state = DialogState::Opened; }
            }
        }

        void setNewTitle(std::string newTitle) {
            title = std::move(newTitle);
        }

        void setConfirmButtonText(const std::string& text) { confirmLabel = text; }

        // When false, the built-in close (X), Confirm and Cancel buttons are
        // neither drawn nor hit-tested. Set this from a derived dialog's
        // constructor if it supplies its own buttons via onRender().
        void setShowDefaultButtons(bool show) { showDefaultButtons = show; }


        Action handleEvent(const SDL_Event& ev) {
            if (state != DialogState::Opened) return Action::None;
            SDL_FRect win = animRect();
            if (onHandleEvent(ev)) return Action::None;
            auto hit = [](float x, float y, SDL_FRect r) {
                return x >= r.x && x <= r.x+r.w && y >= r.y && y <= r.y+r.h;
            };
            if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN && ev.button.button == SDL_BUTTON_LEFT) {
                float mx = ev.button.x, my = ev.button.y;
                if (showDefaultButtons) {
                    if (hit(mx, my, closeBtnRect(win)) || hit(mx, my, cancelBtnRect(win)))
                        return Action::Cancel;
                    if (hit(mx, my, confirmBtnRect(win)))
                        return Action::Confirm;
                }
                if (hit(mx, my, win)) return Action::None;
            }
            if (showDefaultButtons && ev.type == SDL_EVENT_KEY_DOWN) {
                if (ev.key.key == SDLK_RETURN) return Action::Confirm;
                if (ev.key.key == SDLK_ESCAPE) return Action::Cancel;
            }
            return Action::None;
        }

        virtual bool onHandleGamepad(float, float, bool, bool) { return false; }

        Action handleGamepad(float cursorX, float cursorY, bool confirmDown, bool confirmDownLastFrame) {
            if (state != DialogState::Opened) return Action::None;
            SDL_FRect win = animRect();
            if (onHandleGamepad(cursorX, cursorY, confirmDown, confirmDownLastFrame)) return Action::None;
            if (showDefaultButtons && confirmDown && !confirmDownLastFrame) {
                if (hit(cursorX, cursorY, closeBtnRect(win)) || hit(cursorX, cursorY, cancelBtnRect(win)))
                    return Action::Cancel;
                if (hit(cursorX, cursorY, confirmBtnRect(win)))
                    return Action::Confirm;
            }
            return Action::None;
        }

        void render() {
            SDL_FRect win = animRect();
            bool fullOpen = (state == DialogState::Opened);
            SDL_FRect shadow = { win.x-10, win.y, win.w+20, win.h+10 };
            fillRect(shadow, {52,110,235,200});
            fillRect(win, {212,208,200,240});
            SDL_FRect titleBar = { win.x, win.y, win.w, 36.0f * progress };
            fillRect(titleBar, {52,110,235,255});
            if (!fullOpen || !font) return;
            float mx, my; SDL_GetMouseState(&mx, &my);
            drawText(title.c_str(), win.x + 8.0f, win.y + 9.0f, {255,255,255,255});
            if (showDefaultButtons) {
                SDL_FRect cb = closeBtnRect(win);
                bool hCb = hit(mx, my, cb);
                fillRect(cb, hCb ? SDL_Color{220,60,60,255} : SDL_Color{170,40,40,255});
                drawRect(cb, hCb ? SDL_Color{255,255,255,255} : SDL_Color{200,200,200,255});
                drawText("X", cb.x + 11.0f, cb.y + 8.0f, {255,255,255,255});
            }
            onRender(win);
            if (showDefaultButtons) {
                SDL_FRect conf = confirmBtnRect(win);
                bool hConf = hit(mx, my, conf);
                fillRect(conf, hConf ? SDL_Color{80,200,100,255} : SDL_Color{52,160,75,255});
                drawText(confirmLabel.c_str(), conf.x + 10.0f, conf.y + 8.0f, {255,255,255,255});
                SDL_FRect canc = cancelBtnRect(win);
                bool hCanc = hit(mx, my, canc);
                fillRect(canc, hCanc ? SDL_Color{180,60,60,255} : SDL_Color{140,40,40,255});
                drawText(cancelLabel.c_str(), canc.x + 10.0f, canc.y + 8.0f, {255,255,255,255});
            }
        }

        void reset() {
            progress = 0.0f;
            state = DialogState::Closed;
            onReset();
        }

    protected:
        Dialog(SDL_Renderer* renderer, TTF_TextEngine* textEngine,
               TTF_Font* font, SDL_Window* window,
               SDL_FRect logicalRect,
               std::string title = "Dialog",
               std::string confirmLabel = "Confirm",
               std::string cancelLabel = "Cancel")
            : renderer(renderer), textEngine(textEngine),
              font(font), window(window),
              logicalRect(logicalRect),
              title(std::move(title)),
              confirmLabel(std::move(confirmLabel)),
              cancelLabel(std::move(cancelLabel)) {}

        virtual void onOpen() {}
        virtual bool onHandleEvent(const SDL_Event&) { return false; }
        virtual void onRender(SDL_FRect) {}
        virtual void onReset() {}

        SDL_Renderer* renderer;
        TTF_TextEngine* textEngine;
        TTF_Font* font;
        SDL_Window* window;
        SDL_FRect logicalRect;

        SDL_FRect animRect() const {
            float cx = logicalRect.x + logicalRect.w * 0.5f;
            float cy = logicalRect.y + logicalRect.h * 0.5f;
            return { cx - logicalRect.w * 0.5f * progress,
                     cy - logicalRect.h * 0.5f * progress,
                     logicalRect.w * progress,
                     logicalRect.h * progress };
        }

        void fillRect(SDL_FRect r, SDL_Color c) {
            SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, c.a);
            SDL_RenderFillRect(renderer, &r);
        }
        void drawRect(SDL_FRect r, SDL_Color c) {
            SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, c.a);
            SDL_RenderRect(renderer, &r);
        }
        void drawText(const char* s, float x, float y, SDL_Color c) {
            TTF_Text* t = TTF_CreateText(textEngine, font, s, 0);
            if (!t) return;
            TTF_SetTextColor(t, c.r, c.g, c.b, c.a);
            TTF_DrawRendererText(t, x, y);
            TTF_DestroyText(t);
        }
        static bool hit(float x, float y, SDL_FRect r) {
            return x >= r.x && x <= r.x+r.w && y >= r.y && y <= r.y+r.h;
        }

    private:
        std::string title, confirmLabel, cancelLabel;
        bool showDefaultButtons = true;
        static SDL_FRect closeBtnRect(SDL_FRect w) { return { w.x+w.w-20, w.y, 36, 36 }; }
        static SDL_FRect confirmBtnRect(SDL_FRect w) { return { w.x+w.w-120, w.y+w.h-50, 106, 36 }; }
        static SDL_FRect cancelBtnRect(SDL_FRect w) { return { w.x+14, w.y+w.h-50, 106, 36 }; }
    };

    // ----------------------------------------------------------------
    // SceneInspector — right-side panel that shows editable properties
    // for whichever IGuiElement (or entity) is currently selected.
    // Lives entirely in screen-space; does not scroll with the editor.
    // ----------------------------------------------------------------
    class AddChildDialog : public Dialog {
    public:
        AddChildDialog(SDL_Renderer* renderer, TTF_TextEngine* textEngine, TTF_Font* font,
                    SDL_Window* window, Panel* parentPanel);

        bool onHandleGamepad(float cursorX, float cursorY, bool confirmDown, bool confirmDownLastFrame) override;

        Panel* parentPanel = nullptr;   // the panel we are adding a child to
        std::vector<std::string> options = {"Button", "LineEdit", "SpinBox"};
        OptionBox typeOption;

    protected:
        void onOpen() override;
        bool onHandleEvent(const SDL_Event& ev) override;
        void onRender(SDL_FRect win) override;
        void onReset() override;
    };

    class CheckBox : public IGuiElement {
    public:
        CheckBox(SDL_Renderer* renderer, TTF_TextEngine* textEngine, TTF_Font* font,
                SDL_FRect rect, bool initial = false)
            : renderer(renderer), textEngine(textEngine), font(font), rect(rect), value(initial)
        {
            checkTexture = g_resources.TextureManager.Get("checkmark");
            if (!checkTexture) {
                g_resources.TextureManager.Load("checkmark", getAssetsPath() + "checkmark.svg");
                checkTexture = g_resources.TextureManager.Get("checkmark");
            }
        }

        std::string getType() const override { return "CheckBox"; }
        float getX() const override { return rect.x; }
        float getY() const override { return rect.y; }
        float getWidth() const override { return rect.w; }
        float getHeight() const override { return rect.h; }
        void setRect(SDL_FRect r) override { rect = r; }
        void setPos(SDL_Point p) override { rect.x = p.x; rect.y = p.y; }

        bool        getValue() const { return value; }
        void setValue(bool v) { value = v; }

        void render(float offsetX, float offsetY) override {
            SDL_FRect r = { rect.x - offsetX, rect.y - offsetY, rect.w, rect.h };
            SDL_Color bg = value ? SDL_Color{60, 100, 200, 255} : SDL_Color{200, 200, 200, 255};
            SDL_SetRenderDrawColor(renderer, bg.r, bg.g, bg.b, bg.a);
            SDL_RenderFillRect(renderer, &r);
            SDL_SetRenderDrawColor(renderer, 80, 80, 80, 255);
            SDL_RenderRect(renderer, &r);

            if (value && checkTexture) {
                SDL_FRect dst = { r.x + (r.w - 32)*0.5f, r.y + (r.h - 32)*0.5f, 32, 32 };
                SDL_RenderTexture(renderer, checkTexture, nullptr, &dst);
            }
        }

        bool handleEvent(const SDL_Event& ev, SDL_Window* window, float offsetX, float offsetY) override {
            if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN && ev.button.button == SDL_BUTTON_LEFT) {
                float mx = ev.button.x, my = ev.button.y;
                SDL_FRect r = { rect.x - offsetX, rect.y - offsetY, rect.w, rect.h };
                if (mx >= r.x && mx <= r.x + r.w && my >= r.y && my <= r.y + r.h) {
                    value = !value;
                    return true;
                }
            }
            return false;
        }

        void handleGamepad(float cursorX, float cursorY, float offsetX, float offsetY,
                        SDL_Window* window, bool confirmDown, bool confirmDownLastFrame) override {
            SDL_FRect r = { rect.x - offsetX, rect.y - offsetY, rect.w, rect.h };
            if (confirmDown && !confirmDownLastFrame) {
                if (cursorX >= r.x && cursorX <= r.x + r.w &&
                    cursorY >= r.y && cursorY <= r.y + r.h) {
                    value = !value;
                }
            }
        }

    private:
        SDL_Renderer* renderer;
        TTF_TextEngine* textEngine;
        TTF_Font* font;
        SDL_FRect rect;
        bool value;
        SDL_Texture* checkTexture = nullptr;
    };

    class FileExplorer : public Dialog {
    public:
        FileExplorer(SDL_Renderer* r, TTF_TextEngine* te, TTF_Font* f,
            SDL_Window* w, const std::string& rootPath, const std::string& filter)
        : Dialog(r, te, f, w, {0,0,600,500}, "Open File", "Open", "Cancel"),
        currentPath(rootPath.empty() ? std::filesystem::current_path().string() : rootPath),
        rootPath(rootPath.empty() ? std::filesystem::current_path().string() : rootPath),
        filter(filter),
        pathEdit(r, te, f, {0,0,1,1}, "Path..."),
        renameEdit(r, te, f, {0,0,1,1}, ""),
        filenameEdit(r, te, f, {0,0,1,1}, "filename"),
        renderer(r), font(f), textEngine(te)
        {
            listScrollbar.setOrientation(Gui::ScrollOrientation::Vertical);
            listScrollbar.onChange = [this](float v) { listScrollOffset = v; };
            refreshEntries();
            loadIcons();
        }

        ~FileExplorer() {
            if (folderIcon)  SDL_DestroyTexture(folderIcon);
            if (cppIcon)     SDL_DestroyTexture(cppIcon);
            if (hIcon)       SDL_DestroyTexture(hIcon);
            if (sceneIcon)   SDL_DestroyTexture(sceneIcon);
            if (textIcon)    SDL_DestroyTexture(textIcon);
            if (musicIcon)   SDL_DestroyTexture(musicIcon);
            if (imgIcon)     SDL_DestroyTexture(imgIcon);
        }

        void setCallback(std::function<void(const std::string&)> cb) { callback = cb; }
        void setFilter(const std::string& f) { filter = f; }

        void setCurrentPath(const std::string& path) {
            currentPath = path;
            refreshEntries();
        }

        void goToRoot() {
            currentPath = rootPath;
            refreshEntries();
        }

        bool isSaveMode() const { return saveMode; }

        void setSaveMode(bool enabled, const std::string& ext) {
            saveMode = enabled;
            saveExtension = ext;
            if (enabled) {
                setNewTitle("Save File");
                setConfirmButtonText("Save");
                filenameEdit.clear();
                if (!ext.empty()) filenameEdit.appendText("new_file" + ext);
            } else {
                setNewTitle("Open File");
                setConfirmButtonText("Open");
            }
        }

        void triggerSaveCallback() {
            if (!callback) return;
            std::string fname = filenameEdit.getText();
            if (fname.empty()) return;
            if (!saveExtension.empty() && fname.find(saveExtension) == std::string::npos) {
                fname += saveExtension;
            }
            std::string fullPath = (std::filesystem::path(currentPath) / fname).string();
            callback(fullPath);
        }

    private:
        bool saveMode = false;
        std::string saveExtension = "";
        Gui::LineEdit filenameEdit;

        SDL_FRect getItemContextMenuRect() const {
            float w = 160.0f, h = 28.0f * itemContextMenuItems.size() + 6.0f;
            float x = itemContextMenuX, y = itemContextMenuY;
            SDL_FRect win = animRect();
            if (x + w > win.x + win.w) x = win.x + win.w - w - 10;
            if (y + h > win.y + win.h) y = win.y + win.h - h - 10;
            if (x < win.x + 10) x = win.x + 10;
            if (y < win.y + 70) y = win.y + 70;
            return { x, y, w, h };
        }
        


        void commitRename() {
            std::string newName = renameEdit.getText();
            if (renameIndex >= 0 && renameIndex < (int)entries.size()) {
                std::string oldName = entries[renameIndex];
                
                if (!newName.empty() && newName != oldName) {
                    std::filesystem::path oldPath = std::filesystem::path(currentPath) / oldName;
                    std::filesystem::path newPath = std::filesystem::path(currentPath) / newName;
                    
                    if (!std::filesystem::exists(newPath)) {
                        std::error_code ec;
                        std::filesystem::rename(oldPath, newPath, ec);
                        if (ec) {
                            SDL_Log("Failed to rename '%s' to '%s': %s", oldName.c_str(), newName.c_str(), ec.message().c_str());
                        } else {
                            SDL_Log("Renamed '%s' to '%s'", oldName.c_str(), newName.c_str());
                        }
                    } else {
                        SDL_Log("Failed to rename: '%s' already exists.", newName.c_str());
                    }
                }
            }
            
            isRenaming = false;
            renameEdit.deactivate(window);
            refreshEntries();
            
            // Try to re-select the renamed item in the list
            for (size_t i = 0; i < entries.size(); ++i) {
                if (entries[i] == newName) {
                    selectedIndex = (int)i;
                    break;
                }
            }
        }

    private:
        Gui::Scrollbar listScrollbar;
        float listScrollOffset = 0.0f;
        SDL_FRect getListRect(const SDL_FRect& win) const {
            return { win.x + 10, win.y + 75, win.w - 20, win.h - 140 };
        }
        void updateScrollbarGeometry(const SDL_FRect& listRect) {
            const float rowHeight = 24.0f;
            float contentHeight = entries.size() * rowHeight;
            float viewHeight = listRect.h;
            // Place the scrollbar on the right edge of the list (inside, with a small margin)
            float sbWidth = 12.0f;
            listScrollbar.setGeometry(
                listRect.x + listRect.w - sbWidth - 2,
                listRect.y + 2,
                sbWidth,
                listRect.h - 4,
                contentHeight,
                viewHeight
            );
            // Clamp the offset to the new max
            listScrollOffset = std::clamp(listScrollOffset, 0.0f, listScrollbar.maxOffset());
            listScrollbar.setOffsetNoCallback(listScrollOffset);
        }
        void updateScrollbar() {
            // Called whenever the list content changes (refreshEntries) or the dialog opens.
            // We need a valid win rect to compute listRect; we can use the animRect() if open,
            // otherwise fallback to logicalRect.
            SDL_FRect win = (state == DialogState::Opened) ? animRect() : logicalRect;
            SDL_FRect listRect = getListRect(win);
            updateScrollbarGeometry(listRect);
        }

        float listRectHeight() const {
            SDL_FRect win = (state == DialogState::Opened) ? animRect() : logicalRect;
            return getListRect(win).h;
        }

        void ensureItemVisible(int index) {
            if (index < 0 || index >= (int)entries.size()) return;
            float rowHeight = 24.0f;
            float itemTop = index * rowHeight;
            float itemBottom = itemTop + rowHeight;
            if (itemTop < listScrollOffset) {
                listScrollOffset = itemTop;
            } else if (itemBottom > listScrollOffset + listRectHeight()) {
                listScrollOffset = itemBottom - listRectHeight();
            }
            listScrollOffset = std::clamp(listScrollOffset, 0.0f, listScrollbar.maxOffset());
            listScrollbar.setOffsetNoCallback(listScrollOffset);
        }

    protected:
        void onOpen() override {
            int winW, winH;
            SDL_GetWindowSize(window, &winW, &winH);
            logicalRect = {
                (winW - 600.0f) * 0.5f,
                (winH - 400.0f) * 0.5f,
                600.0f, 400.0f
            };
            pathEdit.setPlaceholder("Path...");
            selectedIndex = -1;
            selectedFilePath.clear();
            goToRoot();
            loadIcons();
            updateScrollbar();
        }

        bool onHandleEvent(const SDL_Event& ev) override {
            // ── Scrollbar gets first chance at all events ──
            if (listScrollbar.handleEvent(ev)) return true;

            if (saveMode) {
                if (filenameEdit.handleEvent(ev, window, 0.0f, 0.0f)) return true;
            }

            // ── Delete confirmation overlay ──
            if (showDeleteConfirmation) {
                if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN && ev.button.button == SDL_BUTTON_LEFT) {
                    float mx = ev.button.x, my = ev.button.y;
                    SDL_FRect win = animRect();
                    float boxW = 300.0f, boxH = 120.0f;
                    float boxX = win.x + (win.w - boxW) * 0.5f;
                    float boxY = win.y + (win.h - boxH) * 0.5f;
                    SDL_FRect yesBtn = { boxX + 20.0f, boxY + boxH - 50.0f, 120.0f, 36.0f };
                    SDL_FRect noBtn  = { boxX + boxW - 140.0f, boxY + boxH - 50.0f, 120.0f, 36.0f };

                    if (mx >= yesBtn.x && mx <= yesBtn.x + yesBtn.w &&
                        my >= yesBtn.y && my <= yesBtn.y + yesBtn.h) {
                        if (deleteIndex >= 0 && deleteIndex < (int)entries.size()) {
                            std::string pathToDelete = (std::filesystem::path(currentPath) / entries[deleteIndex]).string();
                            std::error_code ec;
                            std::filesystem::remove_all(pathToDelete, ec);
                            if (ec) SDL_Log("Failed to delete '%s': %s", pathToDelete.c_str(), ec.message().c_str());
                            else    SDL_Log("Deleted '%s'", pathToDelete.c_str());
                        }
                        showDeleteConfirmation = false;
                        refreshEntries();
                        return true;
                    }
                    if (mx >= noBtn.x && mx <= noBtn.x + noBtn.w &&
                        my >= noBtn.y && my <= noBtn.y + noBtn.h) {
                        showDeleteConfirmation = false;
                        return true;
                    }
                }
                if (ev.type == SDL_EVENT_KEY_DOWN) {
                    if (ev.key.key == SDLK_ESCAPE) { showDeleteConfirmation = false; return true; }
                }
                return true; // consume all other events while confirming
            }

            // ── Overwrite confirmation (Save mode) ──
            if (showOverwriteConfirmation) {
                if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN && ev.button.button == SDL_BUTTON_LEFT) {
                    float mx = ev.button.x, my = ev.button.y;
                    SDL_FRect win = animRect();
                    float boxW = 320.0f, boxH = 120.0f;
                    float boxX = win.x + (win.w - boxW) * 0.5f;
                    float boxY = win.y + (win.h - boxH) * 0.5f;
                    SDL_FRect yesBtn = { boxX + 20.0f, boxY + boxH - 50.0f, 130.0f, 36.0f };
                    SDL_FRect noBtn  = { boxX + boxW - 150.0f, boxY + boxH - 50.0f, 130.0f, 36.0f };

                    if (mx >= yesBtn.x && mx <= yesBtn.x + yesBtn.w &&
                        my >= yesBtn.y && my <= yesBtn.y + yesBtn.h) {
                        if (overwriteIndex >= 0 && overwriteIndex < (int)entries.size() && callback) {
                            std::string fullPath = (std::filesystem::path(currentPath) / entries[overwriteIndex]).string();
                            callback(fullPath);
                        }
                        showOverwriteConfirmation = false;
                        return true;
                    }
                    if (mx >= noBtn.x && mx <= noBtn.x + noBtn.w &&
                        my >= noBtn.y && my <= noBtn.y + noBtn.h) {
                        showOverwriteConfirmation = false;
                        return true;
                    }
                }
                if (ev.type == SDL_EVENT_KEY_DOWN) {
                    if (ev.key.key == SDLK_ESCAPE) { showOverwriteConfirmation = false; return true; }
                    if (ev.key.key == SDLK_RETURN) {
                        if (overwriteIndex >= 0 && overwriteIndex < (int)entries.size() && callback) {
                            std::string fullPath = (std::filesystem::path(currentPath) / entries[overwriteIndex]).string();
                            callback(fullPath);
                        }
                        showOverwriteConfirmation = false;
                        return true;
                    }
                }
                return true;
            }

            // ── Active rename mode ──
            if (isRenaming) {
                bool wasActive = renameEdit.isActive();
                renameEdit.handleEvent(ev, window, 0.0f, 0.0f);

                if (ev.type == SDL_EVENT_KEY_DOWN) {
                    if (ev.key.key == SDLK_RETURN) {
                        commitRename();
                        return true;
                    }
                    if (ev.key.key == SDLK_ESCAPE) {
                        isRenaming = false;
                        renameEdit.deactivate(window);
                        return true;
                    }
                }

                if (wasActive && !renameEdit.isActive()) {
                    commitRename();
                    return true;
                }
                return true; // consume while renaming
            }

            // ── Context menu (empty space) ──
            if (showContextMenu) {
                if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN && ev.button.button == SDL_BUTTON_LEFT) {
                    float mx = ev.button.x, my = ev.button.y;
                    SDL_FRect menuRect = getContextMenuRect();
                    if (mx >= menuRect.x && mx <= menuRect.x + menuRect.w &&
                        my >= menuRect.y && my <= menuRect.y + menuRect.h) {
                        float itemH = 28.0f;
                        int idx = (int)((my - menuRect.y) / itemH);
                        if (idx >= 0 && idx < (int)contextMenuItems.size()) {
                            if (idx == 0) createNewFolder();
                            else if (idx == 1) createNewScene();
                            else if (idx == 2) createNewScript();
                            else if (idx == 3) createNewHeader();
                            else if (idx == 4) createNewConfig();
                            showContextMenu = false;
                            return true;
                        }
                    } else {
                        showContextMenu = false;
                        return true;
                    }
                }
                if (ev.type == SDL_EVENT_KEY_DOWN || ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                    showContextMenu = false;
                    return true;
                }
                return true;
            }

            // ── Item context menu (right‑click on a file/folder) ──
            if (showItemContextMenu) {
                if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN && ev.button.button == SDL_BUTTON_LEFT) {
                    float mx = ev.button.x, my = ev.button.y;
                    SDL_FRect menuRect = getItemContextMenuRect();
                    if (mx >= menuRect.x && mx <= menuRect.x + menuRect.w &&
                        my >= menuRect.y && my <= menuRect.y + menuRect.h) {
                        float itemH = 28.0f;
                        int idx = (int)((my - menuRect.y) / itemH);
                        if (idx == 0) { // Rename
                            isRenaming = true;
                            renameIndex = selectedIndex;
                            renameEdit.clear();
                            for (char c : entries[renameIndex]) renameEdit.appendText(std::string(1, c));
                            renameEdit.setActive(true);
                            SDL_StartTextInput(window);
                        } else if (idx == 1) { // Delete
                            showDeleteConfirmation = true;
                            deleteIndex = selectedIndex;
                            showItemContextMenu = false;
                        }
                        showItemContextMenu = false;
                        return true;
                    } else {
                        showItemContextMenu = false;
                        return true;
                    }
                }
                if (ev.type == SDL_EVENT_KEY_DOWN || ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                    showItemContextMenu = false;
                    return true;
                }
                return true;
            }

            // ── Back button ──
            if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN && ev.button.button == SDL_BUTTON_LEFT) {
                float mx = ev.button.x, my = ev.button.y;
                SDL_FRect win = animRect();
                SDL_FRect backBtn = { win.x + 10, win.y + 40, 30, 28 };
                if (mx >= backBtn.x && mx <= backBtn.x + backBtn.w &&
                    my >= backBtn.y && my <= backBtn.y + backBtn.h) {
                    goToParent();
                    return true;
                }
            }

            // ── Path edit ──
            if (pathEdit.handleEvent(ev, window, 0.0f, 0.0f)) return true;

            // ── Mouse interactions on the file list ──
            if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                float mx = ev.button.x, my = ev.button.y;
                SDL_FRect win = animRect();
                SDL_FRect listRect = getListRect(win);

                // Left‑click: select / open / double‑click
                if (ev.button.button == SDL_BUTTON_LEFT) {
                    if (mx >= listRect.x && mx <= listRect.x + listRect.w &&
                        my >= listRect.y && my <= listRect.y + listRect.h) {
                        float rowHeight = 24.0f;
                        // ------------------------- IMPORTANT -------------------------
                        // Offset the Y coordinate by the current scroll offset so that
                        // clicks land on the correct row even when the list is scrolled.
                        int idx = (int)((my - listRect.y + listScrollOffset) / rowHeight);
                        // -------------------------------------------------------------
                        if (idx >= 0 && idx < (int)entries.size()) {
                            selectedIndex = idx;
                            if (isDir[idx]) {
                                goToDirectory(entries[idx]);
                            } else if (ev.button.clicks >= 2) {
                                if (saveMode) {
                                    filenameEdit.clear();
                                    for (char c : entries[idx]) filenameEdit.appendText(std::string(1, c));
                                    showOverwriteConfirmation = true;
                                    overwriteIndex = idx;
                                } else {
                                    selectedFilePath = (std::filesystem::path(currentPath) / entries[idx]).string();
                                    selectFile(entries[idx]);
                                }
                            }
                            return true;
                        }
                    }
                }

                // Right‑click: context menu (item or empty space)
                if (ev.button.button == SDL_BUTTON_RIGHT) {
                    if (mx >= listRect.x && mx <= listRect.x + listRect.w &&
                        my >= listRect.y && my <= listRect.y + listRect.h) {
                        float rowHeight = 24.0f;
                        // Again, apply scroll offset to get the correct row index
                        int idx = (int)((my - listRect.y + listScrollOffset) / rowHeight);
                        if (idx >= 0 && idx < (int)entries.size()) {
                            // Right‑click on an item → show item context menu
                            selectedIndex = idx;
                            itemContextMenuX = mx;
                            itemContextMenuY = my;
                            showItemContextMenu = true;
                            showContextMenu = false;
                        } else {
                            // Right‑click on empty space → show "New..." menu
                            contextMenuX = mx;
                            contextMenuY = my;
                            showContextMenu = true;
                            showItemContextMenu = false;
                        }
                        return true;
                    }
                }
            }

            // ── Keyboard navigation ──
            if (ev.type == SDL_EVENT_KEY_DOWN) {
                if (ev.key.key == SDLK_RETURN) {
                    if (selectedIndex >= 0 && selectedIndex < (int)entries.size()) {
                        if (isDir[selectedIndex]) {
                            goToDirectory(entries[selectedIndex]);
                        } else {
                            selectedFilePath = (std::filesystem::path(currentPath) / entries[selectedIndex]).string();
                            selectFile(entries[selectedIndex]);
                        }
                        return true;
                    }
                }
                if (ev.key.key == SDLK_BACKSPACE || ev.key.key == SDLK_UP) {
                    goToParent();
                    return true;
                }
                if (ev.key.key == SDLK_DOWN) {
                    selectedIndex = std::min(selectedIndex + 1, (int)entries.size() - 1);
                    // Ensure the newly selected item is visible (scroll to it if needed)
                    ensureItemVisible(selectedIndex);
                    return true;
                }
                if (ev.key.key == SDLK_UP) {
                    selectedIndex = std::max(selectedIndex - 1, 0);
                    ensureItemVisible(selectedIndex);
                    return true;
                }
            }

            // ── Mouse wheel inside the list area is already caught by the scrollbar,
            //     but we also let the scrollbar handle it directly (done at the top).
            //     No extra handling needed here.

            return false;
        }

        void onRender(SDL_FRect win) override {
            float mx, my;
            SDL_GetMouseState(&mx, &my);

            // Back button
            SDL_FRect backBtn = { win.x + 10, win.y + 40, 30, 28 };
            bool hovered = (mx >= backBtn.x && mx <= backBtn.x + backBtn.w &&
                            my >= backBtn.y && my <= backBtn.y + backBtn.h);
            SDL_Color backColor = hovered ? SDL_Color{80,80,80,255} : SDL_Color{60,60,60,255};
            fillRect(backBtn, backColor);
            drawText("<", backBtn.x + 8, backBtn.y + 4, {220,220,220,255});

            // Path bar
            pathEdit.setRect({ win.x + 45, win.y + 40, win.w - 98, 28 });
            pathEdit.render(0.0f, 0.0f);

            // ── File list area ──
            SDL_FRect listRect = getListRect(win);
            // Background and border
            SDL_SetRenderDrawColor(renderer, 50, 50, 60, 255);
            SDL_RenderFillRect(renderer, &listRect);
            SDL_SetRenderDrawColor(renderer, 80, 80, 100, 255);
            SDL_RenderRect(renderer, &listRect);

            // ── Clip to list area ──
            SDL_Rect clip = { (int)listRect.x, (int)listRect.y, (int)listRect.w, (int)listRect.h };
            SDL_SetRenderClipRect(renderer, &clip);

            float rowHeight = 24.0f;
            float y = listRect.y - listScrollOffset;   // apply scroll offset
            const float iconSize = 32.0f;
            const float textOffset = iconSize + 6.0f;

            for (size_t i = 0; i < entries.size(); ++i) {
                // Skip rows that are completely outside the visible area
                if (y + rowHeight < listRect.y || y > listRect.y + listRect.h) {
                    y += rowHeight;
                    continue;
                }

                // Row background (selected / normal)
                SDL_Color bg = (i == selectedIndex) ? SDL_Color{70, 70, 120, 255} : SDL_Color{60, 60, 70, 255};
                SDL_SetRenderDrawColor(renderer, bg.r, bg.g, bg.b, bg.a);
                SDL_FRect rowRect = { listRect.x, y, listRect.w, rowHeight };
                SDL_RenderFillRect(renderer, &rowRect);

                // Icon
                SDL_Texture* icon = nullptr;
                if (isDir[i]) {
                    icon = folderIcon;
                } else {
                    std::string ext = getFileExtension(entries[i]);
                    if (ext == ".cpp" || ext == ".hpp") icon = cppIcon;
                    else if (ext == ".h") icon = hIcon;
                    else if (ext == ".json") icon = sceneIcon;
                    else if (ext == ".txt" || ext == "")  icon = textIcon;
                    else if (ext == ".mp3" || ext == ".wav") icon = musicIcon;
                    else if (ext == ".svg" || ext == ".svg") icon = imgIcon;
                }
                if (icon) {
                    SDL_FRect iconRect = { listRect.x + 4, y + (rowHeight - iconSize) * 0.5f, iconSize, iconSize };
                    SDL_RenderTexture(renderer, icon, nullptr, &iconRect);
                } else {
                    drawText(isDir[i] ? "[DIR]" : "[FILE]", listRect.x + 4, y + 2, {200,200,180,255});
                }

                // File / folder name (or rename edit box if active)
                SDL_Color textColor = isDir[i] ? SDL_Color{200,200,180,255} : SDL_Color{220,220,240,255};
                if (isRenaming && i == renameIndex) {
                    SDL_FRect editRect = { listRect.x + textOffset, y + 2, listRect.w - textOffset - 10, rowHeight - 4 };
                    renameEdit.setRect(editRect);
                    renameEdit.render(0.0f, 0.0f);
                } else {
                    drawText(entries[i].c_str(), listRect.x + textOffset, y + 2, textColor);
                }

                y += rowHeight;
            }

            SDL_SetRenderClipRect(renderer, nullptr);

            // ── Render the scrollbar ──
            // Update its geometry first (in case listRect changed)
            updateScrollbarGeometry(listRect);
            listScrollbar.render(renderer, 0.0f, 0.0f);

            // ── Context menus ──
            if (showContextMenu) {
                SDL_FRect menuRect = getContextMenuRect();
                SDL_SetRenderDrawColor(renderer, 40, 40, 50, 220);
                SDL_RenderFillRect(renderer, &menuRect);
                SDL_SetRenderDrawColor(renderer, 100, 100, 130, 255);
                SDL_RenderRect(renderer, &menuRect);

                float itemH = 28.0f;
                float yPos = menuRect.y;
                for (size_t i = 0; i < contextMenuItems.size(); ++i) {
                    SDL_FRect itemRect = { menuRect.x, yPos, menuRect.w, itemH };
                    float mX, mY;
                    SDL_GetMouseState(&mX, &mY);
                    bool hover = (mX >= itemRect.x && mX <= itemRect.x + itemRect.w &&
                                mY >= itemRect.y && mY <= itemRect.y + itemRect.h);
                    if (hover) {
                        SDL_SetRenderDrawColor(renderer, 80, 80, 120, 255);
                        SDL_RenderFillRect(renderer, &itemRect);
                    }
                    drawText(contextMenuItems[i].c_str(), menuRect.x + 8, yPos + 4, {255,255,255,255});
                    yPos += itemH;
                }
            }

            if (showItemContextMenu) {
                SDL_FRect menuRect = getItemContextMenuRect();
                SDL_SetRenderDrawColor(renderer, 40, 40, 50, 220);
                SDL_RenderFillRect(renderer, &menuRect);
                SDL_SetRenderDrawColor(renderer, 100, 100, 130, 255);
                SDL_RenderRect(renderer, &menuRect);

                float itemH = 28.0f;
                float yPos = menuRect.y;
                for (size_t i = 0; i < itemContextMenuItems.size(); ++i) {
                    SDL_FRect itemRect = { menuRect.x, yPos, menuRect.w, itemH };
                    float mX, mY;
                    SDL_GetMouseState(&mX, &mY);
                    bool hover = (mX >= itemRect.x && mX <= itemRect.x + itemRect.w &&
                                mY >= itemRect.y && mY <= itemRect.y + itemRect.h);
                    if (hover) {
                        SDL_SetRenderDrawColor(renderer, 80, 80, 120, 255);
                        SDL_RenderFillRect(renderer, &itemRect);
                    }
                    drawText(itemContextMenuItems[i].c_str(), menuRect.x + 8, yPos + 4, {255,255,255,255});
                    yPos += itemH;
                }
            }

            // ── Save‑mode file name field ──
            if (saveMode) {
                drawText("File:", win.x + 10, win.y + win.h - 40, {220,220,220,255});
                filenameEdit.setRect({ win.x + 130, win.y + win.h - 45, win.w - 260, 28 });
                filenameEdit.render(0.0f, 0.0f);
            }
            if (showDeleteConfirmation) {
                SDL_FRect win = animRect();
                float boxW = 300.0f, boxH = 120.0f;
                float boxX = win.x + (win.w - boxW) * 0.5f;
                float boxY = win.y + (win.h - boxH) * 0.5f;
                
                // Darken background
                SDL_SetRenderDrawColor(renderer, 0, 0, 0, 150);
                SDL_RenderFillRect(renderer, &win);
                
                // Dialog Box
                SDL_SetRenderDrawColor(renderer, 40, 40, 50, 240);
                SDL_FRect box = { boxX, boxY, boxW, boxH };
                SDL_RenderFillRect(renderer, &box);
                SDL_SetRenderDrawColor(renderer, 100, 100, 130, 255);
                SDL_RenderRect(renderer, &box);
                
                // Message Text
                std::string msg = "Delete '" + (deleteIndex >= 0 && deleteIndex < (int)entries.size() ? entries[deleteIndex] : "") + "'?";
                drawText(msg.c_str(), boxX + 20.0f, boxY + 20.0f, {255, 255, 255, 255});
                
                // Buttons
                SDL_FRect yesBtn = { boxX + 20.0f, boxY + boxH - 50.0f, 120.0f, 36.0f };
                SDL_FRect noBtn  = { boxX + boxW - 140.0f, boxY + boxH - 50.0f, 120.0f, 36.0f };
                
                float mx, my; SDL_GetMouseState(&mx, &my);
                
                bool hoverYes = (mx >= yesBtn.x && mx <= yesBtn.x + yesBtn.w && my >= yesBtn.y && my <= yesBtn.y + yesBtn.h);
                SDL_SetRenderDrawColor(renderer, hoverYes ? 220 : 180, 60, 60, 255);
                SDL_RenderFillRect(renderer, &yesBtn);
                drawText("Yes", yesBtn.x + 45.0f, yesBtn.y + 8.0f, {255, 255, 255, 255});
                
                bool hoverNo = (mx >= noBtn.x && mx <= noBtn.x + noBtn.w && my >= noBtn.y && my <= noBtn.y + noBtn.h);
                SDL_SetRenderDrawColor(renderer, hoverNo ? 100 : 80, 100, 120, 255);
                SDL_RenderFillRect(renderer, &noBtn);
                drawText("No", noBtn.x + 48.0f, noBtn.y + 8.0f, {255, 255, 255, 255});
            }
            if (showOverwriteConfirmation) {
                SDL_FRect win = animRect();
                float boxW = 320.0f, boxH = 120.0f;
                float boxX = win.x + (win.w - boxW) * 0.5f;
                float boxY = win.y + (win.h - boxH) * 0.5f;

                // Darken background
                SDL_SetRenderDrawColor(renderer, 0, 0, 0, 150);
                SDL_RenderFillRect(renderer, &win);

                // Dialog Box
                SDL_SetRenderDrawColor(renderer, 40, 40, 50, 240);
                SDL_FRect box = { boxX, boxY, boxW, boxH };
                SDL_RenderFillRect(renderer, &box);
                SDL_SetRenderDrawColor(renderer, 100, 100, 130, 255);
                SDL_RenderRect(renderer, &box);

                // Message Text
                std::string msg = "Overwrite '" + (overwriteIndex >= 0 && overwriteIndex < (int)entries.size() ? entries[overwriteIndex] : "") + "'?";
                drawText(msg.c_str(), boxX + 20.0f, boxY + 20.0f, {255, 255, 255, 255});

                // Buttons
                SDL_FRect yesBtn = { boxX + 20.0f, boxY + boxH - 50.0f, 130.0f, 36.0f };
                SDL_FRect noBtn  = { boxX + boxW - 150.0f, boxY + boxH - 50.0f, 130.0f, 36.0f };

                float mx, my; SDL_GetMouseState(&mx, &my);

                bool hoverYes = (mx >= yesBtn.x && mx <= yesBtn.x + yesBtn.w && my >= yesBtn.y && my <= yesBtn.y + yesBtn.h);
                SDL_SetRenderDrawColor(renderer, hoverYes ? 220 : 180, 60, 60, 255);
                SDL_RenderFillRect(renderer, &yesBtn);
                drawText("Yes", yesBtn.x + 48.0f, yesBtn.y + 8.0f, {255, 255, 255, 255});

                bool hoverNo2 = (mx >= noBtn.x && mx <= noBtn.x + noBtn.w && my >= noBtn.y && my <= noBtn.y + noBtn.h);
                SDL_SetRenderDrawColor(renderer, hoverNo2 ? 100 : 80, 100, 120, 255);
                SDL_RenderFillRect(renderer, &noBtn);
                drawText("No", noBtn.x + 53.0f, noBtn.y + 8.0f, {255, 255, 255, 255});
            }
        }

        void onReset() override {
            selectedIndex = -1;
            selectedFilePath.clear();
            pathEdit.clear();
            pathEdit.deactivate(window);
            showContextMenu = false;
            showDeleteConfirmation = false;
            showOverwriteConfirmation = false;
        }

    public:
        bool hasSelectedFile() const { return !selectedFilePath.empty(); }
        std::string getSelectedFilePath() const { return selectedFilePath; }

        void triggerCallback() {
            if (!callback) return;
            std::string filePath = selectedFilePath;
            if (filePath.empty() && selectedIndex >= 0 && selectedIndex < (int)entries.size() && !isDir[selectedIndex]) {
                filePath = (std::filesystem::path(currentPath) / entries[selectedIndex]).string();
            }
            if (!filePath.empty()) {
                callback(filePath);
            }
        }

    private:
        std::string currentPath;
        std::string rootPath;
        std::string filter;
        std::vector<std::string> entries;
        std::vector<bool> isDir;
        int selectedIndex = -1;
        std::function<void(const std::string&)> callback;
        Gui::LineEdit pathEdit;
        Gui::LineEdit renameEdit;
        bool showHidden = false;
        std::string selectedFilePath;

        SDL_Renderer*   renderer;
        TTF_TextEngine* textEngine;
        TTF_Font*       font;

        // GPU textures
        SDL_Texture* folderIcon = nullptr;
        SDL_Texture* cppIcon    = nullptr;
        SDL_Texture* hIcon      = nullptr;
        SDL_Texture* sceneIcon  = nullptr;
        SDL_Texture* textIcon   = nullptr;
        SDL_Texture* musicIcon  = nullptr;
        SDL_Texture* imgIcon    = nullptr;

        bool showContextMenu = false;
        float contextMenuX = 0, contextMenuY = 0;
        std::vector<std::string> contextMenuItems = {
            "New Folder",
            "New Scene (.json)",
            "New Script (.cpp)",
            "New Header (.h)",
            "New Config (.txt)",
        };
        bool showItemContextMenu = false;
        float itemContextMenuX = 0, itemContextMenuY = 0;
        std::vector<std::string> itemContextMenuItems = {"Rename", "Delete"};
        bool isRenaming = false;
        int renameIndex = -1;
        bool showDeleteConfirmation = false;
        int deleteIndex = -1;
        bool showOverwriteConfirmation = false;
        int overwriteIndex = -1;


        // --------------------------------------------------------------
        // Helpers
        // --------------------------------------------------------------
        static std::string getFileExtension(const std::string& name) {
            size_t dot = name.rfind('.');
            if (dot == std::string::npos) return "";
            std::string ext = name.substr(dot);
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            return ext;
        }

        void loadIcons() {
            // Helper: try to load texture; on failure, create fallback
            auto loadOrFallback = [this](const std::string& filename, SDL_Color fallbackColor, const std::string& fallbackLabel) -> SDL_Texture* {
                std::string path = getAssetsPath() + filename;
                SDL_Texture* tex = IMG_LoadTexture(renderer, path.c_str());
                if (tex) {
                    return tex;
                }
                SDL_Log("Failed to load icon: %s (%s)", path.c_str(), SDL_GetError());
                return nullptr;
            };

            if (!folderIcon)  folderIcon  = loadOrFallback("folder_icon.svg",  SDL_Color{200,180,50,255}, "Dir");
            if (!cppIcon)     cppIcon     = loadOrFallback("cpp_icon.svg",     SDL_Color{80,160,255,255}, "C++");
            if (!hIcon)       hIcon       = loadOrFallback("h_icon.svg",       SDL_Color{255,120,80,255}, "Head");
            if (!sceneIcon)   sceneIcon   = loadOrFallback("scene_icon.svg",   SDL_Color{80,200,120,255}, "Scne");
            if (!textIcon)    textIcon    = loadOrFallback("file_icon.svg",    SDL_Color{80,100,120,255}, "Txt");
            if (!musicIcon)   musicIcon   = loadOrFallback("music_icon.svg",   SDL_Color{80,100,120,255}, "Wav/Mp4");
            if (!imgIcon)   imgIcon   = loadOrFallback("img_icon.svg",   SDL_Color{80,100,120,255}, "Svg");
        }

        SDL_FRect getContextMenuRect() const {
            float w = 160.0f, h = 28.0f * contextMenuItems.size() + 6.0f;
            float x = contextMenuX, y = contextMenuY;
            SDL_FRect win = animRect();
            if (x + w > win.x + win.w) x = win.x + win.w - w - 10;
            if (y + h > win.y + win.h) y = win.y + win.h - h - 10;
            if (x < win.x + 10) x = win.x + 10;
            if (y < win.y + 70) y = win.y + 70;
            return { x, y, w, h };
        }

        // --------------------------------------------------------------
        // Directory listing
        // --------------------------------------------------------------
        void refreshEntries() {
            entries.clear();
            isDir.clear();
            try {
                for (const auto& entry : std::filesystem::directory_iterator(currentPath)) {
                    std::string name = entry.path().filename().string();
                    if (!showHidden && name[0] == '.') continue;
                    bool dir = entry.is_directory();
                    if (!dir && !matchesFilter(name)) continue;
                    entries.push_back(name);
                    isDir.push_back(dir);
                }
                std::vector<size_t> order(entries.size());
                std::iota(order.begin(), order.end(), 0);
                std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
                    if (isDir[a] != isDir[b]) return isDir[a] > isDir[b];
                    return entries[a] < entries[b];
                });
                std::vector<std::string> sortedEntries;
                std::vector<bool> sortedIsDir;
                for (size_t idx : order) {
                    sortedEntries.push_back(entries[idx]);
                    sortedIsDir.push_back(isDir[idx]);
                }
                entries.swap(sortedEntries);
                isDir.swap(sortedIsDir);
                selectedIndex = -1;
                pathEdit.clear();
                for (char c : currentPath) pathEdit.appendText(std::string(1, c));
            } catch (const std::exception& e) {
                SDL_Log("FileExplorer: error reading directory: %s", e.what());
            }
            updateScrollbar();
        }

        void goToParent() {
            if (currentPath == rootPath) return;
            std::filesystem::path p(currentPath);
            if (p.has_parent_path()) {
                currentPath = p.parent_path().string();
                refreshEntries();
            }
        }

        void goToDirectory(const std::string& dirName) {
            std::filesystem::path newPath = std::filesystem::path(currentPath) / dirName;
            if (std::filesystem::is_directory(newPath)) {
                currentPath = newPath.string();
                refreshEntries();
            }
        }

        void selectFile(const std::string& fileName) {
            std::string fullPath = (std::filesystem::path(currentPath) / fileName).string();
            if (callback) callback(fullPath);
        }

        bool matchesFilter(const std::string& fileName) const {
            if (filter == "*" || filter.empty()) return true;
            auto patterns = splitFilter();
            for (const auto& pat : patterns) {
                if (pat == "*") return true;
                
                // --- ADD THIS ---
                if (pat == "*.*") return true; // Treat *.* as a wildcard for all files
                // ----------------

                if (pat.find('*') != std::string::npos) {
                    std::string prefix = pat.substr(0, pat.find('*'));
                    std::string suffix = pat.substr(pat.find('*') + 1);
                    if (fileName.size() >= prefix.size() + suffix.size() &&
                        fileName.compare(0, prefix.size(), prefix) == 0 &&
                        fileName.compare(fileName.size() - suffix.size(), suffix.size(), suffix) == 0)
                        return true;
                } else {
                    if (fileName == pat) return true;
                }
            }
            return false;
        }

        std::vector<std::string> splitFilter() const {
            std::vector<std::string> parts;
            size_t start = 0, end;
            while ((end = filter.find(';', start)) != std::string::npos) {
                parts.push_back(filter.substr(start, end - start));
                start = end + 1;
            }
            parts.push_back(filter.substr(start));
            return parts;
        }

        void createNewFolder() {
            std::string base = "new_folder";
            int counter = 1;
            std::string fullName;
            do {
                fullName = base + (counter > 1 ? "_" + std::to_string(counter) : "");
                ++counter;
            } while (std::filesystem::exists(std::filesystem::path(currentPath) / fullName));

            std::string folderPath = (std::filesystem::path(currentPath) / fullName).string();
            
            // Create the directory on the disk
            if (!std::filesystem::create_directory(folderPath)) {
                SDL_Log("Failed to create folder: %s", folderPath.c_str());
                return;
            }

            refreshEntries();
            // Automatically select the newly created folder in the list
            for (size_t i = 0; i < entries.size(); ++i) {
                if (entries[i] == fullName) { selectedIndex = (int)i; break; }
            }
        }

        void createNewScene() {
            std::string base = "new_scene";
            std::string ext = ".json";
            int counter = 1;
            std::string fullName;
            do {
                fullName = base + (counter > 1 ? "_" + std::to_string(counter) : "") + ext;
                ++counter;
            } while (std::filesystem::exists(std::filesystem::path(currentPath) / fullName));

            std::string filePath = (std::filesystem::path(currentPath) / fullName).string();
            nlohmann::json j;
            j["scene_name"] = "SceneName";
            j["script_attached"] = "ProjectName/scripts/SceneName.cpp";
            j["entities"] = nlohmann::json::array();
            j["gui_elements"] = nlohmann::json::array();
            std::ofstream out(filePath);
            out << j.dump(4);
            out.close();

            refreshEntries();
            
            for (size_t i = 0; i < entries.size(); ++i) {
                if (entries[i] == fullName) { selectedIndex = (int)i; break; }
            }
        }

        void createNewScript() {
            std::string base = "new_script";
            std::string ext = ".cpp";
            int counter = 1;
            std::string fullName;
            do {
                fullName = base + (counter > 1 ? "_" + std::to_string(counter) : "") + ext;
                ++counter;
            } while (std::filesystem::exists(std::filesystem::path(currentPath) / fullName));

            std::string filePath = (std::filesystem::path(currentPath) / fullName).string();
            
            // Using the bare-bones program.cpp template
            std::string templateContent =
                "#include \"engine.h\"\n"
                "#include <iostream>\n"
                "#include <cmath>\n"
                "\n"
                "#ifdef __EMSCRIPTEN__\n"
                "#include <emscripten/emscripten.h>\n"
                "#endif\n"
                "\n"
                "struct ProgramContext {\n"
                "    SDL_Renderer*   renderer    = nullptr;\n"
                "    TTF_TextEngine* textEngine  = nullptr;\n"
                "    SDL_Window*     window      = nullptr;\n"
                "};\n"
                "\n"
                "class ProgramScript : public ScriptBase {\n"
                "public:\n"
                "    std::string getName() const override { return \"ProgramScript\"; }\n"
                "    ProgramContext* ctx = nullptr;\n"
                "    void onStart() override;\n"
                "    void onUpdate(float dt) override;\n"
                "    void onDraw() override;\n"
                "    void onEnd() override;\n"
                "private:\n"
                "    float elapsed = 0.0f;\n"
                "};\n"
                "\n"
                "static SDL_Renderer* g_renderer = nullptr;\n"
                "static SDL_Window* g_window = nullptr;\n"
                "static TTF_TextEngine* g_textEngine = nullptr;\n"
                "static ProgramScript* g_script = nullptr;\n"
                "static ProgramContext* g_ctx = nullptr;\n"
                "static bool g_running = true;\n"
                "static Uint64 g_lastTime = 0;\n"
                "\n"
                "#ifdef __EMSCRIPTEN__\n"
                "void main_loop_callback() {\n"
                "    if (!g_running) { emscripten_cancel_main_loop(); return; }\n"
                "    Uint64 now = SDL_GetTicks(); float dt = (float)(now - g_lastTime) / 1000.0f; g_lastTime = now;\n"
                "    SDL_Event e;\n"
                "    while (SDL_PollEvent(&e)) {\n"
                "        if (e.type == SDL_EVENT_QUIT) g_running = false;\n"
                "        if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) g_running = false;\n"
                "    }\n"
                "    g_script->onUpdate(dt);\n"
                "    SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_BLEND);\n"
                "    SDL_SetRenderDrawColor(g_renderer, 10, 10, 20, 255);\n"
                "    SDL_RenderClear(g_renderer);\n"
                "    int winW = 1600, winH = 900; SDL_GetWindowSize(g_window, &winW, &winH); float w = (float)winW;\n"
                "    for (int i = 0; i < 9; ++i) {\n"
                "        SDL_SetRenderDrawColor(g_renderer, 40, 50, 100, (Uint8)(8 + i * 3));\n"
                "        SDL_FRect band = { 0, (float)(i * 100), w, 100 };\n"
                "        SDL_RenderFillRect(g_renderer, &band);\n"
                "    }\n"
                "    g_script->onDraw(); SDL_RenderPresent(g_renderer);\n"
                "}\n"
                "#endif\n"
                "\n"
                "void ProgramScript::onStart() { if (!ctx) return; elapsed = 0.0f; SDL_Log(\"[ProgramScript] onStart\"); }\n"
                "void ProgramScript::onUpdate(float dt) { elapsed += dt; }\n"
                "void ProgramScript::onDraw() {\n"
                "    if (!ctx || !ctx->renderer || !ctx->textEngine || !ctx->window) return;\n"
                "    int winW = 1600, winH = 900; SDL_GetWindowSize(ctx->window, &winW, &winH); float w = (float)winW;\n"
                "    SDL_SetRenderDrawBlendMode(ctx->renderer, SDL_BLENDMODE_BLEND);\n"
                "    SDL_FRect titleBg = { 0, 0, w, 110 };\n"
                "    SDL_SetRenderDrawColor(ctx->renderer, 12, 12, 22, 220);\n"
                "    SDL_RenderFillRect(ctx->renderer, &titleBg);\n"
                "    }\n"
                "}\n"
                "void ProgramScript::onEnd() { SDL_Log(\"[ProgramScript] onEnd\"); }\n"
                "\n"
                "int main(int argc, char* argv[]) {\n"
                "    const float windowWidth = 1600.0f, windowHeight = 900.0f;\n"
                "    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) { std::cerr << \"SDL_Init failed: \" << SDL_GetError() << \"\\n\"; return -1; }\n"
                "    if (!TTF_Init()) { std::cerr << \"TTF_Init failed: \" << SDL_GetError() << \"\\n\"; SDL_Quit(); return -1; }\n"
                "    SDL_Window* window = SDL_CreateWindow(\"Program\", windowWidth, windowHeight, SDL_WINDOW_RESIZABLE);\n"
                "    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);\n"
                "    if (!window || !renderer) { std::cerr << \"Window/Renderer creation failed\\n\"; TTF_Quit(); SDL_Quit(); return -1; }\n"
                "    TTF_Font* bodyFont = ProjectScript_TTF_OpenFont(\"SampleProject/assets/fonts/fredoka.ttf\", 22);\n"
                "    TTF_TextEngine* textEngine = TTF_CreateRendererTextEngine(renderer);\n"
                "    ProgramContext ctx; ctx.renderer = renderer; ctx.textEngine = textEngine; ctx.window = window;\n"
                "    ProgramScript script; script.ctx = &ctx; script.onStart();\n"
                "    g_renderer = renderer; g_window = window; g_textEngine = textEngine;  g_script = &script; g_ctx = &ctx; g_lastTime = SDL_GetTicks();\n"
                "#ifdef __EMSCRIPTEN__\n"
                "    emscripten_set_main_loop(main_loop_callback, 0, 1);\n"
                "#else\n"
                "    bool running = true; Uint64 lastTime = SDL_GetTicks();\n"
                "    while (running) {\n"
                "        Uint64 now = SDL_GetTicks(); float dt = (float)(now - lastTime) / 1000.0f; lastTime = now;\n"
                "        SDL_Event e;\n"
                "        while (SDL_PollEvent(&e)) { if (e.type == SDL_EVENT_QUIT) running = false; if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) running = false; }\n"
                "        script.onUpdate(dt);\n"
                "        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);\n"
                "        SDL_SetRenderDrawColor(renderer, 10, 10, 20, 255);\n"
                "        SDL_RenderClear(renderer);\n"
                "        int winW = 1600, winH = 900; SDL_GetWindowSize(window, &winW, &winH); float w = (float)winW;\n"
                "        for (int i = 0; i < 9; ++i) { SDL_SetRenderDrawColor(renderer, 40, 50, 100, (Uint8)(8 + i * 3)); SDL_FRect band = { 0, (float)(i * 100), w, 100 }; SDL_RenderFillRect(renderer, &band); }\n"
                "        script.onDraw(); SDL_RenderPresent(renderer);\n"
                "    }\n"
                "#endif\n"
                "    script.onEnd();\n"
                "    \n"
                "    if (textEngine) TTF_DestroyRendererTextEngine(textEngine);\n"
                "    SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window); TTF_Quit(); SDL_Quit();\n"
                "    return 0;\n"
                "}\n";

            std::ofstream out(filePath);
            out << templateContent;
            out.close();

            refreshEntries();
            for (size_t i = 0; i < entries.size(); ++i) {
                if (entries[i] == fullName) { selectedIndex = (int)i; break; }
            }
        }

        void createNewHeader() {
            std::string base = "new_header";
            std::string ext = ".h";
            int counter = 1;
            std::string fullName;
            do {
                fullName = base + (counter > 1 ? "_" + std::to_string(counter) : "") + ext;
                ++counter;
            } while (std::filesystem::exists(std::filesystem::path(currentPath) / fullName));

            std::string filePath = (std::filesystem::path(currentPath) / fullName).string();
            
            // Using the bare-bones program.h template
            std::string headerContent =
                "#pragma once\n"
                "#include \"engine.h\"\n"
                "\n"
                "// Bare-bones context holding just what's needed for rendering\n"
                "struct ProgramContext {\n"
                "    SDL_Renderer*   renderer    = nullptr;\n"
                "    TTF_TextEngine* textEngine  = nullptr;\n"
                "    SDL_Window*     window      = nullptr;\n"
                "};\n"
                "\n"
                "class ProgramScript : public ScriptBase {\n"
                "public:\n"
                "    std::string getName() const override { return \"ProgramScript\"; }\n"
                "    ProgramContext* ctx = nullptr;\n"
                "\n"
                "    void onStart() override;\n"
                "    void onUpdate(float dt) override;\n"
                "    void onDraw() override;\n"
                "    void onEnd() override;\n"
                "\n"
                "private:\n"
                "    float elapsed = 0.0f;\n"
                "};\n";

            std::ofstream out(filePath);
            out << headerContent;
            out.close();

            refreshEntries();
            for (size_t i = 0; i < entries.size(); ++i) {
                if (entries[i] == fullName) { selectedIndex = (int)i; break; }
            }
        }

        void createNewConfig() {
            std::string filePath = (std::filesystem::path(currentPath) / "CMakeLists.txt").string();
            
            // Use the current folder name as the project name, fallback to "NewProject"
            std::string projectName = std::filesystem::path(currentPath).filename().string();
            if (projectName.empty() || projectName == "." || projectName == "..") {
                projectName = "NewProject";
            }

            std::string templateContent =
                "# projects/" + projectName + "/CMakeLists.txt\n"
                "\n"
                "cmake_minimum_required(VERSION 3.20)\n"
                "\n"
                "set(PROJECT_SOURCES\n"
                "    scripts/MainMenuScript.cpp\n"
                ")\n"
                "\n"
                "add_executable(" + projectName + " ${PROJECT_SOURCES})\n"
                "\n"
                "# Tell engine.h we are building a project script\n"
                "target_transpile_definitions(" + projectName + " PRIVATE PROJECT_SCRIPT_BUILD=1)\n"
                "\n"
                "target_link_libraries(" + projectName + " PRIVATE DispersedEngine)\n"
                "\n"
                "target_include_directories(" + projectName + " PRIVATE\n"
                "    ${CMAKE_CURRENT_SOURCE_DIR}/scripts\n"
                ")\n"
                "\n"
                "# ── Post-build: copy the ENTIRE project folder next to the executable ────────\n"
                "# This creates Release/" + projectName + "/ containing assets/, scenes/, scripts/, etc.\n"
                "# Now the exe can just use \"./" + projectName + "/...\" to find everything!\n"
                "add_custom_command(TARGET " + projectName + " POST_BUILD\n"
                "    COMMAND ${CMAKE_COMMAND} -E copy_directory\n"
                "        ${CMAKE_CURRENT_SOURCE_DIR}\n"
                "        $<TARGET_FILE_DIR:" + projectName + ">/" + projectName + "\n"
                "    COMMENT \"Copying entire " + projectName + " folder next to executable\"\n"
                ")\n"
                "\n"
                "# ── Platform settings ──────────────────────────────────────────────────────────────\n"
                "if(EMSCRIPTEN)\n"
                "    set_target_properties(" + projectName + " PROPERTIES SUFFIX \".html\")\n"
                "    target_link_options(" + projectName + " PRIVATE\n"
                "        \"-sALLOW_MEMORY_GROWTH=1\"\n"
                "        \"-sUSE_WEBGL2=1\"\n"
                "        \"-sASYNCIFY\"\n"
                "        \"-sFORCE_FILESYSTEM\"\n"
                "        # Mount the entire project folder to /" + projectName + " in the virtual FS\n"
                "        \"--preload-file=${CMAKE_CURRENT_SOURCE_DIR}@/" + projectName + "\"\n"
                "        \"--shell-file=${CMAKE_SOURCE_DIR}/minimal_shell.html\"\n"
                "    )\n"
                "else()\n"
                "    set_target_properties(" + projectName + " PROPERTIES\n"
                "        SUFFIX \".exe\"\n"
                "    )\n"
                "endif()\n";

            std::ofstream out(filePath);
            out << templateContent;
            out.close();

            refreshEntries();
            for (size_t i = 0; i < entries.size(); ++i) {
                if (entries[i] == "CMakeLists.txt") { selectedIndex = (int)i; break; }
            }
        }
    };

    // ----------------------------------------------------------------
    // OptionSpinBox – like a SpinBox but cycles through string options
    // using left/right arrow buttons. No dropdown list. Has the ability to be editable/non-editable.
    // ----------------------------------------------------------------
    class OptionSpinBox : public IGuiElement {
    public:
        OptionSpinBox(SDL_Renderer* renderer, TTF_TextEngine* textEngine, TTF_Font* font,
                      SDL_FRect rect, const std::vector<std::string>& opts = {},
                      bool editable = false)
            : renderer(renderer), textEngine(textEngine), font(font),
              rect(rect), options(opts), currentIndex(0), editable(editable),
              editLine(renderer, textEngine, font, SDL_FRect{0,0,1,1}, "rename...") {}

        std::string getType() const override { return "OptionSpinBox"; }
        float getX() const override { return rect.x; }
        float getY() const override { return rect.y; }
        float getWidth() const override { return rect.w; }
        float getHeight() const override { return rect.h; }
        void setRect(SDL_FRect r) override { rect = r; }
        void setPos(SDL_Point p) override { rect.x = p.x; rect.y = p.y; }

        void setOptions(const std::vector<std::string>& opts) {
            options = opts;
            if (currentIndex >= (int)options.size()) currentIndex = (int)options.size() - 1;
            if (currentIndex < 0) currentIndex = 0;
            if (isEditing) cancelEdit(nullptr);
        }
        void setCurrentIndex(int idx) {
            if (idx >= 0 && idx < (int)options.size()) currentIndex = idx;
            else if (options.empty()) currentIndex = -1;
            else currentIndex = 0;
        }
        int getCurrentIndex() const { return currentIndex; }
        std::string getCurrentOption() const {
            if (currentIndex < 0 || currentIndex >= (int)options.size()) return "";
            return options[currentIndex];
        }

        void setEditable(bool e) { editable = e; }
        bool isEditable() const { return editable; }

        std::function<void(int)> onChange;                     // when option changes (by arrows)
        std::function<void(int, const std::string&)> onRenamed; // when option is renamed (if editable)

        bool handleEvent(const SDL_Event& ev, SDL_Window* window, float offsetX, float offsetY) override {
            if (isEditing && editable) {
                bool consumed = editLine.handleEvent(ev, window, offsetX, offsetY);
                if (!editLine.isActive()) {
                    commitRename(window);
                }
                if (ev.type == SDL_EVENT_KEY_DOWN && ev.key.key == SDLK_RETURN) {
                    if (editLine.isActive()) {
                        commitRename(window);
                        return true;
                    }
                }
                return consumed;
            }

            if (ev.type != SDL_EVENT_MOUSE_BUTTON_DOWN || ev.button.button != SDL_BUTTON_LEFT)
                return false;

            float mx = ev.button.x, my = ev.button.y;
            SDL_FRect shifted = { rect.x - offsetX, rect.y - offsetY, rect.w, rect.h };
            if (!inRect(mx, my, shifted)) return false;

            SDL_FRect leftBtn = { shifted.x, shifted.y, 30, shifted.h };
            SDL_FRect rightBtn = { shifted.x + shifted.w - 30, shifted.y, 30, shifted.h };
            SDL_FRect textArea = { shifted.x + 30, shifted.y, shifted.w - 60, shifted.h };

            if (inRect(mx, my, leftBtn)) {
                cycle(-1);
                return true;
            }
            if (inRect(mx, my, rightBtn)) {
                cycle(+1);
                return true;
            }
            if (editable && inRect(mx, my, textArea)) {
                startEdit(window);
                return true;
            }
            return false;
        }

        void handleGamepad(float cursorX, float cursorY, float offsetX, float offsetY,
                           SDL_Window* window, bool confirmDown, bool confirmDownLastFrame) override {
            if (isEditing && editable) {
                editLine.handleGamepad(cursorX, cursorY, offsetX, offsetY, window,
                                       confirmDown, confirmDownLastFrame);
                if (!editLine.isActive()) {
                    commitRename(window);
                }
                return;
            }

            SDL_FRect shifted = { rect.x - offsetX, rect.y - offsetY, rect.w, rect.h };
            if (confirmDown && !confirmDownLastFrame && inRect(cursorX, cursorY, shifted)) {
                cycle(+1);
            }
        }

        void render(float offsetX, float offsetY) override {
            SDL_FRect shifted = { rect.x - offsetX, rect.y - offsetY, rect.w, rect.h };

            // Background
            SDL_SetRenderDrawColor(renderer, 60, 60, 70, 255);
            SDL_RenderFillRect(renderer, &shifted);
            SDL_SetRenderDrawColor(renderer, 130, 130, 150, 255);
            SDL_RenderRect(renderer, &shifted);

            // Left button
            SDL_FRect leftBtn = { shifted.x, shifted.y, 30, shifted.h };
            SDL_SetRenderDrawColor(renderer, 60, 60, 60, 255);
            SDL_RenderFillRect(renderer, &leftBtn);
            drawCentredText("<", leftBtn, {220,220,220,255});

            // Right button
            SDL_FRect rightBtn = { shifted.x + shifted.w - 30, shifted.y, 30, shifted.h };
            SDL_SetRenderDrawColor(renderer, 60, 60, 60, 255);
            SDL_RenderFillRect(renderer, &rightBtn);
            drawCentredText(">", rightBtn, {220,220,220,255});

            // Text area
            SDL_FRect textArea = { shifted.x + 30, shifted.y, shifted.w - 60, shifted.h };

            if (isEditing && editable) {
                editLine.setRect(textArea);
                editLine.render(0.0f, 0.0f);
            } else {
                SDL_Color bg = {255,255,255,255};
                SDL_SetRenderDrawColor(renderer, bg.r, bg.g, bg.b, bg.a);
                SDL_RenderFillRect(renderer, &textArea);
                SDL_SetRenderDrawColor(renderer, 130,130,130,255);
                SDL_RenderRect(renderer, &textArea);

                std::string display = getCurrentOption();
                if (display.empty()) display = "[empty]";
                SDL_Color tc = {20,20,20,255};
                TTF_Text* t = TTF_CreateText(textEngine, font, display.c_str(), 0);
                if (t) {
                    TTF_SetTextColor(t, tc.r, tc.g, tc.b, tc.a);
                    TTF_DrawRendererText(t, textArea.x + 6.0f, textArea.y + (textArea.h - 20.0f) * 0.5f);
                    TTF_DestroyText(t);
                }
            }
        }

    private:
        SDL_Renderer* renderer;
        TTF_TextEngine* textEngine;
        TTF_Font* font;
        SDL_FRect rect;
        std::vector<std::string> options;
        int currentIndex = 0;
        bool editable = true;

        bool isEditing = false;
        Gui::LineEdit editLine;

        void cycle(int dir) {
            if (options.empty()) return;
            int newIdx = currentIndex + dir;
            if (newIdx < 0) newIdx = (int)options.size() - 1;
            if (newIdx >= (int)options.size()) newIdx = 0;
            if (newIdx != currentIndex) {
                currentIndex = newIdx;
                if (onChange) onChange(currentIndex);
            }
        }

        void startEdit(SDL_Window* window) {
            if (options.empty()) return;
            isEditing = true;
            editLine.setPlaceholder("rename...");
            editLine.clear();
            std::string current = getCurrentOption();
            for (char c : current) editLine.appendText(std::string(1, c));
            editLine.setActive(true);
            SDL_StartTextInput(window);
        }

        void commitRename(SDL_Window* window) {
            if (!isEditing) return;
            std::string newName = editLine.getText();
            if (!newName.empty() && currentIndex >= 0 && currentIndex < (int)options.size()) {
                options[currentIndex] = newName;
                if (onRenamed) onRenamed(currentIndex, newName);
            }
            cancelEdit(window);
        }

        void cancelEdit(SDL_Window* window) {
            if (!isEditing) return;
            isEditing = false;
            editLine.setActive(false);
            if (window) SDL_StopTextInput(window);
        }

        void drawCentredText(const char* str, SDL_FRect r, SDL_Color c) {
            TTF_Text* t = TTF_CreateText(textEngine, font, str, 0);
            if (!t) return;
            TTF_SetTextColor(t, c.r, c.g, c.b, c.a);
            int tw = 0, th = 0;
            TTF_GetTextSize(t, &tw, &th);
            TTF_DrawRendererText(t, r.x + (r.w - tw) * 0.5f, r.y + (r.h - th) * 0.5f);
            TTF_DestroyText(t);
        }

        static bool inRect(float x, float y, SDL_FRect r) {
            return x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h;
        }
    };


   class AnimationFrameEditor : public Dialog {
    public:
        AnimationFrameEditor(SDL_Renderer* renderer, TTF_TextEngine* textEngine, TTF_Font* font,
                            SDL_Window* window, Gui::FileExplorer& fileExp)
            : Dialog(renderer, textEngine, font, window, {0,0,750,680}, "Edit Animation Clips", "Save", "Cancel"),
            fileExplorer(fileExp),
            clipSelector(renderer, textEngine, font, SDL_FRect{0,0,1,1}),
            addFrameBtn(renderer, font, "+", SDL_FPoint{0,0}, 40, 30),
            removeFrameBtn(renderer, font, "-", SDL_FPoint{0,0}, 40, 30),
            moveUpBtn(renderer, font, "up", SDL_FPoint{0,0}, 40, 30),
            moveDownBtn(renderer, font, "dn", SDL_FPoint{0,0}, 40, 30),
            loadResourceBtn(renderer, font, "L-.animres", SDL_FPoint{0,0}, 120, 30),
            saveResourceBtn(renderer, font, "S-.animres", SDL_FPoint{0,0}, 120, 30),
            browseFrameBtn(renderer, font, "O-Img", SDL_FPoint{0,0}, 100, 30),
            playBtn(renderer, font, "Play", SDL_FPoint{0,0}, 40, 30),
            stopBtn(renderer, font, "Stop", SDL_FPoint{0,0}, 40, 30),
            addClipBtn(renderer, font, "+ Clip", SDL_FPoint{0,0}, 80, 30),
            removeClipBtn(renderer, font, "- Clip", SDL_FPoint{0,0}, 90, 30),
            setActiveBtn(renderer, font, "Activate", SDL_FPoint{0,0}, 90, 30)
        {
            setupCallbacks();
            clipSelector.setEditable(true);
            clipSelector.onChange = [this](int idx) {
                if (target && idx >= 0 && idx < (int)target->clips.size()) {
                    selectedClipIndex = idx;
                    syncFramesFromSelectedClip();
                }
            };
            clipSelector.onRenamed = [this](int idx, const std::string& newName) {
                if (target && idx >= 0 && idx < (int)target->clips.size()) {
                    target->clips[idx].name = newName;
                    // Refresh the clip selector options list
                    std::vector<std::string> clipNames;
                    for (const auto& clip : target->clips) clipNames.push_back(clip.name);
                    clipSelector.setOptions(clipNames);
                    clipSelector.setCurrentIndex(idx);
                    // Also update the active clip index if this was the active one?
                    // Not needed; active index is separate.
                }
            };
            speedSpinBox = std::make_unique<Gui::SpinBox>(renderer, textEngine, font,
                                              SDL_FRect{0,0,1,1}, 0.1f, 60.0f, 10.0f, 0.5f);
            speedSpinBox->onChange = [this](float newSpeed) {
                auto* clip = getSelectedClip();
                if (clip) {
                    clip->speed = newSpeed;
                }
            };
        }

        void setTarget(Components::AnimationState* anim) {
            target = anim;
            onResourceLoaded = nullptr;
            syncFromTarget();
        }

        // Must be called (e.g. right alongside setTarget()) before
        // load/saveResource() so frame image paths can be resolved/
        // relativized against the right project folder.
        void setProjectRoot(const std::string& root) { projectRoot = root; }

        bool onHandleGamepad(float cursorX, float cursorY, bool confirmDown, bool confirmDownLastFrame) override {
            // Delegate to interactive widgets; return true if consumed.
            bool consumed = false;
            // We can forward gamepad to buttons, but this is a simplified stub.
            return consumed;
        }

    protected:
        void onOpen() override {
            int w, h; SDL_GetWindowSize(window, &w, &h);
            logicalRect = { (w-750)*0.5f, (h-680)*0.5f, 750, 680 };
            syncFromTarget();
        }

        bool onHandleEvent(const SDL_Event& ev) override {
            // Clip management
            if (clipSelector.handleEvent(ev, window, 0,0)) return true;
            if (addClipBtn.handleEvent(ev, window, 0,0)) return true;
            if (removeClipBtn.handleEvent(ev, window, 0,0)) return true;
            if (setActiveBtn.handleEvent(ev, window, 0,0)) return true;
            // Frame management
            if (addFrameBtn.handleEvent(ev, window, 0,0)) return true;
            if (removeFrameBtn.handleEvent(ev, window, 0,0)) return true;
            if (moveUpBtn.handleEvent(ev, window, 0,0)) return true;
            if (moveDownBtn.handleEvent(ev, window, 0,0)) return true;
            if (loadResourceBtn.handleEvent(ev, window, 0,0)) return true;
            if (saveResourceBtn.handleEvent(ev, window, 0,0)) return true;
            if (browseFrameBtn.handleEvent(ev, window, 0,0)) return true;
            if (playBtn.handleEvent(ev, window, 0,0)) return true;
            if (stopBtn.handleEvent(ev, window, 0,0)) return true;
            if (speedSpinBox->handleEvent(ev, window, 0,0)) return true;

            // Click on frame list
            if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN && ev.button.button == SDL_BUTTON_LEFT) {
                float mx = ev.button.x, my = ev.button.y;
                if (mx >= frameListRect.x && mx <= frameListRect.x + frameListRect.w &&
                    my >= frameListRect.y && my <= frameListRect.y + frameListRect.h) {
                    float rowH = 25;
                    int idx = (int)((my - frameListRect.y) / rowH);
                    if (idx >= 0 && idx < (int)frameEntries.size()) {
                        selectedFrameIndex = idx;
                        currentFrame = idx;
                        return true;
                    }
                }
            }
            return false;
        }

        void onRender(SDL_FRect win) override {
            float y = win.y + 50;

            // --- Clip management area ---
            // Clip selector dropdown
            clipSelector.setRect({ win.x + 10, y, 200, 26 });
            clipSelector.render(0,0);
            // Clip buttons
            float bx = win.x + 230;
            addClipBtn.setRect({ bx, y, 80, 26 }); addClipBtn.render(0,0); bx += 100;
            removeClipBtn.setRect({ bx, y, 90, 26 }); removeClipBtn.render(0,0); bx += 100;
            setActiveBtn.setRect({ bx, y, 90, 26 }); setActiveBtn.render(0,0);
            y += 35;

            // --- Preview Canvas ---
            const float previewH = 170.0f;
            SDL_FRect previewRect = { win.x + 10, y, win.w - 30, previewH };
            SDL_SetRenderDrawColor(renderer, 40, 40, 50, 255);
            SDL_RenderFillRect(renderer, &previewRect);
            SDL_SetRenderDrawColor(renderer, 80, 80, 100, 255);
            SDL_RenderRect(renderer, &previewRect);
            renderPreview(previewRect);
            y += previewH + 15;

            // --- Frame list ---
            drawText("Frames:", win.x + 10, y, {200,200,200,255}); y += 25;
            frameListRect = { win.x + 10, y, win.w - 30, 150 };
            SDL_SetRenderDrawColor(renderer, 50,50,60,255);
            SDL_RenderFillRect(renderer, &frameListRect);
            SDL_Rect clipRect = { (int)frameListRect.x, (int)frameListRect.y, (int)frameListRect.w, (int)frameListRect.h };
            SDL_SetRenderClipRect(renderer, &clipRect);
            float rowH = 25;
            for (size_t i = 0; i < frameEntries.size(); ++i) {
                SDL_FRect row = { frameListRect.x, frameListRect.y + i*rowH, frameListRect.w, rowH };
                if ((int)i == selectedFrameIndex) {
                    SDL_SetRenderDrawColor(renderer, 70,70,120,255);
                    SDL_RenderFillRect(renderer, &row);
                }
                std::string displayText = frameEntries[i];
                if (displayText.empty()) displayText = "(empty)";
                int textWidth = 0, textHeight = 0;
                TTF_GetStringSize(font, displayText.c_str(), displayText.size(), &textWidth, &textHeight);
                float maxWidth = frameListRect.w - 10;
                if (textWidth > maxWidth) {
                    std::string truncated = "...";
                    int truncWidth = 0;
                    TTF_GetStringSize(font, "...", 3, &truncWidth, nullptr);
                    int availableWidth = maxWidth - truncWidth;
                    int startIdx = (int)displayText.size() - 1;
                    int currentWidth = 0;
                    while (startIdx >= 0 && currentWidth < availableWidth) {
                        int charW = 0;
                        TTF_GetStringSize(font, displayText.substr(startIdx, 1).c_str(), 1, &charW, nullptr);
                        currentWidth += charW;
                        if (currentWidth > availableWidth) break;
                        startIdx--;
                    }
                    startIdx++;
                    truncated = "..." + displayText.substr(startIdx);
                    displayText = truncated;
                }
                drawText(displayText.c_str(), row.x+5, row.y+2, {220,220,220,255});
            }
            SDL_SetRenderClipRect(renderer, nullptr);
            y = frameListRect.y + frameListRect.h + 10;

            drawText("Speed:", win.x + 10, y, {200,200,200,255});
            speedSpinBox->setRect({ win.x + 70, y + 20, 100, 26 });
            speedSpinBox->render(0,0);
            y += 60;

            // --- Frame operation buttons ---
            float btnX = win.x + 10;
            addFrameBtn.setRect({ btnX, y, 40, 30 }); addFrameBtn.render(0,0); btnX += 45;
            removeFrameBtn.setRect({ btnX, y, 40, 30 }); removeFrameBtn.render(0,0); btnX += 45;
            moveUpBtn.setRect({ btnX, y, 40, 30 }); moveUpBtn.render(0,0); btnX += 45;
            moveDownBtn.setRect({ btnX, y, 40, 30 }); moveDownBtn.render(0,0); btnX += 45;
            browseFrameBtn.setRect({ btnX, y, 100, 30 }); browseFrameBtn.render(0,0); btnX += 105;
            loadResourceBtn.setRect({ btnX, y, 120, 30 }); loadResourceBtn.render(0,0); btnX += 125;
            saveResourceBtn.setRect({ btnX, y, 120, 30 }); saveResourceBtn.render(0,0);
            y += 40;
            playBtn.setRect({ win.x + 10, y, 40, 30 }); playBtn.render(0,0);
            stopBtn.setRect({ win.x + 60, y, 120, 30 }); stopBtn.render(0,0);
        }

        void onReset() override {
            // nothing specific
        }

    public:
        void tick(float dt) override {
            Dialog::tick(dt);
            if (isPlaying && target) {
                // Use the clip currently selected in the dropdown (the same
                // one renderPreview() and the frame list show), not
                // target->active() -- that's the entity's separately-tracked
                // "live" clip, which only changes when Activate is pressed.
                // Ticking against active() while previewing a different
                // selected clip is what made Play/Stop look like they were
                // animating the wrong clip after switching the dropdown.
                auto* clip = getSelectedClip();
                if (clip && !clip->imageFrameResources.empty() && clip->speed > 0.0001f) {
                    frameTimer += dt;
                    float frameDuration = 1.0f / clip->speed;
                    if (frameTimer >= frameDuration) {
                        frameTimer -= frameDuration;
                        currentFrame = (currentFrame + 1) % (int)clip->imageFrameResources.size();
                    }
                }
            }
        }

    public:
        // Fires with the full filesystem path of the .animres file that was
        // just loaded from or saved to disk, so the owner (EntityInspector)
        // can record it on the entity -- mirrors how the Physics Resource
        // Load/Save buttons compute and store their own relative path.
        std::function<void(const std::string&)> onResourceLoaded;

    private:
        Components::AnimationState* target = nullptr;
        Gui::FileExplorer& fileExplorer;
        std::string projectRoot; // set via setProjectRoot(); used to resolve/relativize frame image paths

        // Clip management widgets
        Gui::OptionSpinBox clipSelector;
        Gui::Button addClipBtn, removeClipBtn, setActiveBtn;

        // Frame management widgets
        Gui::Button addFrameBtn, removeFrameBtn, moveUpBtn, moveDownBtn;
        Gui::Button loadResourceBtn, saveResourceBtn;
        Gui::Button browseFrameBtn;
        Gui::Button playBtn, stopBtn;

        std::unique_ptr<Gui::SpinBox> speedSpinBox;

        std::vector<std::string> frameEntries;
        int selectedFrameIndex = -1;
        SDL_FRect frameListRect = {0,0,0,0};
        int selectedClipIndex = 0;

        // Preview playback state
        bool isPlaying = false;
        int currentFrame = 0;
        float frameTimer = 0.0f;

        // ------------------------------------------------------------------
        // Helpers
        // ------------------------------------------------------------------
        void setupCallbacks() {
            addClipBtn.onClicked = [this]() { addClip(); };
            removeClipBtn.onClicked = [this]() { removeClip(); };
            setActiveBtn.onClicked = [this]() { setActiveClip(); };

            addFrameBtn.onClicked = [this]() { addFrame(); };
            removeFrameBtn.onClicked = [this]() { removeFrame(); };
            moveUpBtn.onClicked = [this]() { moveFrame(-1); };
            moveDownBtn.onClicked = [this]() { moveFrame(1); };
            loadResourceBtn.onClicked = [this]() { loadResource(); };
            saveResourceBtn.onClicked = [this]() { saveResource(); };
            browseFrameBtn.onClicked = [this]() { browseSelectedFrame(); };
            playBtn.onClicked = [this]() { isPlaying = true; };
            stopBtn.onClicked = [this]() { isPlaying = false; };
        }

        void syncFromTarget() {
            if (!target) return;
            // Update clip selector
            std::vector<std::string> clipNames;
            for (const auto& clip : target->clips) clipNames.push_back(clip.name);
            clipSelector.setOptions(clipNames);
            selectedClipIndex = target->activeClipIndex;
            clipSelector.setCurrentIndex(selectedClipIndex);
            syncFramesFromSelectedClip();
        }

        void syncFramesFromSelectedClip() {
            if (!target) return;
            if (selectedClipIndex >= 0 && selectedClipIndex < (int)target->clips.size()) {
                const auto& clip = target->clips[selectedClipIndex];  // <-- add this
                frameEntries = clip.imageFrameResources;
                speedSpinBox->setValue(clip.speed);
            } else {
                frameEntries.clear();
            }
            if (!frameEntries.empty()) {
                selectedFrameIndex = 0;
                currentFrame = 0;
            } else {
                selectedFrameIndex = -1;
            }
            // Switching clips shouldn't carry over a partially-elapsed
            // frame duration from the old clip's (possibly different) speed.
            frameTimer = 0.0f;
        }

        Components::AnimationClip* getSelectedClip() {
            if (!target) return nullptr;
            if (selectedClipIndex < 0 || selectedClipIndex >= (int)target->clips.size()) return nullptr;
            return &target->clips[selectedClipIndex];
        }

        void addClip() {
            if (!target) return;
            Components::AnimationClip newClip;
            newClip.name = "clip" + std::to_string(target->clips.size() + 1);
            target->clips.push_back(newClip);
            selectedClipIndex = (int)target->clips.size() - 1;
            syncFromTarget();
        }

        void removeClip() {
            if (!target || target->clips.size() <= 1) return;
            if (selectedClipIndex < 0 || selectedClipIndex >= (int)target->clips.size()) return;
            target->clips.erase(target->clips.begin() + selectedClipIndex);
            if (target->activeClipIndex >= (int)target->clips.size())
                target->activeClipIndex = (int)target->clips.size() - 1;
            if (selectedClipIndex >= (int)target->clips.size())
                selectedClipIndex = (int)target->clips.size() - 1;
            syncFromTarget();
        }

        void setActiveClip() {
            if (!target || selectedClipIndex < 0 || selectedClipIndex >= (int)target->clips.size()) return;
            target->activeClipIndex = selectedClipIndex;
            syncFromTarget();
            clipSelector.setCurrentIndex(target->activeClipIndex);
        }

        void addFrame() {
            auto* clip = getSelectedClip();
            if (!clip) return;
            clip->imageFrameResources.push_back("new_texture");
            syncFramesFromSelectedClip();
        }

        void removeFrame() {
            auto* clip = getSelectedClip();
            if (!clip || selectedFrameIndex < 0 || selectedFrameIndex >= (int)clip->imageFrameResources.size()) return;
            clip->imageFrameResources.erase(clip->imageFrameResources.begin() + selectedFrameIndex);
            syncFramesFromSelectedClip();
            if (selectedFrameIndex >= (int)clip->imageFrameResources.size())
                selectedFrameIndex = (int)clip->imageFrameResources.size() - 1;
        }

        void moveFrame(int dir) {
            auto* clip = getSelectedClip();
            if (!clip || selectedFrameIndex < 0) return;
            int newIdx = selectedFrameIndex + dir;
            if (newIdx < 0 || newIdx >= (int)clip->imageFrameResources.size()) return;
            std::swap(clip->imageFrameResources[selectedFrameIndex], clip->imageFrameResources[newIdx]);
            syncFramesFromSelectedClip();
            selectedFrameIndex = newIdx;
        }

        void browseSelectedFrame() {
            auto* clip = getSelectedClip();
            if (!clip || selectedFrameIndex < 0) return;
            fileExplorer.setFilter("*.png;*.jpg;*.svg;*.jpeg");
            fileExplorer.setSaveMode(false, "");
            fileExplorer.setCallback([this, clip](const std::string& path) {
                if (selectedFrameIndex >= 0 && selectedFrameIndex < (int)clip->imageFrameResources.size()) {
                    clip->imageFrameResources[selectedFrameIndex] = path;
                    syncFramesFromSelectedClip();
                }
                fileExplorer.reset();
            });
            fileExplorer.open();
        }

        void loadResource() {
            fileExplorer.setFilter("*.animres");
            fileExplorer.setSaveMode(false, "");
            fileExplorer.setCallback([this](const std::string& path) {
                if (!target) return;
                try {
                    std::ifstream f(path);
                    if (!f.is_open()) {
                        SDL_Log("Failed to open animation resource: %s", path.c_str());
                        fileExplorer.reset();
                        return;
                    }
                    nlohmann::json j;
                    f >> j;
                    *target = ComponentResourceManager::parseAnimationJson(j);
                    resolveAnimationFramePaths(*target, projectRoot);
                    syncFromTarget();
                    if (onResourceLoaded) onResourceLoaded(path);
                } catch (const std::exception& e) {
                    SDL_Log("Error loading animation resource: %s", e.what());
                }
                fileExplorer.reset();
            });
            fileExplorer.open();
        }

        void saveResource() {
            if (!target) return;
            fileExplorer.setFilter("*.animres");
            fileExplorer.setSaveMode(true, ".animres");
            fileExplorer.setCallback([this](const std::string& path) {
                try {
                    // Relativize a copy for the on-disk file; *target keeps
                    // absolute paths so rendering keeps working immediately
                    // after this save, with no reload needed.
                    Components::AnimationState toSave = *target;
                    relativizeAnimationFramePaths(toSave, projectRoot);
                    nlohmann::json j = ComponentResourceManager::serializeAnimationJson(toSave);
                    std::ofstream out(path);
                    if (out.is_open()) {
                        out << j.dump(4);
                        SDL_Log("Saved animation resource to %s", path.c_str());
                        if (onResourceLoaded) onResourceLoaded(path);
                    } else {
                        SDL_Log("Failed to save animation resource to %s", path.c_str());
                    }
                } catch (const std::exception& e) {
                    SDL_Log("Error saving animation resource: %s", e.what());
                }
                fileExplorer.reset();
            });
            fileExplorer.open();
        }


        void renderPreview(SDL_FRect previewRect) {
            auto* clip = getSelectedClip();
            if (!clip) return;
            if (currentFrame >= 0 && currentFrame < (int)clip->imageFrameResources.size()) {
                const std::string& path = clip->imageFrameResources[currentFrame];
                if (!path.empty()) {
                    SDL_Texture* tex = g_resources.TextureManager.Get(path);
                    if (!tex) {
                        g_resources.TextureManager.Load(path, path);
                        tex = g_resources.TextureManager.Get(path);
                    }
                    if (tex) {
                        float tw, th;
                        SDL_GetTextureSize(tex, &tw, &th);
                        float scaleX = previewRect.w / tw;
                        float scaleY = previewRect.h / th;
                        float scale = std::min(scaleX, scaleY);
                        float drawW = tw * scale;
                        float drawH = th * scale;
                        SDL_FRect dst = {
                            previewRect.x + (previewRect.w - drawW) * 0.5f,
                            previewRect.y + (previewRect.h - drawH) * 0.5f,
                            drawW, drawH
                        };
                        SDL_RenderTexture(renderer, tex, nullptr, &dst);
                    } else {
                        drawText("Image not found", previewRect.x + 10, previewRect.y + 10, {200,200,200,255});
                    }
                }
            }
        }
    };

    class SceneInspector {
    private:
        std::vector<std::unique_ptr<IGuiElement>>* guiElementsPtr = nullptr;
        AddChildDialog* addChildDialog = nullptr;
        struct ChildButtonInfo {
            SDL_FRect rect;
            size_t childIndex;
        };
        std::vector<ChildButtonInfo> childRemoveButtons;
        SDL_FRect addChildButtonRect;
        float inspectorScrollOffset = 0.0f;
        Gui::Scrollbar inspectorScrollbar;

    public:
        static constexpr float PANEL_W = 260.0f;

        float panelX() const {
            int w, h; SDL_GetWindowSize(window, &w, &h);
            return (float)w - PANEL_W;
        }

        SceneInspector(SDL_Renderer* r, TTF_TextEngine* te, TTF_Font* f, SDL_Window* w)
            : renderer(r), textEngine(te), font(f), window(w) {
            inspectorScrollbar.setOrientation(Gui::ScrollOrientation::Vertical);
            inspectorScrollbar.onChange = [this](float v){ inspectorScrollOffset = v; };
        }

        void setGuiElementsVector(std::vector<std::unique_ptr<IGuiElement>>* vec) { guiElementsPtr = vec; }
        void setAddChildDialog(AddChildDialog* dialog) { addChildDialog = dialog; }

        void setTarget(IGuiElement* elem) {
            if (target == elem) return;
            commitAllFields();
            target = elem;
            rebuildFields();
            inspectorScrollOffset = 0.0f;
        }

        void handleGamepad(float cursorX, float cursorY, bool confirmDown, bool confirmDownLastFrame) {
            if (!target) return;
            for (auto& f : fields) {
                for (auto& wgt : f.widgets)
                    wgt->handleGamepad(cursorX, cursorY, 0.0f, 0.0f, window, confirmDown, confirmDownLastFrame);
            }
        }

        IGuiElement* getTarget() const { return target; }

        bool handleEvent(const SDL_Event& ev) {
            if (!target) return false;
            if (inspectorScrollbar.handleEvent(ev)) return true;

            bool consumed = false;
            for (auto& f : fields) {
                for (auto& wgt : f.widgets)
                    if (wgt->handleEvent(ev, window, 0.0f, 0.0f)) consumed = true;
            }
            if (ev.type == SDL_EVENT_KEY_DOWN && (ev.key.key == SDLK_RETURN || ev.key.key == SDLK_TAB)) {
                commitAllFields();
            }
            if (target && target->getType() == "Panel") {
                handlePanelChildEvents(ev);
            }
            return consumed;
        }

        void render(float windowHeight) {
            if (!target) {
                SDL_FRect bg = { panelX(), 0, PANEL_W, windowHeight };
                SDL_SetRenderDrawColor(renderer, 28, 28, 35, 245);
                SDL_RenderFillRect(renderer, &bg);
                SDL_SetRenderDrawColor(renderer, 60, 60, 75, 255);
                SDL_RenderRect(renderer, &bg);
                drawLabel("Inspector", panelX() + 10, 10, {180,180,200,255});
                drawLabel("Click gui elements.", panelX() + 10, 40, {100,100,120,255});
                return;
            }

            SDL_FRect bg = { panelX(), 0, PANEL_W, windowHeight };
            SDL_SetRenderDrawColor(renderer, 28, 28, 35, 245);
            SDL_RenderFillRect(renderer, &bg);
            SDL_SetRenderDrawColor(renderer, 60, 60, 75, 255);
            SDL_RenderRect(renderer, &bg);

            float contentHeight = 0.0f;
            contentHeight += 10.0f + 22.0f + 22.0f + 8.0f;
            for (auto& f : fields) {
                contentHeight += 32.0f; // label
                for (auto& wgt : f.widgets) contentHeight += 32.0f + 4.0f;
                contentHeight += 8.0f;
            }
            if (target && target->getType() == "Panel") {
                contentHeight += 38.0f;
                auto* panel = static_cast<Panel*>(target);
                contentHeight += panel->getChildren().size() * (26.0f + 4.0f);
            }
            contentHeight += 8.0f;

            const float sbW = 12.0f;
            float viewHeight = windowHeight;
            inspectorScrollbar.setGeometry(panelX() + PANEL_W - sbW, 0.0f, sbW, viewHeight, contentHeight, viewHeight);
            inspectorScrollOffset = inspectorScrollbar.offset;

            SDL_Rect clip = { (int)panelX(), 0, (int)(PANEL_W - sbW), (int)viewHeight };
            SDL_SetRenderClipRect(renderer, &clip);

            float y = 10.0f - inspectorScrollOffset;
            drawLabel("Inspector", panelX() + 10, y, {200,200,220,255}); y += 22;
            drawLabel(("[" + target->getType() + "]").c_str(), panelX() + 10, y, {140,140,180,255}); y += 22;
            SDL_SetRenderDrawColor(renderer, 60, 60, 80, 255);
            SDL_FRect div = { panelX() + 5, y, PANEL_W - 10, 1 };
            SDL_RenderFillRect(renderer, &div); y += 8;

            for (auto& f : fields) {
                drawLabel(f.label.c_str(), panelX() + 8, y, {160,160,190,255}); y += 32;
                float widgetY = y;
                for (auto& wgt : f.widgets) {
                    wgt->setRect({ panelX() + 8, widgetY, PANEL_W - sbW - 20, 26 });
                    wgt->render(0.0f, 0.0f);
                    widgetY += 30;
                }
                y = widgetY + 8;
            }

            if (target && target->getType() == "Panel") {
                auto* panel = static_cast<Panel*>(target);
                const auto& children = panel->getChildren();

                SDL_FRect addBtn = { panelX() + 8, y, PANEL_W - sbW - 20, 30 };
                addChildButtonRect = addBtn;
                SDL_SetRenderDrawColor(renderer, 80, 120, 200, 255);
                SDL_RenderFillRect(renderer, &addBtn);
                drawLabel("+ Add Child", addBtn.x + 10, addBtn.y + 5, {255,255,255,255});
                y += addBtn.h + 8;

                childRemoveButtons.clear();
                for (size_t i = 0; i < children.size(); ++i) {
                    auto* child = children[i].get();
                    SDL_FRect row = { panelX() + 8, y, PANEL_W - sbW - 20, 26 };
                    drawLabel(("Child " + std::to_string(i) + " (" + child->getType() + ")").c_str(),
                            row.x + 5, row.y + 2, {200,200,220,255});
                    SDL_FRect removeBtn = { row.x + row.w - 30, row.y, 26, 26 };
                    SDL_SetRenderDrawColor(renderer, 200, 40, 40, 255);
                    SDL_RenderFillRect(renderer, &removeBtn);
                    drawLabel("X", removeBtn.x + 8, removeBtn.y + 2, {255,255,255,255});
                    childRemoveButtons.push_back({removeBtn, i});
                    y += row.h + 4;
                }
            }

            drawLabel("Enter = commit changes", panelX() + 8, y + 4, {80, 80, 100, 255});
            SDL_SetRenderClipRect(renderer, nullptr);
            inspectorScrollbar.render(renderer, 0.0f, 0.0f);
        }

        void commitAllFields() {
            if (!target) return;
            applyFields();
        }

        void syncFromTarget() {
            if (!target) return;
            for (auto& f : fields) {
                // For LineEdit and CheckBox, we can sync if not active
                for (auto& wgt : f.widgets) {
                    if (wgt->getType() == "LineEdit") {
                        auto* le = static_cast<Gui::LineEdit*>(wgt.get());
                        if (!le->isActive()) {
                            std::string live = liveValueForKey(f.key);
                            if (live != f.lastSyncedText) {
                                le->clear();
                                for (char c : live) le->appendText(std::string(1, c));
                                f.lastSyncedText = live;
                            }
                        }
                    } else if (wgt->getType() == "CheckBox") {
                        auto* cb = static_cast<Gui::CheckBox*>(wgt.get());
                        // we don't have a way to get live bool from key generically, so skip for now
                    }
                }
            }
        }

    private:
        struct Field {
            std::string label;
            std::string key;
            std::vector<std::unique_ptr<IGuiElement>> widgets;
            std::string lastSyncedText = "";
            float lastSyncedValue = 0.0f;
        };

        SDL_Renderer* renderer;
        TTF_TextEngine* textEngine;
        TTF_Font* font;
        SDL_Window* window;
        IGuiElement* target = nullptr;
        std::vector<Field> fields;

        void rebuildFields() {
            fields.clear();
            if (!target) return;

            auto addSpinBox = [&](const std::string& lbl, const std::string& key, float val, float min=0.0f, float max=9999.0f, float step=0.5f) {
                Field f; f.label = lbl; f.key = key; f.lastSyncedValue = val;
                auto sb = std::make_unique<Gui::SpinBox>(renderer, textEngine, font, SDL_FRect{0,0,1,1}, min, max, val, step);
                f.widgets.push_back(std::move(sb));
                fields.push_back(std::move(f));
            };
            auto addLineEdit = [&](const std::string& lbl, const std::string& key, const std::string& val) {
                Field f; f.label = lbl; f.key = key; f.lastSyncedText = val;
                auto le = std::make_unique<Gui::LineEdit>(renderer, textEngine, font, SDL_FRect{0,0,1,1}, "");
                for (char c : val) le->appendText(std::string(1, c));
                f.widgets.push_back(std::move(le));
                fields.push_back(std::move(f));
            };
            auto addCheckBox = [&](const std::string& lbl, const std::string& key, bool val) {
                Field f; f.label = lbl; f.key = key; f.lastSyncedValue = val ? 1.0f : 0.0f;
                auto cb = std::make_unique<Gui::CheckBox>(renderer, textEngine, font, SDL_FRect{0,0,1,1}, val);
                f.widgets.push_back(std::move(cb));
                fields.push_back(std::move(f));
            };

            // Common geometry: x, y, w, h as SpinBox
            addSpinBox("X", "x", target->getX(), -25000.0f, 25000.0f);
            addSpinBox("Y", "y", target->getY(), -25000.0f, 25000.0f);
            addSpinBox("Width", "w", target->getWidth(), 1.0f, 9999.0f);
            addSpinBox("Height", "h", target->getHeight(), 1.0f, 9999.0f);

            // Type-specific
            if (target->getType() == "Button") {
                auto* b = static_cast<Button*>(target);
                addLineEdit("Text", "btn_text", b->getText());
            } else if (target->getType() == "LineEdit") {
                auto* le = static_cast<LineEdit*>(target);
                addLineEdit("Placeholder", "le_placeholder", le->getPlaceholder());
            } else if (target->getType() == "SpinBox") {
                auto* sb = static_cast<SpinBox*>(target);
                addSpinBox("Min", "sb_min", sb->getMin());
                addSpinBox("Max", "sb_max", sb->getMax());
                addSpinBox("Step", "sb_step", sb->getStep());
                addSpinBox("Value", "sb_val", sb->getValue());
            } else if (target->getType() == "Panel") {
                auto* p = static_cast<Panel*>(target);
                addSpinBox("BG Red", "panel_r", (float)p->bgColor.r, 0, 255, 1);
                addSpinBox("BG Green", "panel_g", (float)p->bgColor.g, 0, 255, 1);
                addSpinBox("BG Blue", "panel_b", (float)p->bgColor.b, 0, 255, 1);
                addSpinBox("BG Alpha", "panel_a", (float)p->bgColor.a, 0, 255, 1);
            }
            // Add support for other types with boolean if needed – currently none
        }

        std::string liveValueForKey(const std::string& key) const {
            char buf[32];
            if (key == "x") { std::snprintf(buf, sizeof(buf), "%.1f", target->getX()); return buf; }
            if (key == "y") { std::snprintf(buf, sizeof(buf), "%.1f", target->getY()); return buf; }
            if (key == "w") { std::snprintf(buf, sizeof(buf), "%.1f", target->getWidth()); return buf; }
            if (key == "h") { std::snprintf(buf, sizeof(buf), "%.1f", target->getHeight()); return buf; }

            if (target->getType() == "Button") {
                auto* b = static_cast<Button*>(target);
                if (key == "btn_text") return b->getText();
            } else if (target->getType() == "LineEdit") {
                auto* le = static_cast<LineEdit*>(target);
                if (key == "le_placeholder") return le->getPlaceholder();
            } else if (target->getType() == "SpinBox") {
                auto* sb = static_cast<SpinBox*>(target);
                if (key == "sb_min")  { std::snprintf(buf, sizeof(buf), "%.1f", sb->getMin()); return buf; }
                if (key == "sb_max")  { std::snprintf(buf, sizeof(buf), "%.1f", sb->getMax()); return buf; }
                if (key == "sb_step") { std::snprintf(buf, sizeof(buf), "%.1f", sb->getStep()); return buf; }
                if (key == "sb_val")  { std::snprintf(buf, sizeof(buf), "%.1f", sb->getValue()); return buf; }
            } else if (target->getType() == "Panel") {
                auto* p = static_cast<Panel*>(target);
                if (key == "panel_r") { std::snprintf(buf, sizeof(buf), "%d", (int)p->bgColor.r); return buf; }
                if (key == "panel_g") { std::snprintf(buf, sizeof(buf), "%d", (int)p->bgColor.g); return buf; }
                if (key == "panel_b") { std::snprintf(buf, sizeof(buf), "%d", (int)p->bgColor.b); return buf; }
                if (key == "panel_a") { std::snprintf(buf, sizeof(buf), "%d", (int)p->bgColor.a); return buf; }
            }
            return "";
        }

        void applyFields() {
            if (!target) return;

            auto changed = [&](const std::string& key) -> bool {
                for (auto& f : fields) {
                    for (auto& wgt : f.widgets) {
                        if (f.key == key && wgt->getType() == "LineEdit") {
                            auto* le = static_cast<LineEdit*>(wgt.get());
                            return le->getText() != f.lastSyncedText;
                        }
                        if (f.key == key && wgt->getType() == "SpinBox") {
                            auto* sb = static_cast<SpinBox*>(wgt.get());
                            return sb->getValue() != f.lastSyncedValue;
                        }
                        if (f.key == key && wgt->getType() == "CheckBox") {
                            auto* cb = static_cast<CheckBox*>(wgt.get());
                            return (cb->getValue() ? 1.0f : 0.0f) != f.lastSyncedValue;
                        }
                    }
                }
                return false;
            };
            auto fval = [&](const std::string& key) -> float {
                for (auto& f : fields) {
                    for (auto& wgt : f.widgets) {
                        if (f.key == key && wgt->getType() == "SpinBox") {
                            return static_cast<SpinBox*>(wgt.get())->getValue();
                        }
                    }
                }
                return 0.0f;
            };
            auto fstr = [&](const std::string& key) -> std::string {
                for (auto& f : fields) {
                    for (auto& wgt : f.widgets) {
                        if (f.key == key && wgt->getType() == "LineEdit") {
                            return static_cast<LineEdit*>(wgt.get())->getText();
                        }
                    }
                }
                return "";
            };
            auto fbool = [&](const std::string& key) -> bool {
                for (auto& f : fields) {
                    for (auto& wgt : f.widgets) {
                        if (f.key == key && wgt->getType() == "CheckBox") {
                            return static_cast<CheckBox*>(wgt.get())->getValue();
                        }
                    }
                }
                return false;
            };
            auto markSynced = [&](const std::string& key) {
                for (auto& f : fields) {
                    if (f.key == key) {
                        for (auto& wgt : f.widgets) {
                            if (wgt->getType() == "LineEdit")
                                f.lastSyncedText = static_cast<LineEdit*>(wgt.get())->getText();
                            else if (wgt->getType() == "SpinBox")
                                f.lastSyncedValue = static_cast<SpinBox*>(wgt.get())->getValue();
                            else if (wgt->getType() == "CheckBox")
                                f.lastSyncedValue = static_cast<CheckBox*>(wgt.get())->getValue() ? 1.0f : 0.0f;
                        }
                    }
                }
            };

            // Common geometry
            bool xC = changed("x"), yC = changed("y"), wC = changed("w"), hC = changed("h");
            if (xC || yC || wC || hC) {
                SDL_FRect r = {
                    xC ? fval("x") : target->getX(),
                    yC ? fval("y") : target->getY(),
                    wC ? fval("w") : target->getWidth(),
                    hC ? fval("h") : target->getHeight()
                };
                if (r.w > 0 && r.h > 0) {
                    target->setRect(r);
                    markSynced("x"); markSynced("y"); markSynced("w"); markSynced("h");
                }
            }

            // Type-specific
            if (target->getType() == "Button") {
                if (changed("btn_text")) {
                    std::string t = fstr("btn_text");
                    if (!t.empty()) { static_cast<Button*>(target)->setText(t); markSynced("btn_text"); }
                }
            } else if (target->getType() == "LineEdit") {
                if (changed("le_placeholder")) {
                    static_cast<LineEdit*>(target)->setPlaceholder(fstr("le_placeholder"));
                    markSynced("le_placeholder");
                }
            } else if (target->getType() == "SpinBox") {
                auto* sb = static_cast<SpinBox*>(target);
                if (changed("sb_min"))  { sb->setMin(fval("sb_min"));   markSynced("sb_min"); }
                if (changed("sb_max"))  { sb->setMax(fval("sb_max"));   markSynced("sb_max"); }
                if (changed("sb_step")) { sb->setStep(fval("sb_step")); markSynced("sb_step"); }
                if (changed("sb_val"))  { sb->setValue(fval("sb_val")); markSynced("sb_val"); }
            } else if (target->getType() == "Panel") {
                auto* p = static_cast<Panel*>(target);
                if (changed("panel_r")) { p->bgColor.r = (Uint8)std::clamp((int)fval("panel_r"), 0, 255); markSynced("panel_r"); }
                if (changed("panel_g")) { p->bgColor.g = (Uint8)std::clamp((int)fval("panel_g"), 0, 255); markSynced("panel_g"); }
                if (changed("panel_b")) { p->bgColor.b = (Uint8)std::clamp((int)fval("panel_b"), 0, 255); markSynced("panel_b"); }
                if (changed("panel_a")) { p->bgColor.a = (Uint8)std::clamp((int)fval("panel_a"), 0, 255); markSynced("panel_a"); }
            }
        }

        void handlePanelChildEvents(const SDL_Event& ev) {
            if (ev.type != SDL_EVENT_MOUSE_BUTTON_DOWN || ev.button.button != SDL_BUTTON_LEFT) return;
            float mx = ev.button.x, my = ev.button.y;

            if (mx >= addChildButtonRect.x && mx <= addChildButtonRect.x + addChildButtonRect.w &&
                my >= addChildButtonRect.y && my <= addChildButtonRect.y + addChildButtonRect.h) {
                if (addChildDialog && target && target->getType() == "Panel") {
                    addChildDialog->parentPanel = static_cast<Panel*>(target);
                    modeBeforeDialog = currentEditMode;
                    currentEditMode = EditMode::Dialog;
                    addChildDialog->open();
                }
                return;
            }

            for (auto& info : childRemoveButtons) {
                if (mx >= info.rect.x && mx <= info.rect.x + info.rect.w &&
                    my >= info.rect.y && my <= info.rect.y + info.rect.h) {
                    if (target && target->getType() == "Panel") {
                        auto* panel = static_cast<Panel*>(target);
                        auto& children = panel->getChildrenMutable();
                        if (info.childIndex < children.size()) {
                            children.erase(children.begin() + info.childIndex);
                            rebuildFields();
                        }
                    }
                    return;
                }
            }
        }

        void drawLabel(const char* s, float x, float y, SDL_Color c) {
            TTF_Text* t = TTF_CreateText(textEngine, font, s, 0);
            if (!t) return;
            TTF_SetTextColor(t, c.r, c.g, c.b, c.a);
            TTF_DrawRendererText(t, x, y);
            TTF_DestroyText(t);
        }
    };

   class EntityInspector {
    public:
        static constexpr float PANEL_W = 260.0f;
        float panelX() const {
            int w, h; SDL_GetWindowSize(window, &w, &h);
            return (float)w - PANEL_W;
        }

        EntityInspector(SDL_Renderer* r, TTF_TextEngine* te, TTF_Font* f, SDL_Window* w, FileExplorer& fileExp)
        : renderer(r), textEngine(te), font(f), window(w),
        fileExplorer(fileExp),
        componentSelector(r, te, f, SDL_FRect{0,0,1,1}, componentOptions),
        addComponentBtn(r, f, "Add Component", SDL_FPoint{0,0}, 100, 30)
        {
            inspectorScrollbar.setOrientation(Gui::ScrollOrientation::Vertical);
            inspectorScrollbar.onChange = [this](float v){ inspectorScrollOffset = v; };
            animFrameEditor = std::make_unique<Gui::AnimationFrameEditor>(renderer, textEngine, font, window, fileExplorer);
        }

        void setTarget(ECSWorld& w, Entity e) {
            if (world == &w && targetEntity == e) return;
            commitAllFields(); world = &w; targetEntity = e;
            rebuildFields(); inspectorScrollOffset = 0.0f;
        }

        void clearTarget() {
            commitAllFields(); world = nullptr; targetEntity = (Entity)-1;
            fields.clear(); inspectorScrollOffset = 0.0f;
        }

        bool handleEvent(const SDL_Event& ev) {
            if (inspectorScrollbar.handleEvent(ev)) return true;
            if (!world || targetEntity == (Entity)-1) return false;
            bool consumed = false;
            for (auto& f : fields) {
                for (auto& wgt : f.widgets) {
                    if (wgt->handleEvent(ev, window, 0.0f, 0.0f)) consumed = true;
                    if (wgt->getType() == "LineEdit") {
                        auto* le = static_cast<Gui::LineEdit*>(wgt.get());
                        if (le->isActive()) f.textEverActive = true;
                    }
                }
            }
            if (componentSelector.handleEvent(ev, window, 0.0f, 0.0f)) return true;
            if (addComponentBtn.handleEvent(ev, window, 0.0f, 0.0f)) return true;
            if (animFrameEditor && animFrameEditor->isOpen()) {
                // The dialog handles its own events in the main loop; we don't forward here.
            }
            if (ev.type == SDL_EVENT_KEY_DOWN && (ev.key.key == SDLK_RETURN || ev.key.key == SDLK_TAB)) commitAllFields();
            // Safe now: we're fully done iterating `fields` above, so clearing
            // and rebuilding it here can't yank widgets out from under an
            // in-progress loop.
            if (pendingRebuild) { pendingRebuild = false; rebuildFields(); }
            return consumed;
        }

        void handleGamepad(float cursorX, float cursorY, bool confirmDown, bool confirmDownLastFrame) {
            if (!world || targetEntity == (Entity)-1) return;
            for (auto& f : fields) {
                for (auto& wgt : f.widgets)
                    wgt->handleGamepad(cursorX, cursorY, 0.0f, 0.0f, window, confirmDown, confirmDownLastFrame);
            }
            // Same reasoning as handleEvent(): rebuild only after the loop above
            // has fully finished, never from inside a widget's own callback.
            if (pendingRebuild) { pendingRebuild = false; rebuildFields(); }
        }

        void render(float windowHeight) {
            if (!world || targetEntity == (Entity)-1) { drawEmptyPanel(windowHeight); return; }
            SDL_FRect bg = { panelX(), 0, PANEL_W, windowHeight };
            SDL_SetRenderDrawColor(renderer, 28, 28, 35, 245); SDL_RenderFillRect(renderer, &bg);
            SDL_SetRenderDrawColor(renderer, 60, 60, 75, 255); SDL_RenderRect(renderer, &bg);
            
            float contentHeight = 0.0f;
            contentHeight += 10.0f + 24.0f + 22.0f + 8.0f;
            for (auto& f : fields) {
                contentHeight += 32.0f;
                for (auto& wgt : f.widgets) contentHeight += 32.0f + 4.0f;
                contentHeight += 8.0f;
            }
            contentHeight += 80.0f;
            contentHeight += 8.0f;
            
            float extraDropdownHeight = 0.0f;
            for (auto& f : fields) {
                for (auto& wgt : f.widgets) {
                    if (wgt->getType() == "OptionBox") {
                        auto* opt = static_cast<Gui::OptionBox*>(wgt.get());
                        if (opt->isOpen()) extraDropdownHeight += opt->getDropdownHeight();
                    }
                }
            }
            if (componentSelector.isOpen()) extraDropdownHeight += componentSelector.getDropdownHeight();
            contentHeight += extraDropdownHeight;
            contentHeight += 20.0f;

            const float sbW = 12.0f;
            float viewHeight = windowHeight;
            inspectorScrollbar.setGeometry(panelX() + PANEL_W - sbW, 0.0f, sbW, viewHeight, contentHeight, viewHeight);
            inspectorScrollOffset = inspectorScrollbar.offset;
            
            SDL_Rect clip = { (int)panelX(), 0, (int)(PANEL_W - sbW), (int)viewHeight };
            SDL_SetRenderClipRect(renderer, &clip);
            
            float y = 10.0f - inspectorScrollOffset;
            drawLabel("Entity Inspector", panelX() + 10, y, {200,200,220,255}); y += 24;
            drawLabel(("ID: " + std::to_string(targetEntity)).c_str(), panelX() + 10, y, {140,140,180,255}); y += 22;
            SDL_SetRenderDrawColor(renderer, 60, 60, 80, 255);
            SDL_FRect div = { panelX() + 5, y, PANEL_W - sbW - 10, 1 }; SDL_RenderFillRect(renderer, &div); y += 8;
            
            for (auto& f : fields) {
                drawLabel(f.label.c_str(), panelX() + 8, y, {160,160,190,255}); y += 32;
                float widgetY = y;
                for (auto& wgt : f.widgets) {
                    wgt->setRect({ panelX() + 8, widgetY, PANEL_W - sbW - 20, 26 });
                    wgt->render(0.0f, 0.0f);
                    widgetY += 30;
                }
                y = widgetY + 8;
            }
            
            float addUIY = y + 20.0f;
            drawLabel("Add Component:", panelX() + 8, addUIY, {160,160,190,255}); 
            addUIY += 24.0f;
            componentSelector.setRect({ panelX() + 8, addUIY, PANEL_W - sbW - 20, 26 });
            componentSelector.render(0.0f, 0.0f);
            addUIY += 32.0f;
            addComponentBtn.setRect({ panelX() + 8, addUIY, PANEL_W - sbW - 20, 30 });
            addComponentBtn.render(0.0f, 0.0f);
            
            drawLabel("Enter = commit changes", panelX() + 8, y + 4, {80, 80, 100, 255});
            
            // --- FIX: DISABLE CLIPPING BEFORE RENDERING DROPDOWNS ---
            SDL_SetRenderClipRect(renderer, nullptr);
            
            for (auto& f : fields) {
                for (auto& wgt : f.widgets) {
                    if (wgt->getType() == "OptionBox") {
                        auto* opt = static_cast<Gui::OptionBox*>(wgt.get());
                        if (opt->isOpen()) opt->render(0.0f, 0.0f);
                    }
                }
            }
            if (componentSelector.isOpen()) componentSelector.render(0.0f, 0.0f);
            
            inspectorScrollbar.render(renderer, 0.0f, 0.0f);
        }

        void commitAllFields() {
            if (!world || targetEntity == (Entity)-1) return;
            Entity e = targetEntity;
            for (auto& f : fields) {
                for (auto& wgt : f.widgets) {
                    if (wgt->getType() == "SpinBox") {
                        auto* sb = static_cast<Gui::SpinBox*>(wgt.get());
                        float val = sb->getValue();
                        if (val != f.lastSyncedValue) {
                            if (f.key == "pos_x") {
                                if (world->has_position3d[e]) world->position3d_pool[e].x = val;
                                else world->position_pool[e].x = val;
                            }
                            else if (f.key == "pos_y") {
                                if (world->has_position3d[e]) world->position3d_pool[e].y = val;
                                else world->position_pool[e].y = val;
                            }
                            else if (f.key == "pos_z") {
                                if (world->has_position3d[e]) world->position3d_pool[e].z = val;
                            }
                            else if (f.key == "rect_w") world->rectangle_shape_pool[e].w = val;
                            else if (f.key == "rect_h") world->rectangle_shape_pool[e].h = val;
                            else if (f.key == "depth3d") world->depth3d_pool[e].depth = val;
                            else if (f.key == "z_index") world->z_index_pool[e].z = (int)val;
                            else if (f.key == "phys_shape") {
                                auto newShape = SpinboxIndexToShapeType((int)val);
                                if (newShape == Physics::ShapeType::Polygon) {
                                    auto& pb = world->physics_body_pool[e];
                                    float oldW = pb.width;
                                    float oldH = pb.height;
                                    float newW = world->rectangle_shape_pool[e].w;
                                    float newH = world->rectangle_shape_pool[e].h;
                                    
                                    // Rescale existing polygon points to match new dimensions
                                    if (!pb.polygonPoints.empty() && oldW > 0.0001f && oldH > 0.0001f) {
                                        float scaleX = newW / oldW;
                                        float scaleY = newH / oldH;
                                        for (auto& pt : pb.polygonPoints) {
                                            pt.x *= scaleX;
                                            pt.y *= scaleY;
                                        }
                                    }
                                    
                                    // Always update width/height to match rectangle shape
                                    pb.width = newW;
                                    pb.height = newH;
                                    
                                    // Create default points if none exist
                                    if (pb.polygonPoints.empty()) {
                                        pb.polygonPoints = MakeDefaultTrianglePoints(newW, newH);
                                    }
                                }
                                world->physics_body_pool[e].shapeType = newShape;
                            }
                            else if (f.key == "phys_r") world->physics_body_pool[e].radius = val;
                            else if (f.key == "anim_speed") world->animation_state_pool[e].active().speed = val;
                            else if (f.key == "sfx_vol") world->sfx_emitter_pool[e].volume = val;
                            else if (f.key == "sfx_pitch") world->sfx_emitter_pool[e].pitch = val;
                            else if (f.key == "sfx_speed") world->sfx_emitter_pool[e].speed = val;
                            else if (f.key == "rotation") world->rotation_pool[e].degrees = val;
                            else if (f.key == "scale_x") world->scale_pool[e].x = val;
                            else if (f.key == "scale_y") world->scale_pool[e].y = val;
                            else if (f.key == "mat3d_r") world->material3d_pool[e].r = val / 255.0f;
                            else if (f.key == "mat3d_g") world->material3d_pool[e].g = val / 255.0f;
                            else if (f.key == "mat3d_b") world->material3d_pool[e].b = val / 255.0f;
                            else if (f.key == "mesh_w") world->mesh3d_pool[e].meshW = val;
                            else if (f.key == "mesh_h") world->mesh3d_pool[e].meshH = val;
                            else if (f.key == "mesh_d") world->mesh3d_pool[e].meshD = val;
                            else if (f.key == "gridmap3d_cw") world->gridmap3d_pool[e].cellWidth = val;
                            else if (f.key == "gridmap3d_ch") world->gridmap3d_pool[e].cellHeight = val;
                            else if (f.key == "gridmap3d_cd") world->gridmap3d_pool[e].cellDepth = val;
                            f.lastSyncedValue = val;
                        }
                    } else if (wgt->getType() == "LineEdit") {
                        auto* le = static_cast<Gui::LineEdit*>(wgt.get());
                        std::string text = le->getText();
                        if (text != f.lastSyncedText) {
                            if (f.key == "metadata_name") {
                                // Guard: only commit if the user actually
                                // focused this field. Selecting an entity
                                // rebuilds this field from the entity's
                                // current name (lastSyncedText is seeded to
                                // match), so a diff here should only ever
                                // happen because of real typing -- but this
                                // makes that guarantee explicit rather than
                                // implicit, so a name can never be reset to
                                // blank just by selecting/reselecting.
                                if (f.textEverActive) {
                                    world->metadata_pool[e].name = text;
                                }
                            } else if (f.key == "tex_res") {
                                if (!text.empty()) {
                                    g_resources.TextureManager.Load(text, text);
                                }
                                world->texture_ref_pool[e].resourceName = text;
                            } else if (f.key == "sfx_name") {
                                world->sfx_emitter_pool[e].sfxName = text;
                            } else if (f.key == "tile_tex") {
                                auto& tm = world->tilemap_pool[e];
                                tm.textureName = text;
                                // Cell size is never set manually -- it always
                                // matches the loaded image's own pixel size, so
                                // painted cells line up 1:1 with the art.
                                if (!text.empty()) {
                                    SDL_Texture* tmTex = g_resources.TextureManager.Get(text);
                                    if (!tmTex) {
                                        g_resources.TextureManager.Load(text, text);
                                        tmTex = g_resources.TextureManager.Get(text);
                                    }
                                    if (tmTex) {
                                        float tw = 0.0f, th = 0.0f;
                                        SDL_GetTextureSize(tmTex, &tw, &th);
                                        if (tw > 0.0f) tm.cellWidth = tw;
                                        if (th > 0.0f) tm.cellHeight = th;
                                    }
                                }
                            }  else if (f.key == "mesh3d_path") {
                                world->mesh3d_pool[e].modelPath = text;
                            } else if (f.key == "gridmap3d_mat") {
                                world->gridmap3d_pool[e].materialName = text;
                            }
                            f.lastSyncedText = text;
                        }
                    } else if (wgt->getType() == "CheckBox") {
                        auto* cb = static_cast<Gui::CheckBox*>(wgt.get());
                        bool val = cb->getValue();
                        if (val != (f.lastSyncedValue > 0.5f)) {
                            if (f.key == "sfx_col") world->sfx_emitter_pool[e].playOnCollision = val;
                            else if (f.key == "tile_snap") world->tilemap_pool[e].snapEnabled = val;
                            else if (f.key == "gridmap3d_snap") world->gridmap3d_pool[e].snapEnabled = val;
                            else if (f.key == "mat3d_override") world->material3d_pool[e].overrideColor = val;
                            f.lastSyncedValue = val ? 1.0f : 0.0f;
                        }
                    }
                }
            }
        }


        
        void syncFromWorld() {
            if (!world || targetEntity == (Entity)-1) return;
            Entity e = targetEntity;
            for (auto& f : fields) {
                // Special handling for the animation panel
                if (f.key == "anim_panel") {
                    auto& anim = world->animation_state_pool[e];
                    Gui::OptionSpinBox* clipSelector = nullptr;
                    Gui::SpinBox* widthSpin = nullptr;
                    Gui::SpinBox* heightSpin = nullptr;
                    for (auto& wgt : f.widgets) {
                        if (wgt->getType() == "OptionSpinBox") {
                            clipSelector = static_cast<Gui::OptionSpinBox*>(wgt.get());
                        } else if (wgt->getType() == "SpinBox") {
                            if (!widthSpin) {
                                widthSpin = static_cast<Gui::SpinBox*>(wgt.get());
                            } else {
                                heightSpin = static_cast<Gui::SpinBox*>(wgt.get());
                            }
                        }
                    }
                    if (clipSelector) {
                        std::vector<std::string> clipNames;
                        for (const auto& clip : anim.clips) clipNames.push_back(clip.name);
                        clipSelector->setOptions(clipNames);
                        clipSelector->setCurrentIndex(anim.activeClipIndex);
                    }
                    if (widthSpin) {
                        float val = anim.active().frameWidth;
                        if (val != widthSpin->getValue()) widthSpin->setValue(val);
                    }
                    if (heightSpin) {
                        float val = anim.active().frameHeight;
                        if (val != heightSpin->getValue()) heightSpin->setValue(val);
                    }
                    continue;
                }
                
                // Generic handling for other fields
                for (auto& wgt : f.widgets) {
                    if (wgt->getType() == "SpinBox") {
                        auto* sb = static_cast<Gui::SpinBox*>(wgt.get());
                        if (!sb->isActive()) {
                            float worldVal = 0.0f;
                            if (f.key == "pos_x") {
                                worldVal = world->has_position3d[e] ? world->position3d_pool[e].x : world->position_pool[e].x;
                            } else if (f.key == "pos_y") {
                                worldVal = world->has_position3d[e] ? world->position3d_pool[e].y : world->position_pool[e].y;
                            } else if (f.key == "pos_z") {
                                // FIXED: was incorrectly using position_pool[e].y
                                worldVal = world->has_position3d[e] ? world->position3d_pool[e].z : 0.0f;
                            } else if (f.key == "rect_w") worldVal = world->rectangle_shape_pool[e].w;
                            else if (f.key == "rect_h") worldVal = world->rectangle_shape_pool[e].h;
                            else if (f.key == "depth3d") worldVal = world->depth3d_pool[e].depth;
                            else if (f.key == "z_index") worldVal = (float)world->z_index_pool[e].z;
                            else if (f.key == "phys_shape") worldVal = (float)ShapeTypeToSpinboxIndex(world->physics_body_pool[e].shapeType);
                            else if (f.key == "phys_w") worldVal = world->physics_body_pool[e].width;
                            else if (f.key == "phys_h") worldVal = world->physics_body_pool[e].height;
                            else if (f.key == "phys_r") worldVal = world->physics_body_pool[e].radius;
                            else if (f.key == "anim_speed") worldVal = world->animation_state_pool[e].active().speed;
                            else if (f.key == "sfx_vol") worldVal = world->sfx_emitter_pool[e].volume;
                            else if (f.key == "sfx_pitch") worldVal = world->sfx_emitter_pool[e].pitch;
                            else if (f.key == "sfx_speed") worldVal = world->sfx_emitter_pool[e].speed;
                            else if (f.key == "rotation") worldVal = world->rotation_pool[e].degrees;
                            else if (f.key == "scale_x") worldVal = world->scale_pool[e].x;
                            else if (f.key == "scale_y") worldVal = world->scale_pool[e].y;
                            else if (f.key == "mat3d_r") worldVal = world->material3d_pool[e].r * 255.0f;
                            else if (f.key == "mat3d_g") worldVal = world->material3d_pool[e].g * 255.0f;
                            else if (f.key == "mat3d_b") worldVal = world->material3d_pool[e].b * 255.0f;
                            else if (f.key == "gridmap3d_cw") worldVal = world->gridmap3d_pool[e].cellWidth;
                            else if (f.key == "gridmap3d_ch") worldVal = world->gridmap3d_pool[e].cellHeight;
                            else if (f.key == "gridmap3d_cd") worldVal = world->gridmap3d_pool[e].cellDepth;
                            if (worldVal != f.lastSyncedValue) { 
                                sb->setValue(worldVal); 
                                f.lastSyncedValue = worldVal; 
                            }
                        }
                    } else if (wgt->getType() == "LineEdit") {
                        auto* le = static_cast<Gui::LineEdit*>(wgt.get());
                        if (!le->isActive()) {
                            std::string worldText = "";
                            if (f.key == "metadata_name") worldText = world->metadata_pool[e].name;
                            else if (f.key == "tex_res") worldText = world->texture_ref_pool[e].resourceName;
                            else if (f.key == "sfx_name") worldText = world->sfx_emitter_pool[e].sfxName;
                            else if (f.key == "tile_tex") worldText = world->tilemap_pool[e].textureName;
                            else if (f.key == "mesh3d_path") worldText = world->mesh3d_pool[e].modelPath;
                            else if (f.key == "gridmap3d_mat") worldText = world->gridmap3d_pool[e].materialName;
                            if (worldText != f.lastSyncedText) {
                                le->clear(); 
                                for (char c : worldText) le->appendText(std::string(1, c)); 
                                f.lastSyncedText = worldText;
                            }
                        }
                    } else if (wgt->getType() == "CheckBox") {
                        auto* cb = static_cast<Gui::CheckBox*>(wgt.get());
                        bool worldVal = false;
                        if (f.key == "sfx_col") worldVal = world->sfx_emitter_pool[e].playOnCollision;
                        else if (f.key == "tile_snap") worldVal = world->tilemap_pool[e].snapEnabled;
                        else if (f.key == "gridmap3d_snap") worldVal = world->gridmap3d_pool[e].snapEnabled;
                        else if (f.key == "mat3d_override") worldVal = world->material3d_pool[e].overrideColor;
                        if (worldVal != (f.lastSyncedValue > 0.5f)) {
                            cb->setValue(worldVal);
                            f.lastSyncedValue = worldVal ? 1.0f : 0.0f;
                        }
                    }
                }
            }
        }

    private:
        struct Field {
            std::string label;
            std::string key;
            std::vector<std::unique_ptr<IGuiElement>> widgets;
            float lastSyncedValue = 0.0f;
            std::string lastSyncedText = "";
            // Set once a LineEdit widget in this field has actually been
            // clicked into / focused by the user. Used to guard against
            // writing to world state (e.g. metadata name) from anything
            // other than a genuine user edit -- rebuilding this field (which
            // happens every time the selection changes) always seeds
            // lastSyncedText/widget text from the current world value, so a
            // freshly-rebuilt field can never itself look like an edit; this
            // flag makes that guarantee explicit and easy to audit.
            bool textEverActive = false;

            Field() = default;
            Field(Field&&) = default;
            Field& operator=(Field&&) = default;
            Field(const Field&) = delete;
            Field& operator=(const Field&) = delete;
        };

        SDL_Renderer* renderer;
        TTF_TextEngine* textEngine;
        TTF_Font* font;
        SDL_Window* window;
        FileExplorer& fileExplorer;
        ECSWorld* world = nullptr;
        Entity targetEntity = (Entity)-1;
        std::vector<Field> fields;
        bool pendingRebuild = false; // see handleEvent()/clipSelector onChange
        Gui::Scrollbar inspectorScrollbar;
        float inspectorScrollOffset = 0.0f;
        std::string projectRoot;

        
        std::vector<std::string> componentOptions = {"PhysicsBody", "TextureRef", "AnimationState", "SfxEmitter", "Rotation", "Scale", "TileMap"};
        Gui::OptionBox componentSelector;
        Gui::Button addComponentBtn;

    public:
        std::unique_ptr<Gui::AnimationFrameEditor> animFrameEditor;
        void setProjectRoot(const std::string& root) { projectRoot = root; }

    private:
        void rebuildFields() {
            fields.clear();
            if (!world || targetEntity == (Entity)-1) return;
            Entity e = targetEntity;
            
            // Determine if this is a 3D entity
            bool is3D = world->has_depth3d[e] || world->has_mesh3d[e] || world->has_position3d[e];
            
            // --- Filter component options based on 3D/2D mode ---
            std::vector<std::string> availableComponents;
            if (is3D) {
                // 3D mode components
                if (!world->has_mesh3d[e]) availableComponents.push_back("Mesh3D");
                if (!world->has_material3d[e]) availableComponents.push_back("Material3D");
                if (!world->has_collider3d[e]) availableComponents.push_back("Collider3D");
                if (!world->has_depth3d[e]) availableComponents.push_back("Depth3D");
                if (!world->has_gridmap3d[e]) availableComponents.push_back("GridMap3D");
                // These work in both modes
                if (!world->has_physics_body[e]) availableComponents.push_back("PhysicsBody");
                if (!world->has_animation_state[e]) availableComponents.push_back("AnimationState");
                if (!world->has_rotation[e]) availableComponents.push_back("Rotation");
                if (!world->has_scale[e]) availableComponents.push_back("Scale");
            } else {
                // 2D mode components
                if (!world->has_physics_body[e]) availableComponents.push_back("PhysicsBody");
                if (!world->has_texture_ref[e]) availableComponents.push_back("TextureRef");
                if (!world->has_animation_state[e]) availableComponents.push_back("AnimationState");
                if (!world->has_sfx_emitter[e]) availableComponents.push_back("SfxEmitter");
                if (!world->has_rotation[e]) availableComponents.push_back("Rotation");
                if (!world->has_scale[e]) availableComponents.push_back("Scale");
                if (!world->has_tilemap[e]) availableComponents.push_back("TileMap");
            }
            componentSelector.setOptions(availableComponents);
            
            auto addSpinBox = [&](const std::string& label, const std::string& key, float val, float min=0.0f, float max=9999.0f, float step=0.5f) {
                Field f; f.label = label; f.key = key; f.lastSyncedValue = val;
                auto sb = std::make_unique<Gui::SpinBox>(renderer, textEngine, font, SDL_FRect{0,0,1,1}, min, max, val, step);
                // Add onChange callback to commit immediately
                sb->onChange = [this, key, e](float newVal) {
                    if (!world || e >= world->entity_count) return;
                    if (key == "pos_x") {
                        if (world->has_position3d[e]) world->position3d_pool[e].x = newVal;
                        else world->position_pool[e].x = newVal;
                    } else if (key == "pos_y") {
                        if (world->has_position3d[e]) world->position3d_pool[e].y = newVal;
                        else world->position_pool[e].y = newVal;
                    } else if (key == "pos_z") {
                        if (world->has_position3d[e]) world->position3d_pool[e].z = newVal;
                    } else if (key == "rect_w") world->rectangle_shape_pool[e].w = newVal;
                    else if (key == "rect_h") world->rectangle_shape_pool[e].h = newVal;
                    else if (key == "depth3d") world->depth3d_pool[e].depth = newVal;
                    else if (key == "rotation") world->rotation_pool[e].degrees = newVal;
                    else if (key == "scale_x") world->scale_pool[e].x = newVal;
                    else if (key == "scale_y") world->scale_pool[e].y = newVal;
                    // Mesh3DRef's own dimensions -- deliberately separate from
                    // rect_w/rect_h/depth3d above, which drive the entity's
                    // outline. Without these three cases the Mesh W/H/D spin
                    // boxes below render but silently do nothing on edit.
                    else if (key == "mesh_w") world->mesh3d_pool[e].meshW = newVal;
                    else if (key == "mesh_h") world->mesh3d_pool[e].meshH = newVal;
                    else if (key == "mesh_d") world->mesh3d_pool[e].meshD = newVal;
                    else if (key == "gridmap3d_cw") world->gridmap3d_pool[e].cellWidth = newVal;
                    else if (key == "gridmap3d_ch") world->gridmap3d_pool[e].cellHeight = newVal;
                    else if (key == "gridmap3d_cd") world->gridmap3d_pool[e].cellDepth = newVal;
                    
                    // Update lastSyncedValue in the fields array since 'f' is moved into it
                    for (auto& field : this->fields) {
                        if (field.key == key) {
                            field.lastSyncedValue = newVal;
                            break;
                        }
                    }
                };
                f.widgets.push_back(std::move(sb));
                fields.push_back(std::move(f));
            };
            
            auto addLineEdit = [&](const std::string& lbl, const std::string& key, const std::string& val) {
                Field f; f.label = lbl; f.key = key; f.lastSyncedText = val;
                auto le = std::make_unique<Gui::LineEdit>(renderer, textEngine, font, SDL_FRect{0,0,1,1}, "Type here...");
                for (char c : val) le->appendText(std::string(1, c));
                f.widgets.push_back(std::move(le));
                fields.push_back(std::move(f));
            };
            
            auto addCheckBox = [&](const std::string& label, const std::string& key, bool val) {
                Field f; f.label = label; f.key = key; f.lastSyncedValue = val ? 1.0f : 0.0f;
                auto cb = std::make_unique<Gui::CheckBox>(renderer, textEngine, font, SDL_FRect{0,0,1,1}, val);
                // NOTE: Gui::CheckBox has no onChange callback. Changes are committed
                // through commitAllFields() (triggered by Enter/Tab or explicit calls),
                // which reads getValue() and writes to the world based on f.key.
                f.widgets.push_back(std::move(cb));
                fields.push_back(std::move(f));
            };
            
            auto addTexturePath = [&](const std::string& label, const std::string& key, const std::string& val) {
                Field f; f.label = label; f.key = key; f.lastSyncedText = val;
                auto le = std::make_unique<Gui::LineEdit>(renderer, textEngine, font, SDL_FRect{0,0,1,1}, "texture path...");
                for (char c : val) le->appendText(std::string(1, c));
                auto btn = std::make_unique<Gui::Button>(renderer, font, "...", SDL_FPoint{0,0}, 30, 26);
                btn->onClicked = [this, lePtr = le.get()]() {
                    fileExplorer.setFilter("*.svg;*.png;*.jpg;*.jpeg");
                    fileExplorer.setSaveMode(false, "");
                    fileExplorer.setCallback([this, lePtr](const std::string& path) {
                        lePtr->clear();
                        for (char c : path) lePtr->appendText(std::string(1, c));
                        lePtr->deactivate(nullptr);
                        this->commitAllFields();
                    });
                    fileExplorer.open();
                };
                f.widgets.push_back(std::move(le));
                f.widgets.push_back(std::move(btn));
                fields.push_back(std::move(f));
            };
            
            auto addAnimationState = [&]() {
                auto& anim = world->animation_state_pool[e];
                if (anim.clips.empty()) {
                    anim.clips.push_back(Components::AnimationClip{});
                }
                std::vector<std::string> clipNames;
                for (const auto& clip : anim.clips) clipNames.push_back(clip.name);
                auto clipSelector = std::make_unique<Gui::OptionSpinBox>(
                    renderer, textEngine, font, SDL_FRect{0,0,1,1}, clipNames, false);
                clipSelector->setCurrentIndex(anim.activeClipIndex);
                clipSelector->onChange = [this, e](int idx) {
                    if (!world || e >= world->entity_count) return;
                    auto& anim = world->animation_state_pool[e];
                    if (idx >= 0 && idx < (int)anim.clips.size()) {
                        anim.activeClipIndex = idx;
                        this->pendingRebuild = true;
                    }
                };
                auto playBtn = std::make_unique<Gui::Button>(renderer, font, "Play", SDL_FPoint{0,0}, 50, 26);
                playBtn->onClicked = [this, e]() {
                    if (!world || e >= world->entity_count) return;
                    world->animation_state_pool[e].active().isPlaying = true;
                };
                auto stopBtn = std::make_unique<Gui::Button>(renderer, font, "Stop", SDL_FPoint{0,0}, 50, 26);
                stopBtn->onClicked = [this, e]() {
                    if (!world || e >= world->entity_count) return;
                    world->animation_state_pool[e].active().isPlaying = false;
                };
                auto widthSpin = std::make_unique<Gui::SpinBox>(renderer, textEngine, font,
                    SDL_FRect{0,0,1,1}, 0.0f, 9999.0f, anim.active().frameWidth, 1.0f);
                widthSpin->onChange = [this, e](float val) {
                    if (!world || e >= world->entity_count) return;
                    world->animation_state_pool[e].active().frameWidth = val;
                };
                auto heightSpin = std::make_unique<Gui::SpinBox>(renderer, textEngine, font,
                    SDL_FRect{0,0,1,1}, 0.0f, 9999.0f, anim.active().frameHeight, 1.0f);
                heightSpin->onChange = [this, e](float val) {
                    if (!world || e >= world->entity_count) return;
                    world->animation_state_pool[e].active().frameHeight = val;
                };
                auto editBtn = std::make_unique<Gui::Button>(renderer, font,
                    "Edit Clips...", SDL_FPoint{0,0}, 100, 26);
                editBtn->onClicked = [this, e]() {
                    if (!world || e >= world->entity_count) return;
                    auto& anim = world->animation_state_pool[e];
                    animFrameEditor->setTarget(&anim);
                    animFrameEditor->onResourceLoaded = [this, e](const std::string& path) {
                        if (world && e < world->entity_count) {
                            std::string relPath = std::filesystem::relative(path, projectRoot).string();
                            if (relPath.empty()) relPath = path;
                            world->animation_resource_path[e] = relPath;
                        }
                        this->commitAllFields();
                        this->rebuildFields();
                    };
                    modeBeforeDialog = currentEditMode;
                    currentEditMode = EditMode::Dialog;
                    animFrameEditor->open();
                };
                Field f;
                f.label = "Animation";
                f.key = "anim_panel";
                f.widgets.push_back(std::move(clipSelector));
                auto hbox = std::make_unique<Gui::HBoxContainer>();
                hbox->setRect(SDL_FRect{0,0,120,30});
                hbox->addChild(std::move(playBtn));
                hbox->addChild(std::move(stopBtn));
                f.widgets.push_back(std::move(hbox));
                f.widgets.push_back(std::move(widthSpin));
                f.widgets.push_back(std::move(heightSpin));
                f.widgets.push_back(std::move(editBtn));
                fields.push_back(std::move(f));
            };
            
            if (world->has_metadata[e]) addLineEdit("Name", "metadata_name", world->metadata_pool[e].name);
            
            if (world->has_position[e] || world->has_position3d[e]) {
                if (is3D) {
                    if (!world->has_position3d[e]) {
                        world->add_position3d(e);
                        world->position3d_pool[e].x = world->position_pool[e].x;
                        world->position3d_pool[e].y = 0.0f;
                        world->position3d_pool[e].z = world->has_depth3d[e] ? world->depth3d_pool[e].depth : 0.0f;
                    }
                    addSpinBox("X", "pos_x", world->position3d_pool[e].x, -25000.0f, 25000.0f);
                    addSpinBox("Y", "pos_y", world->position3d_pool[e].y, -25000.0f, 25000.0f);
                    addSpinBox("Z", "pos_z", world->position3d_pool[e].z, -25000.0f, 25000.0f);
                    addSpinBox("Width",  "rect_w", world->rectangle_shape_pool[e].w, 1.0f, 9999.0f);
                    addSpinBox("Height", "rect_h", world->rectangle_shape_pool[e].h, 1.0f, 9999.0f);
                    if (world->has_depth3d[e]) {
                        addSpinBox("Depth",  "depth3d", world->depth3d_pool[e].depth, 0.1f, 9999.0f, 0.1f);
                    }
                } else {
                    addSpinBox("X", "pos_x", world->position_pool[e].x, -25000.0f, 25000.0f);
                    addSpinBox("Y", "pos_y", world->position_pool[e].y, -25000.0f, 25000.0f);
                    addSpinBox("Width",  "rect_w", world->rectangle_shape_pool[e].w, 1.0f, 9999.0f);
                    addSpinBox("Height", "rect_h", world->rectangle_shape_pool[e].h, 1.0f, 9999.0f);
                }
            }
            
            // Hide Z-Index in 3D mode (only for 2D)
            if (!is3D && world->has_z_index[e]) {
                addSpinBox("Z-Index", "z_index", (float)world->z_index_pool[e].z, 0, 1000, 1.0f);
            }
            
            if (world->has_rotation[e]) addSpinBox("Rotation (deg)", "rotation", world->rotation_pool[e].degrees, -360.0f, 360.0f, 1.0f);
            if (world->has_scale[e]) {
                addSpinBox("Scale X", "scale_x", world->scale_pool[e].x, 0.01f, 10.0f, 0.1f);
                addSpinBox("Scale Y", "scale_y", world->scale_pool[e].y, 0.01f, 10.0f, 0.1f);
            }
            
            if (world->has_physics_body[e]) {
                auto& phys = world->physics_body_pool[e];
                addSpinBox("Shape (0=Rct,1=Crc,2=Poly)", "phys_shape",
                        (float)ShapeTypeToSpinboxIndex(phys.shapeType), 0, 2, 1);
                addSpinBox("Width", "phys_w", phys.width, 1, 9999, 1);
                addSpinBox("Height", "phys_h", phys.height, 1, 9999, 1);
                addSpinBox("Radius", "phys_r", phys.radius, 1, 9999, 1);
                Field btnField;
                btnField.label = "Physics Resource";
                btnField.key = "phys_res_buttons";
                auto loadBtn = std::make_unique<Gui::Button>(renderer, font, "Load .physicsres",
                                                            SDL_FPoint{0,0}, 120, 26);
                loadBtn->onClicked = [this, e]() {
                    if (!world || e >= world->entity_count) return;
                    this->commitAllFields();
                    std::string resFolder = (std::filesystem::path(projectRoot) / "resources").string();
                    fileExplorer.setCurrentPath(resFolder);
                    fileExplorer.setFilter("*.physicsres");
                    fileExplorer.setSaveMode(false, "");
                    fileExplorer.setCallback([this, e](const std::string& path) {
                        if (!world || e >= world->entity_count) return;
                        try {
                            std::ifstream f(path);
                            if (!f.is_open()) {
                                SDL_Log("Failed to open physics resource: %s", path.c_str());
                                fileExplorer.reset();
                                return;
                            }
                            nlohmann::json j;
                            f >> j;
                            auto& phys = world->physics_body_pool[e];
                            phys = ComponentResourceManager::parsePhysicsJson(j);
                            phys.bodyId = b2_nullBodyId;
                            std::string relPath = std::filesystem::relative(path, projectRoot).string();
                            if (relPath.empty()) relPath = path;
                            world->physics_resource_path[e] = relPath;
                            world->has_physics_body[e] = 1;
                            this->rebuildFields();
                        } catch (const std::exception& ex) {
                            SDL_Log("Error loading physics resource: %s", ex.what());
                        }
                        fileExplorer.reset();
                    });
                    fileExplorer.open();
                };
                btnField.widgets.push_back(std::move(loadBtn));
                auto saveBtn = std::make_unique<Gui::Button>(renderer, font, "Save .physicsres",
                                                            SDL_FPoint{0,0}, 120, 26);
                saveBtn->onClicked = [this, e]() {
                    if (!world || e >= world->entity_count) return;
                    this->commitAllFields();
                    std::string resFolder = (std::filesystem::path(projectRoot) / "resources").string();
                    fileExplorer.setCurrentPath(resFolder);
                    fileExplorer.setSaveMode(true, ".physicsres");
                    fileExplorer.setCallback([this, e](const std::string& path) {
                        if (!world || e >= world->entity_count) return;
                        try {
                            nlohmann::json j = ComponentResourceManager::serializePhysicsJson(world->physics_body_pool[e]);
                            std::ofstream out(path);
                            if (out.is_open()) {
                                out << j.dump(4);
                                std::string relPath = std::filesystem::relative(path, projectRoot).string();
                                world->physics_resource_path[e] = relPath;
                                SDL_Log("Saved physics resource to %s", path.c_str());
                            } else {
                                SDL_Log("Failed to save physics resource to %s", path.c_str());
                            }
                        } catch (const std::exception& ex) {
                            SDL_Log("Error saving physics resource: %s", ex.what());
                        }
                        fileExplorer.reset();
                    });
                    fileExplorer.open();
                };
                btnField.widgets.push_back(std::move(saveBtn));
                fields.push_back(std::move(btnField));
            }
            
            if (world->has_texture_ref[e]) {
                auto& t = world->texture_ref_pool[e];
                addTexturePath("Tex Resource", "tex_res", t.resourceName);
            }
            
            if (world->has_animation_state[e]) {
                addAnimationState();
            }
            
            if (world->has_sfx_emitter[e]) {
                auto& s = world->sfx_emitter_pool[e];
                addLineEdit("SFX Name", "sfx_name", s.sfxName);
                addSpinBox("Volume", "sfx_vol", s.volume, 0.0f, 1.0f, 0.1f);
                addSpinBox("Pitch", "sfx_pitch", s.pitch, 0.1f, 3.0f, 0.1f);
                addSpinBox("Speed", "sfx_speed", s.speed, 0.1f, 3.0f, 0.1f);
                addCheckBox("Play On Col", "sfx_col", s.playOnCollision);
            }
            
            if (world->has_tilemap[e]) {
                auto& tm = world->tilemap_pool[e];
                Field texField;
                texField.label = "Tile Texture";
                texField.key = "tile_tex";
                texField.lastSyncedText = tm.textureName;
                auto le = std::make_unique<Gui::LineEdit>(renderer, textEngine, font,
                    SDL_FRect{0,0,1,1}, "texture path...");
                for (char c : tm.textureName) le->appendText(std::string(1, c));
                auto btn = std::make_unique<Gui::Button>(renderer, font, "...", SDL_FPoint{0,0}, 30, 26);
                btn->onClicked = [this, lePtr = le.get()]() {
                    fileExplorer.setFilter("*.svg;*.png;*.jpg;*.jpeg");
                    fileExplorer.setSaveMode(false, "");
                    fileExplorer.setCallback([this, lePtr](const std::string& path) {
                        lePtr->clear();
                        for (char c : path) lePtr->appendText(std::string(1, c));
                        lePtr->deactivate(nullptr);
                        this->commitAllFields();
                    });
                    fileExplorer.open();
                };
                texField.widgets.push_back(std::move(le));
                texField.widgets.push_back(std::move(btn));
                fields.push_back(std::move(texField));
                addCheckBox("Snap To Grid", "tile_snap", tm.snapEnabled);
                Field clearField;
                clearField.label = "Clear Tiles";
                clearField.key = "tile_clear";
                auto clearBtn = std::make_unique<Gui::Button>(renderer, font, "Clear All", SDL_FPoint{0,0}, 120, 26);
                clearBtn->onClicked = [this, e]() {
                    if (!world || e >= world->entity_count) return;
                    world->tilemap_pool[e].cells.clear();
                    world->tilemap_pool[e].cellOffsets.clear();
                    this->rebuildFields();
                };
                clearField.widgets.push_back(std::move(clearBtn));
                fields.push_back(std::move(clearField));
            }
            
            // --- 3D-specific components ---
            if (world->has_mesh3d[e]) {
                auto& mesh = world->mesh3d_pool[e];
                Field meshField;
                meshField.label = "Mesh Path";
                meshField.key = "mesh3d_path";
                meshField.lastSyncedText = mesh.modelPath;
                auto le = std::make_unique<Gui::LineEdit>(renderer, textEngine, font, SDL_FRect{0,0,1,1}, "model path...");
                for (char c : mesh.modelPath) le->appendText(std::string(1, c));
                auto btn = std::make_unique<Gui::Button>(renderer, font, "...", SDL_FPoint{0,0}, 30, 26);
                btn->onClicked = [this, lePtr = le.get()]() {
                    fileExplorer.setFilter("*.obj;*.fbx;*.gltf;*.glb");
                    fileExplorer.setSaveMode(false, "");
                    fileExplorer.setCallback([this, lePtr](const std::string& path) {
                        lePtr->clear();
                        for (char c : path) lePtr->appendText(std::string(1, c));
                        lePtr->deactivate(nullptr);
                        this->commitAllFields();
                    });
                    fileExplorer.open();
                };
                meshField.widgets.push_back(std::move(le));
                meshField.widgets.push_back(std::move(btn));
                fields.push_back(std::move(meshField));
                // Add mesh dimension spinboxes
                addSpinBox("Mesh W", "mesh_w", mesh.meshW, 0.0f, 9999.0f, 0.1f);
                addSpinBox("Mesh H", "mesh_h", mesh.meshH, 0.0f, 9999.0f, 0.1f);
                addSpinBox("Mesh D", "mesh_d", mesh.meshD, 0.0f, 9999.0f, 0.1f);
            }
            
            if (world->has_material3d[e]) {
                auto& mat = world->material3d_pool[e];
                addCheckBox("Override Color", "mat3d_override", mat.overrideColor);
                if (mat.overrideColor) {
                    addSpinBox("R", "mat3d_r", mat.r * 255.0f, 0, 255, 1);
                    addSpinBox("G", "mat3d_g", mat.g * 255.0f, 0, 255, 1);
                    addSpinBox("B", "mat3d_b", mat.b * 255.0f, 0, 255, 1);
                }
            }
            
            if (world->has_gridmap3d[e]) {
                auto& gm = world->gridmap3d_pool[e];
                Field texField;
                texField.label = "GridMap Material";
                texField.key = "gridmap3d_mat";
                texField.lastSyncedText = gm.materialName;
                auto le = std::make_unique<Gui::LineEdit>(renderer, textEngine, font, SDL_FRect{0,0,1,1}, "material path...");
                for (char c : gm.materialName) le->appendText(std::string(1, c));
                auto btn = std::make_unique<Gui::Button>(renderer, font, "...", SDL_FPoint{0,0}, 30, 26);
                btn->onClicked = [this, lePtr = le.get()]() {
                    fileExplorer.setFilter("*.png;*.jpg;*.svg;*.obj");
                    fileExplorer.setSaveMode(false, "");
                    fileExplorer.setCallback([this, lePtr](const std::string& path) {
                        lePtr->clear();
                        for (char c : path) lePtr->appendText(std::string(1, c));
                        lePtr->deactivate(nullptr);
                        this->commitAllFields();
                    });
                    fileExplorer.open();
                };
                texField.widgets.push_back(std::move(le));
                texField.widgets.push_back(std::move(btn));
                fields.push_back(std::move(texField));
                
                addCheckBox("Snap To Grid", "gridmap3d_snap", gm.snapEnabled);
                addSpinBox("Cell W", "gridmap3d_cw", gm.cellWidth, 0.1f, 100.0f, 0.1f);
                addSpinBox("Cell H", "gridmap3d_ch", gm.cellHeight, 0.1f, 100.0f, 0.1f);
                addSpinBox("Cell D", "gridmap3d_cd", gm.cellDepth, 0.1f, 100.0f, 0.1f);
                
                Field clearField;
                clearField.label = "Clear Cells";
                clearField.key = "gridmap3d_clear";
                auto clearBtn = std::make_unique<Gui::Button>(renderer, font, "Clear All", SDL_FPoint{0,0}, 120, 26);
                clearBtn->onClicked = [this, e]() {
                    if (!world || e >= world->entity_count) return;
                    world->gridmap3d_pool[e].cells.clear();
                    this->rebuildFields();
                };
                clearField.widgets.push_back(std::move(clearBtn));
                fields.push_back(std::move(clearField));
            }
            
            addComponentBtn.onClicked = [this, is3D]() {
                if (!world || targetEntity == (Entity)-1) return;
                Entity e = targetEntity;
                std::string comp = componentSelector.getCurrentOption();
                if (comp == "PhysicsBody") world->add_physics_body(e);
                else if (comp == "TextureRef") world->add_texture_ref(e);
                else if (comp == "AnimationState") world->add_animation_state(e);
                else if (comp == "SfxEmitter") world->add_sfx_emitter(e);
                else if (comp == "Rotation") world->add_rotation(e);
                else if (comp == "Scale") world->add_scale(e);
                else if (comp == "TileMap") world->add_tilemap(e);
                else if (comp == "Mesh3D") { world->add_mesh3d(e); world->mesh3d_pool[e].modelPath = ""; }
                else if (comp == "Material3D") world->add_material3d(e);
                else if (comp == "Collider3D") world->add_collider3d(e);
                else if (comp == "Depth3D") world->add_depth3d(e);
                else if (comp == "GridMap3D") world->add_gridmap3d(e);
                rebuildFields();
            };
        }

        void drawEmptyPanel(float windowHeight) {
            SDL_FRect bg = { panelX(), 0, PANEL_W, windowHeight };
            SDL_SetRenderDrawColor(renderer, 28, 28, 35, 245); SDL_RenderFillRect(renderer, &bg);
            SDL_SetRenderDrawColor(renderer, 60, 60, 75, 255); SDL_RenderRect(renderer, &bg);
            drawLabel("Entity Inspector", panelX() + 10, 10, {180,180,200,255});
            drawLabel("Click entities.", panelX() + 10, 40, {100,100,120,255});
        }

        void drawLabel(const char* s, float x, float y, SDL_Color c) {
            TTF_Text* t = TTF_CreateText(textEngine, font, s, 0);
            if (!t) return; TTF_SetTextColor(t, c.r, c.g, c.b, c.a); TTF_DrawRendererText(t, x, y); TTF_DestroyText(t);
        }
    };

    class TextEditor : public IGuiElement {
    public:
        TextEditor(SDL_Renderer* r, TTF_TextEngine* te, TTF_Font* f, SDL_FRect rct)
            : renderer(r), textEngine(te), font(f), rect(rct) {
            lines.push_back("");
            verticalScrollbar.setOrientation(Gui::ScrollOrientation::Vertical);
            horizontalScrollbar.setOrientation(Gui::ScrollOrientation::Horizontal);
            // Set callbacks to update editor scroll when user drags thumb
            verticalScrollbar.onChange = [this](float v){ scrollY = v; };
            horizontalScrollbar.onChange = [this](float v){ scrollX = v; };
            refreshScrollbarGeometry();
        }

        ~TextEditor() = default;

        // IGuiElement overrides
        std::string getType() const override { return "TextEditor"; }
        float getX() const override { return rect.x; }
        float getY() const override { return rect.y; }
        float getWidth() const override { return rect.w; }
        float getHeight() const override { return rect.h; }
        void setRect(SDL_FRect r) override { rect = r; refreshScrollbarGeometry(); }
        void setPos(SDL_Point p) override { rect.x = (float)p.x; rect.y = (float)p.y; refreshScrollbarGeometry(); }

    private:
        int tabWidth = 4;

        struct UndoState {
            std::vector<std::string> lines;
            int cursorRow;
            int cursorCol;
        };
        std::vector<UndoState> undoStack;
        std::vector<UndoState> redoStack;
        static constexpr size_t MAX_UNDO_STATES = 100;

        // Add these methods to TextEditor class:
        void pushUndoState() {
            undoStack.push_back({lines, cursorRow, cursorCol});
            if (undoStack.size() > MAX_UNDO_STATES) {
                undoStack.erase(undoStack.begin());
            }
            redoStack.clear();
        }

        void undo() {
            if (undoStack.empty()) return;
            redoStack.push_back({lines, cursorRow, cursorCol});
            auto state = undoStack.back();
            undoStack.pop_back();
            lines = state.lines;
            cursorRow = state.cursorRow;
            cursorCol = state.cursorCol;
            hasSelection = false;
            updateScrollbars();
        }

        void redo() {
            if (redoStack.empty()) return;
            undoStack.push_back({lines, cursorRow, cursorCol});
            auto state = redoStack.back();
            redoStack.pop_back();
            lines = state.lines;
            cursorRow = state.cursorRow;
            cursorCol = state.cursorCol;
            hasSelection = false;
            updateScrollbars();
        }

    public:
        void setTabWidth(int w) { tabWidth = std::max(1, w); }
        int getTabWidth() const { return tabWidth; }

        bool handleEvent(const SDL_Event& e, SDL_Window* window, float offsetX, float offsetY) override {
            if (!visible) return false;
            // Handle scrollbars first
            float origVX = verticalScrollbar.trackX, origVY = verticalScrollbar.trackY;
            float origHX = horizontalScrollbar.trackX, origHY = horizontalScrollbar.trackY;
            verticalScrollbar.trackX -= offsetX; verticalScrollbar.trackY -= offsetY;
            horizontalScrollbar.trackX -= offsetX; horizontalScrollbar.trackY -= offsetY;
            bool sbConsumed = false;
            if (verticalScrollbar.handleEvent(e)) sbConsumed = true;
            if (horizontalScrollbar.handleEvent(e)) sbConsumed = true;
            verticalScrollbar.trackX = origVX; verticalScrollbar.trackY = origVY;
            horizontalScrollbar.trackX = origHX; horizontalScrollbar.trackY = origHY;
            if (sbConsumed) {
                updateScrollbars();
                return true;
            }            
            // Keyboard input
            if (e.type == SDL_EVENT_KEY_DOWN) {
                bool shift = (e.key.mod & SDL_KMOD_SHIFT) != 0;
                bool ctrl  = (e.key.mod & SDL_KMOD_CTRL) != 0;
                if (ctrl && e.key.key == SDLK_Z) {
                    undo();
                    return true;
                }
                // Ctrl+Y or Ctrl+Shift+Z: Redo
                if (ctrl && (e.key.key == SDLK_Y || (shift && e.key.key == SDLK_Z))) {
                    redo();
                    return true;
                }
                if (ctrl && e.key.key == SDLK_A) {
                    selAnchorRow = 0; selAnchorCol = 0;
                    cursorRow = (int)lines.size() - 1;
                    cursorCol = getLineLength(cursorRow);
                    hasSelection = true;
                    return true;
                }
                if (ctrl && e.key.key == SDLK_C) {
                    if (hasSelection) {
                        auto [sRow, sCol, eRow, eCol] = normalizedSelection();
                        std::string selectedText = "";
                        if (sRow == eRow) {
                            selectedText = lines[sRow].substr(sCol, eCol - sCol);
                        } else {
                            selectedText = lines[sRow].substr(sCol) + "\n";
                            for (int r = sRow + 1; r < eRow; ++r) {
                                selectedText += lines[r] + "\n";
                            }
                            selectedText += lines[eRow].substr(0, eCol);
                        }
                        SDL_SetClipboardText(selectedText.c_str());
                    }
                    return true;
                }

                // ── Clipboard: Cut (Ctrl+X) ───────────────────────────────
                if (ctrl && e.key.key == SDLK_X) {
                    if (hasSelection) {
                        auto [sRow, sCol, eRow, eCol] = normalizedSelection();
                        std::string selectedText = "";
                        if (sRow == eRow) {
                            selectedText = lines[sRow].substr(sCol, eCol - sCol);
                        } else {
                            selectedText = lines[sRow].substr(sCol) + "\n";
                            for (int r = sRow + 1; r < eRow; ++r) {
                                selectedText += lines[r] + "\n";
                            }
                            selectedText += lines[eRow].substr(0, eCol);
                        }
                        SDL_SetClipboardText(selectedText.c_str());
                        deleteSelection();
                        updateScrollbars();
                    }
                    return true;
                }

                // ── Clipboard: Paste (Ctrl+V) ─────────────────────────────
                if (ctrl && e.key.key == SDLK_V) {
                    char* clipText = SDL_GetClipboardText();
                    if (clipText) {
                        std::string text(clipText);
                        SDL_free(clipText); // SDL3 requires manual freeing of clipboard strings
                        
                        if (hasSelection) deleteSelection();
                        
                        for (char c : text) {
                            if (c == '\n') {
                                newline();
                            } else if (c == '\r') {
                                continue; // Skip Windows carriage returns
                            } else if (c == '\t') {
                                insertChar('\t');
                            } else if (c >= 32 && c < 127) {
                                insertChar(c);
                            }
                        }
                        updateScrollbars();
                    }
                    return true;
                }
                switch (e.key.key) {
                    case SDLK_LEFT:  moveCursor(cursorRow, cursorCol - 1, shift); updateScrollbars(); return true;
                    case SDLK_RIGHT: moveCursor(cursorRow, cursorCol + 1, shift); updateScrollbars(); return true;
                    case SDLK_UP:    moveCursor(cursorRow - 1, cursorCol, shift); updateScrollbars(); return true;
                    case SDLK_DOWN:  moveCursor(cursorRow + 1, cursorCol, shift); updateScrollbars(); return true;
                    case SDLK_HOME:  moveCursor(cursorRow, 0, shift); updateScrollbars(); return true;
                    case SDLK_END:   moveCursor(cursorRow, getLineLength(cursorRow), shift); updateScrollbars(); return true;
                    case SDLK_PAGEUP: {
                        int rows = (int)(rect.h / LINE_HEIGHT) - 1;
                        moveCursor(cursorRow - rows, cursorCol, shift);
                        updateScrollbars();
                        return true;
                    }
                    case SDLK_PAGEDOWN: {
                        int rows = (int)(rect.h / LINE_HEIGHT) - 1;
                        moveCursor(cursorRow + rows, cursorCol, shift);
                        updateScrollbars();
                        return true;
                    }
                    case SDLK_BACKSPACE: backspace(); updateScrollbars(); return true;
                    case SDLK_DELETE:    deleteChar(); updateScrollbars(); return true;
                    case SDLK_RETURN:    newline(); updateScrollbars(); return true;
                    case SDLK_TAB:       insertChar('\t'); updateScrollbars(); return true;
                    case SDLK_ESCAPE:
                        // Close via escape key instead of button
                        if (onClose) onClose();
                        return true;
                    default:
                        break;
                }
            }
            // Text input (characters)
            if (e.type == SDL_EVENT_TEXT_INPUT) {
                const char* text = e.text.text;
                for (int i = 0; text[i]; ++i) {
                    if (text[i] >= 32 && text[i] < 127) { // printable ASCII
                        insertChar(text[i]);
                    }
                }
                updateScrollbars();
                return true;
            }
            // Mouse wheel inside the editor area
            if (e.type == SDL_EVENT_MOUSE_WHEEL) {
                float mx, my;
                SDL_GetMouseState(&mx, &my);
                SDL_FRect shiftedRect = { rect.x - offsetX, rect.y - offsetY, rect.w, rect.h };
                if (mx >= shiftedRect.x && mx <= shiftedRect.x + shiftedRect.w &&
                    my >= shiftedRect.y && my <= shiftedRect.y + shiftedRect.h) {
                    float delta = (e.wheel.y > 0) ? -LINE_HEIGHT * 3 : LINE_HEIGHT * 3;
                    scrollY = std::clamp(scrollY + delta, 0.0f, verticalScrollbar.maxOffset());
                    if (e.wheel.x != 0) {
                        scrollX = std::clamp(scrollX + e.wheel.x * CHAR_WIDTH * 3, 0.0f, horizontalScrollbar.maxOffset());
                    }
                    updateScrollbars();
                    return true;
                }
            }
            if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT) {
                float mx = e.button.x, my = e.button.y;
                SDL_FRect shiftedRect = { rect.x - offsetX, rect.y - offsetY, rect.w - 12, rect.h - 12 };
                if (mx >= shiftedRect.x && mx <= shiftedRect.x + shiftedRect.w &&
                    my >= shiftedRect.y && my <= shiftedRect.y + shiftedRect.h) {
                    // Start text input so SDL generates SDL_EVENT_TEXT_INPUT events
                    SDL_StartTextInput(window);
                    int row, col;
                    hitTestRowCol(mx, my, shiftedRect, row, col);
                    bool shift = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
                    moveCursor(row, col, shift);
                    mouseSelecting = true;
                    updateScrollbars();
                    return true;
                }
            }
            if (e.type == SDL_EVENT_MOUSE_MOTION && mouseSelecting) {
                float mx = e.motion.x, my = e.motion.y;
                SDL_FRect shiftedRect = { rect.x - offsetX, rect.y - offsetY, rect.w - 12, rect.h - 12 };
                int row, col;
                hitTestRowCol(mx, my, shiftedRect, row, col);
                moveCursor(row, col, true);
                updateScrollbars();
                return true;
            }
            if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_LEFT) {
                mouseSelecting = false;
            }
            return false;
        }

        // Maps a screen point to a (row, col), using real font metrics
        // (xToCol) rather than a fixed per-character width.
        void hitTestRowCol(float mx, float my, SDL_FRect shiftedRect, int& row, int& col) const {
            float localX = mx - shiftedRect.x + scrollX - PADDING_X;
            float localY = my - shiftedRect.y + scrollY - PADDING_Y;
            row = (int)(localY / LINE_HEIGHT);
            if (row < 0) row = 0;
            if (row >= (int)lines.size()) row = (int)lines.size() - 1;
            col = xToCol(lines[row], localX);
        }

        void handleGamepad(float cursorX, float cursorY, float offsetX, float offsetY,
                               SDL_Window* window, bool confirmDown, bool confirmDownLastFrame) override {
            if (!visible) return;
            // We don't use cursor position; we rely on keyboard mapping.
            // However, if the user has a gamepad, they can use the stick to move the cursor.
            // This is a simplification – we'll just pass through to keyboard events.
            // For full integration, we'd need to map gamepad buttons to actions.
            // For now, we only support keyboard.
            (void)cursorX; (void)cursorY; (void)offsetX; (void)offsetY; (void)window;
            (void)confirmDown; (void)confirmDownLastFrame;
        }

        
        void render(float offsetX, float offsetY) override {
            if (!visible) return;

            // Background
            SDL_SetRenderDrawColor(renderer, 32, 32, 42, 255);
            SDL_FRect bgRect = { rect.x - offsetX, rect.y - offsetY, rect.w, rect.h };
            SDL_RenderFillRect(renderer, &bgRect);
            SDL_SetRenderDrawColor(renderer, 70, 70, 90, 255);
            SDL_RenderRect(renderer, &bgRect);

            // Clip
            SDL_Rect clip = { (int)bgRect.x, (int)bgRect.y, (int)bgRect.w, (int)bgRect.h };
            SDL_SetRenderClipRect(renderer, &clip);

            // Draw text and cursor
            renderText(offsetX, offsetY);
            renderCursor(offsetX, offsetY);

            // Stop clipping
            SDL_SetRenderClipRect(renderer, nullptr);

            // Draw scrollbars ONCE, passing the offsets
            verticalScrollbar.render(renderer, offsetX, offsetY);
            horizontalScrollbar.render(renderer, offsetX, offsetY);
        }

        bool isLogicScript = false;

        static bool hasExtension(const std::string& path, const std::string& ext) {
            if (path.size() < ext.size()) return false;
            return path.compare(path.size() - ext.size(), ext.size(), ext) == 0;
        }

        // File operations
        void loadFile(const std::string& path) {
            std::ifstream file(path);
            if (!file.is_open()) {
                SDL_Log("TextEditor: failed to open %s", path.c_str());
                return;
            }
            // LogicScript covers both regular source (.ls -> .cpp) and
            // header source (.lh -> .h); same syntax/highlighting either
            // way, see transpile.h.
            isLogicScript = hasExtension(path, ".ls") || hasExtension(path, ".lh");
            lines.clear();
            std::string line;
            while (std::getline(file, line)) {
                std::string converted;
                converted.reserve(line.size() * tabWidth); // rough reserve
                for (char c : line) {
                    if (c == '\t') {
                        converted.append(tabWidth, ' ');
                    } else {
                        converted += c;
                    }
                }
                lines.push_back(converted);
            }
            filePath = path;
            cursorRow = 0;
            cursorCol = 0;
            scrollX = 0.0f;
            scrollY = 0.0f;
            hasSelection = false;
            mouseSelecting = false;
            refreshScrollbarGeometry();
            updateScrollbars();
        }



        void saveFile() {
            if (filePath.empty()) return;
            std::ofstream file(filePath);
            if (!file.is_open()) {
                SDL_Log("TextEditor: failed to save %s", filePath.c_str());
                return;
            }
            for (size_t i = 0; i < lines.size(); ++i) {
                file << lines[i];
                if (i + 1 < lines.size()) file << '\n';
            }
            SDL_Log("TextEditor: saved to %s", filePath.c_str());
        }

        bool transpileLogicScript(std::string& errOut) {
            if (!isLogicScript || filePath.empty()) {
                errOut = "Not a .ls or .lh file.";
                return false;
            }
            // .ls -> .cpp, .lh -> .h — LSTranspile::siblingCppPath() picks
            // the right target extension so a header never overwrites (or
            // gets overwritten by) a same-stem .cpp/.ls.
            std::string outPath = LSTranspile::siblingCppPath(filePath);
            bool ok = LSTranspile::transpileFile(filePath, outPath, errOut);
            if (ok) SDL_Log("LogicScript: transpiled %s -> %s", filePath.c_str(), outPath.c_str());
            else    SDL_Log("LogicScript: transpile failed:\n%s", errOut.c_str());
            return ok;
        }

        bool transpileCPlusPlusScript(std::string& errOut) {
            if (isLogicScript || filePath.empty()) {
                errOut = "Not a .cpp or .h file.";
                return false;
            }
            // .cpp -> .ls, .h -> .lh — mirror of siblingCppPath() above.
            std::string outPath = LSTranspile::siblingLsPath(filePath);
            bool ok = LSTranspile::transpileCppFileToLs(filePath, outPath, errOut);
            if (ok) SDL_Log("LogicScript: transpiled %s -> %s", filePath.c_str(), outPath.c_str());
            else    SDL_Log("LogicScript: transpile failed:\n%s", errOut.c_str());
            return ok;
        }
                        

        bool isFileLoaded() const { return !filePath.empty(); }
        const std::string& getFilePath() const { return filePath; }

        // Visibility
        void setVisible(bool v) { visible = v; }
        bool isVisible() const { return visible; }

        // Close callback (called when user clicks close button)
        std::function<void()> onClose;
        

    private:
        SDL_Renderer* renderer;
        TTF_TextEngine* textEngine;
        TTF_Font* font;
        SDL_FRect rect;
        bool visible = true;

        std::string filePath;
        std::vector<std::string> lines;
        int cursorRow = 0, cursorCol = 0;
        float scrollX = 0.0f, scrollY = 0.0f;

        // Selection: anchor is where a drag or shift-select started; the
        // cursor is the live end. No selection while hasSelection is false.
        int selAnchorRow = 0, selAnchorCol = 0;
        bool hasSelection = false;
        bool mouseSelecting = false;

        Gui::Scrollbar verticalScrollbar;
        Gui::Scrollbar horizontalScrollbar;

        static constexpr float LINE_HEIGHT = 25.0f;
        static constexpr float CHAR_WIDTH = 25.0f;
        static constexpr float PADDING_X = 6.0f;
        static constexpr float PADDING_Y = 6.0f;
        // Helper methods
        float getLineHeight() const {
            if (!font) return 20.0f;
            return (float)TTF_GetFontHeight(font);
        }

        void refreshScrollbarGeometry() {
            // Compute max line width using actual font
            float maxLineWidth = 0.0f;
            for (const auto& line : lines) {
                int w = 0, h = 0;
                if (!line.empty() && font) {
                    TTF_GetStringSize(font, line.c_str(), line.size(), &w, &h);
                }
                maxLineWidth = std::max(maxLineWidth, (float)w);
            }
            float contentW = maxLineWidth + 2 * PADDING_X;
            float contentH = lines.size() * getLineHeight() + 2 * PADDING_Y; // use dynamic line height
            
            // FIX: Subtract padding from the view size so maxOffset() correctly 
            // allows scrolling to the very edge of the text content.
            float viewW = rect.w - 12.0f - PADDING_X * 2;
            float viewH = rect.h - 12.0f - PADDING_Y * 2;
            
            verticalScrollbar.setGeometry(rect.x + rect.w - 12, rect.y, 12, rect.h,
                                        contentH, viewH);
            horizontalScrollbar.setGeometry(rect.x, rect.y + rect.h - 12, rect.w - 12, 12,
                                            contentW, viewW);
            // Sync offsets without callback
            verticalScrollbar.setOffsetNoCallback(scrollY);
            horizontalScrollbar.setOffsetNoCallback(scrollX);
        }

        void renderText(float offsetX, float offsetY) {
            // Clip to visible area
            float viewX = rect.x - offsetX + PADDING_X;
            float viewY = rect.y - offsetY + PADDING_Y;
            float viewW = rect.w - 12.0f - PADDING_X * 2;
            float viewH = rect.h - 12.0f - PADDING_Y * 2;

            // Start rendering from the line that fits the scroll
            int startLine = (int)(scrollY / LINE_HEIGHT);
            float yOffset = viewY - scrollY + startLine * LINE_HEIGHT;

            int selStartRow = 0, selStartCol = 0, selEndRow = 0, selEndCol = 0;
            if (hasSelection) std::tie(selStartRow, selStartCol, selEndRow, selEndCol) = normalizedSelection();

            for (size_t i = startLine; i < lines.size(); ++i) {
                float y = yOffset + (i - startLine) * LINE_HEIGHT;
                if (y > viewY + viewH) break;
                const std::string& line = lines[i];
                float x = viewX - scrollX;

                // Selection highlight behind the text for this line
                if (hasSelection && (int)i >= selStartRow && (int)i <= selEndRow) {
                    int fromCol = ((int)i == selStartRow) ? selStartCol : 0;
                    int toCol   = ((int)i == selEndRow)   ? selEndCol   : (int)line.size();
                    float hx0 = x + textWidthUpTo(line, fromCol);
                    // Selecting past end-of-line (multi-line selection) shows
                    // a little highlight past the last character, like most editors.
                    float hx1 = x + textWidthUpTo(line, toCol);
                    if (toCol >= (int)line.size() && (int)i < selEndRow) hx1 += CHAR_WIDTH * 0.5f;
                    if (hx1 > hx0) {
                        SDL_FRect selRect = { hx0, y, hx1 - hx0, LINE_HEIGHT };
                        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
                        SDL_SetRenderDrawColor(renderer, 80, 120, 220, 110);
                        SDL_RenderFillRect(renderer, &selRect);
                    }
                }

                // Syntax-highlighted text, one draw call per token so each
                // can have its own color (TTF_Text only supports one color
                // per object).
                for (const auto& tok : tokenizeLine(line)) {
                    std::string sub = line.substr(tok.start, tok.len);
                    if (sub.empty()) continue;
                    float tx = x + textWidthUpTo(line, (int)tok.start);
                    TTF_Text* t = TTF_CreateText(textEngine, font, sub.c_str(), 0);
                    if (t) {
                        TTF_SetTextColor(t, tok.color.r, tok.color.g, tok.color.b, tok.color.a);
                        TTF_DrawRendererText(t, tx, y);
                        TTF_DestroyText(t);
                    }
                }
            }
        }

        void renderCursor(float offsetX, float offsetY) {
            SDL_FPoint pos = getCursorPixelPos(offsetX, offsetY);
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
            SDL_FRect cursorRect = { pos.x, pos.y, 2.0f, LINE_HEIGHT };
            SDL_RenderFillRect(renderer, &cursorRect);
        }


        void moveCursor(int row, int col, bool extend = false) {
            if (row < 0) row = 0;
            if (row >= (int)lines.size()) row = (int)lines.size() - 1;
            int maxCol = getLineLength(row);
            if (col < 0) col = 0;
            if (col > maxCol) col = maxCol;
            if (extend) {
                if (!hasSelection) { selAnchorRow = cursorRow; selAnchorCol = cursorCol; hasSelection = true; }
            } else {
                hasSelection = false;
            }
            cursorRow = row;
            cursorCol = col;
            if (extend && selAnchorRow == cursorRow && selAnchorCol == cursorCol) hasSelection = false;
            // Ensure cursor is visible
            SDL_FPoint pos = getCursorPixelPos(0.0f, 0.0f);
            float viewX = rect.x + PADDING_X;
            float viewY = rect.y + PADDING_Y;
            float viewW = rect.w - 12.0f - PADDING_X * 2;
            float viewH = rect.h - 12.0f - PADDING_Y * 2;
            if (pos.x < viewX) scrollX = std::max(0.0f, scrollX - (viewX - pos.x));
            else if (pos.x + CHAR_WIDTH > viewX + viewW) scrollX += (pos.x + CHAR_WIDTH - viewX - viewW);
            if (pos.y < viewY) scrollY = std::max(0.0f, scrollY - (viewY - pos.y));
            else if (pos.y + LINE_HEIGHT > viewY + viewH) scrollY += (pos.y + LINE_HEIGHT - viewY - viewH);
            // Clamp scroll to max
            scrollX = std::clamp(scrollX, 0.0f, horizontalScrollbar.maxOffset());
            scrollY = std::clamp(scrollY, 0.0f, verticalScrollbar.maxOffset());
            horizontalScrollbar.offset = scrollX;
            verticalScrollbar.offset = scrollY;
        }
        

        void insertChar(char ch) {
            if (cursorRow < 0 || cursorRow >= (int)lines.size()) return;
            if (hasSelection) deleteSelection();
            std::string& line = lines[cursorRow];
            if (cursorCol < 0) cursorCol = 0;
            if (cursorCol > (int)line.size()) cursorCol = (int)line.size();

            if (ch == '\t') {
                line.insert(cursorCol, tabWidth, ' ');
                cursorCol += tabWidth;
            } else {
                line.insert(cursorCol, 1, ch);
                cursorCol++;
            }
            pushUndoState();
        }

        void deleteChar() {
            if (cursorRow < 0 || cursorRow >= (int)lines.size()) return;
            if (hasSelection) { deleteSelection(); return; }
            std::string& line = lines[cursorRow];
            if (cursorCol < (int)line.size()) {
                line.erase(cursorCol, 1);
            } else if (cursorRow + 1 < (int)lines.size()) {
                // Join with next line
                std::string& next = lines[cursorRow + 1];
                line += next;
                lines.erase(lines.begin() + cursorRow + 1);
            }
            pushUndoState();
        }

        void backspace() {
            if (cursorRow < 0 || cursorRow >= (int)lines.size()) return;
            if (hasSelection) { deleteSelection(); return; }
            if (cursorCol == 0 && cursorRow > 0) {
                // Merge with previous line
                std::string& prev = lines[cursorRow - 1];
                std::string& curr = lines[cursorRow];
                prev += curr;
                lines.erase(lines.begin() + cursorRow);
                cursorRow--;
                cursorCol = (int)prev.size();
            } else if (cursorCol > 0) {
                std::string& line = lines[cursorRow];
                line.erase(cursorCol - 1, 1);
                cursorCol--;
            }
            pushUndoState();
        }

        void newline() {
            if (cursorRow < 0 || cursorRow >= (int)lines.size()) return;
            if (hasSelection) deleteSelection();
            std::string& line = lines[cursorRow];
            std::string rest = line.substr(cursorCol);
            line.erase(cursorCol);
            lines.insert(lines.begin() + cursorRow + 1, rest);
            cursorRow++;
            cursorCol = 0;
            pushUndoState();
        }
        

        void updateScrollbars() {
            // Use setOffsetNoCallback to avoid triggering onChange
            horizontalScrollbar.setOffsetNoCallback(scrollX);
            verticalScrollbar.setOffsetNoCallback(scrollY);
        }

        SDL_FPoint getCursorPixelPos(float offsetX, float offsetY) const {
            float x = rect.x - offsetX + PADDING_X - scrollX + textWidthUpTo(lines[cursorRow], cursorCol);
            float y = rect.y - offsetY + PADDING_Y - scrollY + cursorRow * LINE_HEIGHT;
            return {x, y};
        }

        // Real pixel width of the first `col` characters of `line`, measured
        // with the actual font metrics. The UI font isn't monospace, so a
        // fixed per-character width (the old CHAR_WIDTH-based math) drifts
        // from the real glyph positions more with every character — this is
        // what caused the cursor/clicks to land in the wrong place.
        float textWidthUpTo(const std::string& line, int col) const {
            if (col <= 0 || line.empty() || !font) return 0.0f;
            std::string sub = line.substr(0, std::min((size_t)col, line.size()));
            int w = 0, h = 0;
            if (!sub.empty()) TTF_GetStringSize(font, sub.c_str(), sub.size(), &w, &h);
            return (float)w;
        }

        // Inverse of textWidthUpTo: given a local pixel X (relative to the
        // start of the line's text), returns the column whose character
        // boundary is closest to that X. Used for click-to-caret mapping.
        int xToCol(const std::string& line, float localX) const {
            if (localX <= 0.0f || line.empty()) return 0;
            float prevW = 0.0f;
            for (size_t i = 1; i <= line.size(); ++i) {
                float w = textWidthUpTo(line, (int)i);
                if (localX < (prevW + w) * 0.5f) return (int)i - 1;
                prevW = w;
            }
            return (int)line.size();
        }

        // Returns the selection as (startRow, startCol, endRow, endCol) with
        // start always before (or equal to) end, regardless of which
        // direction the user dragged/shift-selected in.
        std::tuple<int,int,int,int> normalizedSelection() const {
            if (selAnchorRow < cursorRow || (selAnchorRow == cursorRow && selAnchorCol <= cursorCol))
                return {selAnchorRow, selAnchorCol, cursorRow, cursorCol};
            return {cursorRow, cursorCol, selAnchorRow, selAnchorCol};
        }

        // Removes the selected text (if any), collapsing the cursor to
        // where the selection started. Called before typing/backspace/
        // delete/newline whenever hasSelection is true, so those actions
        // replace the selection instead of acting next to it.
        void deleteSelection() {
            if (!hasSelection) return;
            auto [sRow, sCol, eRow, eCol] = normalizedSelection();
            if (sRow == eRow) {
                lines[sRow].erase((size_t)sCol, (size_t)(eCol - sCol));
            } else {
                std::string tail = lines[eRow].substr((size_t)eCol);
                lines[sRow].erase((size_t)sCol);
                lines[sRow] += tail;
                lines.erase(lines.begin() + sRow + 1, lines.begin() + eRow + 1);
            }
            cursorRow = sRow; cursorCol = sCol;
            hasSelection = false;
        }

        // Minimal C++ token classifier for syntax highlighting: comments,
        // preprocessor directives, string literals, numbers, and a common
        // keyword list. Not a real parser — good enough to make code
        // readable, not to validate it.
        struct Token { size_t start, len; SDL_Color color; };
        std::vector<Token> tokenizeLine(const std::string& line) const {
            static const std::vector<std::string> keywords = {
                "if","else","for","while","do","return","class","struct","public","private","protected",
                "void","int","float","double","bool","char","const","static","virtual","override","auto",
                "namespace","using","new","delete","true","false","nullptr","template","typename","this",
                "break","continue","switch","case","default","enum","unsigned","long","short","inline",
                "std","string","vector","include","define","ifdef","ifndef","endif","pragma"
            };
            static const std::vector<std::string> lsKeywords = {
                "num","spnum","bin","chr","str","none","noptr","stay","still","stayexpr",
                "ret","fl","nd","and","or","mod","elif","reg_script","JOIN"
            };
            
            std::vector<Token> tokens;
            const SDL_Color normalColor  = {220, 220, 240, 255};
            const SDL_Color keywordColor = {110, 160, 230, 255};
            const SDL_Color commentColor = {90, 140, 90, 255};
            const SDL_Color stringColor  = {210, 150, 90, 255};
            const SDL_Color numberColor  = {180, 210, 140, 255};
            const SDL_Color ppColor      = {190, 140, 220, 255};
            const SDL_Color directiveColor = {230, 170, 90, 255};
            const SDL_Color lsOperatorColor = {170, 120, 220, 255};

            size_t i = 0;
            while (i < line.size()) {
                if (i + 1 < line.size() && line[i] == '/' && line[i+1] == '/') {
                    tokens.push_back({i, line.size() - i, commentColor});
                    break;
                }
                if (isLogicScript && line[i] == '@') {
                    size_t j = i + 1;
                    while (j < line.size() && (isalnum((unsigned char)line[j]) || line[j] == '_')) j++;
                    tokens.push_back({i, j - i, directiveColor});
                    i = j; continue;
                }
                if (line[i] == '#') {
                    size_t j = i + 1;
                    while (j < line.size() && (isalnum((unsigned char)line[j]) || line[j] == '_')) j++;
                    tokens.push_back({i, j - i, ppColor});
                    i = j; continue;
                }
                if (line[i] == '"') {
                    size_t j = i + 1;
                    while (j < line.size() && line[j] != '"') {
                        if (line[j] == '\\' && j + 1 < line.size()) j++;
                        j++;
                    }
                    if (j < line.size()) j++; // include closing quote
                    tokens.push_back({i, j - i, stringColor});
                    i = j; continue;
                }
                if (isalpha((unsigned char)line[i]) || line[i] == '_') {
                    size_t j = i;
                    while (j < line.size() && (isalnum((unsigned char)line[j]) || line[j] == '_')) j++;
                    std::string word = line.substr(i, j - i);
                    bool isKeyword = std::find(keywords.begin(), keywords.end(), word) != keywords.end();
                    tokens.push_back({i, j - i, isKeyword ? keywordColor : normalColor});
                    i = j; continue;
                }
                if (isdigit((unsigned char)line[i])) {
                    size_t j = i;
                    while (j < line.size() && (isalnum((unsigned char)line[j]) || line[j] == '.')) j++;
                    tokens.push_back({i, j - i, numberColor});
                    i = j; continue;
                }
                if (isLogicScript) {
                    static const std::vector<std::string> lsOps = {"~~", "!~", "~", "%", "$"};
                    bool matched = false;
                    for (auto& op : lsOps) {
                        if (line.compare(i, op.size(), op) == 0) {
                            tokens.push_back({i, op.size(), lsOperatorColor});
                            i += op.size(); matched = true; break;
                        }
                    }
                    if (matched) continue;
                }
                tokens.push_back({i, 1, normalColor});
                i++;
            }
            return tokens;
        }
        int getLineLength(int row) const {
            if (row < 0 || row >= (int)lines.size()) return 0;
            return (int)lines[row].size();
        }
    };

} // end namespace Gui


// Scene and SceneParser are defined after the Gui namespace (at the bottom of this file).

inline void animation_system(ECSWorld& world, float dt)
{
    for (Entity i = 0; i < world.entity_count; i++)
    {
        if (world.has_animation_state[i])
        {
            auto& state = world.animation_state_pool[i];

            
            auto& clip = state.active();          // use active clip
            if (!clip.isPlaying || clip.imageFrameResources.empty()) continue;
            if (clip.speed <= 0.0001f) continue;

            clip.timer += dt;
            float frameDuration = 1.0f / clip.speed;
            if (clip.timer >= frameDuration) {
                clip.timer -= frameDuration;
                int frameCount = (int)clip.imageFrameResources.size();
                if (clip.currentFrame + 1 >= frameCount) {
                    if (clip.loop) {
                        clip.currentFrame = 0;
                    } else {
                        clip.currentFrame = frameCount - 1; // hold on the last frame
                        clip.isPlaying = false;
                        clip.timer = 0.0f;
                    }
                } else {
                    clip.currentFrame++;
                }
            }
        }
    }
}


inline bool render_entity_texture(SDL_Renderer* renderer, const ECSWorld& world, Entity i, float screenX, float screenY, float cameraZoom = 1.0f)
{
    SDL_FRect srcRect = {0,0,0,0};
    SDL_Texture* tex = g_resources.TextureManager.Get(world.texture_ref_pool[i].resourceName);


    if (!tex) {
        return false;
    }

    float tw, th;
    SDL_GetTextureSize(tex, &tw, &th);
    
    // UPDATED: Multiply by cameraZoom so textures scale with the camera
    float scaleX = (world.has_scale[i] ? world.scale_pool[i].x : 1.0f) * cameraZoom;
    float scaleY = (world.has_scale[i] ? world.scale_pool[i].y : 1.0f) * cameraZoom;
    float rot = world.has_rotation[i] ? world.rotation_pool[i].degrees : 0.0f;

    SDL_FRect dst = { screenX, screenY, tw * scaleX, th * scaleY };
    SDL_FPoint center = { dst.w * 0.5f, dst.h * 0.5f };

    // SDL3's SDL_RenderTextureRotated
    if (srcRect.w > 0 && srcRect.h > 0) {
        SDL_RenderTextureRotated(renderer, tex, &srcRect, &dst, rot, &center, SDL_FLIP_NONE);
    } else {
        SDL_RenderTextureRotated(renderer, tex, nullptr, &dst, rot, &center, SDL_FLIP_NONE);
    }
    return true;
}

inline bool render_entity_animation(SDL_Renderer* renderer, const ECSWorld& world, Entity i, float screenX, float screenY, float cameraZoom = 1.0f)
{
    // ── Editor canvas preview for AnimationState ────────────────────────
    // Renders the active clip's current frame on the editor canvas.
    // Works with the Play/Stop buttons in Entity Inspector which toggle
    // the active clip's isPlaying flag. animation_system() advances the
    // frame counter each frame when isPlaying is true.
    if (!world.has_animation_state[i]) return false;

    const auto& anim = world.animation_state_pool[i];
    const auto& clip = anim.active();

    if (clip.imageFrameResources.empty()) return false;

    int frameIdx = clip.currentFrame;
    if (frameIdx < 0) frameIdx = 0;
    if (frameIdx >= (int)clip.imageFrameResources.size())
        frameIdx = (int)clip.imageFrameResources.size() - 1;

    const std::string& texPath = clip.imageFrameResources[frameIdx];
    if (texPath.empty()) return false;

    SDL_Texture* tex = g_resources.TextureManager.Get(texPath);
    if (!tex) {
        if (!g_resources.TextureManager.Load(texPath, texPath)) {
            std::cerr << "Anim frame failed to load: " << texPath << " - " << SDL_GetError() << "\n";
            return false;
        }
        tex = g_resources.TextureManager.Get(texPath);
    }
    if (!tex) return false;

    float tw, th;
    SDL_GetTextureSize(tex, &tw, &th);

    // Per-clip frame size override (set in the Inspector)
    if (clip.frameWidth > 0.0f) tw = clip.frameWidth;
    if (clip.frameHeight > 0.0f) th = clip.frameHeight;

    float scaleX = (world.has_scale[i] ? world.scale_pool[i].x : 1.0f) * cameraZoom;
    float scaleY = (world.has_scale[i] ? world.scale_pool[i].y : 1.0f) * cameraZoom;
    float rot    = world.has_rotation[i] ? world.rotation_pool[i].degrees : 0.0f;

    SDL_FRect dst = { screenX, screenY, tw * scaleX, th * scaleY };
    SDL_FPoint center = { dst.w * 0.5f, dst.h * 0.5f };

    SDL_RenderTextureRotated(renderer, tex, nullptr, &dst, rot, &center, SDL_FLIP_NONE);
    return true;
}


inline bool render_entity_tilemap(SDL_Renderer* renderer, const ECSWorld& world, Entity i, float screenX, float screenY, float cameraZoom = 1.0f)
{
    if (!world.has_tilemap[i]) return false;
    auto& tm = world.tilemap_pool[i];
    if (tm.cells.empty() || tm.textureName.empty()) return false;

    SDL_Texture* tex = g_resources.TextureManager.Get(tm.textureName);

    if (!tex) {
        if (!g_resources.TextureManager.Load(tm.textureName, tm.textureName)) {
            return false;
        }
        tex = g_resources.TextureManager.Get(tm.textureName);
    }
    if (!tex) return false;

    float rotDeg = world.has_rotation[i] ? world.rotation_pool[i].degrees : 0.0f;
    float scaleX = (world.has_scale[i] ? world.scale_pool[i].x : 1.0f) * cameraZoom;
    float scaleY = (world.has_scale[i] ? world.scale_pool[i].y : 1.0f) * cameraZoom;
    float rotRad = rotDeg * M_PI / 180.0f;
    float cosA = cosf(rotRad), sinA = sinf(rotRad);

    float tileW = tm.cellWidth * scaleX;
    float tileH = tm.cellHeight * scaleY;

    // Skip rendering if tiles are too small (less than 0.5 pixels)
    if (tileW < 0.5f || tileH < 0.5f) return true; 

    for (size_t idx = 0; idx < tm.cells.size(); ++idx) {
        auto& cell = tm.cells[idx];
        float offX = (idx < tm.cellOffsets.size()) ? tm.cellOffsets[idx].x : 0.0f;
        float offY = (idx < tm.cellOffsets.size()) ? tm.cellOffsets[idx].y : 0.0f;

        // Local position of the tile
        float lx = cell.first * tm.cellWidth + offX;
        float ly = cell.second * tm.cellHeight + offY;

        // Center of the tile in local space
        float cx_l = lx + tm.cellWidth * 0.5f;
        float cy_l = ly + tm.cellHeight * 0.5f;

        // Apply scale and zoom
        float sx = cx_l * scaleX;
        float sy = cy_l * scaleY;

        // Apply rotation
        float rx = sx * cosA - sy * sinA;
        float ry = sx * sinA + sy * cosA;

        SDL_FPoint screenCenter = { screenX + rx, screenY + ry };

        SDL_FRect dst = {
            screenCenter.x - tileW * 0.5f,
            screenCenter.y - tileH * 0.5f,
            tileW,
            tileH
        };

        SDL_FPoint center = { tileW * 0.5f, tileH * 0.5f };
        SDL_RenderTextureRotated(renderer, tex, nullptr, &dst, rotDeg, &center, SDL_FLIP_NONE);
    }
    return true;
}

inline void render_physics_shape_overlay(SDL_Renderer* renderer, const Components::PhysicsBodyDef& phys,
                                         float centerX, float centerY,
                                         float rotDeg = 0.0f, float scaleX = 1.0f, float scaleY = 1.0f)
{
    // Save caller's draw color / blend mode so this overlay never leaks state
    // into whatever gets rendered after it (grid, GUI panels, gizmos, etc.)
    SDL_BlendMode prevBlend = SDL_BLENDMODE_NONE;
    SDL_GetRenderDrawBlendMode(renderer, &prevBlend);
    Uint8 prevR, prevG, prevB, prevA;
    SDL_GetRenderDrawColor(renderer, &prevR, &prevG, &prevB, &prevA);

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    // Bright, fully-opaque magenta outline + translucent fill. Using a color
    // nothing else in the editor draws with, plus a filled body (not just
    // 1px outline lines), means this overlay can never be silently painted
    // over by another opaque draw call later in the frame. Previously the
    // default 50x50 physics box exactly matched the default 50x50 selection
    // bounding box drawn right after it, so the overlay's semi-transparent
    // lines were fully overwritten pixel-for-pixel and effectively invisible.
    const Uint8 fillR = 255, fillG = 0, fillB = 255, fillA = 90;
    const Uint8 lineR = 255, lineG = 0, lineB = 255, lineA = 255;

    float rotRad = rotDeg * 3.14159265f / 180.0f;
    float cosR = cos(rotRad);
    float sinR = sin(rotRad);

    // Helper to transform local points (relative to center) to world space.
    // Scale is applied FIRST, in unrotated local space, then the result is
    // rotated. Doing it the other way around (rotate, then scale each axis
    // independently) introduces shear whenever scaleX != scaleY and the
    // shape is rotated off-axis - that's what made rectangles look "skewed"
    // when rotated.
    auto transformPoint = [&](float lx, float ly, float& outX, float& outY) {
        float sx = lx * scaleX;
        float sy = ly * scaleY;
        float rx = sx * cosR - sy * sinR;
        float ry = sx * sinR + sy * cosR;
        outX = centerX + rx;
        outY = centerY + ry;
    };

    // Fills a convex polygon (fan triangulation) with the translucent fill color.
    auto fillConvex = [&](const std::vector<SDL_FPoint>& pts) {
        if (pts.size() < 3) return;
        SDL_FColor col = { fillR / 255.0f, fillG / 255.0f, fillB / 255.0f, fillA / 255.0f };
        std::vector<SDL_Vertex> verts;
        verts.reserve(pts.size());
        for (const auto& p : pts) verts.push_back(SDL_Vertex{ { p.x, p.y }, col, { 0.0f, 0.0f } });
        std::vector<int> idx;
        idx.reserve((pts.size() - 2) * 3);
        for (size_t i = 1; i + 1 < pts.size(); ++i) {
            idx.push_back(0);
            idx.push_back((int)i);
            idx.push_back((int)i + 1);
        }
        SDL_RenderGeometry(renderer, nullptr, verts.data(), (int)verts.size(), idx.data(), (int)idx.size());
    };

    if (phys.shapeType == Physics::ShapeType::Rectangle) {
        float hw = phys.width * 0.5f;
        float hh = phys.height * 0.5f;
        SDL_FPoint p[4];
        transformPoint(-hw, -hh, p[0].x, p[0].y);
        transformPoint( hw, -hh, p[1].x, p[1].y);
        transformPoint( hw,  hh, p[2].x, p[2].y);
        transformPoint(-hw,  hh, p[3].x, p[3].y);

        fillConvex({ p[0], p[1], p[2], p[3] });

        SDL_SetRenderDrawColor(renderer, lineR, lineG, lineB, lineA);
        SDL_RenderLine(renderer, p[0].x, p[0].y, p[1].x, p[1].y);
        SDL_RenderLine(renderer, p[1].x, p[1].y, p[2].x, p[2].y);
        SDL_RenderLine(renderer, p[2].x, p[2].y, p[3].x, p[3].y);
        SDL_RenderLine(renderer, p[3].x, p[3].y, p[0].x, p[0].y);
    }
    else if (phys.shapeType == Physics::ShapeType::Circle) {
        float r = phys.radius * scaleX;
        const int segments = 32;
        std::vector<SDL_FPoint> circlePts;
        circlePts.reserve(segments);
        for (int i = 0; i < segments; ++i) {
            float angle = 2 * 3.14159f * i / segments;
            circlePts.push_back({ centerX + cosf(angle) * r, centerY + sinf(angle) * r });
        }

        fillConvex(circlePts);

        SDL_SetRenderDrawColor(renderer, lineR, lineG, lineB, lineA);
        for (int i = 0; i < segments; ++i) {
            const SDL_FPoint& a = circlePts[i];
            const SDL_FPoint& b = circlePts[(i + 1) % segments];
            SDL_RenderLine(renderer, a.x, a.y, b.x, b.y);
        }
    }
    else if (phys.shapeType == Physics::ShapeType::Polygon) {
        if (!phys.polygonPoints.empty()) {
            // polygonPoints are stored relative to the TOP-LEFT in
            // edit_object_with_editor_mouse. transformPoint expects
            // coordinates relative to the CENTER, so subtract half-width /
            // half-height before transforming.
            float hw = phys.width * 0.5f;
            float hh = phys.height * 0.5f;

            std::vector<SDL_FPoint> pts;
            pts.reserve(phys.polygonPoints.size());
            for (const auto& pt : phys.polygonPoints) {
                float lx = pt.x - hw;
                float ly = pt.y - hh;
                float px, py;
                transformPoint(lx, ly, px, py);
                pts.push_back({ px, py });
            }

            fillConvex(pts);

            SDL_SetRenderDrawColor(renderer, lineR, lineG, lineB, lineA);
            for (size_t i = 0; i < pts.size(); ++i) {
                const SDL_FPoint& a = pts[i];
                const SDL_FPoint& b = pts[(i + 1) % pts.size()];
                SDL_RenderLine(renderer, a.x, a.y, b.x, b.y);
            }
        }
    }

    // Restore whatever the caller had set so nothing downstream is affected.
    SDL_SetRenderDrawColor(renderer, prevR, prevG, prevB, prevA);
    SDL_SetRenderDrawBlendMode(renderer, prevBlend);
}

inline void draw_physics_debug_overlay(SDL_Renderer* renderer, const ECSWorld& world, const Tools::Camera& camera) {
    for (Entity i = 0; i < world.entity_count; ++i) {
        if (!world.has_physics_body[i] || !world.has_position[i]) continue;
        
        float w = world.has_rectangle_shape[i] ? world.rectangle_shape_pool[i].w : 50.0f;
        float h = world.has_rectangle_shape[i] ? world.rectangle_shape_pool[i].h : 50.0f;
        
        SDL_FPoint screenPos = camera.worldToScreen(world.position_pool[i].x, world.position_pool[i].y);
        float centerX = screenPos.x + (w * camera.zoom) * 0.5f;
        float centerY = screenPos.y + (h * camera.zoom) * 0.5f;
        
        float rot = world.has_rotation[i] ? world.rotation_pool[i].degrees : 0.0f;
        float sx = (world.has_scale[i] ? world.scale_pool[i].x : 1.0f) * camera.zoom;
        float sy = (world.has_scale[i] ? world.scale_pool[i].y : 1.0f) * camera.zoom;
        
        render_physics_shape_overlay(renderer, world.physics_body_pool[i], centerX, centerY, rot, sx, sy);
    }
}

struct GizmoHandles {
    SDL_FRect redHandle;   // end of red line
    SDL_FRect greenHandle; // end of green line
    SDL_FRect blueHandle;  // rotation handle
    bool anyHit(float x, float y) const {
        return (x >= redHandle.x && x <= redHandle.x + redHandle.w &&
                y >= redHandle.y && y <= redHandle.y + redHandle.h) ||
               (x >= greenHandle.x && x <= greenHandle.x + greenHandle.w &&
                y >= greenHandle.y && y <= greenHandle.y + greenHandle.h) ||
               (x >= blueHandle.x && x <= blueHandle.x + blueHandle.w &&
                y >= blueHandle.y && y <= blueHandle.y + blueHandle.h);
    }
};


enum GizmoOperation {
    GizmoNone,
    GizmoScaleX,
    GizmoScaleY,
    GizmoRotate
};

inline GizmoOperation currentGizmoOp = GizmoNone;

struct GizmoDragState {
    Entity target = (Entity)-1;
    float startMouseX = 0.0f, startMouseY = 0.0f;
    float startValue = 0.0f;         // initial width/height or angle
    float startCenterX = 0.0f, startCenterY = 0.0f;
};

inline GizmoDragState gizmoDrag;


inline GizmoHandles render_transform_gizmo(SDL_Renderer* renderer, float screenX, float screenY, bool isSelected) {
    GizmoHandles handles{};
    if (!isSelected) return handles;

    const float len = 30.0f;
    const float handleSize = 8.0f;

    // Red X line
    SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255);
    SDL_RenderLine(renderer, screenX, screenY, screenX + len, screenY);
    handles.redHandle = { screenX + len - handleSize/2, screenY - handleSize/2, handleSize, handleSize };
    SDL_SetRenderDrawColor(renderer, 255, 100, 100, 255);
    SDL_RenderFillRect(renderer, &handles.redHandle);

    // Green Y line
    SDL_SetRenderDrawColor(renderer, 0, 255, 0, 255);
    SDL_RenderLine(renderer, screenX, screenY, screenX, screenY + len);
    handles.greenHandle = { screenX - handleSize/2, screenY + len - handleSize/2, handleSize, handleSize };
    SDL_SetRenderDrawColor(renderer, 100, 255, 100, 255);
    SDL_RenderFillRect(renderer, &handles.greenHandle);

    // Blue rotation handle (square at offset)
    float rotX = screenX + 21.0f, rotY = screenY - 27.0f;
    handles.blueHandle = { rotX, rotY, 12, 12 };
    SDL_SetRenderDrawColor(renderer, 0, 100, 255, 255);
    SDL_RenderLine(renderer, screenX, screenY, rotX + 6, rotY + 6);
    SDL_SetRenderDrawColor(renderer, 100, 180, 255, 255);
    SDL_RenderFillRect(renderer, &handles.blueHandle);

    return handles;
}

inline void movement_system(ECSWorld& world, float dt) {
    (void)dt;
    for (Entity i = 0; i < world.entity_count; ++i) {
        if (world.has_position[i] && world.has_physics_body[i]) {
            auto& phys = world.physics_body_pool[i];
            if (b2Body_IsValid(phys.bodyId)) {
                b2Vec2 posM = b2Body_GetPosition(phys.bodyId);
                b2Vec2 centerPx = Physics::MToPx(posM);
                float w = world.has_rectangle_shape[i] ? world.rectangle_shape_pool[i].w : 50.0f;
                float h = world.has_rectangle_shape[i] ? world.rectangle_shape_pool[i].h : 50.0f;
                world.position_pool[i].x = centerPx.x - (w * 0.5f);
                world.position_pool[i].y = centerPx.y - (h * 0.5f);

                // Sync rotation
                if (world.has_rotation[i]) {
                    b2Rot rotation = b2Body_GetRotation(phys.bodyId);
                    float angleRad = b2Rot_GetAngle(rotation);
                    world.rotation_pool[i].degrees = angleRad * 180.0f / (float)M_PI;
                }
            }
        }
    }
}

inline void deselect_all(ECSWorld& world) {
    for (Entity i = 0; i < world.entity_count; i++) {
        if (world.has_selection[i]) world.selection_pool[i].isSelected = false;
    }
}

inline void drawText(TTF_TextEngine* textEngine, TTF_Font* font, const char* s, float x, float y, SDL_Color c) {
    TTF_Text* t = TTF_CreateText(textEngine, font, s, 0);
    if (!t) return;
    TTF_SetTextColor(t, c.r, c.g, c.b, c.a);
    TTF_DrawRendererText(t, x, y);
    TTF_DestroyText(t);
}

// ── Helper: draw the world-space X and Y axes ───────────────────────────────
inline void render_world_axes(SDL_Renderer* renderer,
                              const Tools::Camera& camera,
                              float canvasX, float canvasY,
                              float canvasW, float canvasH)
{
    float wL, wT, wR, wB;
    camera.getVisibleWorldBounds(canvasX, canvasY, canvasW, canvasH, wL, wT, wR, wB);

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    // X axis (red) – world y = 0
    SDL_FPoint a = camera.worldToScreen(wL, 0.0f);
    SDL_FPoint b = camera.worldToScreen(wR, 0.0f);
    SDL_SetRenderDrawColor(renderer, 200, 60, 60, 210);
    SDL_RenderLine(renderer, a.x, a.y, b.x, b.y);

    // Y axis (green) – world x = 0
    a = camera.worldToScreen(0.0f, wT);
    b = camera.worldToScreen(0.0f, wB);
    SDL_SetRenderDrawColor(renderer, 60, 200, 60, 210);
    SDL_RenderLine(renderer, a.x, a.y, b.x, b.y);

    // Origin dot
    SDL_FPoint o = camera.worldToScreen(0.0f, 0.0f);
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    SDL_FRect dot = { o.x - 3.0f, o.y - 3.0f, 6.0f, 6.0f };
    SDL_RenderFillRect(renderer, &dot);
}



// In engine.h, REPLACE your render_canvas_ruler with this:

inline void render_canvas_ruler(SDL_Renderer* renderer,
                                TTF_TextEngine* textEngine, TTF_Font* font,
                                const Tools::Camera& camera,
                                float canvasX, float canvasY,
                                float canvasW, float canvasH)
{
    constexpr float RULER_THICK = 20.0f;
    constexpr int   MAJOR_STEP  = 100;
    constexpr int   MINOR_STEP  = 50;

    const SDL_Color bgCol    = { 45,  45,  45, 240 };
    const SDL_Color majorCol = { 220, 220, 220, 255 };
    const SDL_Color minorCol = { 120, 120, 120, 255 };
    const SDL_Color labelCol = { 230, 230, 200, 255 };

    // ── Horizontal ruler (top strip) ─────────────────────────────────────
    SDL_SetRenderDrawColor(renderer, bgCol.r, bgCol.g, bgCol.b, bgCol.a);
    SDL_FRect h_ruler = { canvasX, canvasY, canvasW, RULER_THICK };
    SDL_RenderFillRect(renderer, &h_ruler);

    {
        // Get the exact world coordinates of the canvas top edge
        SDL_FPoint wLeft  = camera.screenToWorld(canvasX,          canvasY);
        SDL_FPoint wRight = camera.screenToWorld(canvasX + canvasW, canvasY);
        int startX = (int)std::floor(wLeft.x  / (float)MINOR_STEP) * MINOR_STEP;
        int endX   = (int)std::ceil (wRight.x / (float)MINOR_STEP) * MINOR_STEP;

        for (int wx = startX; wx <= endX; wx += MINOR_STEP) {
            bool major = ((wx % MAJOR_STEP) == 0);
            
            // FIX: Use wLeft.y instead of 0.0f. This ensures that if the camera 
            // is rotated or panned, the X projection remains perfectly aligned 
            // with the actual top edge of the canvas.
            SDL_FPoint sp = camera.worldToScreen((float)wx, wLeft.y);
            
            if (sp.x < canvasX || sp.x > canvasX + canvasW) continue;
            float tickH = major ? RULER_THICK : RULER_THICK * 0.45f;
            SDL_SetRenderDrawColor(renderer,
                major ? majorCol.r : minorCol.r,
                major ? majorCol.g : minorCol.g,
                major ? majorCol.b : minorCol.b, 255);
            SDL_RenderLine(renderer,
                sp.x, canvasY + RULER_THICK - tickH,
                sp.x, canvasY + RULER_THICK);
                
            if (major && font && textEngine) {
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%d", wx);
                TTF_Text* t = TTF_CreateText(textEngine, font, buf, 0);
                if (t) {
                    TTF_SetTextColor(t, labelCol.r, labelCol.g, labelCol.b, labelCol.a);
                    TTF_DrawRendererText(t, sp.x + 2.0f, canvasY + 2.0f);
                    TTF_DestroyText(t);
                }
            }
        }
    }

    // ── Vertical ruler (left strip) ──────────────────────────────────────
    SDL_SetRenderDrawColor(renderer, bgCol.r, bgCol.g, bgCol.b, bgCol.a);
    SDL_FRect v_ruler = { canvasX, canvasY + RULER_THICK, RULER_THICK, canvasH - RULER_THICK };
    SDL_RenderFillRect(renderer, &v_ruler);

    {
        // Get the exact world coordinates of the canvas left edge
        SDL_FPoint wTop = camera.screenToWorld(canvasX, canvasY);
        SDL_FPoint wBot = camera.screenToWorld(canvasX, canvasY + canvasH);
        int startY = (int)std::floor(wTop.y / (float)MINOR_STEP) * MINOR_STEP;
        int endY   = (int)std::ceil (wBot.y / (float)MINOR_STEP) * MINOR_STEP;

        for (int wy = startY; wy <= endY; wy += MINOR_STEP) {
            bool major = ((wy % MAJOR_STEP) == 0);
            
            // FIX: Use wTop.x instead of 0.0f for the same alignment reasons.
            SDL_FPoint sp = camera.worldToScreen(wTop.x, (float)wy);
            
            if (sp.y < canvasY + RULER_THICK || sp.y > canvasY + canvasH) continue;
            float tickW = major ? RULER_THICK : RULER_THICK * 0.45f;
            SDL_SetRenderDrawColor(renderer,
                major ? majorCol.r : minorCol.r,
                major ? majorCol.g : minorCol.g,
                major ? majorCol.b : minorCol.b, 255);
            SDL_RenderLine(renderer,
                canvasX + RULER_THICK - tickW, sp.y,
                canvasX + RULER_THICK,         sp.y);
                
            if (major && font && textEngine) {
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%d", wy);
                TTF_Text* t = TTF_CreateText(textEngine, font, buf, 0);
                if (t) {
                    TTF_SetTextColor(t, labelCol.r, labelCol.g, labelCol.b, labelCol.a);
                    TTF_DrawRendererText(t, canvasX + 2.0f, sp.y + 2.0f);
                    TTF_DestroyText(t);
                }
            }
        }
    }

    // Corner square covers the overlap of both strips.
    SDL_SetRenderDrawColor(renderer, 35, 35, 35, 240);
    SDL_FRect square = { canvasX, canvasY, RULER_THICK, RULER_THICK };
    SDL_RenderFillRect(renderer, &square);
}



inline void render_system_and_scene_gui_in_editor(
    SDL_Renderer* renderer,
    TTF_TextEngine* textEngine,
    TTF_Font* font,
    const ECSWorld & world,
    float viewX, float viewY,
    float scrollX, float scrollY,
    std::vector<std::unique_ptr<Gui::IGuiElement>>& guiElements,
    Tools::Camera& camera)
{
    // NOTE: camera.targetX/targetY are already kept in sync with
    // editorScrollX/editorScrollY (scrollbars and zoom write directly into
    // the camera target, and main.cpp mirrors camera.targetX/Y back into
    // editorScrollX/Y every frame). Adding scrollX/scrollY to camera.targetX
    // here would double-count the pan/scroll offset for entity rendering,
    // desyncing what's drawn from what edit_object_with_editor_mouse picks
    // via camera.screenToWorld() as soon as the camera is panned or zoomed.
    // So entities are rendered using the camera target as-is.
    float savedTargetX = camera.targetX;
    float savedTargetY = camera.targetY;

    // ── Pass 1: Entities, transformed through the camera ─────────────────
    // Build a list of entities that have position, then sort by z_index
    std::vector<Entity> renderOrder;
    renderOrder.reserve(world.entity_count);
    for (Entity i = 0; i < world.entity_count; ++i) {
        if (world.has_position[i]) {
            renderOrder.push_back(i);
        }
    }

    // Sort by z_index (entities without z_index get 0)
    std::sort(renderOrder.begin(), renderOrder.end(), [&](Entity a, Entity b) {
        int za = world.has_z_index[a] ? world.z_index_pool[a].z : 0;
        int zb = world.has_z_index[b] ? world.z_index_pool[b].z : 0;
        return za < zb;
    });

    // Render in z-order
    for (Entity i : renderOrder) {
        // Use camera to get screen coordinates
        SDL_FPoint screen = camera.worldToScreen(
            world.position_pool[i].x, world.position_pool[i].y);
        float screenX = screen.x;
        float screenY = screen.y;

        float w = world.has_rectangle_shape[i] ? world.rectangle_shape_pool[i].w : 50.0f;
        float h = world.has_rectangle_shape[i] ? world.rectangle_shape_pool[i].h : 50.0f;
        
        // Scale dimensions by camera zoom
        float scaledW = w * camera.zoom;
        float scaledH = h * camera.zoom;
        float centerX = screenX + scaledW * 0.5f;
        float centerY = screenY + scaledH * 0.5f;

        // Render texture/animation
        bool hasTexture = false;
        if (world.has_texture_ref[i]) {
            hasTexture = render_entity_texture(renderer, world, i, screenX, screenY, camera.zoom);
        }
        if (world.has_animation_state[i]) {
            hasTexture = render_entity_animation(renderer, world, i, screenX, screenY, camera.zoom) || hasTexture;
        }

        // Draw transformed outline (if no texture or selected)
        bool isSelected = world.has_selection[i] && world.selection_pool[i].isSelected;
        if (!hasTexture || isSelected) {
            float rot = world.has_rotation[i] ? world.rotation_pool[i].degrees * (M_PI / 180.0f) : 0.0f;
            float sx = (world.has_scale[i] ? world.scale_pool[i].x : 1.0f) * camera.zoom;
            float sy = (world.has_scale[i] ? world.scale_pool[i].y : 1.0f) * camera.zoom;

            SDL_FPoint corners[4] = {
                {-w/2.0f, -h/2.0f},
                { w/2.0f, -h/2.0f},
                { w/2.0f,  h/2.0f},
                {-w/2.0f,  h/2.0f}
            };
            float cosA = cosf(rot), sinA = sinf(rot);
            for (int j = 0; j < 4; ++j) {
                float x = corners[j].x * sx;
                float y = corners[j].y * sy;
                corners[j].x = centerX + x * cosA - y * sinA;
                corners[j].y = centerY + x * sinA + y * cosA;
            }

            if (isSelected) {
                SDL_SetRenderDrawColor(renderer,
                    world.selection_pool[i].selectionColor.r,
                    world.selection_pool[i].selectionColor.g,
                    world.selection_pool[i].selectionColor.b,
                    world.selection_pool[i].selectionColor.a);
            } else {
                SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
            }
            for (int j = 0; j < 4; ++j) {
                int next = (j + 1) % 4;
                SDL_RenderLine(renderer,
                    corners[j].x, corners[j].y,
                    corners[next].x, corners[next].y);
            }
        }

        // Gizmo
        if (isSelected) {
            render_transform_gizmo(renderer, centerX, centerY, true);
        }

        // rot/sx/sy hoisted here so the vertex-handle block further down can reuse them
        float rot = world.has_rotation[i] ? world.rotation_pool[i].degrees : 0.0f;
        float sx = world.has_scale[i] ? world.scale_pool[i].x : 1.0f;
        float sy = world.has_scale[i] ? world.scale_pool[i].y : 1.0f;

        // Render physics overlay LAST (aside from metadata label and vertex handles)
        if (world.has_physics_body[i]) {
            render_physics_shape_overlay(renderer, world.physics_body_pool[i], 
                            centerX, centerY, rot, 
                            world.has_scale[i] ? world.scale_pool[i].x * camera.zoom : camera.zoom,
                            world.has_scale[i] ? world.scale_pool[i].y * camera.zoom : camera.zoom);
        }

        // --- Vertex handles (only for selected polygon entities) ---
        if (isSelected && world.has_physics_body[i] &&
            world.physics_body_pool[i].shapeType == Physics::ShapeType::Polygon) {
            auto& phys = world.physics_body_pool[i];
            if (!phys.polygonPoints.empty()) {
                float hw = phys.width * 0.5f;
                float hh = phys.height * 0.5f;
                float rotRad = rot * 3.14159265f / 180.0f;
                float cosR = cos(rotRad);
                float sinR = sin(rotRad);

                for (const auto& pt : phys.polygonPoints) {
                    float lx = pt.x - hw;
                    float ly = pt.y - hh;
                    float sxp = lx * sx;
                    float syp = ly * sy;
                    float rx = sxp * cosR - syp * sinR;
                    float ry = sxp * sinR + syp * cosR;
                    float vx = centerX + rx * camera.zoom;
                    float vy = centerY + ry * camera.zoom;
                    SDL_FRect handleRect = { vx - 4.0f, vy - 4.0f, 8.0f, 8.0f };
                    SDL_SetRenderDrawColor(renderer, 0, 255, 255, 255);
                    SDL_RenderFillRect(renderer, &handleRect);
                    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
                    SDL_RenderRect(renderer, &handleRect);
                }
            }
        }

        if (world.has_tilemap[i]) {
            auto& tm = world.tilemap_pool[i];
            SDL_Texture* tex = g_resources.TextureManager.Get(tm.textureName);
            if (!tex && !tm.textureName.empty()) {
                g_resources.TextureManager.Load(tm.textureName, tm.textureName);
                tex = g_resources.TextureManager.Get(tm.textureName);
            }
            if (tex && !tm.cells.empty()) {
                float rotDeg = world.has_rotation[i] ? world.rotation_pool[i].degrees : 0.0f;
                float scaleX = (world.has_scale[i] ? world.scale_pool[i].x : 1.0f) * camera.zoom;
                float scaleY = (world.has_scale[i] ? world.scale_pool[i].y : 1.0f) * camera.zoom;
                float rotRad = rotDeg * M_PI / 180.0f;
                float cosA = cosf(rotRad), sinA = sinf(rotRad);

                // Entity world position (top‑left), converted to screen space
                // ONCE here. Every cell's offset from this point is computed
                // directly in screen space below (local units * scale*zoom),
                // rather than being added back into world space and run
                // through worldToScreen a second time -- doing that latter
                // thing was the bug: worldToScreen() already multiplies
                // whatever it's given by camera.zoom, so an offset that
                // already had zoom baked into it (via scaleX/scaleY) was
                // being zoomed twice. That's invisible at zoom == 1 (since
                // zoom*zoom == zoom there) but at any other zoom level every
                // cell lands progressively farther from where it should be
                // the farther it sits from the entity's origin -- which is
                // exactly why offset/unsnapped cells (bigger local offsets)
                // and painting/erasing/re-grabbing them looked "slightly
                // off": the hit-testing below uses plain local coordinates
                // (correct), while the old render math didn't (wrong), so
                // the two disagreed.
                SDL_FPoint entityScreen = camera.worldToScreen(world.position_pool[i].x, world.position_pool[i].y);

                // Tile size in screen space (unrotated) -- same for every cell.
                float tileW = tm.cellWidth * scaleX;
                float tileH = tm.cellHeight * scaleY;

                // Screen rect of the cell currently being drag-moved (if any),
                // captured while we render it below so we can outline it in
                // blue afterwards -- the outline has to be drawn on top of
                // every tile, not just this one, so it can't happen inline.
                bool haveDragRect = false;
                SDL_FRect dragScreenRect{};
                bool draggingThisEntity = tileCellDrag.active && tileCellDrag.target == i;

                // Screen rects of every non-dragged cell, collected here and
                // outlined in a second pass after all textures are drawn --
                // same reasoning as the blue drag outline: it must sit on
                // top of neighbouring tiles, not just the one it belongs to.
                std::vector<SDL_FRect> cellOutlineRects;
                cellOutlineRects.reserve(tm.cells.size());

                // Skip drawing if tiles are too small (less than 0.5 pixels)
                if (tileW >= 0.5f && tileH >= 0.5f) {
                    for (size_t idx = 0; idx < tm.cells.size(); ++idx) {
                        auto& cell = tm.cells[idx];
                        float offX = (idx < tm.cellOffsets.size()) ? tm.cellOffsets[idx].x : 0.0f;
                        float offY = (idx < tm.cellOffsets.size()) ? tm.cellOffsets[idx].y : 0.0f;

                        // Tile top‑left in local space (no rotation/scale yet),
                        // including the per-cell drag offset. This is the same
                        // formula the hit-testing (paint/erase/grab) below
                        // uses, so the two always agree on where a cell is.
                        float lx = cell.first * tm.cellWidth + offX;
                        float ly = cell.second * tm.cellHeight + offY;

                        // Tile centre in local space
                        float cx_l = lx + tm.cellWidth * 0.5f;
                        float cy_l = ly + tm.cellHeight * 0.5f;

                        // Apply scale*zoom -- this is already a screen-space
                        // offset from the entity's screen position, so it
                        // must NOT be passed through worldToScreen again.
                        float sx = cx_l * scaleX;
                        float sy = cy_l * scaleY;

                        // Apply rotation (screen-space offset)
                        float rx = sx * cosA - sy * sinA;
                        float ry = sx * sinA + sy * cosA;

                        SDL_FPoint screenCenter = { entityScreen.x + rx, entityScreen.y + ry };
                        SDL_FRect dst = {
                            screenCenter.x - tileW * 0.5f,
                            screenCenter.y - tileH * 0.5f,
                            tileW,
                            tileH
                        };
                        SDL_FPoint center = { tileW * 0.5f, tileH * 0.5f };
                        SDL_RenderTextureRotated(renderer, tex, nullptr, &dst, rotDeg, &center, SDL_FLIP_NONE);

                        if (draggingThisEntity && (int)idx == tileCellDrag.cellIndex) {
                            haveDragRect = true;
                            dragScreenRect = dst;
                        } else {
                            cellOutlineRects.push_back(dst);
                        }
                    }
                }

                // Faint outline over every cell that ISN'T currently being
                // dragged, so the actual selectable/paintable bounds of each
                // placed texture are visible -- including cells sitting off
                // the grid from an unsnapped offset, whose bounds otherwise
                // aren't obvious from the texture alone. Drawn in its own
                // pass, after every tile, so it isn't painted over by a
                // neighbouring tile.
                if (!cellOutlineRects.empty()) {
                    // Save/restore blend mode so this translucent outline
                    // never leaks an alpha blend state into whatever draws
                    // after it (grid, GUI, ruler, etc.) -- same pattern used
                    // by render_physics_shape_overlay above.
                    SDL_BlendMode prevBlend = SDL_BLENDMODE_NONE;
                    SDL_GetRenderDrawBlendMode(renderer, &prevBlend);
                    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
                    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 90);
                    for (auto& r : cellOutlineRects) {
                        SDL_RenderRect(renderer, &r);
                    }
                    SDL_SetRenderDrawBlendMode(renderer, prevBlend);
                }

                // Blue outline over the cell currently being dragged, drawn
                // last so it sits on top of every tile (including neighbours
                // it may be passing over) and on top of the faint outlines
                // above. Works the same whether snapping is on (tile jumps
                // cell-to-cell) or off (tile follows the cursor freely)
                // since it just outlines wherever the tile was actually
                // drawn this frame -- which, now that the double-zoom is
                // fixed, always matches where the cursor can actually grab
                // and release it.
                if (haveDragRect) {
                    SDL_SetRenderDrawColor(renderer, 60, 140, 255, 255);
                    SDL_FRect outline = dragScreenRect;
                    SDL_RenderRect(renderer, &outline);
                    outline.x -= 1.0f; outline.y -= 1.0f;
                    outline.w += 2.0f; outline.h += 2.0f;
                    SDL_RenderRect(renderer, &outline);
                }
            }
        }

        // Metadata label
        if (world.has_metadata[i]) {
            TTF_Text* textObj = TTF_CreateText(textEngine, font, world.metadata_pool[i].name.c_str(), 0);
            if (textObj) {
                TTF_SetTextColor(textObj, 255, 255, 255, 255);
                TTF_DrawRendererText(textObj, screenX, screenY);
                TTF_DestroyText(textObj);
            }
        }
    }

    // ── Pass 2: World-space axes (drawn through the camera transform) ────
    render_world_axes(renderer, camera, viewX, viewY, canvasViewW, canvasViewH);

    // ── Pass 3: GUI elements (screen-space, without legacy scroll offsets) ──
    for (auto& elem : guiElements) {
        elem->render(0.0f, 0.0f);
        elem->renderSelectionOutline(renderer, 0.0f, 0.0f);
    }

    // ── Pass 4: Optional grid ────────────────────────────────────────────
    if (editor_showGrid) {
        float gridW = editor_cellW, gridH = editor_cellH;

        // The grid's own origin -- where cell (0,0) sits in world space.
        // Defaults to the world origin, but when a tilemap entity is
        // selected we anchor the grid to that entity's position instead,
        // since that's the origin painting actually uses (see the
        // right-click paint handler: localX/localY = worldPt - entity
        // position, then floor(local / cellWidth)). Without this offset
        // the overlay only lines up with painted tiles when the tilemap
        // entity happens to sit at (0,0).
        float gridOriginX = 0.0f, gridOriginY = 0.0f;
        if (lastSelectedEntity != (Entity)-1 &&
            world.has_tilemap[lastSelectedEntity]) {
            auto& tm = world.tilemap_pool[lastSelectedEntity];
            gridW = tm.cellWidth;
            gridH = tm.cellHeight;
            if (world.has_position[lastSelectedEntity]) {
                gridOriginX = world.position_pool[lastSelectedEntity].x;
                gridOriginY = world.position_pool[lastSelectedEntity].y;
            }
        }
        if (gridW <= 0.0f || gridH <= 0.0f) {
            gridW = gridH = 32.0f;
        }

        // Compute visible world bounds
        float wL, wT, wR, wB;
        camera.getVisibleWorldBounds(viewX, viewY, canvasViewW, canvasViewH,
                                    wL, wT, wR, wB);

        // Check if grid lines would be at least 2 pixels apart on screen.
        // Measured from the grid's own origin (not the world origin) so the
        // zoom-density check is unaffected by how far the tilemap entity is
        // from (0,0); it only cares about on-screen spacing of one cell.
        SDL_FPoint p1 = camera.worldToScreen(gridOriginX, gridOriginY);
        SDL_FPoint p2 = camera.worldToScreen(gridOriginX + gridW, gridOriginY);
        float screenDistX = sqrtf((p2.x - p1.x) * (p2.x - p1.x) + (p2.y - p1.y) * (p2.y - p1.y));
        p2 = camera.worldToScreen(gridOriginX, gridOriginY + gridH);
        float screenDistY = sqrtf((p2.x - p1.x) * (p2.x - p1.x) + (p2.y - p1.y) * (p2.y - p1.y));

        // If the distance is less than 2 pixels, skip drawing the grid (it's too dense)
        const float MIN_GRID_PIXELS = 2.0f;
        if (screenDistX < MIN_GRID_PIXELS || screenDistY < MIN_GRID_PIXELS) {
            // Don't draw the grid – it would be a solid fill
            // (You could optionally draw major lines only, but we simply skip)
            // goto skip_grid; (we'll just not draw)
        } else {
            // Align to grid, relative to the grid's origin rather than world
            // (0,0) -- this is what actually keeps the lines under the
            // painted tiles regardless of where the tilemap entity lives or
            // how far the camera has panned/zoomed.
            float startX = gridOriginX + floorf((wL - gridOriginX) / gridW) * gridW;
            float endX   = gridOriginX + ceilf((wR - gridOriginX) / gridW) * gridW;
            float startY = gridOriginY + floorf((wT - gridOriginY) / gridH) * gridH;
            float endY   = gridOriginY + ceilf((wB - gridOriginY) / gridH) * gridH;

            SDL_SetRenderDrawColor(renderer, 100, 100, 100, 255); // opaque grey
            // (use alpha 255 to avoid accumulation – user can adjust)

            // Vertical lines
            for (float x = startX; x <= endX; x += gridW) {
                SDL_FPoint a = camera.worldToScreen(x, wT);
                SDL_FPoint b = camera.worldToScreen(x, wB);
                SDL_RenderLine(renderer, a.x, a.y, b.x, b.y);
            }
            // Horizontal lines
            for (float y = startY; y <= endY; y += gridH) {
                SDL_FPoint a = camera.worldToScreen(wL, y);
                SDL_FPoint b = camera.worldToScreen(wR, y);
                SDL_RenderLine(renderer, a.x, a.y, b.x, b.y);
            }
        }
    }

    // ── Pass 5: Screen-space ruler (drawn last so it sits on top) ────────
    render_canvas_ruler(renderer, textEngine, font, camera, viewX, viewY, canvasViewW, canvasViewH);

    // Restore the camera target (main.cpp owns it).
    camera.targetX = savedTargetX;
    camera.targetY = savedTargetY;
}

// 2. Pure Game Render System (NO editor canvas offset)
// This replaces render_system_in_editor by removing the "+ 105" offset
inline void render_system_game_debug(SDL_Renderer* renderer, TTF_TextEngine* textEngine, TTF_Font* font, const ECSWorld& world) {
    for (Entity i = 0; i < world.entity_count; ++i) {
        if (world.has_position[i]) {
            
            // Pure position, NO + 105 offset!
            SDL_FRect outlineRect = { 
                world.position_pool[i].x, 
                world.position_pool[i].y, 
                50.0f, 50.0f 
            };

            if (world.has_rectangle_shape[i]) {
                outlineRect.w = world.rectangle_shape_pool[i].w;
                outlineRect.h = world.rectangle_shape_pool[i].h;
            }
        
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
            
            
            if (world.has_metadata[i]) {
                TTF_Text* textObj = TTF_CreateText(textEngine, font, world.metadata_pool[i].name.c_str(), 0);
                if (textObj) {
                    TTF_SetTextColor(textObj, 255, 255, 255, 255);
                    TTF_DrawRendererText(textObj, world.position_pool[i].x, world.position_pool[i].y);
                    TTF_DestroyText(textObj);
                }
            }

            SDL_RenderRect(renderer, &outlineRect);
        }
    }
}

inline void render_system_game_debug_no_text(SDL_Renderer* renderer, TTF_TextEngine* textEngine, const ECSWorld& world) {
    for (Entity i = 0; i < world.entity_count; ++i) {
        if (world.has_position[i]) {
            
            // Pure position, NO + 105 offset!
            SDL_FRect outlineRect = { 
                world.position_pool[i].x, 
                world.position_pool[i].y, 
                50.0f, 50.0f 
            };

            if (world.has_rectangle_shape[i]) {
                outlineRect.w = world.rectangle_shape_pool[i].w;
                outlineRect.h = world.rectangle_shape_pool[i].h;
            }
        
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);

            SDL_RenderRect(renderer, &outlineRect);
        }
    }
}

inline void edit_object_with_editor_mouse(SDL_Renderer* renderer, ECSWorld& world,
    std::vector<std::unique_ptr<Gui::IGuiElement>>& guiElements,
    Gui::IGuiElement*& selectedGuiElem,
    const SDL_Event& e)
{
    if (currentEditMode == EditMode::Dialog) return;
    const float kPolyPointPickRadius = 10.0f;

    // ------------------------------------------------------------------
    // Helper: screen-space hit test for GUI elements. GUI is rendered at
    // raw positions (offset 0,0) in the editor, so raw mouse coords vs
    // raw element rect is correct at every camera pan/zoom level.
    // ------------------------------------------------------------------
    auto guiHitTest = [](Gui::IGuiElement* elem, float x, float y) -> bool {
        if (!elem) return false;
        return x >= elem->getX() && x <= elem->getX() + elem->getWidth() &&
               y >= elem->getY() && y <= elem->getY() + elem->getHeight();
    };

    // ------------------------------------------------------------------
    // Helper: convert world (canvas) coordinates to local polygon space
    // ------------------------------------------------------------------
    auto worldToLocalPolygon = [&](Entity ent, float wx, float wy) -> b2Vec2 {
        if (ent >= world.entity_count) return {0,0};
        float rectW = world.has_rectangle_shape[ent] ? world.rectangle_shape_pool[ent].w : 50.0f;
        float rectH = world.has_rectangle_shape[ent] ? world.rectangle_shape_pool[ent].h : 50.0f;
        float posX = world.position_pool[ent].x;
        float posY = world.position_pool[ent].y;
        float centerX = posX + rectW * 0.5f;
        float centerY = posY + rectH * 0.5f;
        float rotDeg = world.has_rotation[ent] ? world.rotation_pool[ent].degrees : 0.0f;
        float scaleX = world.has_scale[ent] ? world.scale_pool[ent].x : 1.0f;
        float scaleY = world.has_scale[ent] ? world.scale_pool[ent].y : 1.0f;
        auto& phys = world.physics_body_pool[ent];
        float halfW = phys.width * 0.5f;
        float halfH = phys.height * 0.5f;
        float dx = wx - centerX;
        float dy = wy - centerY;
        float rad = -rotDeg * 3.14159265f / 180.0f;
        float cosA = cos(rad);
        float sinA = sin(rad);
        float rx = dx * cosA - dy * sinA;
        float ry = dx * sinA + dy * cosA;
        float sx = (fabs(scaleX) > 1e-6f) ? rx / scaleX : 0.0f;
        float sy = (fabs(scaleY) > 1e-6f) ? ry / scaleY : 0.0f;
        return { sx + halfW, sy + halfH };
    };

    // --- Polygon point editing (left/right click) ---
    if (editor_isDrawingPolygon &&
        e.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
        (e.button.button == SDL_BUTTON_LEFT || e.button.button == SDL_BUTTON_RIGHT)) {
        float mx = e.button.x, my = e.button.y;
        if (isInsideCanvas(mx, my)) {
            SDL_FPoint worldPt = g_editorCamera.screenToWorld(mx, my);
            float logicalX = worldPt.x;
            float logicalY = worldPt.y;
            Entity ent = lastSelectedEntity;
            if (ent != (Entity)-1 && world.has_physics_body[ent]) {
                auto& phys = world.physics_body_pool[ent];
                if (phys.shapeType == Physics::ShapeType::Polygon) {
                    b2Vec2 clickPt = worldToLocalPolygon(ent, logicalX, logicalY);
                    if (e.button.button == SDL_BUTTON_LEFT) {
                        if (phys.polygonPoints.size() >= 3) {
                            float dx = clickPt.x - phys.polygonPoints.front().x;
                            float dy = clickPt.y - phys.polygonPoints.front().y;
                            if (dx*dx + dy*dy <= kPolyPointPickRadius * kPolyPointPickRadius) {
                                editor_isDrawingPolygon = false;
                                return;
                            }
                        }
                        phys.polygonPoints.push_back(clickPt);
                        return;
                    } else {
                        int nearestIdx = -1;
                        float nearestDistSq = kPolyPointPickRadius * kPolyPointPickRadius;
                        for (size_t i = 0; i < phys.polygonPoints.size(); ++i) {
                            float dx = clickPt.x - phys.polygonPoints[i].x;
                            float dy = clickPt.y - phys.polygonPoints[i].y;
                            float distSq = dx*dx + dy*dy;
                            if (distSq <= nearestDistSq) { nearestDistSq = distSq; nearestIdx = (int)i; }
                        }
                        if (nearestIdx != -1) phys.polygonPoints.erase(phys.polygonPoints.begin() + nearestIdx);
                        return;
                    }
                }
            }
        }
    }

    // --- Middle-mouse vertex dragging (disabled while drawing polygon) ---
    if (!editor_isDrawingPolygon) {
        if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_MIDDLE) {
            float mx = e.button.x, my = e.button.y;
            if (!isInsideCanvas(mx, my)) return;
            Entity ent = lastSelectedEntity;
            if (ent != (Entity)-1 && world.has_physics_body[ent] &&
                world.physics_body_pool[ent].shapeType == Physics::ShapeType::Polygon) {
                auto& phys = world.physics_body_pool[ent];
                SDL_FPoint worldPt = g_editorCamera.screenToWorld(mx, my);
                b2Vec2 localPt = worldToLocalPolygon(ent, worldPt.x, worldPt.y);
                int nearestIdx = -1;
                float nearestDistSq = kPolyPointPickRadius * kPolyPointPickRadius;
                for (size_t i = 0; i < phys.polygonPoints.size(); ++i) {
                    float dx = localPt.x - phys.polygonPoints[i].x;
                    float dy = localPt.y - phys.polygonPoints[i].y;
                    float d2 = dx*dx + dy*dy;
                    if (d2 < nearestDistSq) { nearestDistSq = d2; nearestIdx = (int)i; }
                }
                if (nearestIdx != -1) {
                    vertexDrag.target = ent;
                    vertexDrag.vertexIndex = nearestIdx;
                    vertexDrag.startMouseX = mx;
                    vertexDrag.startMouseY = my;
                    vertexDrag.startVertexX = phys.polygonPoints[nearestIdx].x;
                    vertexDrag.startVertexY = phys.polygonPoints[nearestIdx].y;
                    isDraggingVertex = true;
                    return;
                }
            }
        }
        if (e.type == SDL_EVENT_MOUSE_MOTION && isDraggingVertex) {
            Entity ent = vertexDrag.target;
            if (ent != (Entity)-1 && world.has_physics_body[ent]) {
                auto& phys = world.physics_body_pool[ent];
                if (phys.shapeType == Physics::ShapeType::Polygon &&
                    vertexDrag.vertexIndex >= 0 &&
                    vertexDrag.vertexIndex < (int)phys.polygonPoints.size()) {
                    SDL_FPoint worldPt = g_editorCamera.screenToWorld(e.motion.x, e.motion.y);
                    phys.polygonPoints[vertexDrag.vertexIndex] = worldToLocalPolygon(ent, worldPt.x, worldPt.y);
                    vertexDrag.startMouseX = e.motion.x;
                    vertexDrag.startMouseY = e.motion.y;
                }
            }
        }
        if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_MIDDLE) {
            isDraggingVertex = false;
            vertexDrag.target = (Entity)-1;
            vertexDrag.vertexIndex = -1;
        }
    }

    // --- Left-button up (stop dragging) ---
    if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_LEFT) {
        isDraggingLeftMouse = false;
        currentGizmoOp = GizmoNone;
    }

    // --- Select mode (with gizmo) ---
    if (currentEditMode == EditMode::Select) {
        if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT) {
            SDL_FPoint worldPt = g_editorCamera.screenToWorld(e.button.x, e.button.y);
            float logicalX = worldPt.x;
            float logicalY = worldPt.y;

            Entity selectedEnt = lastSelectedEntity;
            if (selectedEnt != (Entity)-1 && world.has_position[selectedEnt]) {
                float cx = world.position_pool[selectedEnt].x + (world.has_rectangle_shape[selectedEnt] ? world.rectangle_shape_pool[selectedEnt].w : 50.0f) * 0.5f;
                float cy = world.position_pool[selectedEnt].y + (world.has_rectangle_shape[selectedEnt] ? world.rectangle_shape_pool[selectedEnt].h : 50.0f) * 0.5f;
                // The gizmo hit-rects must be computed exactly where the gizmo
                // is DRAWN (render pass uses camera.worldToScreen). The old
                // canvasViewX/Y + pos - scroll formula ignored camera zoom and
                // was offset by canvasViewY, so the invisible hit-rects didn't
                // line up with the visible handles.
                SDL_FPoint gizmoPos = g_editorCamera.worldToScreen(cx, cy);
                float screenCX = gizmoPos.x;
                float screenCY = gizmoPos.y;
                GizmoHandles handles = render_transform_gizmo(renderer, screenCX, screenCY, true);
                float mx = e.button.x, my = e.button.y;
                if (mx >= handles.redHandle.x && mx <= handles.redHandle.x + handles.redHandle.w &&
                    my >= handles.redHandle.y && my <= handles.redHandle.y + handles.redHandle.h) {
                    currentGizmoOp = GizmoScaleX;
                    gizmoDrag.target = selectedEnt;
                    gizmoDrag.startMouseX = mx; gizmoDrag.startMouseY = my;
                    gizmoDrag.startValue = world.has_scale[selectedEnt] ? world.scale_pool[selectedEnt].x : 1.0f;
                    gizmoDrag.startCenterX = screenCX; gizmoDrag.startCenterY = screenCY;
                    isDraggingLeftMouse = true;
                    return;
                }
                else if (mx >= handles.greenHandle.x && mx <= handles.greenHandle.x + handles.greenHandle.w &&
                         my >= handles.greenHandle.y && my <= handles.greenHandle.y + handles.greenHandle.h) {
                    currentGizmoOp = GizmoScaleY;
                    gizmoDrag.target = selectedEnt;
                    gizmoDrag.startMouseX = mx; gizmoDrag.startMouseY = my;
                    gizmoDrag.startValue = world.has_scale[selectedEnt] ? world.scale_pool[selectedEnt].y : 1.0f;
                    gizmoDrag.startCenterX = screenCX; gizmoDrag.startCenterY = screenCY;
                    isDraggingLeftMouse = true;
                    return;
                }
                else if (mx >= handles.blueHandle.x && mx <= handles.blueHandle.x + handles.blueHandle.w &&
                         my >= handles.blueHandle.y && my <= handles.blueHandle.y + handles.blueHandle.h) {
                    currentGizmoOp = GizmoRotate;
                    gizmoDrag.target = selectedEnt;
                    gizmoDrag.startMouseX = mx; gizmoDrag.startMouseY = my;
                    gizmoDrag.startValue = world.has_rotation[selectedEnt] ? world.rotation_pool[selectedEnt].degrees : 0.0f;
                    gizmoDrag.startCenterX = screenCX; gizmoDrag.startCenterY = screenCY;
                    isDraggingLeftMouse = true;
                    return;
                }
            }

            // GUI picking — screen-space, camera-independent. Runtime input is
            // disabled in main.cpp, so the only thing you can do to a scene
            // GUI element here is select it via this box.
            Gui::IGuiElement* topmostGui = nullptr;
            for (auto& elem : guiElements) {
                if (guiHitTest(elem.get(), (float)e.button.x, (float)e.button.y)) topmostGui = elem.get();
            }

            // Entity picking — world-space through the camera (zoom-aware).
            Entity topmostEntity = (Entity)-1;
            int maxZ = 0; bool foundEntity = false;
            for (Entity i = 0; i < world.entity_count; i++) {
                if (world.has_position[i] && world.has_selection[i]) {
                    float entityW = world.has_rectangle_shape[i] ? world.rectangle_shape_pool[i].w : 50.0f;
                    float entityH = world.has_rectangle_shape[i] ? world.rectangle_shape_pool[i].h : 50.0f;
                    if (logicalX >= world.position_pool[i].x && logicalX <= world.position_pool[i].x + entityW &&
                        logicalY >= world.position_pool[i].y && logicalY <= world.position_pool[i].y + entityH) {
                        int currentZ = world.has_z_index[i] ? world.z_index_pool[i].z : 0;
                        if (!foundEntity || currentZ > maxZ) { maxZ = currentZ; topmostEntity = i; foundEntity = true; }
                    }
                }
            }

            if (topmostGui) {
                if (selectedGuiElem) selectedGuiElem->editorSelected = false;
                selectedGuiElem = topmostGui; selectedGuiElem->editorSelected = true;
                deselect_all(world); lastSelectedEntity = (Entity)-1;
            } else if (foundEntity) {
                if (selectedGuiElem) { selectedGuiElem->editorSelected = false; selectedGuiElem = nullptr; }
                if (currentSelectionmode == SelectionMode::SingleSelect) {
                    bool wasSelected = world.selection_pool[topmostEntity].isSelected;
                    for (Entity i = 0; i < world.entity_count; i++) if (world.has_selection[i]) world.selection_pool[i].isSelected = false;
                    if (!wasSelected) { world.selection_pool[topmostEntity].isSelected = true; lastSelectedEntity = topmostEntity; }
                    else lastSelectedEntity = (Entity)-1;
                } else if (currentSelectionmode == SelectionMode::MultiSelect) {
                    world.selection_pool[topmostEntity].isSelected = !world.selection_pool[topmostEntity].isSelected;
                    if (world.selection_pool[topmostEntity].isSelected) lastSelectedEntity = topmostEntity; else lastSelectedEntity = (Entity)-1;
                }
            } else {
                bool insideCanvas = (e.button.y >= canvasViewY) && (e.button.x >= canvasViewX) && (e.button.x <= canvasViewX + canvasViewW);
                if (insideCanvas) {
                    if (selectedGuiElem) { selectedGuiElem->editorSelected = false; selectedGuiElem = nullptr; }
                    if (currentSelectionmode == SelectionMode::SingleSelect) { deselect_all(world); lastSelectedEntity = (Entity)-1; }
                }
            }
        }
        // Gizmo drag (motion)
        if (e.type == SDL_EVENT_MOUSE_MOTION && isDraggingLeftMouse && currentGizmoOp != GizmoNone) {
            Entity ent = gizmoDrag.target;
            if (ent != (Entity)-1 && world.has_position[ent]) {
                float dx = e.motion.x - gizmoDrag.startMouseX;
                float dy = e.motion.y - gizmoDrag.startMouseY;
                if (currentGizmoOp == GizmoScaleX) {
                    float newScale = gizmoDrag.startValue + dx * 0.02f;
                    if (newScale < 0.01f) newScale = 0.01f;
                    if (world.has_scale[ent]) world.scale_pool[ent].x = newScale;
                    gizmoDrag.startValue = newScale;
                    gizmoDrag.startMouseX = e.motion.x; gizmoDrag.startMouseY = e.motion.y;
                } else if (currentGizmoOp == GizmoScaleY) {
                    float newScale = gizmoDrag.startValue + dy * 0.02f;
                    if (newScale < 0.01f) newScale = 0.01f;
                    if (world.has_scale[ent]) world.scale_pool[ent].y = newScale;
                    gizmoDrag.startValue = newScale;
                    gizmoDrag.startMouseX = e.motion.x; gizmoDrag.startMouseY = e.motion.y;
                } else if (currentGizmoOp == GizmoRotate) {
                    float startAngle = atan2(gizmoDrag.startMouseY - gizmoDrag.startCenterY,
                                             gizmoDrag.startMouseX - gizmoDrag.startCenterX);
                    float currentAngle = atan2(e.motion.y - gizmoDrag.startCenterY,
                                               e.motion.x - gizmoDrag.startCenterX);
                    float deltaDeg = (currentAngle - startAngle) * 180.0f / (float)M_PI;
                    float newDeg = gizmoDrag.startValue + deltaDeg;
                    if (world.has_rotation[ent]) world.rotation_pool[ent].degrees = newDeg;
                    gizmoDrag.startValue = newDeg;
                    gizmoDrag.startMouseX = e.motion.x; gizmoDrag.startMouseY = e.motion.y;
                }
            }
        }
        if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_RIGHT) {
            Entity ent = lastSelectedEntity;
            if (ent != (Entity)-1 && world.has_tilemap[ent] && world.has_position[ent]) {
                auto& tm = world.tilemap_pool[ent];
                float mx = (float)e.button.x, my = (float)e.button.y;
                // Paint anywhere on the canvas -- painting no longer requires
                // a RectangleShape component, and the cell is always
                // computed by snapping to the entity's own cellWidth/
                // cellHeight (which mirrors the loaded image's pixel size),
                // regardless of whether editor_showGrid is currently on.
                if (isInsideCanvas(mx, my) && tm.cellWidth > 0.0f && tm.cellHeight > 0.0f) {
                    SDL_FPoint worldPt = g_editorCamera.screenToWorld(mx, my);
                    float localX = worldPt.x - world.position_pool[ent].x;
                    float localY = worldPt.y - world.position_pool[ent].y;
                    if (tm.cellOffsets.size() != tm.cells.size()) tm.cellOffsets.resize(tm.cells.size(), SDL_FPoint{0.0f, 0.0f});

                    // Hit-test against each painted cell's *actual* rect
                    // (anchor cell shifted by its per-cell offset), topmost
                    // first. Using the real rect -- rather than only
                    // floor(local/cellSize) -- means this keeps working once
                    // a cell has been dragged off its original grid slot.
                    auto hitCellAt = [&](float lx, float ly) -> int {
                        for (int idx = (int)tm.cells.size() - 1; idx >= 0; --idx) {
                            float rx = tm.cells[idx].first  * tm.cellWidth  + tm.cellOffsets[idx].x;
                            float ry = tm.cells[idx].second * tm.cellHeight + tm.cellOffsets[idx].y;
                            if (lx >= rx && lx < rx + tm.cellWidth &&
                                ly >= ry && ly < ry + tm.cellHeight) {
                                return idx;
                            }
                        }
                        return -1;
                    };

                    const bool* keys = SDL_GetKeyboardState(nullptr);
                    bool sHeld = keys && keys[SDL_SCANCODE_S];

                    if (sHeld) {
                        // Hold S, then right-click-drag an existing cell to
                        // move it. Nothing is painted/erased here -- this
                        // just grabs the tile under the cursor (if any) and
                        // hands off to the SDL_EVENT_MOUSE_MOTION handler
                        // below for the actual drag.
                        int idx = hitCellAt(localX, localY);
                        if (idx != -1) {
                            float rx = tm.cells[idx].first  * tm.cellWidth  + tm.cellOffsets[idx].x;
                            float ry = tm.cells[idx].second * tm.cellHeight + tm.cellOffsets[idx].y;
                            tileCellDrag.active = true;
                            tileCellDrag.target = ent;
                            tileCellDrag.cellIndex = idx;
                            tileCellDrag.grabDX = localX - rx; // keep the tile
                            tileCellDrag.grabDY = localY - ry; // under the cursor at the point it was grabbed
                        }
                    } else {
                        // Plain right click paints. If the cell under the
                        // cursor is already painted, right-clicking it again
                        // erases it -- a single button that both places and
                        // removes tiles.
                        int idx = hitCellAt(localX, localY);
                        if (idx != -1) {
                            tm.cells.erase(tm.cells.begin() + idx);
                            tm.cellOffsets.erase(tm.cellOffsets.begin() + idx);
                        } else {
                            int col = (int)std::floor(localX / tm.cellWidth);
                            int row = (int)std::floor(localY / tm.cellHeight);
                            tm.cells.push_back({col, row});
                            tm.cellOffsets.push_back(SDL_FPoint{0.0f, 0.0f});
                        }
                    }
                }
            }
        }
        // --- Dragging a grabbed cell (hold S + right-drag) ---
        if (e.type == SDL_EVENT_MOUSE_MOTION && tileCellDrag.active) {
            Entity ent = tileCellDrag.target;
            if (ent != (Entity)-1 && world.has_tilemap[ent] && world.has_position[ent] &&
                tileCellDrag.cellIndex >= 0 &&
                tileCellDrag.cellIndex < (int)world.tilemap_pool[ent].cells.size()) {
                auto& tm = world.tilemap_pool[ent];
                int idx = tileCellDrag.cellIndex;
                SDL_FPoint worldPt = g_editorCamera.screenToWorld(e.motion.x, e.motion.y);
                float localX = worldPt.x - world.position_pool[ent].x - tileCellDrag.grabDX;
                float localY = worldPt.y - world.position_pool[ent].y - tileCellDrag.grabDY;
                if (tm.snapEnabled) {
                    // Snapped: the cell locks fully onto the grid (rounded to
                    // the nearest cell, not floored, so it snaps to whichever
                    // grid box the tile's top-left is closest to) and its
                    // offset stays zero -- only the anchor (col,row) moves.
                    int col = (int)std::floor((localX + tm.cellWidth * 0.5f) / tm.cellWidth);
                    int row = (int)std::floor((localY + tm.cellHeight * 0.5f) / tm.cellHeight);
                    tm.cells[idx] = { col, row };
                    tm.cellOffsets[idx] = SDL_FPoint{0.0f, 0.0f};
                } else {
                    // Unsnapped: free pixel placement. The tile still tracks
                    // an anchor cell (used for painting/erase hit-testing)
                    // but carries a sub-cell offset so it can sit anywhere,
                    // not just on grid lines.
                    int col = (int)std::floor(localX / tm.cellWidth);
                    int row = (int)std::floor(localY / tm.cellHeight);
                    tm.cells[idx] = { col, row };
                    tm.cellOffsets[idx] = { localX - col * tm.cellWidth, localY - row * tm.cellHeight };
                }
            }
        }
        if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_RIGHT && tileCellDrag.active) {
            tileCellDrag.active = false;
            tileCellDrag.target = (Entity)-1;
            tileCellDrag.cellIndex = -1;
        }
    }
    // --- MoveWithMouse mode ---
    else if (currentEditMode == EditMode::MoveWithMouse) {
        if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT) {
            // A drag may start either on the selected GUI element (anywhere on
            // screen — it's screen-space chrome and may sit outside the
            // canvas) or inside the canvas when entities are selected.
            bool overSelectedGui = selectedGuiElem && guiHitTest(selectedGuiElem, (float)e.button.x, (float)e.button.y);
            bool hasSelectedEntities = false;
            for (Entity i = 0; i < world.entity_count; i++) {
                if (world.has_position[i] && world.has_selection[i] && world.selection_pool[i].isSelected) {
                    hasSelectedEntities = true;
                    break;
                }
            }
            bool canDrag = overSelectedGui || (isInsideCanvas(e.button.x, e.button.y) && hasSelectedEntities);
            if (canDrag) {
                isDraggingLeftMouse = true;
                lastDragX = e.button.x;
                lastDragY = e.button.y;
            }
        } else if (e.type == SDL_EVENT_MOUSE_MOTION && isDraggingLeftMouse) {
            int dx = e.motion.x - lastDragX;
            int dy = e.motion.y - lastDragY;
            if (dx != 0 || dy != 0) {
                if (selectedGuiElem) {
                    // GUI is screen-space: moves 1:1 with the mouse, completely
                    // independent of camera pan AND zoom. No canvas clamp — it
                    // may live anywhere on screen.
                    SDL_FPoint newPos = { selectedGuiElem->getX() + dx, selectedGuiElem->getY() + dy };
                    selectedGuiElem->setPos({ static_cast<int>(newPos.x), static_cast<int>(newPos.y) });
                } else {
                    // Entities are world-space: convert the screen-space mouse
                    // delta through the camera zoom so the entity tracks the
                    // cursor at every zoom level.
                    float worldDx = dx / g_editorCamera.zoom;
                    float worldDy = dy / g_editorCamera.zoom;
                    for (Entity i = 0; i < world.entity_count; i++) {
                        if (world.has_position[i] && world.has_selection[i] && world.selection_pool[i].isSelected) {
                            world.position_pool[i].x += worldDx;
                            world.position_pool[i].y += worldDy;
                        }
                    }
                }
                lastDragX = e.motion.x;
                lastDragY = e.motion.y;
            }
        }
    }
    // --- Delete mode ---
    else if (currentEditMode == EditMode::Delete) {
        if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT) {
            // GUI first (it renders on top), in screen-space.
            Gui::IGuiElement* guiToDelete = nullptr;
            for (auto& elem : guiElements) {
                if (guiHitTest(elem.get(), (float)e.button.x, (float)e.button.y)) guiToDelete = elem.get();
            }
            if (guiToDelete) {
                if (selectedGuiElem == guiToDelete) selectedGuiElem = nullptr;
                guiElements.erase(std::remove_if(guiElements.begin(), guiElements.end(),
                    [guiToDelete](const std::unique_ptr<Gui::IGuiElement>& p) { return p.get() == guiToDelete; }), guiElements.end());
                return;
            }
            // Entities in world-space through the camera.
            SDL_FPoint worldPt = g_editorCamera.screenToWorld(e.button.x, e.button.y);
            float logicalX = worldPt.x;
            float logicalY = worldPt.y;
            Entity topmostEntity = (Entity)-1; int maxZ = 0; bool found = false;
            for (Entity i = 0; i < world.entity_count; i++) {
                if (world.has_position[i] && world.has_selection[i]) {
                    float entityW = world.has_rectangle_shape[i] ? world.rectangle_shape_pool[i].w : 50.0f;
                    float entityH = world.has_rectangle_shape[i] ? world.rectangle_shape_pool[i].h : 50.0f;
                    if (logicalX >= world.position_pool[i].x && logicalX <= world.position_pool[i].x + entityW &&
                        logicalY >= world.position_pool[i].y && logicalY <= world.position_pool[i].y + entityH) {
                        int currentZ = world.has_z_index[i] ? world.z_index_pool[i].z : 0;
                        if (!found || currentZ > maxZ) { maxZ = currentZ; topmostEntity = i; found = true; }
                    }
                }
            }
            if (topmostEntity != (Entity)-1) world.delete_entity(topmostEntity);
        }
    }
}

inline void edit_object_with_editor_gamepad(ECSWorld& world,
std::vector<std::unique_ptr<Gui::IGuiElement>>& guiElements,
Gui::IGuiElement*& selectedGuiElem,
float cursorX, float cursorY, bool confirmDown, bool confirmDownLastFrame)
{
    if (currentEditMode == EditMode::Dialog) return;
    if (!confirmDown) isGamepadDragging = false;

    SDL_FPoint worldPt = g_editorCamera.screenToWorld(cursorX, cursorY);
    float logicalX = worldPt.x;
    float logicalY = worldPt.y;

    Gui::IGuiElement* topmostGui = nullptr;
    for (auto& elem : guiElements) {
        float ex = elem->getX();
        float ey = elem->getY();
        float ew = elem->getWidth();
        float eh = elem->getHeight();
        if (logicalX >= ex && logicalX <= ex + ew && logicalY >= ey && logicalY <= ey + eh) {
            topmostGui = elem.get();
        }
    }

    Entity topmostEntity = (Entity)-1;
    int maxZ = -1;
    bool foundEntity = false;
    for (Entity i = 0; i < world.entity_count; i++) {
        if (world.has_position[i] && world.has_selection[i]) {
            float entityW = world.has_rectangle_shape[i] ? world.rectangle_shape_pool[i].w : 50.0f;
            float entityH = world.has_rectangle_shape[i] ? world.rectangle_shape_pool[i].h : 50.0f;
            if (logicalX >= world.position_pool[i].x && logicalX <= world.position_pool[i].x + entityW &&
                logicalY >= world.position_pool[i].y && logicalY <= world.position_pool[i].y + entityH) {
                int currentZ = world.has_z_index[i] ? world.z_index_pool[i].z : 0;
                if (!foundEntity || currentZ > maxZ) {
                    maxZ = currentZ;
                    topmostEntity = i;
                    foundEntity = true;
                }
            }
        }
    }

    if (currentEditMode == EditMode::Select) {
        if (!confirmDown && confirmDownLastFrame && !gamepadDidDrag) {
            if (topmostGui) {
                if (selectedGuiElem) selectedGuiElem->editorSelected = false;
                selectedGuiElem = topmostGui;
                selectedGuiElem->editorSelected = true;
                deselect_all(world);
                lastSelectedEntity = (Entity)-1;
            } else if (foundEntity) {
                if (selectedGuiElem) { selectedGuiElem->editorSelected = false; selectedGuiElem = nullptr; }
                if (currentSelectionmode == SelectionMode::SingleSelect) {
                    bool wasSelected = world.selection_pool[topmostEntity].isSelected;
                    for (Entity i = 0; i < world.entity_count; i++) {
                        if (world.has_selection[i]) world.selection_pool[i].isSelected = false;
                    }
                    if (!wasSelected) {
                        world.selection_pool[topmostEntity].isSelected = true;
                        lastSelectedEntity = topmostEntity;
                    } else {
                        lastSelectedEntity = (Entity)-1;
                    }
                } else if (currentSelectionmode == SelectionMode::MultiSelect) {
                    world.selection_pool[topmostEntity].isSelected = !world.selection_pool[topmostEntity].isSelected;
                    if (world.selection_pool[topmostEntity].isSelected)
                        lastSelectedEntity = topmostEntity;
                    else
                        lastSelectedEntity = (Entity)-1;
                }
            } else {
                bool insideCanvas = (cursorY >= canvasViewY) && 
                                    (cursorX >= canvasViewX) && 
                                    (cursorX <= canvasViewX + canvasViewW);
                if (insideCanvas) {
                    if (selectedGuiElem) { selectedGuiElem->editorSelected = false; selectedGuiElem = nullptr; }
                    if (currentSelectionmode == SelectionMode::SingleSelect) {
                        deselect_all(world);
                        lastSelectedEntity = (Entity)-1;
                    }
                }
            }
        }
        if (!confirmDown) gamepadDidDrag = false;
    } else if (currentEditMode == EditMode::MoveWithMouse) {
        if (confirmDown && !confirmDownLastFrame) {
            if (isInsideCanvas(cursorX, cursorY)) {
                isGamepadDragging = true;
                gamepadDidDrag = false;
                lastGamepadCursorX = cursorX;
                lastGamepadCursorY = cursorY;
            }
        } else if (confirmDown && isGamepadDragging) {
            if (isInsideCanvas(cursorX, cursorY)) {
                float dx = cursorX - lastGamepadCursorX;
                float dy = cursorY - lastGamepadCursorY;
                if (dx != 0.0f || dy != 0.0f) {
                    // Same camera-aware conversion as the mouse path: cursor
                    // movement is screen-space, entity positions are
                    // world-space, so divide by zoom. No more fixed-rectangle
                    // clamp -- entities can move anywhere in the world.
                    float worldDx = dx / g_editorCamera.zoom;
                    float worldDy = dy / g_editorCamera.zoom;
                    for (Entity i = 0; i < world.entity_count; i++) {
                        if (world.has_position[i] && world.has_selection[i] && world.selection_pool[i].isSelected) {
                            world.position_pool[i].x += worldDx;
                            world.position_pool[i].y += worldDy;
                        }
                    }
                    if (selectedGuiElem) {
                        SDL_FPoint newPos = { selectedGuiElem->getX() + dx, selectedGuiElem->getY() + dy };
                        clamp_guiElem_position_to_canvas(newPos, selectedGuiElem->getWidth(), selectedGuiElem->getHeight());
                        selectedGuiElem->setPos({ static_cast<int>(newPos.x), static_cast<int>(newPos.y) });
                    }
                    gamepadDidDrag = true;
                    lastGamepadCursorX = cursorX;
                    lastGamepadCursorY = cursorY;
                }
            } else {
                isGamepadDragging = false;
            }
        }
    } else if (currentEditMode == EditMode::Delete) {
        if (!confirmDown && confirmDownLastFrame) {
            if (topmostGui) {
                if (selectedGuiElem == topmostGui) selectedGuiElem = nullptr;
                guiElements.erase(std::remove_if(guiElements.begin(), guiElements.end(),
                    [topmostGui](const std::unique_ptr<Gui::IGuiElement>& p) { return p.get() == topmostGui; }),
                    guiElements.end());
            } else if (topmostEntity != (Entity)-1) {
                world.delete_entity(topmostEntity);
            }
        }
    }
}

inline void render_editor_canvas(SDL_Renderer* renderer) {
    SDL_FRect rect = { canvasViewX, canvasViewY, canvasViewW, canvasViewH };
    SDL_SetRenderDrawColor(renderer, 125, 125, 125, 255);
    SDL_RenderRect(renderer, &rect);
}

// ============================================================
// ScriptBase — every scene must reference a script that derives
// from this class.  onStart/onUpdate/onDraw/onEnd are called by
// the runtime; the editor validates that the attachment exists.
// ============================================================
class ScriptBase {
public:
    virtual ~ScriptBase() = default;
    virtual void onStart(){}
    virtual void onUpdate(float /*dt*/){}
    virtual void onDraw(){}
    virtual void onEnd(){}
    virtual void onEvent(const SDL_Event& /*e*/){}
    virtual std::string getName() const { return "ScriptBase"; }
    virtual void setContext(void* /*context*/) {}
};

// ============================================================
// ScriptRegistry — enables *real* multi-script dispatch: every Scene
// resolves and owns its own ScriptBase subclass instance (looked up by
// name), instead of the whole runtime being hard-wired to one globally
// shared, concrete script type. A project registers each of its script
// classes once (typically via the REGISTER_SCRIPT macro below, at
// namespace scope in the .cpp implementing the class); SceneParser then
// looks up the right factory for whatever script_attached names in the
// scene's JSON and instantiates it polymorphically.
// ============================================================
using ScriptFactory = std::function<std::unique_ptr<ScriptBase>()>;

class ScriptRegistry {
public:
    static ScriptRegistry& instance() {
        static ScriptRegistry reg;
        return reg;
    }

    // Registers `factory` under `name`. Re-registering the same name
    // simply overwrites the previous factory (useful for hot-reload /
    // editor re-registration scenarios).
    void registerScript(const std::string& name, ScriptFactory factory) {
        factories[name] = std::move(factory);
    }

    bool has(const std::string& name) const {
        return factories.find(name) != factories.end();
    }

    // Instantiates a fresh ScriptBase-derived object for `name`, or
    // returns nullptr if nothing is registered under that name (e.g. a
    // typo in script_attached, or a script class that forgot to
    // REGISTER_SCRIPT itself).
    std::unique_ptr<ScriptBase> create(const std::string& name) const {
        auto it = factories.find(name);
        if (it == factories.end()) return nullptr;
        return it->second();
    }

    // Mostly useful for editor tooling/diagnostics (e.g. populating a
    // dropdown of known script classes when authoring a scene).
    std::vector<std::string> names() const {
        std::vector<std::string> out;
        out.reserve(factories.size());
        for (const auto& kv : factories) out.push_back(kv.first);
        return out;
    }

private:
    std::unordered_map<std::string, ScriptFactory> factories;
};

// Self-registers `ClassName` under `nameStr` the first time this
// translation unit is loaded, via a static initializer. Place at
// namespace (file) scope in the .cpp that implements the script:
//
//     REGISTER_SCRIPT(GameScript, "GameScript")
//
// `nameStr` is the key SceneParser looks scripts up by; by convention
// this is the stem of the script_attached path stored in the scene's
// JSON (e.g. script_attached = "OpenWorld/scripts/GameScript.cpp" ->
// key "GameScript"), so no extra per-scene registry wiring is needed —
// dropping a new "Foo.cpp" into a scene's script_attached field just
// works as long as some translation unit did REGISTER_SCRIPT(Foo, "Foo").
#define REGISTER_SCRIPT(ClassName, nameStr)                                  \
    namespace {                                                              \
        struct ClassName##_ScriptRegistrar {                                 \
            ClassName##_ScriptRegistrar() {                                  \
                ScriptRegistry::instance().registerScript(                   \
                    (nameStr),                                               \
                    []() -> std::unique_ptr<ScriptBase> {                    \
                        return std::make_unique<ClassName>();                \
                    });                                                      \
            }                                                                \
        };                                                                   \
        static ClassName##_ScriptRegistrar ClassName##_scriptRegistrarInstance; \
    }

// ============================================================
// Scene and SceneParser — defined here so all Gui types are
// fully available (Button, LineEdit, SpinBox, Panel, IGuiElement).
// ============================================================
struct Scene {
    std::string name = "Untitled Scene";
    std::string scriptAttached;
    bool        scriptValid = false;
    std::string projectRoot;          // absolute path to the project folder
    ECSWorld world;
    std::vector<std::unique_ptr<Gui::IGuiElement>> guiElements;

    // Set true the first time the attached script has generated its biomes
    // and decoration entities for this scene. Persisted to the scene JSON
    // so a later load knows generation already happened and doesn't
    // duplicate the work (e.g. re-spawning decorations on top of the ones
    // already saved in the scene).
    bool areBiomesAndEntitiesGenerated = false;

    // --- Real multi-script support ---
    // The registry key resolved from scriptAttached (by convention, the
    // filename stem — see resolveScriptClassName() below), and the actual
    // polymorphic script instance created for it via ScriptRegistry. Each
    // Scene owns and drives its own script instance, so different scenes
    // in the same project can run entirely different ScriptBase
    // subclasses without the runtime knowing or caring which one it is.
    std::string scriptClassName;
    std::unique_ptr<ScriptBase> script;
};

// By convention, the registry key for a scene's script is the filename
// stem of its script_attached path (e.g. "OpenWorld/scripts/GameScript.cpp"
// -> "GameScript"), matching REGISTER_SCRIPT(GameScript, "GameScript").
inline std::string resolveScriptClassName(const std::string& scriptAttached) {
    if (scriptAttached.empty()) return "";
    return std::filesystem::path(scriptAttached).stem().string();
}

// Shared by both SceneParser::loadFromFile() and
// SceneParser::ProjectScript_loadFromFile(): once scene.scriptAttached has
// been read and validated, resolve its registry key and instantiate the
// matching ScriptBase subclass polymorphically. Leaves scene.script null
// (with a diagnostic) if the scene has no valid script attachment, or if
// script_attached doesn't match anything a translation unit registered via
// REGISTER_SCRIPT — callers (main(), change_scene()) are expected to check
// for null and degrade gracefully rather than assume a script exists.
inline void instantiateScriptForScene(Scene& scene) {
    if (!scene.scriptValid || scene.scriptAttached.empty()) return;

    scene.scriptClassName = resolveScriptClassName(scene.scriptAttached);
    scene.script = ScriptRegistry::instance().create(scene.scriptClassName);

    if (!scene.script) {
        std::cerr << "[SceneParser] WARNING: no script registered under '"
                   << scene.scriptClassName << "' (from script_attached '"
                   << scene.scriptAttached
                   << "') — did you forget REGISTER_SCRIPT(" 
                   << (scene.scriptClassName.empty() ? "YourClass" : scene.scriptClassName)
                   << ", \"" << scene.scriptClassName << "\")? Scene '"
                   << scene.name << "' will run with no active script.\n";
    } else {
        std::cout << "[SceneParser] Scene '" << scene.name << "' — instantiated script '"
                   << scene.scriptClassName << "'.\n";
    }
}



inline bool ProjectScript_TTF_LoadFont(const char* name, const char* file, float pt_size)
{
    return g_resources.FontManager.Load(name, (getProjectsRootForScripts() / std::filesystem::path(file)).string(), pt_size);
}

inline TTF_Font* ProjectScript_TTF_GetFont(const char* name)
{
    return g_resources.FontManager.Get(name);
}

inline void ProjectScript_TTF_Clear()
{
    g_resources.FontManager.Clear();
}

inline bool ProjectScript_IMG_LoadTexture(const char* name, const char* file)
{
    return g_resources.TextureManager.Load(name, (getProjectsRootForScripts() / std::filesystem::path(file)).string());
}

inline SDL_Texture* ProjectScript_IMG_GetTexture(const char* name)
{
    return g_resources.TextureManager.Get(name);
}

inline void ProjectScript_IMG_Clear()
{
    g_resources.TextureManager.Clear();
}


inline void ProjectScript_MIXER_LoadSound(const char* name, const char* file, bool stream)
{
    // use steam = true, if it is long/background music
    return g_resources.AudioManager.Load(name, (getProjectsRootForScripts() / std::filesystem::path(file)).string(), stream);
}

inline void ProjectScript_MIXER_PlaySound(const std::string &name, float volume = (1.0F), float pitch = (1.0F))
{
    return g_resources.AudioManager.PlaySfx(name, volume, pitch);
}

inline void ProjectScript_MIXER_PlayLoopingSound(const std::string &name, float volume = (1.0F), float pitch = (1.0F))
{
     return g_resources.AudioManager.PlayLoopingSfx(name, volume, pitch);
}

inline void ProjectScript_MIXER_StopSound(const std::string &name)
{
    return g_resources.AudioManager.StopSfx(name);
}

inline void ProjectScript_MIXER_PlayHit(const std::string& name, float impulse, float pitch = 1.0f)
{
    return g_resources.AudioManager.PlayHit(name, impulse, pitch);
}


inline void ProjectScript_MIXER_Clear()
{
    g_resources.AudioManager.Clear();
}


inline Entity findEntityByName(Scene* scene, const std::string& name) {
    if (!scene) return (Entity)-1;
    ECSWorld& world = scene->world;
    for (Entity i = 0; i < world.entity_count; ++i) {
        if (world.has_metadata[i] && world.metadata_pool[i].name == name) return i;
    }
    return (Entity)-1;
}

class SceneParser {
private:
    SDL_Renderer* renderer;
    TTF_TextEngine* textEngine;
    TTF_Font* font;
    SDL_Window* window;

public:
    SceneParser(SDL_Renderer* r, TTF_TextEngine* te, TTF_Font* f, SDL_Window* w)
        : renderer(r), textEngine(te), font(f), window(w) {}

    std::string engine_root = std::filesystem::absolute(getEnginePath()).string();
    std::string projects_root = getProjectsPath();

    // ==========================================
    // LOAD SCENE
    // ==========================================
    Scene loadFromFile(const std::string& filepath) {
        Scene scene;
        
        std::string fullPath = filepath;
        #ifdef EMSCRIPTEN
            // On Emscripten, use the path as-is (it's already in virtual FS format)
            if (!fullPath.empty() && fullPath[0] != '/') {
                fullPath = "/" + fullPath;
            }
        #else
            // On native platforms, if the path is not absolute, prepend engine_root.
            // Absolute paths (e.g., C:\... or /...) are used directly.
            if (!fullPath.empty()) {
                bool isAbsolute = false;
        #ifdef _WIN32
                // Windows absolute path: drive letter or starts with \ or /
                if (fullPath.size() > 1 && fullPath[1] == ':') isAbsolute = true;
                if (fullPath[0] == '\\' || fullPath[0] == '/') isAbsolute = true;
        #else
                // Unix absolute path: starts with /
                if (fullPath[0] == '/') isAbsolute = true;
        #endif
                if (!isAbsolute) {
                    fullPath = engine_root + fullPath;
                }
            }
        #endif
                    
        std::ifstream file(fullPath);
        if (!file.is_open()) {
            SDL_Log("Current Working Directory: %s\n", std::filesystem::current_path().string().c_str());
            SDL_Log("Failed to open scene file: %s (tried: %s)\n", filepath.c_str(), fullPath.c_str());
            scene.scriptValid = false;
            return scene;
        }

        std::filesystem::path scenePath(fullPath);
        scene.projectRoot = scenePath.parent_path().parent_path().string();

        nlohmann::json j;
        file >> j;

        scene.name = j.value("scene_name", "Untitled");
        scene.areBiomesAndEntitiesGenerated = j.value("are_biomes_and_entities_generated", false);

        // --- 0. Script attachment (required) ---
        scene.scriptAttached = j.value("script_attached", "");
        if (scene.scriptAttached.empty()) {
            std::cerr << "[Editor] WARNING: Scene '" << scene.name
                    << "' has no script_attached — scene marked INVALID.\n";
            scene.scriptValid = false;
        } else {
        #ifdef EMSCRIPTEN
            // On Emscripten, check in virtual FS
            std::string trueScriptAttached = scene.scriptAttached;
            if (!trueScriptAttached.empty() && trueScriptAttached[0] != '/') {
                trueScriptAttached = "/projects/" + trueScriptAttached;
            }
        #else
            // On native, use projects_root
            std::string trueScriptAttached = projects_root + scene.scriptAttached;
        #endif
            scene.scriptValid = std::filesystem::exists(trueScriptAttached);
            if (!scene.scriptValid)
                std::cerr << "[Editor] WARNING: script_attached '"
                        << scene.scriptAttached
                        << "' not found on disk (tried: " << trueScriptAttached << ") — scene marked INVALID.\n";
            else
                std::cout << "[Editor] Scene '" << scene.name
                        << "' — script '" << scene.scriptAttached << "' OK.\n";
        }
        instantiateScriptForScene(scene);

        // --- 1. Parse ECS Entities ---
        if (j.contains("entities")) {
            for (const auto& entityJson : j["entities"]) {
                Entity id = scene.world.create_entity();
                scene.world.add_selection(id);
                scene.world.selection_pool[id].isSelected = false;
                scene.world.selection_pool[id].selectionColor = {0, 255, 0, 255};

                const auto& comps = entityJson["components"];

                if (comps.contains("Metadata")) {
                    scene.world.add_metadata(id);
                    scene.world.metadata_pool[id].name = comps["Metadata"].value("name", "");
                }
                if (comps.contains("Position")) {
                    scene.world.add_position(id);
                    scene.world.position_pool[id].x = comps["Position"].value("x", 0.0f);
                    scene.world.position_pool[id].y = comps["Position"].value("y", 0.0f);
                }
                if (comps.contains("RectangleShape")) {
                    scene.world.add_rectangle_shape(id);
                    scene.world.rectangle_shape_pool[id].w = comps["RectangleShape"].value("w", 50.0f);
                    scene.world.rectangle_shape_pool[id].h = comps["RectangleShape"].value("h", 50.0f);
                }
                if (comps.contains("ZIndex")) {
                    scene.world.add_z_index(id);
                    scene.world.z_index_pool[id].z = comps["ZIndex"].value("z", 0);
                }
                if (comps.contains("Rotation")) {
                    scene.world.add_rotation(id);
                    scene.world.rotation_pool[id].degrees = comps["Rotation"].value("degrees", 0.0f);
                }
                if (comps.contains("Scale")) {
                    scene.world.add_scale(id);
                    scene.world.scale_pool[id].x = comps["Scale"].value("x", 1.0f);
                    scene.world.scale_pool[id].y = comps["Scale"].value("y", 1.0f);
                }
                // --- NEW: TextureRef ---
                if (comps.contains("TextureRef")) {
                    scene.world.add_texture_ref(id);
                    auto& tex = scene.world.texture_ref_pool[id];
                    tex.resourceName = resolveComponentResourcePath(scene.projectRoot, comps["TextureRef"].value("resourceName", ""));
                }
                // --- NEW: SfxEmitter ---
                if (comps.contains("SfxEmitter")) {
                    scene.world.add_sfx_emitter(id);
                    auto& sfx = scene.world.sfx_emitter_pool[id];
                    sfx.sfxName = comps["SfxEmitter"].value("sfxName", "");
                    sfx.volume = comps["SfxEmitter"].value("volume", 1.0f);
                    sfx.pitch = comps["SfxEmitter"].value("pitch", 1.0f);
                    sfx.speed = comps["SfxEmitter"].value("speed", 1.0f);
                    sfx.playOnCollision = comps["SfxEmitter"].value("playOnCollision", false);
                }
                // --- PhysicsBody ---
                // New format: a plain string filename pointing at a shared
                // .physicsres resource. assign_physics_resource() reuses the
                // cached shared_ptr if some earlier entity in this same load
                // already pulled in that exact file, so the file is only
                // actually read + parsed once no matter how many entities
                // reference it.
                if (comps.contains("PhysicsBody")) {
                    scene.world.add_physics_body(id);
                    const auto& pb = comps["PhysicsBody"];
                    if (pb.is_string()) {
                        std::string resPath = resolveComponentResourcePath(scene.projectRoot, pb.get<std::string>());
                        try {
                            scene.world.assign_physics_resource(id, resPath);
                        } catch (const std::exception& e) {
                            std::cerr << "[SceneParser] " << e.what() << "\n";
                        }
                    } else {
                        // Legacy format: full inline PhysicsBody JSON, kept so
                        // scene files saved before .physicsres existed still load.
                        auto& phys = scene.world.physics_body_pool[id];
                        phys.shapeType = (Physics::ShapeType)pb.value("shapeType", (int)Physics::ShapeType::Rectangle);
                        phys.bodyType  = (b2BodyType)pb.value("bodyType", (int)b2_dynamicBody);
                        phys.width     = pb.value("width", 50.0f);
                        phys.height    = pb.value("height", 50.0f);
                        phys.radius    = pb.value("radius", 25.0f);
                        phys.density   = pb.value("density", 1.0f);
                        phys.isSensor  = pb.value("isSensor", false);
                        phys.category  = pb.value("category", (uint16_t)Physics::LAYER_1);
                        phys.mask      = pb.value("mask", (uint16_t)Physics::LAYER_ALL);
                        phys.polygonPoints.clear();
                        if (pb.contains("polygonPoints") && pb["polygonPoints"].is_array()) {
                            for (const auto& pt : pb["polygonPoints"])
                                phys.polygonPoints.push_back({pt.value("x", 0.0f), pt.value("y", 0.0f)});
                        }
                    }
                    // bodyId remains null – will be re-created by physics_sync_system
                }
                // --- AnimationState ---
                // Same idea: a plain string filename means a shared .animres
                // resource; assign_animation_resource() dedupes the load
                // across entities. An inline object is still accepted for
                // backward compatibility with older scene files.
                if (comps.contains("AnimationState")) {
                    if (!scene.world.has_animation_state[id]) scene.world.add_animation_state(id);
                    const auto& as = comps["AnimationState"];
                    if (as.is_string()) {
                        std::string resPath = resolveComponentResourcePath(scene.projectRoot, as.get<std::string>());
                        try {
                            scene.world.assign_animation_resource(id, resPath, scene.projectRoot);
                        } catch (const std::exception& e) {
                            std::cerr << "[SceneParser] " << e.what() << "\n";
                        }
                    } else if (as.contains("clips") && as["clips"].is_array()) {
                        auto& anim = scene.world.animation_state_pool[id];
                        anim.clips.clear();
                        for (const auto& clipJson : as["clips"]) {
                            Components::AnimationClip clip;
                            clip.name = clipJson.value("name", "default");
                            clip.speed = clipJson.value("speed", 10.0f);
                            clip.isPlaying = clipJson.value("isPlaying", true);
                            clip.loop = clipJson.value("loop", true);
                            clip.imageFrameResources = clipJson.value("imageFrames", std::vector<std::string>());
                            clip.frameWidth = clipJson.value("frameWidth", 0.0f);
                            clip.frameHeight = clipJson.value("frameHeight", 0.0f);
                            anim.clips.push_back(std::move(clip));
                        }
                        anim.activeClipIndex = as.value("activeClipIndex", 0);
                        if (anim.activeClipIndex < 0 || anim.activeClipIndex >= (int)anim.clips.size())
                            anim.activeClipIndex = 0;
                        resolveAnimationFramePaths(anim, scene.projectRoot);
                    }
                }

                if (comps.contains("TileMap")) {
                    scene.world.add_tilemap(id);
                    auto& tm = scene.world.tilemap_pool[id];
                    const auto& jtm = comps["TileMap"];
                    
                    // Resolve the path so the TextureManager can actually find it
                    
                    tm.textureName = resolveComponentResourcePath(scene.projectRoot, jtm.value("textureName", ""));
                    tm.cellWidth = jtm.value("cellWidth", 32.0f);
                    tm.cellHeight = jtm.value("cellHeight", 32.0f);
                    tm.snapEnabled = jtm.value("snapEnabled", true);
                    
                    if (jtm.contains("cells") && jtm["cells"].is_array()) {
                        for (const auto& c : jtm["cells"]) {
                            int col = c[0].get<int>();
                            int row = c[1].get<int>();
                            tm.cells.push_back({col, row});
                        }
                    }
                    tm.cellOffsets.assign(tm.cells.size(), SDL_FPoint{0.0f, 0.0f});
                    if (jtm.contains("cellOffsets") && jtm["cellOffsets"].is_array()) {
                        const auto& offs = jtm["cellOffsets"];
                        for (size_t k = 0; k < offs.size() && k < tm.cellOffsets.size(); ++k) {
                            tm.cellOffsets[k].x = offs[k][0].get<float>();
                            tm.cellOffsets[k].y = offs[k][1].get<float>();
                        }
                    }
                }
            }
        }

        // --- 2. Parse GUI Elements (top-level and Panels) ---
        if (j.contains("gui_elements")) {
            for (const auto& elemJson : j["gui_elements"]) {
                auto elem = parseGuiElement(elemJson);
                if (elem) scene.guiElements.push_back(std::move(elem));
            }
        }
        return scene;
    }

    Scene ProjectScript_loadFromFile(const std::string& filepath) {
        Scene scene;
        
    #ifdef __EMSCRIPTEN__
        // On Emscripten, prepend / to make it absolute path
        std::string fullPath = filepath;
        if (!fullPath.empty() && fullPath[0] != '/') {
            fullPath = "/" + fullPath;
        }
    #else
        // On native, use the projects root
        std::string fullPath = (getProjectsRootForScripts() / std::filesystem::path(filepath)).string();
    #endif
        
        std::ifstream file(fullPath);
        if (!file.is_open()) {
            SDL_Log("Current Working Directory: %s\n", std::filesystem::current_path().string().c_str());
            SDL_Log("Failed to open scene file: %s (tried: %s)\n", filepath.c_str(), fullPath.c_str());
            scene.scriptValid = false;
            return scene;
        }

        std::filesystem::path scenePath(fullPath);
        scene.projectRoot = scenePath.parent_path().parent_path().string();

        nlohmann::json j;
        file >> j;

        scene.name = j.value("scene_name", "Untitled");
        scene.areBiomesAndEntitiesGenerated = j.value("are_biomes_and_entities_generated", false);

        // --- 0. Script attachment (required) ---
        scene.scriptAttached = j.value("script_attached", "");
        if (scene.scriptAttached.empty()) {
            std::cerr << "[Editor] WARNING: Scene '" << scene.name
                    << "' has no script_attached — scene marked INVALID.\n";
            scene.scriptValid = false;
        } else {
    #ifdef __EMSCRIPTEN__
            // On Emscripten, we can't check for .cpp files at runtime since they're transpiled in.
            // Just mark as valid if a script is attached.
            scene.scriptValid = true;
            std::cout << "[Editor] Scene '" << scene.name
                    << "' — script '" << scene.scriptAttached << "' OK (transpiled in).\n";
    #else
            // On native, check that the referenced script file actually exists
            std::string trueScriptAttached = (getProjectsRootForScripts() / std::filesystem::path(scene.scriptAttached)).string();
            scene.scriptValid = std::filesystem::exists(trueScriptAttached);
            if (!scene.scriptValid)
                std::cerr << "[Editor] WARNING: script_attached '"
                        << scene.scriptAttached
                        << "' not found (tried: " << trueScriptAttached << ") — scene marked INVALID.\n";
            else
                std::cout << "[Editor] Scene '" << scene.name
                        << "' — script '" << scene.scriptAttached << "' OK.\n";
    #endif
        }
        instantiateScriptForScene(scene);

        // --- 1. Parse ECS Entities ---
        if (j.contains("entities")) {
            for (const auto& entityJson : j["entities"]) {
                Entity id = scene.world.create_entity();
                scene.world.add_selection(id);
                scene.world.selection_pool[id].isSelected = false;
                scene.world.selection_pool[id].selectionColor = {0, 255, 0, 255};

                const auto& comps = entityJson["components"];

                if (comps.contains("Metadata")) {
                    scene.world.add_metadata(id);
                    scene.world.metadata_pool[id].name = comps["Metadata"].value("name", "");
                }
                if (comps.contains("Position")) {
                    scene.world.add_position(id);
                    scene.world.position_pool[id].x = comps["Position"].value("x", 0.0f);
                    scene.world.position_pool[id].y = comps["Position"].value("y", 0.0f);
                }
                if (comps.contains("RectangleShape")) {
                    scene.world.add_rectangle_shape(id);
                    scene.world.rectangle_shape_pool[id].w = comps["RectangleShape"].value("w", 50.0f);
                    scene.world.rectangle_shape_pool[id].h = comps["RectangleShape"].value("h", 50.0f);
                }
                if (comps.contains("ZIndex")) {
                    scene.world.add_z_index(id);
                    scene.world.z_index_pool[id].z = comps["ZIndex"].value("z", 0);
                }
                if (comps.contains("Rotation")) {
                    scene.world.add_rotation(id);
                    scene.world.rotation_pool[id].degrees = comps["Rotation"].value("degrees", 0.0f);
                }
                if (comps.contains("Scale")) {
                    scene.world.add_scale(id);
                    scene.world.scale_pool[id].x = comps["Scale"].value("x", 1.0f);
                    scene.world.scale_pool[id].y = comps["Scale"].value("y", 1.0f);
                }
                // --- NEW: TextureRef ---
                if (comps.contains("TextureRef")) {
                    scene.world.add_texture_ref(id);
                    auto& tex = scene.world.texture_ref_pool[id];
                    tex.resourceName = resolveComponentResourcePath(scene.projectRoot, comps["TextureRef"].value("resourceName", ""));
                }
                // --- NEW: SfxEmitter ---
                if (comps.contains("SfxEmitter")) {
                    scene.world.add_sfx_emitter(id);
                    auto& sfx = scene.world.sfx_emitter_pool[id];
                    sfx.sfxName = comps["SfxEmitter"].value("sfxName", "");
                    sfx.volume = comps["SfxEmitter"].value("volume", 1.0f);
                    sfx.pitch = comps["SfxEmitter"].value("pitch", 1.0f);
                    sfx.speed = comps["SfxEmitter"].value("speed", 1.0f);
                    sfx.playOnCollision = comps["SfxEmitter"].value("playOnCollision", false);
                }
                // --- PhysicsBody ---
                // New format: a plain string filename pointing at a shared
                // .physicsres resource (resolved against the transpiled
                // project's root, same convention used for scriptAttached
                // above). assign_physics_resource() dedupes the load across
                // entities via ComponentResourceManager.
                if (comps.contains("PhysicsBody")) {
                    scene.world.add_physics_body(id);
                    const auto& pb = comps["PhysicsBody"];
                    if (pb.is_string()) {
                        std::string resPath = resolveComponentResourcePath(scene.projectRoot, pb.get<std::string>());
                        try {
                            scene.world.assign_physics_resource(id, resPath);
                        } catch (const std::exception& e) {
                            std::cerr << "[SceneParser] " << e.what() << "\n";
                        }
                    } else {
                        // Legacy format: full inline PhysicsBody JSON, kept so
                        // scene files saved before .physicsres existed still load.
                        auto& phys = scene.world.physics_body_pool[id];
                        phys.shapeType = (Physics::ShapeType)pb.value("shapeType", (int)Physics::ShapeType::Rectangle);
                        phys.bodyType  = (b2BodyType)pb.value("bodyType", (int)b2_dynamicBody);
                        phys.width     = pb.value("width", 50.0f);
                        phys.height    = pb.value("height", 50.0f);
                        phys.radius    = pb.value("radius", 25.0f);
                        phys.density   = pb.value("density", 1.0f);
                        phys.isSensor  = pb.value("isSensor", false);
                        phys.category  = pb.value("category", (uint16_t)Physics::LAYER_1);
                        phys.mask      = pb.value("mask", (uint16_t)Physics::LAYER_ALL);
                        phys.polygonPoints.clear();
                        if (pb.contains("polygonPoints") && pb["polygonPoints"].is_array()) {
                            for (const auto& pt : pb["polygonPoints"])
                                phys.polygonPoints.push_back({pt.value("x", 0.0f), pt.value("y", 0.0f)});
                        }
                    }
                    // bodyId remains null – will be re-created by physics_sync_system
                }
                // --- AnimationState ---
                // Same idea: a plain string filename means a shared .animres
                // resource; assign_animation_resource() dedupes the load
                // across entities. An inline object is still accepted for
                // backward compatibility with older scene files.
                if (comps.contains("AnimationState")) {
                    if (!scene.world.has_animation_state[id]) scene.world.add_animation_state(id);
                    const auto& as = comps["AnimationState"];
                    if (as.is_string()) {
                        std::string resPath = resolveComponentResourcePath(scene.projectRoot, as.get<std::string>());
                        try {
                            scene.world.assign_animation_resource(id, resPath, scene.projectRoot);
                        } catch (const std::exception& e) {
                            std::cerr << "[SceneParser] " << e.what() << "\n";
                        }
                    } else if (as.contains("clips") && as["clips"].is_array()) {
                        auto& anim = scene.world.animation_state_pool[id];
                        anim.clips.clear();
                        for (const auto& clipJson : as["clips"]) {
                            Components::AnimationClip clip;
                            clip.name = clipJson.value("name", "default");
                            clip.speed = clipJson.value("speed", 10.0f);
                            clip.isPlaying = clipJson.value("isPlaying", true);
                            clip.loop = clipJson.value("loop", true);
                            clip.imageFrameResources = clipJson.value("imageFrames", std::vector<std::string>());
                            clip.frameWidth = clipJson.value("frameWidth", 0.0f);
                            clip.frameHeight = clipJson.value("frameHeight", 0.0f);
                            anim.clips.push_back(std::move(clip));
                        }
                        anim.activeClipIndex = as.value("activeClipIndex", 0);
                        if (anim.activeClipIndex < 0 || anim.activeClipIndex >= (int)anim.clips.size())
                            anim.activeClipIndex = 0;
                        resolveAnimationFramePaths(anim, scene.projectRoot);
                    }
                }

                if (comps.contains("TileMap")) {
                    scene.world.add_tilemap(id);
                    auto& tm = scene.world.tilemap_pool[id];
                    const auto& jtm = comps["TileMap"];
                    
                    // Resolve the path so the TextureManager can actually find it
                    
                    tm.textureName = resolveComponentResourcePath(scene.projectRoot, jtm.value("textureName", ""));
                    SDL_Log("%s", tm.textureName.c_str());
                    tm.cellWidth = jtm.value("cellWidth", 32.0f);
                    tm.cellHeight = jtm.value("cellHeight", 32.0f);
                    tm.snapEnabled = jtm.value("snapEnabled", true);
                    
                    if (jtm.contains("cells") && jtm["cells"].is_array()) {
                        for (const auto& c : jtm["cells"]) {
                            int col = c[0].get<int>();
                            int row = c[1].get<int>();
                            tm.cells.push_back({col, row});
                        }
                    }
                    tm.cellOffsets.assign(tm.cells.size(), SDL_FPoint{0.0f, 0.0f});
                    if (jtm.contains("cellOffsets") && jtm["cellOffsets"].is_array()) {
                        const auto& offs = jtm["cellOffsets"];
                        for (size_t k = 0; k < offs.size() && k < tm.cellOffsets.size(); ++k) {
                            tm.cellOffsets[k].x = offs[k][0].get<float>();
                            tm.cellOffsets[k].y = offs[k][1].get<float>();
                        }
                    }
                }
            }
        }

        // --- 2. Parse GUI Elements (top-level and Panels) ---
        if (j.contains("gui_elements")) {
            for (const auto& elemJson : j["gui_elements"]) {
                auto elem = parseGuiElement(elemJson);
                if (elem) scene.guiElements.push_back(std::move(elem));
            }
        }
        return scene;
    }

    // ==========================================
    // SAVE SCENE
    // ==========================================
    void saveToFile(Scene& scene, const std::string& filepath) {
        // Fresh de-dupe pass: each unique .animres/.physicsres path this
        // scene references gets written to disk at most once below, no
        // matter how many entities share it.
        g_componentResources.beginSavePass();

        if (scene.projectRoot.empty())
        {
            std::filesystem::path scenePath(filepath);
            scene.projectRoot = scenePath.parent_path().parent_path().string();
        }

        nlohmann::json j;
        j["scene_name"]      = scene.name;
        j["script_attached"] = scene.scriptAttached;
        j["are_biomes_and_entities_generated"] = scene.areBiomesAndEntitiesGenerated;

        // --- 1. Save ECS Entities ---
        nlohmann::json entitiesJson = nlohmann::json::array();
        for (Entity i = 0; i < scene.world.entity_count; ++i) {
            nlohmann::json comps;

            if (scene.world.has_metadata[i])
                comps["Metadata"]["name"] = scene.world.metadata_pool[i].name;

            if (scene.world.has_position[i]) {
                comps["Position"]["x"] = scene.world.position_pool[i].x;
                comps["Position"]["y"] = scene.world.position_pool[i].y;
            }

            if (scene.world.has_rectangle_shape[i]) {
                comps["RectangleShape"]["w"] = scene.world.rectangle_shape_pool[i].w;
                comps["RectangleShape"]["h"] = scene.world.rectangle_shape_pool[i].h;
            }

            if (scene.world.has_z_index[i]) {
                comps["ZIndex"]["z"] = scene.world.z_index_pool[i].z;
            }

            if (scene.world.has_rotation[i]) {
                comps["Rotation"]["degrees"] = scene.world.rotation_pool[i].degrees;
            }

            if (scene.world.has_scale[i]) {
                comps["Scale"]["x"] = scene.world.scale_pool[i].x;
                comps["Scale"]["y"] = scene.world.scale_pool[i].y;
            }

            if (scene.world.has_texture_ref[i]) {
                auto& tex = scene.world.texture_ref_pool[i];
                std::string relTex = std::filesystem::relative(tex.resourceName, scene.projectRoot).string();
                if (relTex.empty()) relTex = tex.resourceName; // fallback: keep absolute if relative() fails
                comps["TextureRef"]["resourceName"] = relTex;
            }

            if (scene.world.has_sfx_emitter[i]) {
                auto& sfx = scene.world.sfx_emitter_pool[i];
                comps["SfxEmitter"]["sfxName"] = sfx.sfxName;
                comps["SfxEmitter"]["volume"] = sfx.volume;
                comps["SfxEmitter"]["pitch"] = sfx.pitch;
                comps["SfxEmitter"]["speed"] = sfx.speed;
                comps["SfxEmitter"]["playOnCollision"] = sfx.playOnCollision;
            }

            if (scene.world.has_animation_state[i]) {
                auto& anim = scene.world.animation_state_pool[i];
                // relative to project root
                std::string& resPath = scene.world.animation_resource_path[i];
                if (resPath.empty()) {
                    // Never been saved to a resource file before (e.g. the
                    // component was just added in-editor). Give it one of
                    // its own; the path is stored back onto the entity so
                    // every later save reuses this same file instead of
                    // generating a new one each time.
                    std::string label = scene.world.has_metadata[i] ? scene.world.metadata_pool[i].name : "";
                    if (label.empty()) label = "entity";
                    resPath = "resources/" + label + "_" + std::to_string(i) + ".animres";
                }
                std::string fullResPath = (std::filesystem::path(scene.projectRoot) / resPath).string();
                std::filesystem::create_directories(std::filesystem::path(fullResPath).parent_path());
                // Writes to disk only once per unique path per save pass —
                // if another entity already saved this exact file this
                // pass, this is a no-op.
                g_componentResources.saveAnimation(fullResPath, anim, scene.projectRoot);
                comps["AnimationState"] = resPath; // store the filename, not inline JSON
            }

            if (scene.world.has_physics_body[i]) {
                auto& phys = scene.world.physics_body_pool[i];
                std::string& resPath = scene.world.physics_resource_path[i];
                if (resPath.empty()) {
                    std::string label = scene.world.has_metadata[i] ? scene.world.metadata_pool[i].name : "";
                    if (label.empty()) label = "entity";
                    resPath = "resources/" + label + "_" + std::to_string(i) + ".physicsres";
                }
                std::string fullResPath = (std::filesystem::path(scene.projectRoot) / resPath).string();
                std::filesystem::create_directories(std::filesystem::path(fullResPath).parent_path());
                g_componentResources.savePhysics(fullResPath, phys);
                comps["PhysicsBody"] = resPath; // store the filename, not inline JSON
            }

            if (scene.world.has_tilemap[i]) {
                auto& tm = scene.world.tilemap_pool[i];
                nlohmann::json jtm;
                // Same treatment as TextureRef.resourceName above: store the
                // path relative to the project root, not whatever the file
                // browser handed back (which is relative to the Projects
                // folder and still has the project name on the front).
                // Leaving this un-relative-ized is what makes tiles vanish
                // after a save/reload — resolveComponentResourcePath() ends
                // up doubling the project folder in the path on load.
                std::string relTileTex = std::filesystem::relative(tm.textureName, scene.projectRoot).string();
                if (relTileTex.empty()) relTileTex = tm.textureName; // fallback: keep as-is if relative() fails
                jtm["textureName"] = relTileTex;
                jtm["cellWidth"] = tm.cellWidth;
                jtm["cellHeight"] = tm.cellHeight;
                jtm["snapEnabled"] = tm.snapEnabled;
                nlohmann::json cellsArr = nlohmann::json::array();
                for (auto& cell : tm.cells) {
                    cellsArr.push_back({cell.first, cell.second});
                }
                jtm["cells"] = cellsArr;
                nlohmann::json offsetsArr = nlohmann::json::array();
                for (size_t k = 0; k < tm.cells.size(); ++k) {
                    SDL_FPoint off = (k < tm.cellOffsets.size()) ? tm.cellOffsets[k] : SDL_FPoint{0.0f, 0.0f};
                    offsetsArr.push_back({off.x, off.y});
                }
                jtm["cellOffsets"] = offsetsArr;
                comps["TileMap"] = jtm;
            }

            entitiesJson.push_back({{"components", comps}});
        }
        j["entities"] = entitiesJson;

        // --- 2. Save GUI Elements ---
        nlohmann::json guiArray = nlohmann::json::array();
        for (const auto& elem : scene.guiElements)
            guiArray.push_back(serializeGuiElement(*elem));
        j["gui_elements"] = guiArray;

        std::ofstream outFile(filepath);
        outFile << j.dump(4);
    }

private:
    // ------------------------------------------------------------------
    // Helpers: parse / serialize a single IGuiElement (handles Panel recursion)
    // ------------------------------------------------------------------
    std::unique_ptr<Gui::IGuiElement> parseGuiElement(const nlohmann::json & elemJson) {
        std::string type = elemJson.value("type", "");
        float x = elemJson.value("x", 0.0f);
        float y = elemJson.value("y", 0.0f);
        float w = elemJson.value("w", 100.0f);
        float h = elemJson.value("h",  50.0f);
        SDL_FRect rect = {x, y, w, h};

        if (type == "Button") {
            return std::make_unique<Gui::Button>(
                renderer, font, elemJson.value("text", "Button"), SDL_FPoint{x, y}, w, h);
        }
        if (type == "LineEdit") {
            return std::make_unique<Gui::LineEdit>(
                renderer, textEngine, font, rect,
                elemJson.value("placeholder", "Type here..."));
        }
        if (type == "SpinBox") {
            return std::make_unique<Gui::SpinBox>(
                renderer, textEngine, font, rect,
                elemJson.value("min", 1.0f), elemJson.value("max", 9999.0f),
                elemJson.value("initial", 50.0f), elemJson.value("step", 0.5f));
        }
        if (type == "Panel") {
            auto bgArr = elemJson.value("bg_color", nlohmann::json::array({40,40,48,230}));
            SDL_Color bg = {
                (Uint8)bgArr[0].get<int>(), (Uint8)bgArr[1].get<int>(),
                (Uint8)bgArr[2].get<int>(), (Uint8)bgArr[3].get<int>()
            };
            auto panel = std::make_unique<Gui::Panel>(renderer, textEngine, font, rect, bg);

            // Children positions in JSON are relative to panel origin
            if (elemJson.contains("children")) {
                for (const auto& childJson : elemJson["children"]) {
                    // Adjust child x/y: they are stored relative to panel top-left;
                    // at runtime children receive panelScrollOffset as their offsetY
                    // and their x/y are already in panel-local screen coords (panel.x + local_x)
                    nlohmann::json adjusted = childJson;
                    adjusted["x"] = x + childJson.value("x", 0.0f);
                    adjusted["y"] = y + childJson.value("y", 0.0f);
                    auto child = parseGuiElement(adjusted);
                    if (child) panel->addChild(std::move(child));
                }
            }
            return panel;
        }
        std::cerr << "[SceneParser] Unknown gui element type: " << type << "\n";
        return nullptr;
    }

    nlohmann::json serializeGuiElement(const Gui::IGuiElement& elem) {
        nlohmann::json j;
        j["type"] = elem.getType();
        j["x"]    = elem.getX();
        j["y"]    = elem.getY();
        j["w"]    = elem.getWidth();
        j["h"]    = elem.getHeight();

        if (elem.getType() == "Button") {
            j["text"] = static_cast<const Gui::Button&>(elem).getText();
        } else if (elem.getType() == "LineEdit") {
            j["placeholder"] = static_cast<const Gui::LineEdit&>(elem).getPlaceholder();
        } else if (elem.getType() == "SpinBox") {
            const auto& sb = static_cast<const Gui::SpinBox&>(elem);
            j["min"] = sb.getMin(); j["max"] = sb.getMax();
            j["step"] = sb.getStep(); j["initial"] = sb.getValue();
        } else if (elem.getType() == "Panel") {
            const auto& panel = static_cast<const Gui::Panel&>(elem);
            j["bg_color"] = { panel.bgColor.r, panel.bgColor.g,
                               panel.bgColor.b, panel.bgColor.a };
            nlohmann::json childArr = nlohmann::json::array();
            for (const auto& child : panel.getChildren()) {
                auto childJson = serializeGuiElement(*child);
                // Store children relative to panel origin
                childJson["x"] = child->getX() - elem.getX();
                childJson["y"] = child->getY() - elem.getY();
                childArr.push_back(childJson);
            }
            j["children"] = childArr;
        }

        return j; 

    }
        
};

inline bool change_scene(SceneParser& parser, Scene& targetScene,
                          std::string& sceneFilePathRef,
                          const std::string& newScenePath,
                          void* context,
                          Physics::PhysicsWorld*& physicsWorld,
                          float gravity = 0.0f) {
    // 1. Load the new scene data (but do NOT assign it yet)
    Scene newScene = parser.ProjectScript_loadFromFile(newScenePath);
    if (newScene.projectRoot.empty()) {
        std::cerr << "[change_scene] Failed to load scene: " << newScenePath << "\n";
        return false;
    }

    // 2. End the old script (it may need the old physics world – keep it alive)
    if (targetScene.script) {
        targetScene.script->onEnd();
    }

    // 3. Replace the physics world (old one is now unused)
    delete physicsWorld;
    physicsWorld = new Physics::PhysicsWorld(gravity);

    // 4. Move the new scene into place and start its script (with the fresh world)
    targetScene = std::move(newScene);
    sceneFilePathRef = newScenePath;

    if (targetScene.script) {
        targetScene.script->setContext(context);
        targetScene.script->onStart();
    } else {
        std::cerr << "[change_scene] Scene '" << targetScene.name
                  << "' loaded with no active script (script_attached='"
                  << targetScene.scriptAttached << "').\n";
    }

    return true;
}

// ============================================================
// WorldBinary — Fast binary serialization for generated entities
// ============================================================
namespace WorldBinary {
    constexpr uint32_t MAGIC = 0x444C5257; // "WRLD" in little-endian
    constexpr uint32_t VERSION = 3;

    enum ComponentMask : uint32_t {
        POS = 1 << 0, RECT = 1 << 1, Z = 1 << 2, TEX = 1 << 3,
        ANIM = 1 << 4, PHYS = 1 << 5, ROT = 1 << 6, SCALE = 1 << 7,
        META = 1 << 8, // for metadata
        TILE = 1 << 9 // for tilemaps
    };

    inline std::string makeRelative(const std::string& path, const std::string& root) {
        if (path.empty() || root.empty()) return path;
        std::string rel = std::filesystem::relative(path, root).string();
        return rel.empty() ? path : rel;
    }

    inline bool save(const std::string& filepath, const ECSWorld& world, 
                     const std::vector<Entity>& entities, bool generatedFlag, const std::string& projectRoot) {
        std::ofstream out(filepath, std::ios::binary);
        if (!out) return false;

        uint32_t magic = MAGIC;
        uint32_t version = VERSION;
        uint8_t genFlag = generatedFlag ? 1 : 0;
        uint32_t count = (uint32_t)entities.size();

        out.write((char*)&magic, 4);
        out.write((char*)&version, 4);
        out.write((char*)&genFlag, 1);
        out.write((char*)&count, 4);

        for (Entity e : entities) {
            uint32_t mask = 0;
            if (world.has_position[e]) mask |= POS;
            if (world.has_rectangle_shape[e]) mask |= RECT;
            if (world.has_z_index[e]) mask |= Z;
            if (world.has_texture_ref[e]) mask |= TEX;
            if (world.has_animation_state[e]) mask |= ANIM;
            if (world.has_physics_body[e]) mask |= PHYS;
            if (world.has_rotation[e]) mask |= ROT;
            if (world.has_scale[e]) mask |= SCALE;
            if (world.has_metadata[e]) mask |= META; // <-- ADDED
            if (world.has_tilemap[e])  mask |= TILE;

            out.write((char*)&mask, 4);

            if (mask & POS) {
                out.write((char*)&world.position_pool[e].x, 4);
                out.write((char*)&world.position_pool[e].y, 4);
            }
            if (mask & RECT) {
                out.write((char*)&world.rectangle_shape_pool[e].w, 4);
                out.write((char*)&world.rectangle_shape_pool[e].h, 4);
            }
            if (mask & Z) {
                int32_t z = world.z_index_pool[e].z;
                out.write((char*)&z, 4);
            }
            if (mask & TEX) {
                std::string s = makeRelative(world.texture_ref_pool[e].resourceName, projectRoot);
                uint32_t len = (uint32_t)s.size();
                out.write((char*)&len, 4);
                out.write(s.data(), len);
            }
            if (mask & ANIM) {
                std::string s = makeRelative(world.animation_resource_path[e], projectRoot);
                uint32_t len = (uint32_t)s.size();
                out.write((char*)&len, 4);
                out.write(s.data(), len);
            }
            if (mask & PHYS) {
                // Persist the resource path too (useful for the editor to
                // show/re-link the source .physicsres file), but do NOT rely
                // on it alone: entities whose PhysicsBody was defined inline
                // in the scene JSON (the "legacy" format) have an empty
                // physics_resource_path, and previously that meant nothing
                // usable was saved for them at all. Save the actual body
                // definition directly so a round trip through this file
                // always reconstructs a working physics body.
                std::string s = makeRelative(world.physics_resource_path[e], projectRoot);
                uint32_t len = (uint32_t)s.size();
                out.write((char*)&len, 4);
                out.write(s.data(), len);

                const auto& def = world.physics_body_pool[e];
                int32_t shapeType = (int32_t)def.shapeType;
                int32_t bodyType  = (int32_t)def.bodyType;
                uint8_t isSensor  = def.isSensor ? 1 : 0;
                out.write((char*)&shapeType, 4);
                out.write((char*)&bodyType, 4);
                out.write((char*)&def.width, 4);
                out.write((char*)&def.height, 4);
                out.write((char*)&def.radius, 4);
                out.write((char*)&def.density, 4);
                out.write((char*)&isSensor, 1);
                out.write((char*)&def.category, 2);
                out.write((char*)&def.mask, 2);

                uint32_t polyCount = (uint32_t)def.polygonPoints.size();
                out.write((char*)&polyCount, 4);
                for (const auto& pt : def.polygonPoints) {
                    out.write((char*)&pt.x, 4);
                    out.write((char*)&pt.y, 4);
                }
            }
            if (mask & ROT) {
                out.write((char*)&world.rotation_pool[e].degrees, 4);
            }
            if (mask & SCALE) {
                out.write((char*)&world.scale_pool[e].x, 4);
                out.write((char*)&world.scale_pool[e].y, 4);
            }
            if (mask & META) { // <-- ADDED: Save metadata name
                std::string s = world.metadata_pool[e].name;
                uint32_t len = (uint32_t)s.size();
                out.write((char*)&len, 4);
                out.write(s.data(), len);
            }
            if (mask & TILE) { // <-- ADDED: TileMap incl. per-cell offsets
                const auto& tm = world.tilemap_pool[e];
                std::string s = makeRelative(tm.textureName, projectRoot);
                uint32_t len = (uint32_t)s.size();
                out.write((char*)&len, 4);
                out.write(s.data(), len);
                out.write((char*)&tm.cellWidth, 4);
                out.write((char*)&tm.cellHeight, 4);
                uint8_t snap = tm.snapEnabled ? 1 : 0;
                out.write((char*)&snap, 1);
                uint32_t cellCount = (uint32_t)tm.cells.size();
                out.write((char*)&cellCount, 4);
                for (size_t k = 0; k < tm.cells.size(); ++k) {
                    int32_t col = tm.cells[k].first;
                    int32_t row = tm.cells[k].second;
                    float offX = (k < tm.cellOffsets.size()) ? tm.cellOffsets[k].x : 0.0f;
                    float offY = (k < tm.cellOffsets.size()) ? tm.cellOffsets[k].y : 0.0f;
                    out.write((char*)&col, 4);
                    out.write((char*)&row, 4);
                    out.write((char*)&offX, 4);
                    out.write((char*)&offY, 4);
                }
            }
        }
        return true;
    }

    inline bool load(const std::string& filepath, ECSWorld& world, 
                     std::vector<Entity>& outEntities, bool& generatedFlag, const std::string& projectRoot) {
        std::ifstream in(filepath, std::ios::binary);
        if (!in) return false;

        uint32_t magic, version, count;
        uint8_t genFlag;
        in.read((char*)&magic, 4);
        in.read((char*)&version, 4);
        in.read((char*)&genFlag, 1);
        in.read((char*)&count, 4);

        if (magic != MAGIC || version != VERSION) return false;
        generatedFlag = (genFlag != 0);

        outEntities.clear();
        outEntities.reserve(count);

        for (uint32_t i = 0; i < count; ++i) {
            Entity e = world.create_entity();
            world.add_selection(e);
            outEntities.push_back(e);

            uint32_t mask;
            in.read((char*)&mask, 4);

            if (mask & POS) {
                world.add_position(e);
                in.read((char*)&world.position_pool[e].x, 4);
                in.read((char*)&world.position_pool[e].y, 4);
            }
            if (mask & RECT) {
                world.add_rectangle_shape(e);
                in.read((char*)&world.rectangle_shape_pool[e].w, 4);
                in.read((char*)&world.rectangle_shape_pool[e].h, 4);
            }
            if (mask & Z) {
                world.add_z_index(e);
                int32_t z;
                in.read((char*)&z, 4);
                world.z_index_pool[e].z = z;
            }
            if (mask & TEX) {
                world.add_texture_ref(e);
                uint32_t len;
                in.read((char*)&len, 4);
                std::string s(len, '\0');
                if (len > 0) in.read(s.data(), len);
                world.texture_ref_pool[e].resourceName = resolveComponentResourcePath(projectRoot, s);
            }
            if (mask & ANIM) {
                uint32_t len;
                in.read((char*)&len, 4);
                std::string s(len, '\0');
                if (len > 0) in.read(s.data(), len);
                std::string resPath = resolveComponentResourcePath(projectRoot, s);
                try { world.assign_animation_resource(e, resPath, projectRoot); } catch (...) {}
            }
            if (mask & PHYS) {
                uint32_t len;
                in.read((char*)&len, 4);
                std::string s(len, '\0');
                if (len > 0) in.read(s.data(), len);
                std::string resPath = s.empty() ? std::string() : resolveComponentResourcePath(projectRoot, s);

                int32_t shapeType, bodyType;
                float width, height, radius, density;
                uint8_t isSensor;
                uint16_t category, maskBits;
                in.read((char*)&shapeType, 4);
                in.read((char*)&bodyType, 4);
                in.read((char*)&width, 4);
                in.read((char*)&height, 4);
                in.read((char*)&radius, 4);
                in.read((char*)&density, 4);
                in.read((char*)&isSensor, 1);
                in.read((char*)&category, 2);
                in.read((char*)&maskBits, 2);

                uint32_t polyCount = 0;
                in.read((char*)&polyCount, 4);
                std::vector<b2Vec2> polygonPoints(polyCount);
                for (uint32_t p = 0; p < polyCount; ++p) {
                    in.read((char*)&polygonPoints[p].x, 4);
                    in.read((char*)&polygonPoints[p].y, 4);
                }

                world.add_physics_body(e);
                auto& def = world.physics_body_pool[e];
                def.shapeType = (Physics::ShapeType)shapeType;
                def.bodyType  = (b2BodyType)bodyType;
                def.width     = width;
                def.height    = height;
                def.radius    = radius;
                def.density   = density;
                def.isSensor  = (isSensor != 0);
                def.category  = category;
                def.mask      = maskBits;
                def.polygonPoints = std::move(polygonPoints);
                def.bodyId = b2_nullBodyId; // fresh runtime handle, recreated by physics_sync_system
                world.physics_resource_path[e] = resPath;
            }
            if (mask & ROT) {
                world.add_rotation(e);
                in.read((char*)&world.rotation_pool[e].degrees, 4);
            }
            if (mask & SCALE) {
                world.add_scale(e);
                in.read((char*)&world.scale_pool[e].x, 4);
                in.read((char*)&world.scale_pool[e].y, 4);
            }
            if (mask & META) { // <-- ADDED: Load metadata name
                world.add_metadata(e);
                uint32_t len;
                in.read((char*)&len, 4);
                std::string s(len, '\0');
                if (len > 0) in.read(s.data(), len);
                world.metadata_pool[e].name = s;
            }
            if (mask & TILE) { // <-- ADDED: TileMap incl. per-cell offsets
                world.add_tilemap(e);
                auto& tm = world.tilemap_pool[e];
                uint32_t len;
                in.read((char*)&len, 4);
                std::string s(len, '\0');
                if (len > 0) in.read(s.data(), len);
                tm.textureName = resolveComponentResourcePath(projectRoot, s);
                in.read((char*)&tm.cellWidth, 4);
                in.read((char*)&tm.cellHeight, 4);
                uint8_t snap;
                in.read((char*)&snap, 1);
                tm.snapEnabled = (snap != 0);
                uint32_t cellCount = 0;
                in.read((char*)&cellCount, 4);
                tm.cells.clear();
                tm.cellOffsets.clear();
                tm.cells.reserve(cellCount);
                tm.cellOffsets.reserve(cellCount);
                for (uint32_t k = 0; k < cellCount; ++k) {
                    int32_t col, row;
                    float offX, offY;
                    in.read((char*)&col, 4);
                    in.read((char*)&row, 4);
                    in.read((char*)&offX, 4);
                    in.read((char*)&offY, 4);
                    tm.cells.push_back({col, row});
                    tm.cellOffsets.push_back({offX, offY});
                }
            }
        }
        return true;
    }
};