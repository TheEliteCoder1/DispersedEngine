#pragma once

#include <box2d.h>
#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <vector>
#include <string>
#include <cmath>
#include <unordered_map>
#include <functional>
#include <memory>
#include <algorithm>
#include <cstdio>

namespace Physics {
    constexpr float PIXELS_PER_METER = 32.0f;
    constexpr float M_PI = 3.14;

    inline float PxToM(float px) {return px / PIXELS_PER_METER;};
    inline float MToPx(float m) {return m * PIXELS_PER_METER;};
    inline b2Vec2 PxToM(const b2Vec2& v) {return {v.x / PIXELS_PER_METER, v.y / PIXELS_PER_METER};};
    inline b2Vec2 MToPx(const b2Vec2& v) {return {v.x * PIXELS_PER_METER, v.y * PIXELS_PER_METER};};

    // Collision Layers (bitmasks)
    enum CollisionLayer {
        LAYER_1 = 0x0000,
        LAYER_2 = 0x0001,
        LAYER_3 = 0x0002,
        LAYER_4 = 0x0004,
        LAYER_5 = 0x0008,
        LAYER_6 = 0x0010,
        LAYER_7 = 0x0020,
        LAYER_ALL = 0xFFFF
    };

    enum class ShapeType {Circle, Triangle, Rectangle, Polygon, Chain};

    class PhysicsBody;
    class PhysicsWorld;

    class PhysicsBody {
        public:
            PhysicsBody(b2WorldId world, b2BodyType type, float xPx, float yPx)
            {
                b2BodyDef def = b2DefaultBodyDef();
                def.type = type;
                def.position = PxToM(b2Vec2{xPx, yPx});
                def.userData = this;
                m_body = b2CreateBody(world, &def);
            }

            ~PhysicsBody() {
                if (b2Body_IsValid(m_body)) b2DestroyBody(m_body);
            }

            b2ShapeId AddCircle(float radiusPx, float density, uint16_t category = LAYER_1, uint16_t mask = LAYER_ALL, bool sensor = false) {
                b2Circle c{};
                c.center = {0.0f, 0.0f};
                c.radius = PxToM(radiusPx);

                b2ShapeDef sd = b2DefaultShapeDef();
                sd.density = density;
                sd.material.friction = 0.4f;
                sd.material.restitution = 0.3f;
                sd.isSensor = sensor;
                sd.filter.categoryBits = category;
                sd.filter.maskBits = mask;

                b2ShapeId id = b2CreateCircleShape(m_body, &sd, &c);
                m_shapes.push_back(id);
                return id;
            }

            b2ShapeId AddRectangle(float wPx, float hPx, float density, uint16_t category = LAYER_1, uint16_t mask = LAYER_ALL, bool sensor = false)
            {
                b2Polygon box = b2MakeBox(PxToM(wPx) * 0.5f, PxToM(hPx) * 0.5f);
                b2ShapeDef sd = b2DefaultShapeDef();
                sd.density = density;
                sd.material.friction = 0.4f;
                sd.material.restitution = 0.3f;
                sd.isSensor = sensor;
                sd.filter.categoryBits = category;
                sd.filter.maskBits = mask;
                b2ShapeId id = b2CreatePolygonShape(m_body, &sd, &box);
                m_shapes.push_back(id);
                return id;
            }

            b2ShapeId AddConvexPolygon(const std::vector<b2Vec2>& pointsPx, float density, uint16_t category = LAYER_1, uint16_t mask = LAYER_ALL, bool sensor = false)
            {
                std::vector<b2Vec2> meters;
                meters.reserve(pointsPx.size());
                for (auto p : pointsPx) meters.push_back(PxToM(p));
                b2Hull hull = b2ComputeHull(meters.data(), (int)meters.size());
                if (hull.count < 3) return b2_nullShapeId;
                b2Polygon poly = b2MakePolygon(&hull, 0.0f);
                b2ShapeDef sd = b2DefaultShapeDef();
                sd.density = density;
                sd.material.friction = 0.4;
                sd.material.restitution = 0.3f;
                sd.isSensor = sensor;
                sd.filter.categoryBits = category;
                sd.filter.maskBits = mask;
            }

