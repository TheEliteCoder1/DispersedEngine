#pragma once
#include "engine.h"
#include <algorithm>
#include <cmath>
#include <random>
#include <memory>
#include <functional>
#include <unordered_map>
#include <unordered_set>

// ============================================================================
// particles.h
// ----------------------------------------------------------------------------
// A small, self-contained particle system that reuses engine.h's existing
// machinery where it makes sense, rather than duplicating it:
//
//   - ParticleInstance / EmitterDef (below) hold the per-particle and
//     per-effect data. They used to live in engine.h as Components::Particle
//     / Components::ParticleEmitterDefinition, but nothing there ever wired
//     them into the ECS (no pool, no system) -- dead scaffolding -- so they
//     now live here instead, as this header's own types.
//   - ParticleResourceManager below is the exact same flyweight/cache pattern
//     as engine.h's ComponentResourceManager (.animres / .physicsres), just
//     pointed at a new "*.particleres" JSON file so particle "looks" (color,
//     speed, size, gravity, spread...) are data-driven and shareable across
//     entities/scenes the same way animations and physics bodies already are.
//   - GetCurrentSpriteFrame() mirrors render_entity_animation() /
//     render_entity_texture() so the vanish wipe always fades whatever the
//     entity is *actually* showing this frame (current walk/idle frame),
//     with no game-side bookkeeping required.
//
// The headline feature is VanishSystem: give it an entity and it will fade
// that entity's current animation out from top to bottom while it dissolves
// into a trail of black particles moving the same direction, and it does all
// the per-frame work (spawning, physics, fade math, batched drawing) inside
// this header. game.cpp/game.h only need to:
//
//   1. Hold one:                 Particles::VanishSystem vanishEffects;
//   2. Trigger it once:          vanishEffects.start(player);
//   3. Tick it every frame:      vanishEffects.update(world, dt);
//   4. Skip normal drawing of a vanishing entity, then:
//                                vanishEffects.render(renderer, world, camera);
//
// See the usage note at the bottom of this file for the exact game.cpp diff.
// ============================================================================

namespace Particles {

// Previously these lived in engine.h as Components::Particle /
// Components::ParticleEmitterDefinition, but nothing in the engine ever
// wired them into the ECS (no pool, no system) -- they were dead
// scaffolding. Now that this header is the sole owner of the particle
// system, they're defined here instead, self-contained.

// One live particle. Kept as a plain, tightly-packed struct (no pointers,
// no strings) so a std::vector<ParticleInstance> stays cheap to grow,
// shrink, and iterate -- this is the hot-path data for the whole system.
struct ParticleInstance {
    float x = 0.0f, y = 0.0f;
    float vx = 0.0f, vy = 0.0f;
    float life = 0.0f, maxLife = 0.0f;
    float size = 0.0f, startSize = 0.0f, endSize = 0.0f;
    SDL_Color color{255,255,255,255}, startColor{255,255,255,255}, endColor{255,255,255,255};
    bool active = true;
};

// Data-driven "look" for a burst of particles: how fast/big/long-lived they
// are, what color they fade between, and how they're aimed. This is exactly
// what gets loaded from / saved to a "*.particleres" file below.
struct EmitterDef {
    std::string textureName;          // optional; empty = solid-color quads
    float spawnRate = 10.0f;          // particles per second
    float lifetimeMin = 1.0f, lifetimeMax = 1.0f;
    float speedMin = 50.0f, speedMax = 100.0f;
    float sizeMin = 10.0f, sizeMax = 20.0f;
    float startSize = 10.0f, endSize = 5.0f;
    SDL_Color colorStart{0,0,0,255};
    SDL_Color colorEnd{0,0,0,0};
    float gravity = 0.0f;              // pixels/s² (positive = downward)
    float spreadAngle = 90.0f;         // degrees (full cone, centred on the wipe direction)
    bool loop = false;                 // if false, stops after one burst
};

// A "black dust" look tuned for a character-vanish effect: short-lived,
// gravity-assisted, fading-to-nothing black squares. Callers can start with
// this and override just the fields they care about (see VanishConfig).
inline EmitterDef DefaultVanishEmitter() {
    EmitterDef def;
    def.textureName = "";        // empty = solid-color quads, no texture needed
    def.spawnRate   = 220.0f;    // particles/sec while the wipe line is moving
    def.lifetimeMin = 0.35f;
    def.lifetimeMax = 0.75f;
    def.speedMin    = 15.0f;
    def.speedMax    = 60.0f;
    def.sizeMin     = 3.0f;      // per-particle random starting size range
    def.sizeMax     = 7.0f;
    def.startSize   = 6.0f;      // (unused by VanishSystem; kept for resource round-trips)
    def.endSize     = 0.5f;      // every particle shrinks to this by end of life
    def.colorStart  = SDL_Color{ 0, 0, 0, 255 };
    def.colorEnd    = SDL_Color{ 0, 0, 0, 0 };
    def.gravity     = 30.0f;
    def.spreadAngle = 150.0f;    // cone width around the wipe direction, degrees
    def.loop        = false;
    return def;
}

// ============================================================================
// ParticleResourceManager -- ".particleres" files
// ----------------------------------------------------------------------------
// Deliberately the same shape as engine.h's ComponentResourceManager: a
// filepath-keyed flyweight cache so N entities/effects sharing one
// "burnt_leaves.particleres" file only pay the disk read + JSON parse once,
// plus static parseJson()/serializeJson() helpers an editor-side inspector
// could call directly (mirroring parseAnimationJson/serializeAnimationJson).
// ============================================================================
class ParticleResourceManager {
public:
    std::shared_ptr<EmitterDef> load(const std::string& filepath) {
        std::string key = canonicalKey(filepath);
        auto it = cache.find(key);
        if (it != cache.end()) return it->second;

        std::ifstream in(filepath);
        if (!in.is_open())
            throw std::runtime_error("ParticleResourceManager: cannot open particle resource '" + filepath + "'");
        nlohmann::json j;
        in >> j;

        auto def = std::make_shared<EmitterDef>(parseJson(j));
        cache[key] = def;
        return def;
    }

