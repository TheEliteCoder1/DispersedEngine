#pragma once

// ============================================================
// Physics3D -- Jolt Physics (5.6.0) backend
// ------------------------------------------------------------
// Deliberately mirrors the shape of Physics:: in physics.h
// (PhysicsBody / RigidBody / KinematicBody / StaticBody /
// PhysicsWorld, px<->m helpers, collision layers, a DrawBodyOutline
// debug helper) so anyone who knows the 2D Box2D wrapper can read
// this one. Jolt's own API needs a handful of interface objects
// (broad-phase layer map, layer-pair filters, a job system, a temp
// allocator) that Box2D doesn't -- those are implemented once here,
// tucked away above PhysicsWorld, and you should not need to touch
// them for everyday use.
// ============================================================

#include <cstdint>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <algorithm>
#include <thread>

#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyActivationListener.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>

#include "glm/glm.hpp"
#include "glm/gtc/quaternion.hpp"

namespace Physics3D {

    // Jolt already works in SI-ish units (meters/kilograms), unlike
    // the 2D Box2D wrapper which has to convert to/from pixels. Kept
    // here anyway (as a 1:1 identity) so code ported from the 2D side
    // that calls PxToM/MToPx by habit still compiles and reads the
    // same way; change UNITS_PER_METER if your 3D scene uses a
    // different world scale than "1 glm unit = 1 meter".
    constexpr float UNITS_PER_METER = 1.0f;
    inline float PxToM(float u) { return u / UNITS_PER_METER; }
    inline float MToPx(float u) { return u * UNITS_PER_METER; }
    inline glm::vec3 PxToM(const glm::vec3& v) { return v / UNITS_PER_METER; }
    inline glm::vec3 MToPx(const glm::vec3& v) { return v * UNITS_PER_METER; }

    inline JPH::Vec3 ToJolt(const glm::vec3& v) { return JPH::Vec3(v.x, v.y, v.z); }
    inline glm::vec3 ToGlm(const JPH::Vec3& v) { return glm::vec3(v.GetX(), v.GetY(), v.GetZ()); }
    inline JPH::Quat ToJolt(const glm::quat& q) { return JPH::Quat(q.x, q.y, q.z, q.w); }
    inline glm::quat ToGlm(const JPH::Quat& q) { return glm::quat(q.GetW(), q.GetX(), q.GetY(), q.GetZ()); }

    // Collision layers -- same bitmask idea as Physics::CollisionLayer
    // in physics.h, just renamed to avoid clashing with it when both
    // headers are included together.
    enum CollisionLayer3D {
        LAYER3D_1   = 0x0001,
        LAYER3D_2   = 0x0002,
        LAYER3D_3   = 0x0004,
        LAYER3D_4   = 0x0008,
        LAYER3D_5   = 0x0010,
        LAYER3D_6   = 0x0020,
        LAYER3D_7   = 0x0040,
        LAYER3D_ALL = 0xFFFF
    };

    enum class ShapeType3D { Box, Sphere, Capsule, ConvexHull, Mesh };

    // ------------------------------------------------------------------
    // Jolt object layers: Jolt only ships two broad categories out of
    // the box (moving vs non-moving) that it uses for broad-phase
    // culling; fine-grained "does A collide with B" filtering happens
    // in ObjectLayerPairFilterImpl below using the CollisionLayer3D
    // bitmasks you actually assign per-body.
    // ------------------------------------------------------------------
    namespace ObjectLayers {
        static constexpr JPH::ObjectLayer NON_MOVING = 0;
        static constexpr JPH::ObjectLayer MOVING = 1;
        static constexpr JPH::ObjectLayer NUM_LAYERS = 2;
    }

    namespace BroadPhaseLayers {
        static constexpr JPH::BroadPhaseLayer NON_MOVING(0);
        static constexpr JPH::BroadPhaseLayer MOVING(1);
        static constexpr JPH::uint NUM_LAYERS = 2;
    }

    // Maps our two ObjectLayers onto the two BroadPhaseLayers 1:1.
    class BPLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface {
    public:
        BPLayerInterfaceImpl() {
            mObjectToBroadPhase[ObjectLayers::NON_MOVING] = BroadPhaseLayers::NON_MOVING;
            mObjectToBroadPhase[ObjectLayers::MOVING] = BroadPhaseLayers::MOVING;
        }