            b2ChainId AddChainOutline(const std::vector<b2Vec2>& pointsPx,
                              uint16_t category = LAYER_6,
                              uint16_t mask = LAYER_ALL)
            {
                std::vector<b2Vec2> meters;
                meters.reserve(pointsPx.size());
                for (auto p : pointsPx) meters.push_back(PxToM(p));

                b2ChainDef cd = b2DefaultChainDef();
                cd.points = meters.data();
                cd.count  = (int)meters.size();
                cd.isLoop = true;
                cd.filter.categoryBits = category;
                cd.filter.maskBits = mask;

                b2ChainId id = b2CreateChain(m_body, &cd);
                m_chains.push_back({id, std::move(meters)}); // keep points alive conceptually
                return id;
            }

            void DestroyAllShapes() {
                for (b2ShapeId s : m_shapes)
                    if (b2Shape_IsValid(s)) b2DestroyShape(s, true);
                m_shapes.clear();
                for (auto& c : m_chains)
                    if (b2Chain_IsValid(c.id)) b2DestroyChain(c.id);
                m_chains.clear();
            }

            // accessor methods
            b2BodyId       GetHandle()       { return m_body; }
            const b2BodyId GetHandle() const { return m_body; }

            b2Vec2 GetPositionPx() const { return MToPx(b2Body_GetPosition(m_body)); }
            float  GetAngleRad()   const { return b2Rot_GetAngle(b2Body_GetRotation(m_body)); }
            float  GetAngleDeg()   const { return GetAngleRad() * 180.0f / (float)M_PI; }

            void SetPositionPx(float x, float y) {
                b2Body_SetTransform(m_body, PxToM(b2Vec2{x, y}), b2Body_GetRotation(m_body));
            }
            void SetAngleDeg(float deg) {
                b2Body_SetTransform(m_body, b2Body_GetPosition(m_body), b2MakeRot(deg * (float)M_PI / 180.0f));
            }

            void ApplyForcePx(const b2Vec2& forcePx, bool wake = true) {
                b2Body_ApplyForceToCenter(m_body, forcePx, wake);
            }
            void ApplyImpulsePx(const b2Vec2& impulsePx, bool wake = true) {
                b2Body_ApplyLinearImpulseToCenter(m_body, impulsePx, wake);
            }
            void SetLinearVelocityPx(const b2Vec2& vPx) {
                b2Body_SetLinearVelocity(m_body, vPx);
            }
            b2Vec2 GetLinearVelocityPx() const {
                return b2Body_GetLinearVelocity(m_body);
            }

            void SetCategoryAndMask(uint16_t cat, uint16_t mask) {
                for (b2ShapeId s : m_shapes) {
                    b2Filter f = b2Shape_GetFilter(s);
                    f.categoryBits = cat;
                    f.maskBits = mask;
                    b2Shape_SetFilter(s, f);
                }
            }

            ShapeType GetShapeType() const { return m_kind; }
            void      SetShapeType(ShapeType k) { m_kind = k; }

            // Shape parameters kept around for editing / re-creation.
            float radiusPx = 0.0f;
            float widthPx  = 0.0f;
            float heightPx = 0.0f;
            std::vector<b2Vec2> polygonPointsPx;

            SDL_Texture* texture = nullptr;
            SDL_Color    tint    = { 220, 220, 255, 255 };

        private:
            b2BodyId m_body = b2_nullBodyId;
            ShapeType m_kind = ShapeType::Rectangle;

            std::vector<b2ShapeId> m_shapes;
            struct ChainEntry { b2ChainId id; std::vector<b2Vec2> points; };
            std::vector<ChainEntry> m_chains;
    };

    class RigidBody : public PhysicsBody {
        public:
            RigidBody(b2WorldId w, float x, float y) : PhysicsBody(w, b2_dynamicBody, x, y) {

            }      
    };

    class KinematicBody : public PhysicsBody {
        public: 
            KinematicBody(b2WorldId w, float x, float y) : PhysicsBody(w, b2_kinematicBody, x, y)
            {

            }

            void SetVelocityPx(float vx, float vy)
            {
                b2Body_SetLinearVelocity(GetHandle(), b2Vec2{vx, vy});
            }
    };

    class StaticBody : public PhysicsBody {
        public:
            StaticBody(b2WorldId w, float x, float y) : PhysicsBody(w, b2_staticBody, x, y) {}
    };

    class PhysicsWorld {
        public:
            using HitCallback = std::function<void(PhysicsBody* a, PhysicsBody* b, const b2ContactHitEvent& hit)>; 