    // One write per unique path per save pass, exactly like
    // ComponentResourceManager::saveAnimation()/savePhysics() -- call
    // beginSavePass() once at the top of your save routine.
    void save(const std::string& filepath, const EmitterDef& data) {
        std::string key = canonicalKey(filepath);
        if (writtenThisPass.count(key)) return;
        writtenThisPass.insert(key);

        std::ofstream out(filepath);
        if (!out.is_open())
            throw std::runtime_error("ParticleResourceManager: cannot write particle resource '" + filepath + "'");
        out << serializeJson(data).dump(4);

        cache[key] = std::make_shared<EmitterDef>(data);
    }

    void beginSavePass() { writtenThisPass.clear(); }
    void clear() { cache.clear(); writtenThisPass.clear(); }
    size_t cacheSize() const { return cache.size(); }

    static EmitterDef parseJson(const nlohmann::json& j) {
        EmitterDef d;
        d.textureName = j.value("textureName", std::string());
        d.spawnRate   = j.value("spawnRate", 10.0f);
        d.lifetimeMin = j.value("lifetimeMin", 1.0f);
        d.lifetimeMax = j.value("lifetimeMax", 1.0f);
        d.speedMin    = j.value("speedMin", 50.0f);
        d.speedMax    = j.value("speedMax", 100.0f);
        d.sizeMin     = j.value("sizeMin", 10.0f);
        d.sizeMax     = j.value("sizeMax", 20.0f);
        d.startSize   = j.value("startSize", 10.0f);
        d.endSize     = j.value("endSize", 5.0f);

        auto readColor = [&](const char* key, SDL_Color fallback) -> SDL_Color {
            if (!j.contains(key)) return fallback;
            const auto& c = j[key];
            return SDL_Color{
                (Uint8)c.value("r", (int)fallback.r),
                (Uint8)c.value("g", (int)fallback.g),
                (Uint8)c.value("b", (int)fallback.b),
                (Uint8)c.value("a", (int)fallback.a)
            };
        };
        d.colorStart = readColor("colorStart", SDL_Color{0,0,0,255});
        d.colorEnd   = readColor("colorEnd",   SDL_Color{0,0,0,0});

        d.gravity     = j.value("gravity", 0.0f);
        d.spreadAngle = j.value("spreadAngle", 90.0f);
        d.loop        = j.value("loop", false);
        return d;
    }