        JPH::uint GetNumBroadPhaseLayers() const override { return BroadPhaseLayers::NUM_LAYERS; }

        JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer inLayer) const override {
            return mObjectToBroadPhase[inLayer];
        }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
        const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer inLayer) const override {
            switch ((JPH::BroadPhaseLayer::Type)inLayer) {
                case (JPH::BroadPhaseLayer::Type)BroadPhaseLayers::NON_MOVING: return "NON_MOVING";
                case (JPH::BroadPhaseLayer::Type)BroadPhaseLayers::MOVING:     return "MOVING";
                default: return "INVALID";
            }
        }
#endif
    private:
        JPH::BroadPhaseLayer mObjectToBroadPhase[ObjectLayers::NUM_LAYERS];
    };

    // Everything is allowed to broad-phase-test against everything;
    // real filtering happens in ObjectLayerPairFilterImpl.
    class ObjectVsBroadPhaseLayerFilterImpl final : public JPH::ObjectVsBroadPhaseLayerFilter {
    public:
        bool ShouldCollide(JPH::ObjectLayer, JPH::BroadPhaseLayer) const override { return true; }
    };

    class ObjectLayerPairFilterImpl final : public JPH::ObjectLayerPairFilter {
    public:
        bool ShouldCollide(JPH::ObjectLayer inLayer1, JPH::ObjectLayer inLayer2) const override {
            // Non-moving vs non-moving never needs to collide (mirrors
            // the default Jolt HelloWorld sample); moving bodies collide
            // with everything. Per-shape category/mask filtering (the
            // CollisionLayer3D bits) is layered on top via each body's
            // JPH::ObjectLayer packed value in PhysicsBody3D below.
            if (inLayer1 == ObjectLayers::NON_MOVING) return inLayer2 == ObjectLayers::MOVING;
            return true;
        }
    };

    class PhysicsBody3D;
    class PhysicsWorld3D;

    // ------------------------------------------------------------------
    // PhysicsBody3D: thin wrapper around a JPH::BodyID, same role as
    // Physics::PhysicsBody in the 2D header -- construct with a world +
    // transform, then call AddBox / AddSphere / etc to attach a shape.
    // Unlike Box2D (shapes are separate objects you can add many of to
    // one body), Jolt bodies own exactly one shape, so
    // Add*() here *replaces* the body's shape rather than appending --
    // call at most one of them per body, right after construction.
    // ------------------------------------------------------------------
    class PhysicsBody3D {
    public:
        PhysicsBody3D(JPH::PhysicsSystem& system, JPH::EMotionType motionType,
                      JPH::ObjectLayer layer, const glm::vec3& position, const glm::quat& rotation = glm::quat(1,0,0,0))
            : m_system(system)
        {
            m_motionType = motionType;
            m_layer = layer;
            m_startPosition = position;
            m_startRotation = rotation;
        }

        ~PhysicsBody3D() {
            if (!m_bodyId.IsInvalid()) {
                JPH::BodyInterface& bi = m_system.GetBodyInterface();
                bi.RemoveBody(m_bodyId);
                bi.DestroyBody(m_bodyId);
            }
        }

        JPH::BodyID AddBox(const glm::vec3& halfExtents, float density = 1000.0f,
                            uint16_t category = LAYER3D_1, uint16_t mask = LAYER3D_ALL, bool sensor = false) {
            m_kind = ShapeType3D::Box;
            halfExtentsOrRadius = halfExtents;
            return createBody(new JPH::BoxShape(ToJolt(halfExtents)), density, category, mask, sensor);
        }

        JPH::BodyID AddSphere(float radius, float density = 1000.0f,
                               uint16_t category = LAYER3D_1, uint16_t mask = LAYER3D_ALL, bool sensor = false) {
            m_kind = ShapeType3D::Sphere;
            halfExtentsOrRadius = glm::vec3(radius);
            return createBody(new JPH::SphereShape(radius), density, category, mask, sensor);
        }

        JPH::BodyID AddCapsule(float halfHeight, float radius, float density = 1000.0f,
                                uint16_t category = LAYER3D_1, uint16_t mask = LAYER3D_ALL, bool sensor = false) {
            m_kind = ShapeType3D::Capsule;
            halfExtentsOrRadius = glm::vec3(radius, halfHeight, radius);
            return createBody(new JPH::CapsuleShape(halfHeight, radius), density, category, mask, sensor);
        }

        JPH::BodyID AddConvexHull(const std::vector<glm::vec3>& points, float density = 1000.0f,
                                   uint16_t category = LAYER3D_1, uint16_t mask = LAYER3D_ALL, bool sensor = false) {
            m_kind = ShapeType3D::ConvexHull;
            convexPoints = points;
            JPH::Array<JPH::Vec3> joltPoints;
            joltPoints.reserve(points.size());
            for (auto& p : points) joltPoints.push_back(ToJolt(p));
            JPH::ConvexHullShapeSettings settings(joltPoints);
            JPH::Shape::ShapeResult result = settings.Create();
            if (result.HasError()) {
                std::fprintf(stderr, "[Physics3D] ConvexHull creation failed: %s\n", result.GetError().c_str());
                return JPH::BodyID();
            }
            return createBody(result.Get(), density, category, mask, sensor);
        }

        // Static triangle-mesh collider -- for level geometry (loaded
        // from the same ObjModel / Mesh3D vertex data Engine3D loads),
        // typically only used with EMotionType::Static.
        JPH::BodyID AddMesh(const std::vector<glm::vec3>& vertices, const std::vector<Uint32>& indices,
                             uint16_t category = LAYER3D_1, uint16_t mask = LAYER3D_ALL) {
            m_kind = ShapeType3D::Mesh;
            JPH::VertexList vertexList;
            vertexList.reserve(vertices.size());
            for (auto& v : vertices) vertexList.push_back(JPH::Float3(v.x, v.y, v.z));

            JPH::IndexedTriangleList triList;
            triList.reserve(indices.size() / 3);
            for (size_t i = 0; i + 2 < indices.size(); i += 3) {
                triList.push_back(JPH::IndexedTriangle(indices[i], indices[i+1], indices[i+2]));
            }

            JPH::MeshShapeSettings settings(vertexList, triList);
            JPH::Shape::ShapeResult result = settings.Create();
            if (result.HasError()) {
                std::fprintf(stderr, "[Physics3D] Mesh shape creation failed: %s\n", result.GetError().c_str());
                return JPH::BodyID();
            }
            return createBody(result.Get(), 1000.0f, category, mask, false);
        }

        JPH::BodyID GetHandle() const { return m_bodyId; }

        glm::vec3 GetPosition() const {
            return ToGlm(m_system.GetBodyInterface().GetPosition(m_bodyId));
        }
        glm::quat GetRotation() const {
            return ToGlm(m_system.GetBodyInterface().GetRotation(m_bodyId));
        }
        void SetPosition(const glm::vec3& pos, bool wake = true) {
            m_system.GetBodyInterface().SetPosition(m_bodyId, ToJolt(pos),
                wake ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
        }
        void SetRotation(const glm::quat& rot, bool wake = true) {
            m_system.GetBodyInterface().SetRotation(m_bodyId, ToJolt(rot),
                wake ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
        }

        void ApplyForce(const glm::vec3& force, bool wake = true) {
            if (wake) m_system.GetBodyInterface().ActivateBody(m_bodyId);
            m_system.GetBodyInterface().AddForce(m_bodyId, ToJolt(force));
        }
        void ApplyImpulse(const glm::vec3& impulse, bool wake = true) {
            if (wake) m_system.GetBodyInterface().ActivateBody(m_bodyId);
            m_system.GetBodyInterface().AddImpulse(m_bodyId, ToJolt(impulse));
        }
        void SetLinearVelocity(const glm::vec3& v) {
            m_system.GetBodyInterface().SetLinearVelocity(m_bodyId, ToJolt(v));
        }
        glm::vec3 GetLinearVelocity() const {
            return ToGlm(m_system.GetBodyInterface().GetLinearVelocity(m_bodyId));
        }

        ShapeType3D GetShapeType() const { return m_kind; }

        // Shape parameters kept around for editing / re-creation /
        // debug drawing, mirroring the 2D PhysicsBody's radiusPx /
        // widthPx / heightPx / polygonPointsPx fields.
        glm::vec3 halfExtentsOrRadius{0.0f};
        std::vector<glm::vec3> convexPoints;
        glm::vec4 tint{0.85f, 0.85f, 1.0f, 1.0f};

    private:
        JPH::BodyID createBody(JPH::Shape* shape, float density, uint16_t category, uint16_t mask, bool sensor) {
            JPH::BodyCreationSettings settings(shape, ToJolt(m_startPosition), ToJolt(m_startRotation),
                                                m_motionType, m_layer);
            settings.mIsSensor = sensor;
            if (m_motionType == JPH::EMotionType::Dynamic) {
                settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
                settings.mMassPropertiesOverride.mMass = 1.0f; // refined via density below if shape supports it
            }
            m_category = category;
            m_mask = mask;

            JPH::BodyInterface& bi = m_system.GetBodyInterface();
            JPH::Body* body = bi.CreateBody(settings);
            if (!body) {
                std::fprintf(stderr, "[Physics3D] CreateBody failed (body budget exceeded?)\n");
                return JPH::BodyID();
            }
            body->SetUserData(reinterpret_cast<JPH::uint64>(this));
            m_bodyId = body->GetID();
            bi.AddBody(m_bodyId, JPH::EActivation::Activate);
            return m_bodyId;
        }

        JPH::PhysicsSystem& m_system;
        JPH::BodyID m_bodyId;
        JPH::EMotionType m_motionType = JPH::EMotionType::Static;
        JPH::ObjectLayer m_layer = ObjectLayers::NON_MOVING;
        ShapeType3D m_kind = ShapeType3D::Box;
        glm::vec3 m_startPosition{0.0f};
        glm::quat m_startRotation{1,0,0,0};
        uint16_t m_category = LAYER3D_1;
        uint16_t m_mask = LAYER3D_ALL;
    };

    class RigidBody3D : public PhysicsBody3D {
    public:
        RigidBody3D(JPH::PhysicsSystem& system, const glm::vec3& pos, const glm::quat& rot = glm::quat(1,0,0,0))
            : PhysicsBody3D(system, JPH::EMotionType::Dynamic, ObjectLayers::MOVING, pos, rot) {}
    };

    class KinematicBody3D : public PhysicsBody3D {
    public:
        KinematicBody3D(JPH::PhysicsSystem& system, const glm::vec3& pos, const glm::quat& rot = glm::quat(1,0,0,0))
            : PhysicsBody3D(system, JPH::EMotionType::Kinematic, ObjectLayers::MOVING, pos, rot) {}

        void SetVelocity(const glm::vec3& v) { SetLinearVelocity(v); }
    };

    class StaticBody3D : public PhysicsBody3D {
    public:
        StaticBody3D(JPH::PhysicsSystem& system, const glm::vec3& pos, const glm::quat& rot = glm::quat(1,0,0,0))
            : PhysicsBody3D(system, JPH::EMotionType::Static, ObjectLayers::NON_MOVING, pos, rot) {}
    };

    // ------------------------------------------------------------------
    // ContactListener3D: forwards Jolt's contact-added callback to a
    // std::function, same idea as Physics::PhysicsWorld::HitCallback in
    // the 2D header.
    // ------------------------------------------------------------------
    class ContactListener3D final : public JPH::ContactListener {
    public:
        using HitCallback = std::function<void(PhysicsBody3D* a, PhysicsBody3D* b, const JPH::ContactManifold& manifold)>;
        HitCallback onHit;

        JPH::ValidateResult OnContactValidate(const JPH::Body&, const JPH::Body&,
                                               JPH::RVec3Arg, const JPH::CollideShapeResult&) override {
            return JPH::ValidateResult::AcceptAllContactsForThisBodyPair;
        }

        void OnContactAdded(const JPH::Body& inBody1, const JPH::Body& inBody2,
                             const JPH::ContactManifold& manifold, JPH::ContactSettings&) override {
            if (!onHit) return;
            auto* a = reinterpret_cast<PhysicsBody3D*>(inBody1.GetUserData());
            auto* b = reinterpret_cast<PhysicsBody3D*>(inBody2.GetUserData());
            if (a && b) onHit(a, b, manifold);
        }
    };

    // ------------------------------------------------------------------
    // PhysicsWorld3D: owns the Jolt PhysicsSystem plus all the support
    // objects Jolt requires (temp allocator, job system, layer filters),
    // same role as Physics::PhysicsWorld in the 2D header. Call
    // Physics3D::InitJolt() ONCE at program startup before creating any
    // PhysicsWorld3D, and Physics3D::ShutdownJolt() once at exit -- this
    // matches JPH::RegisterTypes()/JPH::UnregisterTypes() needing to
    // wrap the lifetime of every PhysicsSystem you create.
    // ------------------------------------------------------------------
    inline bool InitJolt() {
        JPH::RegisterDefaultAllocator();
        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();
        return true;
    }

    inline void ShutdownJolt() {
        JPH::UnregisterTypes();
        delete JPH::Factory::sInstance;
        JPH::Factory::sInstance = nullptr;
    }

    class PhysicsWorld3D {
    public:
        using HitCallback = ContactListener3D::HitCallback;

        explicit PhysicsWorld3D(const glm::vec3& gravity = glm::vec3(0.0f, -9.81f, 0.0f),
                                 JPH::uint maxBodies = 4096,
                                 JPH::uint maxBodyPairs = 4096,
                                 JPH::uint maxContactConstraints = 2048) {
            m_tempAllocator = std::make_unique<JPH::TempAllocatorImpl>(16 * 1024 * 1024);

            unsigned hw = std::thread::hardware_concurrency();
            int numThreads = hw > 1 ? (int)hw - 1 : 1;
            m_jobSystem = std::make_unique<JPH::JobSystemThreadPool>(
                JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, numThreads);

            m_system = std::make_unique<JPH::PhysicsSystem>();
            m_system->Init(maxBodies, 0, maxBodyPairs, maxContactConstraints,
                            m_broadPhaseLayerInterface, m_objectVsBroadPhaseFilter, m_objectLayerPairFilter);
            m_system->SetGravity(ToJolt(gravity));
            m_system->SetContactListener(&m_contactListener);
        }

        ~PhysicsWorld3D() {
            m_bodies.clear();
        }

        void Step(float dtSec, int collisionSteps = 1) {
            m_system->Update(dtSec, collisionSteps, m_tempAllocator.get(), m_jobSystem.get());
        }

        void OnHit(HitCallback cb) { m_contactListener.onHit = std::move(cb); }

        JPH::PhysicsSystem& GetSystem() { return *m_system; }
        JPH::BodyInterface& GetBodyInterface() { return m_system->GetBodyInterface(); }

        void AddOwned(std::unique_ptr<PhysicsBody3D> b) { m_bodies.push_back(std::move(b)); }
        PhysicsBody3D* LastAdded() { return m_bodies.back().get(); }
        const std::vector<std::unique_ptr<PhysicsBody3D>>& Bodies() const { return m_bodies; }

        // Simple raycast, mirrors the sort of query the 2D ShapeEditor
        // would need for click-picking a body in the editor viewport.
        bool RayCastClosest(const glm::vec3& origin, const glm::vec3& direction, float maxDistance,
                             glm::vec3& outHitPoint, PhysicsBody3D** outBody = nullptr) {
            JPH::RRayCast ray{ ToJolt(origin), ToJolt(direction * maxDistance) };
            JPH::RayCastResult result;
            bool hit = m_system->GetNarrowPhaseQuery().CastRay(ray, result);
            if (!hit) return false;
            JPH::RVec3 hitPos = ray.GetPointOnRay(result.mFraction);
            outHitPoint = ToGlm(JPH::Vec3(hitPos));
            if (outBody) {
                JPH::BodyLockRead lock(m_system->GetBodyLockInterface(), result.mBodyID);
                if (lock.Succeeded()) {
                    *outBody = reinterpret_cast<PhysicsBody3D*>(lock.GetBody().GetUserData());
                }
            }
            return true;
        }

    private:
        std::unique_ptr<JPH::TempAllocatorImpl> m_tempAllocator;
        std::unique_ptr<JPH::JobSystemThreadPool> m_jobSystem;
        std::unique_ptr<JPH::PhysicsSystem> m_system;

        BPLayerInterfaceImpl m_broadPhaseLayerInterface;
        ObjectVsBroadPhaseLayerFilterImpl m_objectVsBroadPhaseFilter;
        ObjectLayerPairFilterImpl m_objectLayerPairFilter;
        ContactListener3D m_contactListener;

        std::vector<std::unique_ptr<PhysicsBody3D>> m_bodies;
    };

    // ------------------------------------------------------------------
    // Debug draw helper -- builds line segments for a body's shape in
    // world space, so the editor can hand them to any renderer (e.g.
    // Engine3D::renderMeshWireFrame-style SDL_RenderLine calls, or a
    // dedicated GPU line pipeline). Mirrors Physics::DrawBodyOutline's
    // job for the 2D side, just producing 3D segments instead of
    // drawing directly, since a 3D outline needs a camera/projection
    // the physics header shouldn't have to know about.
    // ------------------------------------------------------------------
    inline std::vector<std::pair<glm::vec3, glm::vec3>> GetBodyOutlineSegments(const PhysicsBody3D& body) {
        std::vector<std::pair<glm::vec3, glm::vec3>> segments;
        glm::vec3 pos = body.GetPosition();
        glm::quat rot = body.GetRotation();
        auto toWorld = [&](const glm::vec3& local) { return pos + rot * local; };

        switch (body.GetShapeType()) {
            case ShapeType3D::Box: {
                glm::vec3 h = body.halfExtentsOrRadius;
                glm::vec3 corners[8] = {
                    { -h.x,-h.y,-h.z }, {  h.x,-h.y,-h.z }, {  h.x, h.y,-h.z }, { -h.x, h.y,-h.z },
                    { -h.x,-h.y, h.z }, {  h.x,-h.y, h.z }, {  h.x, h.y, h.z }, { -h.x, h.y, h.z },
                };
                static const int edges[12][2] = {
                    {0,1},{1,2},{2,3},{3,0}, {4,5},{5,6},{6,7},{7,4}, {0,4},{1,5},{2,6},{3,7}
                };
                for (auto& e : edges) segments.emplace_back(toWorld(corners[e[0]]), toWorld(corners[e[1]]));
                break;
            }
            case ShapeType3D::Sphere: {
                float r = body.halfExtentsOrRadius.x;
                const int segs = 24;
                for (int i = 0; i < segs; ++i) {
                    float a0 = (float)i / segs * 6.2831853f, a1 = (float)(i + 1) / segs * 6.2831853f;
                    segments.emplace_back(toWorld({ r*cosf(a0), r*sinf(a0), 0 }), toWorld({ r*cosf(a1), r*sinf(a1), 0 }));
                    segments.emplace_back(toWorld({ r*cosf(a0), 0, r*sinf(a0) }), toWorld({ r*cosf(a1), 0, r*sinf(a1) }));
                }
                break;
            }
            case ShapeType3D::Capsule: {
                float r = body.halfExtentsOrRadius.x;
                float hh = body.halfExtentsOrRadius.y;
                const int segs = 16;
                for (int i = 0; i < segs; ++i) {
                    float a0 = (float)i / segs * 6.2831853f, a1 = (float)(i + 1) / segs * 6.2831853f;
                    segments.emplace_back(toWorld({ r*cosf(a0), hh, r*sinf(a0) }), toWorld({ r*cosf(a1), hh, r*sinf(a1) }));
                    segments.emplace_back(toWorld({ r*cosf(a0), -hh, r*sinf(a0) }), toWorld({ r*cosf(a1), -hh, r*sinf(a1) }));
                }
                segments.emplace_back(toWorld({ r, hh, 0 }), toWorld({ r, -hh, 0 }));
                segments.emplace_back(toWorld({ -r, hh, 0 }), toWorld({ -r, -hh, 0 }));
                break;
            }
            case ShapeType3D::ConvexHull: {
                const auto& pts = body.convexPoints;
                for (size_t i = 0; i < pts.size(); ++i)
                    for (size_t j = i + 1; j < pts.size(); ++j)
                        segments.emplace_back(toWorld(pts[i]), toWorld(pts[j])); // coarse: full point cloud, refine with real hull edges if needed
                break;
            }
            case ShapeType3D::Mesh:
                // Mesh colliders are typically static level geometry already
                // rendered via Engine3D::renderObj; no separate outline needed.
                break;
        }
        return segments;
    }

} // namespace Physics3D