            PhysicsWorld(float gravityYmPerSec = -9.81f) {
                b2WorldDef wd = b2DefaultWorldDef();
                wd.gravity = {0.0f, gravityYmPerSec};
                m_world = b2CreateWorld(&wd);
            }

            ~PhysicsWorld() {
                m_bodies.clear();
                if (b2World_IsValid(m_world)) b2DestroyWorld(m_world);
            }

            void Step(float dtSec, int subSteps = 4) {
                b2World_Step(m_world, dtSec, subSteps);
                
            }

            void OnHit(HitCallback cb) { m_hit = std::move(cb); }

            b2WorldId GetHandle() {return m_world;}

            void AddOwned(std::unique_ptr<PhysicsBody> b) 
            {
                m_bodies.push_back(std::move(b));
            }

            PhysicsBody* LastAdded() {return m_bodies.back().get();}

            const std::vector<std::unique_ptr<PhysicsBody>>& Bodies() const { return m_bodies; }

        private:
            void DispatchContactEvents() {
                if (!m_hit) return;
                b2ContactEvents ev = b2World_GetContactEvents(m_world);
                for (int i = 0; i < ev.hitCount; ++i) {
                    const b2ContactHitEvent& h = ev.hitEvents[i];
                    b2BodyId ba = b2Shape_GetBody(h.shapeIdA);
                    b2BodyId bb = b2Shape_GetBody(h.shapeIdB);
                    auto* pa = static_cast<PhysicsBody*>(b2Body_GetUserData(ba));
                    auto* pb = static_cast<PhysicsBody*>(b2Body_GetUserData(bb));
                    
                    // Fix: Use approachSpeed (measured in m/s) to filter the hit force 
                    // h.normal remains fully accessible inside m_hit if you need the direction
                    if (pa && pb && h.approachSpeed > 0.001f) {
                        m_hit(pa, pb, h);
                    }
                }
            }

            b2WorldId m_world = b2_nullWorldId;
            HitCallback m_hit;
            std::vector<std::unique_ptr<PhysicsBody>> m_bodies;
    };

    // Renderer helper for drawing outlines
    inline void DrawBodyOutline(SDL_Renderer* r, PhysicsBody* b) {
        b2Vec2 posPx = b->GetPositionPx();
        float  angle = b->GetAngleRad();
        float  c = std::cos(angle), s = std::sin(angle);

        auto localToScreen = [&](float lx, float ly) {
            return SDL_FPoint{
                posPx.x + (lx * c - ly * s),
                posPx.y + (lx * s + ly * c)
            };
        };

        SDL_SetRenderDrawColor(r, b->tint.r, b->tint.g, b->tint.b, b->tint.a);

        switch (b->GetShapeType()) {
            case ShapeType::Circle: {
                const int segs = 32;
                float rad = b->radiusPx;
                SDL_FPoint prev = localToScreen(rad, 0);
                for (int i = 1; i <= segs; ++i) {
                    float a = (float)i / segs * 2.0f * (float)M_PI;
                    SDL_FPoint cur = localToScreen(std::cos(a) * rad, std::sin(a) * rad);
                    SDL_RenderLine(r, prev.x, prev.y, cur.x, cur.y);
                    prev = cur;
                }
                SDL_FPoint tip = localToScreen(rad, 0);
                SDL_RenderLine(r, posPx.x, posPx.y, tip.x, tip.y);
                break;
            }
            case ShapeType::Rectangle: {
                float hw = b->widthPx * 0.5f, hh = b->heightPx * 0.5f;
                SDL_FPoint p0 = localToScreen(-hw, -hh);
                SDL_FPoint p1 = localToScreen( hw, -hh);
                SDL_FPoint p2 = localToScreen( hw,  hh);
                SDL_FPoint p3 = localToScreen(-hw,  hh);
                SDL_RenderLine(r, p0.x, p0.y, p1.x, p1.y);
                SDL_RenderLine(r, p1.x, p1.y, p2.x, p2.y);
                SDL_RenderLine(r, p2.x, p2.y, p3.x, p3.y);
                SDL_RenderLine(r, p3.x, p3.y, p0.x, p0.y);
                break;
            }
            case ShapeType::Triangle:
            case ShapeType::Polygon: {
                const auto& pts = b->polygonPointsPx;
                if (pts.size() < 2) break;
                for (size_t i = 0; i < pts.size(); ++i) {
                    SDL_FPoint a = localToScreen(pts[i].x, pts[i].y);
                    SDL_FPoint c = localToScreen(pts[(i+1) % pts.size()].x, pts[(i+1) % pts.size()].y);
                    SDL_RenderLine(r, a.x, a.y, c.x, c.y);
                }
                break;
            }
            case ShapeType::Chain: {
                const auto& pts = b->polygonPointsPx;
                if (pts.size() < 2) break;
                for (size_t i = 0; i < pts.size(); ++i) {
                    SDL_FPoint a = localToScreen(pts[i].x, pts[i].y);
                    SDL_FPoint c = localToScreen(pts[(i+1) % pts.size()].x, pts[(i+1) % pts.size()].y);
                    SDL_RenderLine(r, a.x, a.y, c.x, c.y);
                }
                break;
            }
        }
    }