    static nlohmann::json serializeJson(const EmitterDef& d) {
        nlohmann::json j;
        j["textureName"] = d.textureName;
        j["spawnRate"]   = d.spawnRate;
        j["lifetimeMin"] = d.lifetimeMin;
        j["lifetimeMax"] = d.lifetimeMax;
        j["speedMin"]    = d.speedMin;
        j["speedMax"]    = d.speedMax;
        j["sizeMin"]     = d.sizeMin;
        j["sizeMax"]     = d.sizeMax;
        j["startSize"]   = d.startSize;
        j["endSize"]     = d.endSize;

        auto writeColor = [](const SDL_Color& c) {
            return nlohmann::json{ {"r", c.r}, {"g", c.g}, {"b", c.b}, {"a", c.a} };
        };
        j["colorStart"] = writeColor(d.colorStart);
        j["colorEnd"]   = writeColor(d.colorEnd);

        j["gravity"]     = d.gravity;
        j["spreadAngle"] = d.spreadAngle;
        j["loop"]        = d.loop;
        return j;
    }

private:
    std::unordered_map<std::string, std::shared_ptr<EmitterDef>> cache;
    std::unordered_set<std::string> writtenThisPass;

    // Same normalization trick as ComponentResourceManager::canonicalKey()
    // so "./x.particleres" and "x.particleres" hit the same cache entry.
    static std::string canonicalKey(const std::string& filepath) {
        std::error_code ec;
        auto canon = std::filesystem::weakly_canonical(filepath, ec);
        return ec ? filepath : canon.string();
    }
};

// Single shared instance, engine-wide -- same `inline` trick engine.h uses
// for g_componentResources so this header stays include-anywhere safe.
inline ParticleResourceManager g_particleResources;

// Loads an emitter definition from a "*.particleres" file, resolving the
// stored path against a project root the same way .animres/.physicsres are
// resolved (see resolveComponentResourcePath in engine.h). Falls back to
// DefaultVanishEmitter() if the path is empty or the file can't be read, so
// callers never have to null-check.
inline EmitterDef LoadVanishEmitter(const std::string& storedPath, const std::string& projectRoot = "") {
    if (storedPath.empty()) return DefaultVanishEmitter();
    std::string resolved = resolveComponentResourcePath(projectRoot, storedPath);
    try {
        return *g_particleResources.load(resolved);
    } catch (...) {
        return DefaultVanishEmitter();
    }
}

// ============================================================================
// Sprite-frame lookup
// ----------------------------------------------------------------------------
// Pulls the texture + world-space rect currently being shown for an entity
// -- whatever its active AnimationState clip frame is, or its plain
// TextureRef if it has no animation -- without drawing anything. This is
// intentionally the same lookup order render_entity_animation() /
// render_entity_texture() use, so the vanish wipe always matches whatever
// the entity would otherwise have rendered as this frame (current walk or
// idle frame included).
// ============================================================================
struct SpriteFrame {
    SDL_Texture* tex = nullptr;
    SDL_FRect worldRect = { 0.0f, 0.0f, 0.0f, 0.0f }; // top-left + size, world space
};

inline bool GetCurrentSpriteFrame(const ECSWorld& world, Entity e, SpriteFrame& out) {
    if (e == (Entity)-1 || e >= world.entity_count || !world.has_position[e]) return false;

    float scaleX = world.has_scale[e] ? world.scale_pool[e].x : 1.0f;
    float scaleY = world.has_scale[e] ? world.scale_pool[e].y : 1.0f;

    if (world.has_animation_state[e]) {
        const auto& clip = world.animation_state_pool[e].active();
        if (clip.imageFrameResources.empty()) return false;

        int idx = clip.currentFrame;
        if (idx < 0) idx = 0;
        if (idx >= (int)clip.imageFrameResources.size()) idx = (int)clip.imageFrameResources.size() - 1;

        const std::string& path = clip.imageFrameResources[idx];
        if (path.empty()) return false;

        SDL_Texture* tex = g_resources.TextureManager.Get(path);
        if (!tex) {
            g_resources.TextureManager.Load(path, path);
            tex = g_resources.TextureManager.Get(path);
        }
        if (!tex) return false;

        float tw, th;
        SDL_GetTextureSize(tex, &tw, &th);
        if (clip.frameWidth  > 0.0f) tw = clip.frameWidth;
        if (clip.frameHeight > 0.0f) th = clip.frameHeight;

        out.tex = tex;
        out.worldRect = { world.position_pool[e].x, world.position_pool[e].y, tw * scaleX, th * scaleY };
        return true;
    }

    if (world.has_texture_ref[e]) {
        SDL_Texture* tex = g_resources.TextureManager.Get(world.texture_ref_pool[e].resourceName);
        if (!tex) return false;

        float tw, th;
        SDL_GetTextureSize(tex, &tw, &th);

        out.tex = tex;
        out.worldRect = { world.position_pool[e].x, world.position_pool[e].y, tw * scaleX, th * scaleY };
        return true;
    }

    return false;
}

// ============================================================================
// VanishSystem
// ----------------------------------------------------------------------------
// Fades an entity's current sprite/animation frame out from top to bottom
// (or bottom to top) over `duration` seconds, while spawning black particles
// along the moving fade line so the entity looks like it's dissolving into
// dust rather than just fading out flatly. Multiple entities can vanish at
// once; every particle from every active vanish, across the whole scene, is
// drawn with a single SDL_RenderGeometry call for performance.
//
// Not an ECS component on purpose: a vanish is a short-lived, one-shot
// *effect*, not persistent entity data, so it doesn't need a pool slot on
// every entity in the world (has_vanish[MAX_ENTITIES] would waste memory
// for an effect that's rarely more than one or two instances at a time).
// ============================================================================

enum class WipeDirection { TopToBottom, BottomToTop };

struct VanishConfig {
    float duration   = 1.1f;   // seconds for the fade line to fully cross the sprite
    float bandHeight = 28.0f;  // softness of the fade edge, in world pixels
    int   stripCount = 14;     // horizontal slices used to approximate the per-pixel
                               // alpha gradient (SDL has no per-pixel alpha mod, so the
                               // sprite is sliced into strips and each strip gets its
                               // own SDL_SetTextureAlphaMod call)
    WipeDirection direction = WipeDirection::TopToBottom;
    EmitterDef particleDef  = DefaultVanishEmitter();
    int maxParticles        = 400; // safety cap per instance regardless of spawnRate/duration

    // Optional: fired exactly once, after the sprite has fully faded AND
    // every spawned particle has finished its life -- the natural point to
    // e.g. switch scenes or despawn the entity for good.
    std::function<void(Entity)> onComplete = nullptr;
};

class VanishSystem {
public:
    // Starts (or restarts, if already active) the vanish effect for `e`.
    // Cheap enough to guard with a simple bool on the caller's side, but
    // also safe to call again mid-effect -- it just resets progress rather
    // than stacking a second effect on the same entity.
    void start(Entity e, VanishConfig cfg = VanishConfig{}) {
        for (auto& inst : instances) {
            if (inst.entity == e) {
                inst.cfg = std::move(cfg);
                inst.elapsed = 0.0f;
                inst.spawnAccumulator = 0.0f;
                inst.particles.clear();
                return;
            }
        }
        Instance inst;
        inst.entity = e;
        inst.cfg = std::move(cfg);
        inst.particles.reserve((size_t)inst.cfg.maxParticles);
        instances.push_back(std::move(inst));
    }

    bool isActive(Entity e) const {
        for (auto& inst : instances) if (inst.entity == e) return true;
        return false;
    }

    // Removes the effect immediately without waiting for it to finish (e.g.
    // the entity respawned mid-vanish). Does NOT fire onComplete.
    void cancel(Entity e) {
        instances.erase(
            std::remove_if(instances.begin(), instances.end(),
                            [&](const Instance& i) { return i.entity == e; }),
            instances.end());
    }

    size_t activeCount() const { return instances.size(); }