    // Renderer helper for drawing body textures
    inline void DrawBodyTexture(SDL_Renderer* r, PhysicsBody* b) {
        if (!b->texture) return;
        float tw, th;
        SDL_GetTextureSize(b->texture, &tw, &th);
        b2Vec2 pos = b->GetPositionPx();
        SDL_FRect dst{ pos.x - tw * 0.5f, pos.y - th * 0.5f, (float)tw, (float)th };
        SDL_RenderTexture(r, b->texture, nullptr, &dst);
    }

    // interactive shape editor
    class ShapeEditor {
    public:
        enum class Mode { Idle, DrawingPolygon, Resizing };

        void BeginPolygon() {
            m_mode = Mode::DrawingPolygon;
            m_points.clear();
        }

        bool AddPolygonPoint(float x, float y) {
            if (m_mode != Mode::DrawingPolygon) return false;
            if (m_points.size() >= 3) {
                float dx = x - m_points.front().x;
                float dy = y - m_points.front().y;
                if (dx*dx + dy*dy < 10.0f * 10.0f) {
                    m_mode = Mode::Idle;
                    return true; // closed
                }
            }
            m_points.push_back({ x, y });
            return false;
        }

        void Cancel() { m_points.clear(); m_mode = Mode::Idle; }

        std::vector<b2Vec2> CenteredPolygon() const {
            if (m_points.empty()) return {};
            b2Vec2 c{0,0};
            for (auto p : m_points) { c.x += p.x; c.y += p.y; }
            c.x /= m_points.size(); c.y /= m_points.size();
            std::vector<b2Vec2> out;
            out.reserve(m_points.size());
            for (auto p : m_points) out.push_back({ p.x - c.x, p.y - c.y });
            return out;
        }
        b2Vec2 Centroid() const {
            b2Vec2 c{0,0};
            for (auto p : m_points) { c.x += p.x; c.y += p.y; }
            c.x /= (float)m_points.size(); c.y /= (float)m_points.size();
            return c;
        }

        const std::vector<b2Vec2>& Points() const { return m_points; }
        Mode GetMode() const { return m_mode; }

        PhysicsBody* target = nullptr;
        b2Vec2       dragStart{0,0};
        float        startRadius = 0;
        float        startW = 0, startH = 0;
        std::vector<b2Vec2> startPoly;

        void BeginResize(PhysicsBody* b, float mx, float my) {
            target = b;
            dragStart = { mx, my };
            startRadius = b->radiusPx;
            startW = b->widthPx;
            startH = b->heightPx;
            startPoly = b->polygonPointsPx;
            m_mode = Mode::Resizing;
        }
        void EndResize() { target = nullptr; m_mode = Mode::Idle; }

        void DrawInProgress(SDL_Renderer* r) const {
            if (m_mode != Mode::DrawingPolygon) return;
            SDL_SetRenderDrawColor(r, 255, 255, 0, 255);
            for (size_t i = 1; i < m_points.size(); ++i) {
                SDL_RenderLine(r, m_points[i-1].x, m_points[i-1].y, m_points[i].x, m_points[i].y);
            }
            float mx, my; SDL_GetMouseState(&mx, &my);
            if (!m_points.empty()) {
                SDL_RenderLine(r, m_points.back().x, m_points.back().y, (float)mx, (float)my);
                SDL_RenderLine(r, (float)mx, (float)my, m_points.front().x, m_points.front().y);
            }
        }

    private:
        Mode m_mode = Mode::Idle;
        std::vector<b2Vec2> m_points;
    };


};