    // Advances every active vanish: moves the fade line forward, spawns new
    // particles along it, and steps existing particles' physics/fade. Call
    // once per frame with the same `dt` used elsewhere (e.g. animation_system).
    void update(const ECSWorld& world, float dt) {
        for (size_t idx = 0; idx < instances.size(); ) {
            Instance& inst = instances[idx];

            SpriteFrame frame;
            bool hasFrame = GetCurrentSpriteFrame(world, inst.entity, frame);

            inst.elapsed += dt;
            float progress = inst.cfg.duration > 0.0f
                ? std::clamp(inst.elapsed / inst.cfg.duration, 0.0f, 1.0f)
                : 1.0f;

            if (hasFrame) spawnAlongWipe(inst, frame, progress, dt);
            stepParticles(inst, dt);

            bool finished = (progress >= 1.0f) && inst.particles.empty();
            if (finished) {
                auto callback = inst.cfg.onComplete;
                Entity e = inst.entity;
                instances.erase(instances.begin() + idx);
                if (callback) callback(e);
                continue; // vector shifted; don't advance idx
            }
            ++idx;
        }
    }

    // Draws every active vanish: the fading sprite strips (a handful of
    // draw calls per vanishing entity), then every particle from every
    // instance in one batched SDL_RenderGeometry call. Call from onDraw()
    // (or anywhere after the entity's *normal* sprite has been skipped --
    // see the usage note at the bottom of this file).
    void render(SDL_Renderer* renderer, const ECSWorld& world, const Tools::Camera& camera) {
        for (auto& inst : instances) {
            SpriteFrame frame;
            if (!GetCurrentSpriteFrame(world, inst.entity, frame)) continue;
            float progress = inst.cfg.duration > 0.0f
                ? std::clamp(inst.elapsed / inst.cfg.duration, 0.0f, 1.0f)
                : 1.0f;
            renderSpriteWipe(renderer, camera, frame, inst.cfg, progress);
        }
        renderParticlesBatched(renderer, camera);
    }

private:
    struct Instance {
        Entity entity = (Entity)-1;
        VanishConfig cfg;
        float elapsed = 0.0f;
        float spawnAccumulator = 0.0f;
        std::vector<ParticleInstance> particles;
    };

    std::vector<Instance> instances;

    // Reused every frame to avoid per-frame heap allocation once warmed up.
    std::vector<SDL_Vertex> vertexScratch;
    std::vector<int> indexScratch;

    static float lerp(float a, float b, float t) { return a + (b - a) * t; }

    void spawnAlongWipe(Instance& inst, const SpriteFrame& frame, float progress, float dt) {
        const EmitterDef& def = inst.cfg.particleDef;
        if (progress >= 1.0f) return; // wipe line has finished crossing the sprite; stop emitting
        if ((int)inst.particles.size() >= inst.cfg.maxParticles) return;
        if (def.spawnRate <= 0.0f) return;

        float wipeY = (inst.cfg.direction == WipeDirection::TopToBottom)
            ? frame.worldRect.y + progress * frame.worldRect.h
            : frame.worldRect.y + (1.0f - progress) * frame.worldRect.h;

        inst.spawnAccumulator += def.spawnRate * dt;

        static thread_local std::mt19937 rng{ std::random_device{}() };
        std::uniform_real_distribution<float> xDist(frame.worldRect.x, frame.worldRect.x + frame.worldRect.w);
        std::uniform_real_distribution<float> lifeDist(def.lifetimeMin, std::max(def.lifetimeMin, def.lifetimeMax));
        std::uniform_real_distribution<float> speedDist(def.speedMin, std::max(def.speedMin, def.speedMax));
        std::uniform_real_distribution<float> sizeDist(def.sizeMin, std::max(def.sizeMin, def.sizeMax));

        // 90 degrees = straight down in our angle convention (0 = +X/right),
        // -90 = straight up, so the particle "spray" travels the same
        // direction as the wipe itself.
        float baseAngleDeg = (inst.cfg.direction == WipeDirection::TopToBottom) ? 90.0f : -90.0f;
        std::uniform_real_distribution<float> angleDist(baseAngleDeg - def.spreadAngle * 0.5f,
                                                          baseAngleDeg + def.spreadAngle * 0.5f);

        while (inst.spawnAccumulator >= 1.0f && (int)inst.particles.size() < inst.cfg.maxParticles) {
            inst.spawnAccumulator -= 1.0f;

            ParticleInstance p{};
            p.x = xDist(rng);
            p.y = wipeY;

            float angleRad = angleDist(rng) * 3.14159265f / 180.0f;
            float speed = speedDist(rng);
            p.vx = std::cos(angleRad) * speed;
            p.vy = std::sin(angleRad) * speed;

            p.maxLife = std::max(0.01f, lifeDist(rng));
            p.life = p.maxLife;
            p.startSize = sizeDist(rng);
            p.endSize = def.endSize;
            p.size = p.startSize;
            p.startColor = def.colorStart;
            p.endColor = def.colorEnd;
            p.color = p.startColor;
            p.active = true;

            inst.particles.push_back(p);
        }
    }

    void stepParticles(Instance& inst, float dt) {
        const EmitterDef& def = inst.cfg.particleDef;
        size_t writeIdx = 0;
        for (size_t i = 0; i < inst.particles.size(); ++i) {
            ParticleInstance p = inst.particles[i];
            p.life -= dt;
            if (p.life <= 0.0f) continue; // dropped; compacted below

            p.vy += def.gravity * dt;
            p.x += p.vx * dt;
            p.y += p.vy * dt;

            float t = 1.0f - std::clamp(p.life / p.maxLife, 0.0f, 1.0f);
            p.size    = lerp(p.startSize, p.endSize, t);
            p.color.r = (Uint8)lerp((float)p.startColor.r, (float)p.endColor.r, t);
            p.color.g = (Uint8)lerp((float)p.startColor.g, (float)p.endColor.g, t);
            p.color.b = (Uint8)lerp((float)p.startColor.b, (float)p.endColor.b, t);
            p.color.a = (Uint8)lerp((float)p.startColor.a, (float)p.endColor.a, t);

            inst.particles[writeIdx++] = p;
        }
        inst.particles.resize(writeIdx);
    }

    void renderSpriteWipe(SDL_Renderer* renderer, const Tools::Camera& camera, const SpriteFrame& frame,
                          const VanishConfig& cfg, float progress) {
        int strips = std::max(2, cfg.stripCount);
        float wipeY = (cfg.direction == WipeDirection::TopToBottom)
            ? frame.worldRect.y + progress * frame.worldRect.h
            : frame.worldRect.y + (1.0f - progress) * frame.worldRect.h;
        float halfBand = std::max(cfg.bandHeight, 1.0f) * 0.5f;

        float texW, texH;
        SDL_GetTextureSize(frame.tex, &texW, &texH);

        // Snapshot so other users of this same (shared/cached) texture --
        // e.g. another entity using the same walk animation, or this same
        // entity's own next non-faded frame -- aren't left permanently
        // tinted/transparent after we're done with it this frame.
        Uint8 origR = 255, origG = 255, origB = 255, origA = 255;
        SDL_GetTextureColorMod(frame.tex, &origR, &origG, &origB);
        SDL_GetTextureAlphaMod(frame.tex, &origA);

        for (int s = 0; s < strips; ++s) {
            float v0 = (float)s / (float)strips;
            float v1 = (float)(s + 1) / (float)strips;
            float worldY0 = frame.worldRect.y + v0 * frame.worldRect.h;
            float worldY1 = frame.worldRect.y + v1 * frame.worldRect.h;
            float midY = (worldY0 + worldY1) * 0.5f;

            float alphaF;
            if (cfg.direction == WipeDirection::TopToBottom) {
                // Above the wipe line has already vanished (alpha -> 0);
                // below it is still fully visible (alpha -> 1).
                alphaF = std::clamp((midY - (wipeY - halfBand)) / (halfBand * 2.0f), 0.0f, 1.0f);
            } else {
                alphaF = std::clamp(((wipeY + halfBand) - midY) / (halfBand * 2.0f), 0.0f, 1.0f);
            }
            if (alphaF <= 0.001f) continue; // fully transparent strip: skip the draw call entirely

            SDL_FRect src = { 0.0f, v0 * texH, texW, (v1 - v0) * texH };
            SDL_FPoint topLeft = camera.worldToScreen(frame.worldRect.x, worldY0);
            SDL_FPoint botRight = camera.worldToScreen(frame.worldRect.x + frame.worldRect.w, worldY1);
            SDL_FRect dst = { topLeft.x, topLeft.y, botRight.x - topLeft.x, botRight.y - topLeft.y };

            SDL_SetTextureAlphaMod(frame.tex, (Uint8)std::round(alphaF * 255.0f));
            SDL_RenderTexture(renderer, frame.tex, &src, &dst);
        }

        SDL_SetTextureAlphaMod(frame.tex, origA);
        SDL_SetTextureColorMod(frame.tex, origR, origG, origB);
    }

    // Every particle from every active vanish becomes two triangles (a
    // quad) in one shared vertex/index buffer, drawn with a single
    // SDL_RenderGeometry call -- so ten vanishing entities with hundreds of
    // particles apiece still costs exactly one draw call, not one per
    // particle. texture=nullptr means SDL flat-shades each quad using its
    // per-vertex color, which is exactly what a colored (here: black,
    // fading to transparent) particle needs with no texture at all.
    void renderParticlesBatched(SDL_Renderer* renderer, const Tools::Camera& camera) {
        size_t totalParticles = 0;
        for (auto& inst : instances) totalParticles += inst.particles.size();
        if (totalParticles == 0) return;

        vertexScratch.clear();
        indexScratch.clear();
        vertexScratch.reserve(totalParticles * 4);
        indexScratch.reserve(totalParticles * 6);

        for (auto& inst : instances) {
            for (auto& p : inst.particles) {
                if (!p.active) continue;

                float half = std::max(p.size, 0.5f) * 0.5f * camera.zoom;
                SDL_FPoint c = camera.worldToScreen(p.x, p.y);

                SDL_FColor col = {
                    p.color.r / 255.0f, p.color.g / 255.0f, p.color.b / 255.0f, p.color.a / 255.0f
                };

                int base = (int)vertexScratch.size();
                vertexScratch.push_back({ { c.x - half, c.y - half }, col, { 0.0f, 0.0f } });
                vertexScratch.push_back({ { c.x + half, c.y - half }, col, { 1.0f, 0.0f } });
                vertexScratch.push_back({ { c.x + half, c.y + half }, col, { 1.0f, 1.0f } });
                vertexScratch.push_back({ { c.x - half, c.y + half }, col, { 0.0f, 1.0f } });

                indexScratch.push_back(base + 0);
                indexScratch.push_back(base + 1);
                indexScratch.push_back(base + 2);
                indexScratch.push_back(base + 0);
                indexScratch.push_back(base + 2);
                indexScratch.push_back(base + 3);
            }
        }

        if (!vertexScratch.empty()) {
            SDL_RenderGeometry(renderer, nullptr,
                               vertexScratch.data(), (int)vertexScratch.size(),
                               indexScratch.data(), (int)indexScratch.size());
        }
    }
};

class ParticleBurstSystem {
public:
    struct Burst {
        std::vector<ParticleInstance> particles;
        float elapsed = 0.0f;
        float duration = 1.0f;
        bool finished = false;
    };
    
    void spawnBurst(float x, float y, const EmitterDef& def, int count) {
        Burst b;
        b.duration = def.lifetimeMax + 0.5f;
        static thread_local std::mt19937 rng{ std::random_device{}() };
        std::uniform_real_distribution<float> angleDist(0.0f, 360.0f);
        std::uniform_real_distribution<float> speedDist(def.speedMin, def.speedMax);
        std::uniform_real_distribution<float> lifeDist(def.lifetimeMin, def.lifetimeMax);
        std::uniform_real_distribution<float> sizeDist(def.sizeMin, def.sizeMax);
        
        for (int i = 0; i < count; ++i) {
            ParticleInstance p{};
            p.x = x; p.y = y;
            float angle = angleDist(rng) * 3.14159265f / 180.0f;
            float speed = speedDist(rng);
            p.vx = std::cos(angle) * speed;
            p.vy = std::sin(angle) * speed;
            p.maxLife = lifeDist(rng);
            p.life = p.maxLife;
            p.startSize = sizeDist(rng);
            p.endSize = def.endSize;
            p.size = p.startSize;
            p.startColor = def.colorStart;
            p.endColor = def.colorEnd;
            p.color = p.startColor;
            p.active = true;
            b.particles.push_back(p);
        }
        bursts.push_back(std::move(b));
    }
    
    void update(float dt) {
        for (auto& b : bursts) {
            b.elapsed += dt;
            for (auto& p : b.particles) {
                p.life -= dt;
                if (p.life <= 0.0f) { p.active = false; continue; }
                p.vy += 50.0f * dt; // gravity
                p.x += p.vx * dt;
                p.y += p.vy * dt;
                float t = 1.0f - std::clamp(p.life / p.maxLife, 0.0f, 1.0f);
                p.size = lerp(p.startSize, p.endSize, t);
                p.color.r = (Uint8)lerp((float)p.startColor.r, (float)p.endColor.r, t);
                p.color.g = (Uint8)lerp((float)p.startColor.g, (float)p.endColor.g, t);
                p.color.b = (Uint8)lerp((float)p.startColor.b, (float)p.endColor.b, t);
                p.color.a = (Uint8)lerp((float)p.startColor.a, (float)p.endColor.a, t);
            }
            if (b.elapsed >= b.duration) b.finished = true;
        }
        bursts.erase(std::remove_if(bursts.begin(), bursts.end(), [](const Burst& b){ return b.finished; }), bursts.end());
    }
    
    void render(SDL_Renderer* renderer, const Tools::Camera& camera, bool screenSpace = false) {
        std::vector<SDL_Vertex> vertexScratch;
        std::vector<int> indexScratch;
        for (auto& b : bursts) {
            for (auto& p : b.particles) {
                if (!p.active) continue;
                float half = std::max(p.size, 0.5f) * 0.5f * (screenSpace ? 1.0f : camera.zoom);
                SDL_FPoint c = screenSpace ? SDL_FPoint{p.x, p.y} : camera.worldToScreen(p.x, p.y);
                SDL_FColor col = { p.color.r/255.0f, p.color.g/255.0f, p.color.b/255.0f, p.color.a/255.0f };
                int base = (int)vertexScratch.size();
                vertexScratch.push_back({{c.x-half, c.y-half}, col, {0,0}});
                vertexScratch.push_back({{c.x+half, c.y-half}, col, {1,0}});
                vertexScratch.push_back({{c.x+half, c.y+half}, col, {1,1}});
                vertexScratch.push_back({{c.x-half, c.y+half}, col, {0,1}});
                indexScratch.push_back(base+0); indexScratch.push_back(base+1); indexScratch.push_back(base+2);
                indexScratch.push_back(base+0); indexScratch.push_back(base+2); indexScratch.push_back(base+3);
            }
        }
        if (!vertexScratch.empty()) {
            SDL_RenderGeometry(renderer, nullptr, vertexScratch.data(), (int)vertexScratch.size(), indexScratch.data(), (int)indexScratch.size());
        }
    }

public:
    std::vector<Burst> bursts;
    static float lerp(float a, float b, float t) { return a + (b - a) * t; }
};
} // namespace Particles

// ============================================================================
// Usage note (game.h / game.cpp) -- kept intentionally shallow:
//
//   game.h:
//       #include "particles.h"
//       ...
//       class GameScript : public ScriptBase {
//           ...
//           Particles::VanishSystem vanishEffects;
//       };
//
//   game.cpp, wherever the player currently dies / should vanish:
//       vanishEffects.start(player);   // defaults already look like the
//                                       // requested effect; pass a
//                                       // Particles::VanishConfig to
//                                       // customize duration/color/etc.
//
//   game.cpp, GameScript::onUpdate() (once per frame, any time):
//       vanishEffects.update(ctx->scene->world, dt);
//
//   game.cpp, RenderFrame()'s per-entity draw loop -- skip the entity's
//   normal sprite while it's vanishing so the wipe fully replaces it:
//       for (Entity i : renderOrder) {
//           if (script && script->vanishEffects.isActive(i)) continue;
//           ... existing render_entity_animation/texture calls ...
//       }
//
//   game.cpp, GameScript::onDraw() (called right after the entity loop):
//       vanishEffects.render(ctx->renderer, ctx->scene->world, ctx->camera);
//
// That's the entire integration surface; everything else (fade math,
// particle spawning/physics, batching) stays inside this header.
// ============================================================================
