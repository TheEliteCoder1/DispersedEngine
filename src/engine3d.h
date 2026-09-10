#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <limits>
#include <iostream>
#include <cstring>
#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtc/type_ptr.hpp"
#include "glm/gtc/quaternion.hpp"
#define GLM_ENABLE_EXPERIMENTAL
#include "glm/gtx/matrix_decompose.hpp"
#include "glm/gtx/quaternion.hpp"

#ifndef TINYOBJLOADER_IMPLEMENTATION
#define TINYOBJLOADER_IMPLEMENTATION
#endif
#include <tiny_obj_loader.h>

// ── FBX / glTF importers ────────────────────────────────────
// CRITICAL: these (and anything they transitively pull in --
// fastgltf especially drags in <variant>, <memory_resource>,
// <mutex>, etc.) MUST be included here, at file scope, before
// `namespace Engine3D {` opens below.
//
// Previously these lived inside model_loaders_3d.h, which was
// #included *from inside* `namespace Engine3D { ... }` (further
// down this file). That's fine for ordinary code, but it is NOT
// fine for third-party headers: the preprocessor just pastes
// their text in place, so any standard-library header being
// included for the very first time in the translation unit --
// like <variant>, dragged in by fastgltf/types.hpp -- had its
// `namespace std { ... }` block land *inside* the already-open
// `namespace Engine3D { ... }`, producing a bogus nested
// `Engine3D::std` instead of extending the real `::std`.
// Everything downstream that expected real std::variant /
// std::uint8_t / std::mutex then failed to compile, with error
// messages that literally say `Engine3D::std::<unnamed-tag>` --
// that IS this bug, not a coincidence.
// Rule of thumb: third-party #includes always go at file scope,
// never inside your own namespace, even for headers you only
// use from inside that namespace -- the *using* is namespace-safe,
// the *including* never is.
#include <fastgltf/core.hpp>
#include <fastgltf/types.hpp>
#include <fastgltf/tools.hpp>
#include <ufbx.h>

// ============================================================
// Engine3D
// ------------------------------------------------------------
// Mirrors the resource-manager / "load once, Get() by name"
// pattern used by Texture::Manager / Font::Manager in engine.h,
// but for GPU-resident 3D resources (models, materials, GPU
// textures/samplers) driven through the raw SDL_GPU API.
//
// Two rendering paths live here side by side:
//   1. Legacy CPU wireframe projection (renderMeshWireFrame) --
//      kept as-is for 2D-canvas / debug use, draws through
//      SDL_Renderer, no GPU pipeline required.
//   2. New SDL_GPU driven renderObj() path -- loads faces +
//      materials (diffuse color/texture) via tinyobjloader and
//      renders solid, lit, textured geometry through a real
//      SDL_GPU graphics pipeline (see ObjModel / GpuObjRenderer
//      below).
// ============================================================


namespace Engine3D {
    enum class CanvasMode {
        Mode2D = 0,
        Mode3D
    };

    struct Transform3D {
        glm::vec3 position{0.0f, 0.0f, 0.0f};
        glm::vec3 rotation{0.0f, 0.0f, 0.0f};
        glm::vec3 scale{1.0f, 1.0f, 1.0f};

        glm::mat4 toMatrix() const {
            glm::mat4 model = glm::mat4(1.0f);
            model = glm::translate(model, position);
            model = glm::rotate(model, glm::radians(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
            model = glm::rotate(model, glm::radians(rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
            model = glm::rotate(model, glm::radians(rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
            model = glm::scale(model, scale);
            return model;
        }
    };

    // ------------------------------------------------------------------
    // Legacy CPU-side mesh (flat vertex soup, no indices, no materials).
    // Still useful for quick wireframe previews / editor gizmos.
    // ------------------------------------------------------------------
    struct Mesh3D {
        std::string objFilePath;
        bool isLoaded = false;
        std::vector<float> vertices;
        std::vector<float> normals;
        std::vector<float> texcoords;
    };

    inline bool loadMesh(const std::string& filepath, Mesh3D& outMesh) {
        tinyobj::attrib_t attrib;
        std::vector<tinyobj::shape_t> shapes;
        std::vector<tinyobj::material_t> materials;
        std::string warn, err;

        // Resolve the .mtl relative to the .obj's own directory instead of
        // the process's working directory (tinyobj's default when no
        // basedir is given), same fix as loadObjModel() below.
        std::string baseDir;
        size_t slash = filepath.find_last_of("/\\");
        baseDir = (slash == std::string::npos) ? "./" : filepath.substr(0, slash + 1);

        bool ret = tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err,
                                     filepath.c_str(), baseDir.c_str());

        if (!warn.empty()) {
            std::cout << "[Engine3D] Warning: " << warn << std::endl;
        }
        if (!err.empty()) {
            std::cerr << "[Engine3D] Error: " << err << std::endl;
        }
        if (!ret) {
            return false;
        }

        outMesh.vertices = attrib.vertices;
        outMesh.normals = attrib.normals;
        outMesh.texcoords = attrib.texcoords;
        outMesh.objFilePath = filepath;
        outMesh.isLoaded = true;
        return true;
    }

    inline void renderMeshWireFrame(SDL_Renderer* renderer, const Transform3D& transform, const Mesh3D& mesh, float viewportWidth, float viewportHeight) {
        if (!mesh.isLoaded || mesh.vertices.empty()) return;
        glm::mat4 model = transform.toMatrix();

        SDL_SetRenderDrawColor(renderer, 0, 200, 255, 255);

        auto projectPoint = [&](float x, float y, float z) -> SDL_FPoint {
            glm::vec4 p = model * glm::vec4(x, y, z, 1.0f);
            float fov = 400.0f;
            float zDistance = 5.0f + p.z;
            float depth = (zDistance > 0.1f) ? zDistance : 0.1f;

            float screenX = (p.x * fov) / depth + (viewportWidth * 0.5f);
            float screenY = (p.y * fov) / depth + (viewportHeight * 0.5f);
            return { screenX, screenY };
        };

        for (size_t i = 0; i + 8 < mesh.vertices.size(); i += 9) {
            SDL_FPoint p1 = projectPoint(mesh.vertices[i],     mesh.vertices[i+1], mesh.vertices[i+2]);
            SDL_FPoint p2 = projectPoint(mesh.vertices[i+3],   mesh.vertices[i+4], mesh.vertices[i+5]);
            SDL_FPoint p3 = projectPoint(mesh.vertices[i+6],   mesh.vertices[i+7], mesh.vertices[i+8]);

            SDL_RenderLine(renderer, p1.x, p1.y, p2.x, p2.y);
            SDL_RenderLine(renderer, p2.x, p2.y, p3.x, p3.y);
            SDL_RenderLine(renderer, p3.x, p3.y, p1.x, p1.y);
        }
    }

    // ==================================================================
    // GPU OBJ + material rendering
    // ==================================================================

    // Interleaved GPU vertex layout: position, normal, texcoord.
    struct Vertex3D {
        glm::vec3 position;
        glm::vec3 normal;
        glm::vec2 texcoord;
    };

    // Uniform block pushed to the vertex shader (slot 0). std140-ish
    // layout: two mat4s, 16-byte aligned, matches a GLSL block of
    // `mat4 mvp; mat4 model;`.
    struct ObjVertexUniforms {
        glm::mat4 mvp;
        glm::mat4 model;
    };

    // Uniform block pushed to the fragment shader (slot 0): basic
    // directional-light Lambert term + material tint, matches GLSL
    // `vec4 baseColor; vec4 lightDirAndPad; vec4 ambient;`.
    struct ObjFragmentUniforms {
        glm::vec4 baseColor{1.0f};
        glm::vec4 lightDir{-0.4f, -1.0f, -0.3f, 0.0f};
        glm::vec4 ambient{0.15f, 0.15f, 0.18f, 1.0f};
    };

    struct Material3D {
        std::string name;
        glm::vec3 diffuseColor{0.8f, 0.8f, 0.8f};
        std::string diffuseTexturePath; // empty => use default white texture
        SDL_GPUTexture* gpuTexture = nullptr; // owned unless it's the shared default
        SDL_GPUSampler* gpuSampler = nullptr; // owned unless it's the shared default
        bool ownsTexture = false;
        bool ownsSampler = false;
    };

    // One draw call's worth of a model: a contiguous run of indices
    // that all share the same material.
    struct SubMesh3D {
        int materialIndex = -1; // index into ObjModel::materials, -1 = untextured default
        Uint32 indexOffset = 0; // offset in indices, in *elements* not bytes
        Uint32 indexCount = 0;
    };

    // Fully GPU-resident model: one big vertex/index buffer plus a
    // list of material-grouped submesh draw ranges. This is what
    // renderObj() actually draws.
    struct ObjModel {
        std::string objFilePath;
        bool isLoaded = false;

        SDL_GPUBuffer* vertexBuffer = nullptr;
        SDL_GPUBuffer* indexBuffer = nullptr;
        Uint32 vertexCount = 0;
        Uint32 indexCount = 0;

        std::vector<Material3D> materials;
        std::vector<SubMesh3D> submeshes;

        // Local-space bounding box, handy for camera framing.
        glm::vec3 boundsMin{0.0f};
        glm::vec3 boundsMax{0.0f};
    };

    // Shared 1x1 white texture/sampler used by materials that have no
    // diffuse texture, so the shader can always sample *something*.
    struct DefaultGpuResources {
        SDL_GPUTexture* whiteTexture = nullptr;
        SDL_GPUSampler* linearSampler = nullptr;
        bool isLoaded = false;
    };

    inline std::vector<unsigned char> readBinaryFile(const std::string& path) {
        SDL_IOStream* io = SDL_IOFromFile(path.c_str(), "rb");
        if (!io) {
            std::cerr << "[Engine3D] Could not open file: " << path << " (" << SDL_GetError() << ")" << std::endl;
            return {};
        }
        Sint64 size = SDL_GetIOSize(io);
        std::vector<unsigned char> buffer(size > 0 ? (size_t)size : 0);
        if (size > 0) {
            SDL_ReadIO(io, buffer.data(), (size_t)size);
        }
        SDL_CloseIO(io);
        return buffer;
    }

    inline SDL_GPUShader* loadShaderSPV(SDL_GPUDevice* device,
                                        const std::string& path,
                                        SDL_GPUShaderStage stage,
                                        Uint32 samplerCount = 0,
                                        Uint32 uniformBufferCount = 1) {
        std::vector<unsigned char> code = readBinaryFile(path);
        if (code.empty()) {
            std::cerr << "[Engine3D] Failed to load shader bytecode: " << path << std::endl;
            return nullptr;
        }

        SDL_GPUShaderCreateInfo info = {};
        info.code = code.data();
        info.code_size = code.size();
        info.entrypoint = "main";
        info.format = SDL_GPU_SHADERFORMAT_SPIRV;
        info.stage = stage;
        info.num_samplers = samplerCount;
        info.num_uniform_buffers = uniformBufferCount;
        info.num_storage_buffers = 0;
        info.num_storage_textures = 0;

        SDL_GPUShader* shader = SDL_CreateGPUShader(device, &info);
        if (!shader) {
            std::cerr << "[Engine3D] Failed to create shader from " << path << ": " << SDL_GetError() << std::endl;
        }
        return shader;
    }

    // Uploads raw bytes into a freshly-created GPU buffer of the given
    // usage, via a transfer buffer + copy pass (the standard SDL_GPU
    // upload dance).
    inline SDL_GPUBuffer* createAndUploadBuffer(SDL_GPUDevice* device,
                                                SDL_GPUBufferUsageFlags usage,
                                                const void* data,
                                                Uint32 sizeBytes) {
        if (sizeBytes == 0) return nullptr;

        SDL_GPUBufferCreateInfo bufInfo = {};
        bufInfo.usage = usage;
        bufInfo.size = sizeBytes;
        SDL_GPUBuffer* buffer = SDL_CreateGPUBuffer(device, &bufInfo);
        if (!buffer) {
            std::cerr << "[Engine3D] Failed to create GPU buffer: " << SDL_GetError() << std::endl;
            return nullptr;
        }

        SDL_GPUTransferBufferCreateInfo tbInfo = {};
        tbInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        tbInfo.size = sizeBytes;
        SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(device, &tbInfo);
        if (!transfer) {
            std::cerr << "[Engine3D] Failed to create transfer buffer: " << SDL_GetError() << std::endl;
            SDL_ReleaseGPUBuffer(device, buffer);
            return nullptr;
        }

        void* mapped = SDL_MapGPUTransferBuffer(device, transfer, false);
        if (mapped) {
            std::memcpy(mapped, data, sizeBytes);
            SDL_UnmapGPUTransferBuffer(device, transfer);
        }

        SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
        if (cmd) {
            SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(cmd);
            SDL_GPUTransferBufferLocation src = {};
            src.transfer_buffer = transfer;
            src.offset = 0;
            SDL_GPUBufferRegion dst = {};
            dst.buffer = buffer;
            dst.offset = 0;
            dst.size = sizeBytes;
            SDL_UploadToGPUBuffer(copyPass, &src, &dst, false);
            SDL_EndGPUCopyPass(copyPass);
            SDL_SubmitGPUCommandBuffer(cmd);
        }

        SDL_ReleaseGPUTransferBuffer(device, transfer);
        return buffer;
    }

    // Loads an image file straight to an RGBA32 GPU texture + a
    // default linear sampler. Used for material diffuse maps.
    inline bool loadTextureToGPU(SDL_GPUDevice* device,
                                  const std::string& path,
                                  SDL_GPUTexture** outTexture,
                                  SDL_GPUSampler** outSampler) {
        SDL_Surface* surface = IMG_Load(path.c_str());
        if (!surface) {
            std::cerr << "[Engine3D] Failed to load texture " << path << ": " << SDL_GetError() << std::endl;
            return false;
        }
        SDL_Surface* rgba = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32);
        SDL_DestroySurface(surface);
        if (!rgba) {
            std::cerr << "[Engine3D] Failed to convert texture " << path << std::endl;
            return false;
        }

        SDL_GPUTextureCreateInfo texInfo = {};
        texInfo.type = SDL_GPU_TEXTURETYPE_2D;
        texInfo.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        texInfo.width = (Uint32)rgba->w;
        texInfo.height = (Uint32)rgba->h;
        texInfo.layer_count_or_depth = 1;
        texInfo.num_levels = 1;
        texInfo.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;

        SDL_GPUTexture* texture = SDL_CreateGPUTexture(device, &texInfo);
        if (!texture) {
            std::cerr << "[Engine3D] Failed to create GPU texture: " << SDL_GetError() << std::endl;
            SDL_DestroySurface(rgba);
            return false;
        }

        Uint32 dataSize = (Uint32)(rgba->w * rgba->h * 4);
        SDL_GPUTransferBufferCreateInfo tbInfo = {};
        tbInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        tbInfo.size = dataSize;
        SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(device, &tbInfo);
        if (transfer) {
            void* mapped = SDL_MapGPUTransferBuffer(device, transfer, false);
            if (mapped) {
                std::memcpy(mapped, rgba->pixels, dataSize);
                SDL_UnmapGPUTransferBuffer(device, transfer);
            }
            SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
            if (cmd) {
                SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(cmd);
                SDL_GPUTextureTransferInfo src = {};
                src.transfer_buffer = transfer;
                src.offset = 0;
                SDL_GPUTextureRegion dst = {};
                dst.texture = texture;
                dst.w = (Uint32)rgba->w;
                dst.h = (Uint32)rgba->h;
                dst.d = 1;
                SDL_UploadToGPUTexture(copyPass, &src, &dst, false);
                SDL_EndGPUCopyPass(copyPass);
                SDL_SubmitGPUCommandBuffer(cmd);
            }
            SDL_ReleaseGPUTransferBuffer(device, transfer);
        }
        SDL_DestroySurface(rgba);

        SDL_GPUSamplerCreateInfo sampInfo = {};
        sampInfo.min_filter = SDL_GPU_FILTER_LINEAR;
        sampInfo.mag_filter = SDL_GPU_FILTER_LINEAR;
        sampInfo.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
        sampInfo.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
        sampInfo.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
        sampInfo.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;

        SDL_GPUSampler* sampler = SDL_CreateGPUSampler(device, &sampInfo);
        if (!sampler) {
            std::cerr << "[Engine3D] Failed to create sampler: " << SDL_GetError() << std::endl;
            SDL_ReleaseGPUTexture(device, texture);
            return false;
        }

        *outTexture = texture;
        *outSampler = sampler;
        return true;
    }

    // Creates the shared 1x1 white fallback texture/sampler, mirroring
    // the "always have *a* resource to bind" convention used by
    // Texture::Manager in engine.h (never leave a draw call with a
    // null sampler binding).
    inline bool createDefaultGpuResources(SDL_GPUDevice* device, DefaultGpuResources& out) {
        if (out.isLoaded) return true;

        SDL_GPUTextureCreateInfo texInfo = {};
        texInfo.type = SDL_GPU_TEXTURETYPE_2D;
        texInfo.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        texInfo.width = 1;
        texInfo.height = 1;
        texInfo.layer_count_or_depth = 1;
        texInfo.num_levels = 1;
        texInfo.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
        out.whiteTexture = SDL_CreateGPUTexture(device, &texInfo);
        if (!out.whiteTexture) return false;

        Uint8 whitePixel[4] = { 255, 255, 255, 255 };
        SDL_GPUTransferBufferCreateInfo tbInfo = {};
        tbInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        tbInfo.size = 4;
        SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(device, &tbInfo);
        if (transfer) {
            void* mapped = SDL_MapGPUTransferBuffer(device, transfer, false);
            if (mapped) { std::memcpy(mapped, whitePixel, 4); SDL_UnmapGPUTransferBuffer(device, transfer); }
            SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
            if (cmd) {
                SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(cmd);
                SDL_GPUTextureTransferInfo src = {}; src.transfer_buffer = transfer; src.offset = 0;
                SDL_GPUTextureRegion dst = {}; dst.texture = out.whiteTexture; dst.w = 1; dst.h = 1; dst.d = 1;
                SDL_UploadToGPUTexture(copyPass, &src, &dst, false);
                SDL_EndGPUCopyPass(copyPass);
                SDL_SubmitGPUCommandBuffer(cmd);
            }
            SDL_ReleaseGPUTransferBuffer(device, transfer);
        }

        SDL_GPUSamplerCreateInfo sampInfo = {};
        sampInfo.min_filter = SDL_GPU_FILTER_LINEAR;
        sampInfo.mag_filter = SDL_GPU_FILTER_LINEAR;
        sampInfo.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
        sampInfo.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
        sampInfo.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
        sampInfo.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
        out.linearSampler = SDL_CreateGPUSampler(device, &sampInfo);
        if (!out.linearSampler) return false;

        out.isLoaded = true;
        return true;
    }

    inline void destroyDefaultGpuResources(SDL_GPUDevice* device, DefaultGpuResources& res) {
        if (!device) return;
        if (res.whiteTexture) { SDL_ReleaseGPUTexture(device, res.whiteTexture); res.whiteTexture = nullptr; }
        if (res.linearSampler) { SDL_ReleaseGPUSampler(device, res.linearSampler); res.linearSampler = nullptr; }
        res.isLoaded = false;
    }

    // ------------------------------------------------------------------
    // loadObjModel: parses an .obj (+ its .mtl, resolved relative to the
    // obj's own directory, same as tinyobjloader's default behaviour),
    // builds one interleaved vertex/index buffer for the whole model,
    // grouped into per-material SubMesh3D draw ranges, and uploads any
    // referenced diffuse textures to the GPU.
    // ------------------------------------------------------------------
    inline bool loadObjModel(SDL_GPUDevice* device,
                              const std::string& objPath,
                              DefaultGpuResources& defaults,
                              ObjModel& outModel) {
        if (!createDefaultGpuResources(device, defaults)) {
            SDL_Log("[Engine3D] Could not create default GPU resources.\n");
            return false;
        }

        tinyobj::attrib_t attrib;
        std::vector<tinyobj::shape_t> shapes;
        std::vector<tinyobj::material_t> materials;
        std::string warn, err;

        // tinyobj's mtl_basedir must be a DIRECTORY (trailing slash), not the
        // path to the .mtl file itself -- tinyobj reads the mtllib line out
        // of the .obj and appends that filename onto this directory itself.
        // Derive it from objPath's own folder so "cube.mtl" next to
        // "cube.obj" resolves correctly regardless of process CWD.
        std::string baseDir;
        size_t slash = objPath.find_last_of("/\\");
        baseDir = (slash == std::string::npos) ? "./" : objPath.substr(0, slash + 1);

        bool ret = tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err,
                                     objPath.c_str(), baseDir.c_str(), true /* triangulate */);
        if (!warn.empty()) std::cout << "[Engine3D] Warning loading " << objPath << ": " << warn << std::endl;
        if (!err.empty())  std::cerr << "[Engine3D] Error loading " << objPath << ": " << err << std::endl;
        if (!ret) return false;

        // ---- Build one CPU-side vertex/index list, bucketed by material ----
        // key = material id (or -1 for "no material"), value = indices
        // (into the shared vertex vector) for that material's triangles.
        std::vector<Vertex3D> vertices;
        std::unordered_map<int, std::vector<Uint32>> indicesByMaterial;

        // Deduplicate identical (pos,normal,uv) tuples per obj_index so
        // shared vertices aren't repeated. Key by a string of the three
        // tinyobj indices, simple and good enough for typical models.
        std::unordered_map<std::string, Uint32> vertexCache;

        glm::vec3 bmin( std::numeric_limits<float>::max());
        glm::vec3 bmax(-std::numeric_limits<float>::max());

        for (const auto& shape : shapes) {
            size_t indexOffset = 0;
            for (size_t f = 0; f < shape.mesh.num_face_vertices.size(); ++f) {
                int fv = shape.mesh.num_face_vertices[f];
                int matId = (f < shape.mesh.material_ids.size()) ? shape.mesh.material_ids[f] : -1;

                for (int v = 0; v < fv; ++v) {
                    tinyobj::index_t idx = shape.mesh.indices[indexOffset + v];

                    std::string key = std::to_string(idx.vertex_index) + "_" +
                                       std::to_string(idx.normal_index) + "_" +
                                       std::to_string(idx.texcoord_index);

                    auto found = vertexCache.find(key);
                    Uint32 vIndex;
                    if (found != vertexCache.end()) {
                        vIndex = found->second;
                    } else {
                        Vertex3D vert{};
                        vert.position = glm::vec3(
                            attrib.vertices[3 * idx.vertex_index + 0],
                            attrib.vertices[3 * idx.vertex_index + 1],
                            attrib.vertices[3 * idx.vertex_index + 2]);

                        if (idx.normal_index >= 0 && (size_t)(3 * idx.normal_index + 2) < attrib.normals.size()) {
                            vert.normal = glm::vec3(
                                attrib.normals[3 * idx.normal_index + 0],
                                attrib.normals[3 * idx.normal_index + 1],
                                attrib.normals[3 * idx.normal_index + 2]);
                        } else {
                            vert.normal = glm::vec3(0.0f, 1.0f, 0.0f);
                        }

                        if (idx.texcoord_index >= 0 && (size_t)(2 * idx.texcoord_index + 1) < attrib.texcoords.size()) {
                            vert.texcoord = glm::vec2(
                                attrib.texcoords[2 * idx.texcoord_index + 0],
                                1.0f - attrib.texcoords[2 * idx.texcoord_index + 1]); // flip V for GPU convention
                        } else {
                            vert.texcoord = glm::vec2(0.0f, 0.0f);
                        }

                        bmin = glm::min(bmin, vert.position);
                        bmax = glm::max(bmax, vert.position);

                        vIndex = (Uint32)vertices.size();
                        vertices.push_back(vert);
                        vertexCache.emplace(key, vIndex);
                    }

                    indicesByMaterial[matId].push_back(vIndex);
                }
                indexOffset += fv;
            }
        }

        if (vertices.empty()) {
            std::cerr << "[Engine3D] " << objPath << " produced no geometry." << std::endl;
            return false;
        }

        // ---- Flatten per-material index buckets into one index buffer ----
        std::vector<Uint32> flatIndices;
        std::vector<SubMesh3D> submeshes;
        flatIndices.reserve(vertices.size());
        for (auto& kv : indicesByMaterial) {
            int matId = kv.first;
            std::vector<Uint32>& idxList = kv.second;
            SubMesh3D sub;
            sub.materialIndex = matId;
            sub.indexOffset = (Uint32)flatIndices.size();
            sub.indexCount = (Uint32)idxList.size();
            flatIndices.insert(flatIndices.end(), idxList.begin(), idxList.end());
            submeshes.push_back(sub);
        }

        // ---- Upload vertex + index buffers ----
        outModel.vertexBuffer = createAndUploadBuffer(device, SDL_GPU_BUFFERUSAGE_VERTEX,
                                                       vertices.data(),
                                                       (Uint32)(vertices.size() * sizeof(Vertex3D)));
        outModel.indexBuffer = createAndUploadBuffer(device, SDL_GPU_BUFFERUSAGE_INDEX,
                                                      flatIndices.data(),
                                                      (Uint32)(flatIndices.size() * sizeof(Uint32)));
        if (!outModel.vertexBuffer || !outModel.indexBuffer) {
            std::cerr << "[Engine3D] Failed to upload vertex/index buffers for " << objPath << std::endl;
            return false;
        }

        outModel.vertexCount = (Uint32)vertices.size();
        outModel.indexCount = (Uint32)flatIndices.size();
        outModel.submeshes = std::move(submeshes);
        outModel.boundsMin = bmin;
        outModel.boundsMax = bmax;

        // ---- Materials: diffuse color + optional diffuse texture ----
        outModel.materials.reserve(materials.size());
        for (const auto& m : materials) {
            Material3D mat;
            mat.name = m.name;
            mat.diffuseColor = glm::vec3(m.diffuse[0], m.diffuse[1], m.diffuse[2]);
            if (!m.diffuse_texname.empty()) {
                mat.diffuseTexturePath = baseDir + m.diffuse_texname;
                SDL_GPUTexture* tex = nullptr;
                SDL_GPUSampler* samp = nullptr;
                if (loadTextureToGPU(device, mat.diffuseTexturePath, &tex, &samp)) {
                    mat.gpuTexture = tex;
                    mat.gpuSampler = samp;
                    mat.ownsTexture = true;
                    mat.ownsSampler = true;
                } else {
                    std::cerr << "[Engine3D] Falling back to white texture for material '" << mat.name << "'" << std::endl;
                }
            }
            if (!mat.gpuTexture) {
                mat.gpuTexture = defaults.whiteTexture;
                mat.gpuSampler = defaults.linearSampler;
                mat.ownsTexture = false;
                mat.ownsSampler = false;
            }
            outModel.materials.push_back(mat);
        }

        outModel.objFilePath = objPath;
        outModel.isLoaded = true;
        return true;
    }

    inline void destroyObjModel(SDL_GPUDevice* device, ObjModel& model) {
        if (!device) return;
        for (auto& mat : model.materials) {
            if (mat.ownsTexture && mat.gpuTexture) SDL_ReleaseGPUTexture(device, mat.gpuTexture);
            if (mat.ownsSampler && mat.gpuSampler) SDL_ReleaseGPUSampler(device, mat.gpuSampler);
        }
        model.materials.clear();
        model.submeshes.clear();
        if (model.vertexBuffer) { SDL_ReleaseGPUBuffer(device, model.vertexBuffer); model.vertexBuffer = nullptr; }
        if (model.indexBuffer) { SDL_ReleaseGPUBuffer(device, model.indexBuffer); model.indexBuffer = nullptr; }
        model.isLoaded = false;
    }

    // ------------------------------------------------------------------
    // Depth texture helper -- solid-face rendering needs a depth buffer
    // (the wireframe path never did, since it just drew lines).
    // ------------------------------------------------------------------
    inline SDL_GPUTexture* createDepthTexture(SDL_GPUDevice* device, Uint32 width, Uint32 height,
                                               SDL_GPUTextureFormat format = SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT) {
        SDL_GPUTextureCreateInfo info = {};
        info.type = SDL_GPU_TEXTURETYPE_2D;
        info.format = format;
        info.width = width;
        info.height = height;
        info.layer_count_or_depth = 1;
        info.num_levels = 1;
        info.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
        SDL_GPUTexture* tex = SDL_CreateGPUTexture(device, &info);
        if (!tex) std::cerr << "[Engine3D] Failed to create depth texture: " << SDL_GetError() << std::endl;
        return tex;
    }

    // ------------------------------------------------------------------
    // Pipeline creation for the obj renderer. Expects the vertex shader
    // to consume Vertex3D (pos vec3, normal vec3, uv vec2) at
    // locations 0/1/2, one uniform buffer (ObjVertexUniforms) at vertex
    // slot 0, and the fragment shader to sample one texture (slot 0)
    // plus one uniform buffer (ObjFragmentUniforms) at fragment slot 0.
    // ------------------------------------------------------------------
    inline SDL_GPUGraphicsPipeline* createObjPipeline(SDL_GPUDevice* device,
                                                       SDL_GPUShader* vertexShader,
                                                       SDL_GPUShader* fragmentShader,
                                                       SDL_GPUTextureFormat colorFormat,
                                                       SDL_GPUTextureFormat depthFormat = SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT) {
        SDL_GPUVertexBufferDescription vbDesc = {};
        vbDesc.slot = 0;
        vbDesc.pitch = sizeof(Vertex3D);
        vbDesc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

        SDL_GPUVertexAttribute attrs[3] = {};
        attrs[0].location = 0; attrs[0].buffer_slot = 0;
        attrs[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
        attrs[0].offset = offsetof(Vertex3D, position);

        attrs[1].location = 1; attrs[1].buffer_slot = 0;
        attrs[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
        attrs[1].offset = offsetof(Vertex3D, normal);

        attrs[2].location = 2; attrs[2].buffer_slot = 0;
        attrs[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
        attrs[2].offset = offsetof(Vertex3D, texcoord);

        SDL_GPUColorTargetDescription colorTarget = {};
        colorTarget.format = colorFormat;
        colorTarget.blend_state.enable_blend = false;

        SDL_GPUGraphicsPipelineCreateInfo info = {};
        info.vertex_shader = vertexShader;
        info.fragment_shader = fragmentShader;
        info.vertex_input_state.num_vertex_buffers = 1;
        info.vertex_input_state.vertex_buffer_descriptions = &vbDesc;
        info.vertex_input_state.num_vertex_attributes = 3;
        info.vertex_input_state.vertex_attributes = attrs;
        info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;

        info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_BACK;
        info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
        info.rasterizer_state.enable_depth_clip = true;

        info.depth_stencil_state.enable_depth_test = true;
        info.depth_stencil_state.enable_depth_write = true;
        info.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS;

        info.target_info.num_color_targets = 1;
        info.target_info.color_target_descriptions = &colorTarget;
        info.target_info.has_depth_stencil_target = true;
        info.target_info.depth_stencil_format = depthFormat;

        SDL_GPUGraphicsPipeline* pipeline = SDL_CreateGPUGraphicsPipeline(device, &info);
        if (!pipeline) {
            std::cerr << "[Engine3D] Failed to create obj pipeline: " << SDL_GetError() << std::endl;
        }
        return pipeline;
    }

    // ------------------------------------------------------------------
    // renderObj: draws every submesh of `model` with its own material
    // (diffuse texture + tint), into a render pass that has ALREADY
    // been begun by the caller (mirrors how renderMeshWireFrame expects
    // an active SDL_Renderer -- here it's an active SDL_GPURenderPass
    // instead). Caller is responsible for BeginGPURenderPass/EndGPURenderPass
    // and command buffer submission.
    // ------------------------------------------------------------------
    inline void renderObj(SDL_GPUCommandBuffer* cmd,
                           SDL_GPURenderPass* pass,
                           SDL_GPUGraphicsPipeline* pipeline,
                           const ObjModel& model,
                           const Transform3D& transform,
                           const glm::mat4& view,
                           const glm::mat4& projection,
                           const DefaultGpuResources& defaults) {
        if (!model.isLoaded || !pipeline) return;

        SDL_BindGPUGraphicsPipeline(pass, pipeline);

        SDL_GPUBufferBinding vBinding = {};
        vBinding.buffer = model.vertexBuffer;
        vBinding.offset = 0;
        SDL_BindGPUVertexBuffers(pass, 0, &vBinding, 1);

        SDL_GPUBufferBinding iBinding = {};
        iBinding.buffer = model.indexBuffer;
        iBinding.offset = 0;
        SDL_BindGPUIndexBuffer(pass, &iBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);

        glm::mat4 modelMat = transform.toMatrix();
        ObjVertexUniforms vUniforms{};
        vUniforms.mvp = projection * view * modelMat;
        vUniforms.model = modelMat;
        SDL_PushGPUVertexUniformData(cmd, 0, &vUniforms, sizeof(vUniforms));

        for (const auto& sub : model.submeshes) {
            const Material3D* mat = nullptr;
            if (sub.materialIndex >= 0 && (size_t)sub.materialIndex < model.materials.size()) {
                mat = &model.materials[sub.materialIndex];
            }

            SDL_GPUTextureSamplerBinding texBinding = {};
            texBinding.texture = mat ? mat->gpuTexture : defaults.whiteTexture;
            texBinding.sampler = mat ? mat->gpuSampler : defaults.linearSampler;
            SDL_BindGPUFragmentSamplers(pass, 0, &texBinding, 1);

            ObjFragmentUniforms fUniforms{};
            fUniforms.baseColor = glm::vec4(mat ? mat->diffuseColor : glm::vec3(0.8f), 1.0f);
            SDL_PushGPUFragmentUniformData(cmd, 0, &fUniforms, sizeof(fUniforms));

            SDL_DrawGPUIndexedPrimitives(pass, sub.indexCount, 1, sub.indexOffset, 0, 0);
        }
    }

    // ==================================================================
    // CanvasRenderTarget3D
    // ------------------------------------------------------------------
    // Bundles the offscreen GPU color texture, depth texture, and the
    // SDL_Texture wrapper used to blit the 3D viewport into the 2D
    // editor canvas -- and, critically, keeps all three sized EXACTLY
    // to the on-screen canvas rect.
    //
    // Rendering into a fixed-size texture (e.g. a hardcoded 1390x690)
    // and then stretching it into a canvas rect of a *different* size
    // via SDL_RenderTexture is what makes geometry look distorted: the
    // projection matrix's aspect ratio and the final on-screen aspect
    // ratio disagree, so everything gets non-uniformly squashed or
    // stretched on the blit. Calling resize() whenever the canvas rect
    // changes keeps the render target's pixel dimensions identical to
    // the destination rect, so the blit is always 1:1 and the aspect
    // ratio used for the projection matrix is always correct.
    // ==================================================================
    struct CanvasRenderTarget3D {
        SDL_GPUTexture* colorTexture = nullptr;
        SDL_GPUTexture* depthTexture = nullptr;
        SDL_Texture* sdlTexture = nullptr; // wraps colorTexture for 2D blitting
        Uint32 width = 0;
        Uint32 height = 0;

        bool isValid() const { return colorTexture != nullptr && sdlTexture != nullptr; }

        // Creates (or recreates, if the size actually changed) all three
        // resources at newWidth x newHeight. Cheap no-op if the size
        // already matches. Call this every frame with the current canvas
        // rect size -- it only does real work when the size changes
        // (e.g. the editor panel was resized).
        bool resize(SDL_GPUDevice* device, SDL_Renderer* renderer, Uint32 newWidth, Uint32 newHeight) {
            newWidth = std::max(newWidth, 1u);
            newHeight = std::max(newHeight, 1u);
            if (newWidth == width && newHeight == height && isValid()) return true;

            destroy(device);

            SDL_GPUTextureCreateInfo texInfo = {};
            texInfo.type = SDL_GPU_TEXTURETYPE_2D;
            texInfo.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
            texInfo.width = newWidth;
            texInfo.height = newHeight;
            texInfo.layer_count_or_depth = 1;
            texInfo.num_levels = 1;
            texInfo.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
            colorTexture = SDL_CreateGPUTexture(device, &texInfo);
            if (!colorTexture) {
                std::cerr << "[Engine3D] CanvasRenderTarget3D: failed to create color texture: " << SDL_GetError() << std::endl;
                return false;
            }

            depthTexture = createDepthTexture(device, newWidth, newHeight);
            if (!depthTexture) return false;

            SDL_PropertiesID props = SDL_CreateProperties();
            SDL_SetPointerProperty(props, SDL_PROP_TEXTURE_CREATE_GPU_TEXTURE_POINTER, colorTexture);
            SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_FORMAT_NUMBER, SDL_PIXELFORMAT_RGBA32);
            SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_ACCESS_NUMBER, SDL_TEXTUREACCESS_STATIC);
            SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER, newWidth);
            SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER, newHeight);
            sdlTexture = SDL_CreateTextureWithProperties(renderer, props);
            SDL_DestroyProperties(props);
            if (!sdlTexture) {
                std::cerr << "[Engine3D] CanvasRenderTarget3D: failed to wrap color texture: " << SDL_GetError() << std::endl;
                return false;
            }

            width = newWidth;
            height = newHeight;
            return true;
        }

        float aspect() const { return height > 0 ? (float)width / (float)height : 1.0f; }

        void destroy(SDL_GPUDevice* device) {
            if (sdlTexture) { SDL_DestroyTexture(sdlTexture); sdlTexture = nullptr; }
            if (device) {
                if (depthTexture) { SDL_ReleaseGPUTexture(device, depthTexture); depthTexture = nullptr; }
                if (colorTexture) { SDL_ReleaseGPUTexture(device, colorTexture); colorTexture = nullptr; }
            }
            width = height = 0;
        }
    };

    // ==================================================================
    // Camera3D -- free-fly "editor" camera with Godot 4 viewport-style
    // controls (right-drag to look + WASD/QE to fly while held,
    // middle-drag to pan, scroll to dolly/adjust fly speed).
    //
    // Mirrors Tools::Camera's API surface from engine.h on purpose --
    // setupForCanvas(), setBounds(), a pan()-shaped operation, bounds
    // clamping -- so it reads the same way to anyone already familiar
    // with the 2D editor camera. Position/rotation are unconstrained;
    // world boundaries are opt-in via setBounds() and only affect
    // clampToBounds().
    // ==================================================================
    class Camera3D {
    public:
        // FIXED: was position{0,1.5,-4.5} with yawDeg=90 (looking straight
        // down +Z). That aims the camera almost exactly parallel to the
        // world Z axis, which makes the Z-axis ruler (drawn in
        // renderAxisRulerGizmo3D below) collapse toward the vanishing
        // point -- every tick along Z projects to nearly the same
        // handful of screen pixels, so its numbers overlap into an
        // unreadable smear instead of reading as a ruled line. An
        // angled 3/4 start view (same idea as Blender/Unity/Godot's
        // default editor camera) keeps X, Y, and Z all visually
        // distinct: Y stays vertical, X and Z both recede diagonally
        // into the screen at readable angles.
        glm::vec3 position{-4.0f, 3.0f, -4.0f};
        float yawDeg = 45.0f;    // 0 = looking down +X; 90 = looking down +Z
        float pitchDeg = -25.0f;
        float fovDeg = 60.0f;
        float nearPlane = 0.1f;
        float farPlane = 1000.0f;

        float moveSpeed = 4.0f;             // world units / sec
        float fastMoveMultiplier = 3.0f;    // while holding Shift
        float mouseLookSensitivity = 0.15f; // degrees per pixel of mouse delta
        float panSpeed = 0.01f;             // world units per pixel of mouse delta
        float dollySpeed = 0.75f;           // world units per wheel notch

        // Screen-space viewport this camera is bound to, same role as
        // Tools::Camera's offsetX/offsetY (set via setupForCanvas) --
        // lets callers ask "is the mouse over my canvas" without main.cpp
        // needing to track the rect separately.
        float canvasX = 0, canvasY = 0, canvasW = 0, canvasH = 0;

        bool boundsEnabled = false;
        glm::vec3 boundsMin{-10.0f, -10.0f, -10.0f};
        glm::vec3 boundsMax{ 10.0f,  10.0f,  10.0f};

        void setupForCanvas(float x, float y, float w, float h) {
            canvasX = x; canvasY = y; canvasW = w; canvasH = h;
        }

        void setBounds(const glm::vec3& minB, const glm::vec3& maxB, bool enabled = true) {
            boundsMin = minB; boundsMax = maxB; boundsEnabled = enabled;
        }

        bool isScreenPointInCanvas(float sx, float sy) const {
            return sx >= canvasX && sx <= canvasX + canvasW &&
                   sy >= canvasY && sy <= canvasY + canvasH;
        }

        glm::vec3 getForward() const {
            float yr = glm::radians(yawDeg), pr = glm::radians(pitchDeg);
            return glm::normalize(glm::vec3(
                std::cos(pr) * std::cos(yr),
                std::sin(pr),
                std::cos(pr) * std::sin(yr)));
        }
        glm::vec3 getRight() const {
            return glm::normalize(glm::cross(getForward(), glm::vec3(0.0f, 1.0f, 0.0f)));
        }
        glm::vec3 getUp() const {
            return glm::normalize(glm::cross(getRight(), getForward()));
        }

        glm::vec3 getScreenToWorldRay(float screenX, float screenY) const {
            float aspect = (canvasH > 0.0f) ? (canvasW / canvasH) : 1.0f;
            glm::mat4 view = getViewMatrix();
            glm::mat4 proj = getProjectionMatrix(aspect, false);
            glm::mat4 invVP = glm::inverse(proj * view);
            
            // Normalize screen coords to [-1, 1]
            float nx = (2.0f * (screenX - canvasX)) / canvasW - 1.0f;
            float ny = 1.0f - (2.0f * (screenY - canvasY)) / canvasH;
            
            glm::vec4 rayClip = glm::vec4(nx, ny, -1.0f, 1.0f);
            glm::vec4 rayEye = glm::inverse(proj) * rayClip;
            rayEye.z = -1.0f; 
            rayEye.w = 0.0f;
            
            glm::vec3 rayDir = glm::normalize(glm::vec3(glm::inverse(view) * rayEye));
            return rayDir;
        }

        // ── Godot-4-editor-style input handlers ──────────────────────
        // Called from main.cpp's event loop / per-frame input poll.
        // Mouse capture / cursor hiding while flying is a windowing
        // concern main.cpp owns, same split of responsibility as the
        // 2D Tools::Camera (which never touches SDL_SetWindowRelativeMouseMode
        // itself either).

        // Right-mouse-drag: FPS-style look.
        virtual void lookMouseDelta(float dxPixels, float dyPixels) {
            yawDeg += dxPixels * mouseLookSensitivity;
            pitchDeg -= dyPixels * mouseLookSensitivity;
            pitchDeg = std::clamp(pitchDeg, -89.0f, 89.0f);
        }

        // Middle-mouse-drag: pan on the camera's local right/up plane.
        virtual void panMouseDelta(float dxPixels, float dyPixels) {
            glm::vec3 right = getRight();
            glm::vec3 up = getUp();
            position -= right * (dxPixels * panSpeed);
            position += up * (dyPixels * panSpeed);
        }

        // Scroll wheel: dolly forward/back when not flying (RMB up), or
        // bump fly speed when RMB is held -- same dual role scroll plays
        // in Godot's 3D viewport.
        virtual void scroll(float wheelDelta, bool rmbHeld) {
            if (rmbHeld) {
                moveSpeed = std::clamp(moveSpeed * (wheelDelta > 0.0f ? 1.15f : 0.87f), 0.25f, 50.0f);
            } else {
                position += getForward() * (wheelDelta * dollySpeed);
            }
        }

        // WASD + Q/E fly movement. Only meaningful while RMB is held in
        // the editor (matches Godot: hold right-click to "enter" fly
        // mode) -- main.cpp gates the call on that, not this function.
        virtual void flyMove(float dt, bool forward, bool back, bool left, bool right,
                              bool up, bool down, bool fast) {
            float speed = moveSpeed * (fast ? fastMoveMultiplier : 1.0f) * dt;
            glm::vec3 f = getForward();
            glm::vec3 r = getRight();
            if (forward) position += f * speed;
            if (back)    position -= f * speed;
            if (right)   position += r * speed;
            if (left)    position -= r * speed;
            if (up)      position.y += speed;
            if (down)    position.y -= speed;
        }

        virtual void clampToBounds() {
            if (!boundsEnabled) return;
            position = glm::clamp(position, boundsMin, boundsMax);
        }

        glm::mat4 getViewMatrix() const {
            return glm::lookAt(position, position + getForward(), glm::vec3(0.0f, 1.0f, 0.0f));
        }

        // flipY: SDL_GPU's Vulkan backend has clip-space +Y pointing the
        // opposite way from GL, same fixup main.cpp used to do by hand.
        glm::mat4 getProjectionMatrix(float aspect, bool flipY = true) const {
            glm::mat4 proj = glm::perspective(glm::radians(fovDeg), aspect, nearPlane, farPlane);
            if (flipY) proj[1][1] *= -1.0f;
            return proj;
        }

        // ── CPU-side world -> screen-pixel projection ────────────────
        // For anything drawn with SDL_Renderer on top of the blitted 3D
        // render target (editor gizmos, entity outline overlays, debug
        // physics shapes) rather than through the GPU pipeline -- same
        // role Tools::Camera::worldToScreen() plays for the 2D canvas.
        // Uses ordinary top-left-origin window pixel coordinates (no
        // Vulkan clip-space flip -- that's only needed inside the GPU
        // pass itself), mapped over this camera's canvasX/Y/W/H viewport
        // (set via setupForCanvas(), same rect the GPU render target is
        // sized to). Returns visible=false if the point is behind the
        // camera (would otherwise project to a bogus on-screen location
        // after the perspective divide) so callers can skip drawing it
        // or clip the segment instead.
        struct ScreenPoint { SDL_FPoint pt; bool visible; };
        ScreenPoint worldToScreenCPU(const glm::vec3& worldPos) const {
            glm::mat4 view = getViewMatrix();
            float aspect = (canvasH > 0.0f) ? (canvasW / canvasH) : 1.0f;
            glm::mat4 proj = getProjectionMatrix(aspect, false); // no Vulkan flip for CPU pixel math
            glm::vec4 clip = proj * view * glm::vec4(worldPos, 1.0f);
            if (clip.w <= 0.0001f) return { {0,0}, false }; // behind camera / at the eye
            glm::vec3 ndc = glm::vec3(clip) / clip.w; // [-1,1]
            SDL_FPoint p;
            p.x = canvasX + (ndc.x * 0.5f + 0.5f) * canvasW;
            p.y = canvasY + (1.0f - (ndc.y * 0.5f + 0.5f)) * canvasH; // NDC +Y is up, screen +Y is down
            return { p, true };
        }

        virtual ~Camera3D() = default;
    };

    // ------------------------------------------------------------------
    // Example subclasses showing how a project (as opposed to the editor)
    // would specialize Camera3D for gameplay -- same bounds/projection
    // plumbing inherited from the base, different movement rules. Neither
    // is wired into the editor; they're starting points for project code.
    // ------------------------------------------------------------------
    class FirstPersonCamera3D : public Camera3D {
    public:
        float eyeHeight = 1.7f;

        // Locks the camera to eyeHeight above whatever ground Y the game
        // feeds in and ignores vertical fly input -- typical FPS
        // controller movement instead of free-fly.
        void moveOnGround(float dt, bool forward, bool back, bool left, bool right,
                           bool sprint, float groundY) {
            float speed = moveSpeed * (sprint ? fastMoveMultiplier : 1.0f) * dt;
            glm::vec3 f = getForward(); f.y = 0.0f;
            if (glm::length(f) > 0.0001f) f = glm::normalize(f);
            glm::vec3 r = getRight(); r.y = 0.0f;
            if (glm::length(r) > 0.0001f) r = glm::normalize(r);
            if (forward) position += f * speed;
            if (back)    position -= f * speed;
            if (right)   position += r * speed;
            if (left)    position -= r * speed;
            position.y = groundY + eyeHeight;
            clampToBounds();
        }
    };

    class ThirdPersonCamera3D : public Camera3D {
    public:
        glm::vec3 target{0.0f};
        float distance = 5.0f;
        float minDistance = 1.5f;
        float maxDistance = 20.0f;

        // Orbits around `target` using the inherited yaw/pitch instead of
        // flying position around freely.
        void orbit(float dxPixels, float dyPixels) {
            yawDeg += dxPixels * mouseLookSensitivity;
            pitchDeg = std::clamp(pitchDeg - dyPixels * mouseLookSensitivity, -80.0f, 80.0f);
            updateFromOrbit();
        }
        void zoom(float wheelDelta) {
            distance = std::clamp(distance - wheelDelta * dollySpeed, minDistance, maxDistance);
            updateFromOrbit();
        }
        void updateFromOrbit() {
            position = target - getForward() * distance; // camera sits behind target, looking at it
            clampToBounds();
        }
    };

    // ==================================================================
    // 3D axis/ruler gizmo
    // ------------------------------------------------------------------
    // The 2D editor's ruler (render_canvas_ruler in engine.h) is two
    // fixed strips glued to the top/left of the canvas. That doesn't
    // make sense in 3D -- there is no "top-left" of a scene with depth
    // -- so instead of screen-fixed strips this draws three ruled lines
    // running along the actual world X/Y/Z axes through the origin,
    // color-coded per the editor convention: X = green, Y = blue,
    // Z = red (deliberately NOT the common Red/Green/Blue = X/Y/Z
    // convention -- this matches what was asked for). Tick spacing
    // (1/5 world units, see MAJOR_STEP/MINOR_STEP below) is scaled to
    // Camera3D's actual working distances (moveSpeed, orbit distance,
    // farPlane), not the 2D pixel ruler's spacing -- see the FIXED
    // comment on renderAxisRulerGizmo3D for why.
    //
    // Drawn with plain SDL_Renderer calls (worldToScreenCPU) *after*
    // the GPU render target has been blitted into the canvas rect --
    // same layering the entity outlines below use -- so it always
    // draws on top of the 3D scene, like a wireframe overlay.
    // ==================================================================
    inline void renderAxisRulerGizmo3D(SDL_Renderer* renderer,
                                    TTF_TextEngine* textEngine, TTF_Font* font,
                                    const Camera3D& camera,
                                    float axisLength = 50.0f)  // parameter kept for compatibility but unused
    {
        // ---- Compute visible range along each axis using the camera frustum ----
        float aspect = (camera.canvasH > 0.0f) ? (camera.canvasW / camera.canvasH) : 1.0f;
        glm::mat4 view = camera.getViewMatrix();
        glm::mat4 proj = camera.getProjectionMatrix(aspect, false); // no Vulkan flip for CPU math
        glm::mat4 invVP = glm::inverse(proj * view);

        // 8 frustum corners in NDC
        glm::vec4 ndcCorners[8] = {
            {-1,-1,-1,1}, {1,-1,-1,1}, {1,1,-1,1}, {-1,1,-1,1},
            {-1,-1, 1,1}, {1,-1, 1,1}, {1,1, 1,1}, {-1,1, 1,1}
        };
        std::vector<glm::vec3> worldCorners;
        worldCorners.reserve(8);
        for (const auto& ndc : ndcCorners) {
            glm::vec4 world = invVP * ndc;
            world /= world.w;
            worldCorners.push_back(glm::vec3(world));
        }

        // Axis definitions: X=green, Y=blue, Z=red
        struct AxisDef { glm::vec3 dir; SDL_Color color; const char* label; };
        const AxisDef axes[3] = {
            { {1,0,0}, {60, 220, 90, 255},  "X" },
            { {0,1,0}, {70, 140, 240, 255}, "Y" },
            { {0,0,1}, {230, 70, 70, 255},  "Z" },
        };

        auto clipToCanvas = [&](const SDL_FPoint& p) {
            return p.x >= camera.canvasX && p.x <= camera.canvasX + camera.canvasW &&
                p.y >= camera.canvasY && p.y <= camera.canvasY + camera.canvasH;
        };

        constexpr int   MAJOR_STEP = 5;
        constexpr int   MINOR_STEP = 1;
        constexpr float TICK_PX    = 6.0f;
        constexpr float TICK_PX_MAJOR = 10.0f;

        for (const auto& axis : axes) {
            // Find min and max coordinate along this axis among the frustum corners
            float minCoord = std::numeric_limits<float>::max();
            float maxCoord = -std::numeric_limits<float>::max();
            for (const auto& wc : worldCorners) {
                float coord = glm::dot(wc, axis.dir);
                minCoord = std::min(minCoord, coord);
                maxCoord = std::max(maxCoord, coord);
            }
            // Add a small margin so the line extends slightly beyond the frustum
            float margin = 0.5f;
            minCoord -= margin;
            maxCoord += margin;

            // Main axis line from minCoord to maxCoord
            auto a = camera.worldToScreenCPU(axis.dir * minCoord);
            auto b = camera.worldToScreenCPU(axis.dir * maxCoord);
            if (a.visible && b.visible) {
                SDL_SetRenderDrawColor(renderer, axis.color.r, axis.color.g, axis.color.b, 255);
                SDL_RenderLine(renderer, a.pt.x, a.pt.y, b.pt.x, b.pt.y);
            }

            // Ticks every MINOR_STEP within [minCoord, maxCoord]
            int startTick = (int)std::ceil(minCoord / MINOR_STEP);
            int endTick   = (int)std::floor(maxCoord / MINOR_STEP);
            for (int i = startTick; i <= endTick; ++i) {
                if (i == 0) continue; // skip origin, the three axis lines cross there
                float d = (float)(i * MINOR_STEP);
                bool major = (std::abs(i * MINOR_STEP) % MAJOR_STEP == 0);

                glm::vec3 worldTick = axis.dir * d;
                auto sp = camera.worldToScreenCPU(worldTick);
                if (!sp.visible || !clipToCanvas(sp.pt)) continue;

                // Tick direction: perpendicular to the projected axis line
                auto spNext = camera.worldToScreenCPU(axis.dir * (d + 1.0f));
                float dx = spNext.visible ? spNext.pt.x - sp.pt.x : 1.0f;
                float dy = spNext.visible ? spNext.pt.y - sp.pt.y : 0.0f;
                float len = std::sqrt(dx*dx + dy*dy);
                if (len < 0.0001f) len = 1.0f;
                float perpX = -dy / len, perpY = dx / len;
                float half = major ? TICK_PX_MAJOR : TICK_PX;

                SDL_SetRenderDrawColor(renderer,
                    major ? axis.color.r : (Uint8)(axis.color.r * 0.6f),
                    major ? axis.color.g : (Uint8)(axis.color.g * 0.6f),
                    major ? axis.color.b : (Uint8)(axis.color.b * 0.6f), 255);
                SDL_RenderLine(renderer,
                    sp.pt.x - perpX * half, sp.pt.y - perpY * half,
                    sp.pt.x + perpX * half, sp.pt.y + perpY * half);

                // Major tick label
                if (major && font && textEngine) {
                    char buf[32];
                    std::snprintf(buf, sizeof(buf), "%d", (int)d);
                    TTF_Text* t = TTF_CreateText(textEngine, font, buf, 0);
                    if (t) {
                        TTF_SetTextColor(t, axis.color.r, axis.color.g, axis.color.b, 255);
                        TTF_DrawRendererText(t, sp.pt.x + perpX * (half + 2.0f), sp.pt.y + perpY * (half + 2.0f));
                        TTF_DestroyText(t);
                    }
                }
            }

            // Axis label at the positive end of the visible range (or at a fixed distance)
            float labelPos = maxCoord;
            auto tip = camera.worldToScreenCPU(axis.dir * labelPos);
            if (tip.visible && font && textEngine) {
                TTF_Text* t = TTF_CreateText(textEngine, font, axis.label, 0);
                if (t) {
                    TTF_SetTextColor(t, axis.color.r, axis.color.g, axis.color.b, 255);
                    TTF_DrawRendererText(t, tip.pt.x + 4.0f, tip.pt.y - 12.0f);
                    TTF_DestroyText(t);
                }
            }
        }
    }


    // ==================================================================
    // Default "basic 3D entity" look: a white wireframe cube outline,
    // the exact 3D equivalent of the 2D editor's empty rectangle outline
    // for un-textured entities (render_system_and_scene_gui_in_editor in
    // engine.h). Drawn with the same white-unless-selected convention:
    // pass selectionColor when the entity is selected, nullptr-ish
    // {255,255,255,255} otherwise.
    // ==================================================================
    inline void renderEntityCubeOutline3D(SDL_Renderer* renderer,
                                           const Camera3D& camera,
                                           const glm::vec3& worldCenter,
                                           const glm::vec3& halfExtents, // (w/2, h/2, depth/2)
                                           SDL_Color color = {255, 255, 255, 255}) {
        glm::vec3 h = halfExtents;
        glm::vec3 corners[8] = {
            worldCenter + glm::vec3(-h.x,-h.y,-h.z), worldCenter + glm::vec3( h.x,-h.y,-h.z),
            worldCenter + glm::vec3( h.x, h.y,-h.z), worldCenter + glm::vec3(-h.x, h.y,-h.z),
            worldCenter + glm::vec3(-h.x,-h.y, h.z), worldCenter + glm::vec3( h.x,-h.y, h.z),
            worldCenter + glm::vec3( h.x, h.y, h.z), worldCenter + glm::vec3(-h.x, h.y, h.z),
        };
        static const int edges[12][2] = {
            {0,1},{1,2},{2,3},{3,0}, {4,5},{5,6},{6,7},{7,4}, {0,4},{1,5},{2,6},{3,7}
        };

        Camera3D::ScreenPoint sp[8];
        for (int i = 0; i < 8; ++i) sp[i] = camera.worldToScreenCPU(corners[i]);

        SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
        for (auto& e : edges) {
            if (!sp[e[0]].visible || !sp[e[1]].visible) continue; // simple behind-camera clip: drop the segment
            SDL_RenderLine(renderer, sp[e[0]].pt.x, sp[e[0]].pt.y, sp[e[1]].pt.x, sp[e[1]].pt.y);
        }
    }

    // ==================================================================
    // Skeleton3D / AnimationClip3D -- minimal CPU-side skinning data for
    // rigged/animated models (glTF and FBX both expose this; .obj never
    // does, so Skeleton3D::joints stays empty for obj-loaded models).
    //
    // Deliberately CPU-side and coarse rather than a full GPU-skinning
    // pipeline: computing bone matrices is cheap (a few hundred 4x4
    // multiplies per model per frame, trivial next to a Jolt physics
    // step), and it's exactly the data Physics3D's rigged-collider rig
    // (BuildRiggedApproxCollider in physics3d.h) needs to move capsules
    // with the animation. Wiring these bone matrices into a GPU vertex
    // shader for true per-vertex skinning is a further step -- see the
    // note at the bottom of model_loaders_3d.h -- but everything here
    // (joint hierarchy, keyframe sampling, world matrices) is exactly
    // what that shader path would consume too, so it's not wasted work
    // either way, and CPU skinning-adjacent uses (colliders, gameplay
    // bone queries, attaching props to a hand bone) work today without it.
    // ==================================================================
    struct Joint3D {
        std::string name;
        int parentIndex = -1;              // -1 = root
        glm::mat4 inverseBindMatrix{1.0f};  // model-space -> joint-local bind pose
        glm::mat4 localBindTransform{1.0f}; // this joint's rest pose, relative to its parent
    };

    struct Skeleton3D {
        std::vector<Joint3D> joints;
        bool isSkinned() const { return !joints.empty(); }
    };

    // One keyframe track per joint: separate T/R/S key arrays since
    // glTF (and FBX) both allow each channel to be sampled at different
    // times/rates. Linear interpolation for T/S, nlerp for R -- plenty
    // accurate for editor preview and gameplay at typical mocap/Mixamo
    // frame rates (nlerp vs slerp is imperceptible below ~20 degrees of
    // rotation between keys, which is the overwhelmingly common case).
    struct JointKeyframes {
        std::vector<float> posTimes;    std::vector<glm::vec3> posValues;
        std::vector<float> rotTimes;    std::vector<glm::quat> rotValues;
        std::vector<float> scaleTimes;  std::vector<glm::vec3> scaleValues;
    };

    struct AnimationClip3D {
        std::string name;
        float duration = 0.0f; // seconds
        // Indexed by joint index (same indexing as Skeleton3D::joints);
        // a joint with no keys in this clip just holds its bind pose.
        std::vector<JointKeyframes> jointTracks;
    };

    inline bool loadModel3D(SDL_GPUDevice* device, const std::string& path,
                        DefaultGpuResources& defaults, ObjModel& outModel,
                        Skeleton3D* outSkeleton,
                        std::vector<AnimationClip3D>* outAnimations);

    // Evaluates `clip` at `timeSeconds` (wrapped to [0, duration) if
    // `loop`) and writes one world-space matrix per joint into
    // outWorldMatrices, ready to (a) skin vertices on the GPU, (b) drive
    // Physics3D's per-bone capsule rig, or (c) attach a prop/camera to
    // a named bone.
    inline void sampleAnimationPose(const Skeleton3D& skeleton, const AnimationClip3D& clip,
                                     float timeSeconds, bool loop,
                                     std::vector<glm::mat4>& outWorldMatrices) {
        size_t n = skeleton.joints.size();
        outWorldMatrices.assign(n, glm::mat4(1.0f));
        if (n == 0) return;

        float t = timeSeconds;
        if (clip.duration > 0.0001f) {
            if (loop) { t = std::fmod(t, clip.duration); if (t < 0.0f) t += clip.duration; }
            else       t = std::clamp(t, 0.0f, clip.duration);
        }

        auto sampleVec3 = [](const std::vector<float>& times, const std::vector<glm::vec3>& values,
                              float time, const glm::vec3& fallback) -> glm::vec3 {
            if (times.empty()) return fallback;
            if (time <= times.front()) return values.front();
            if (time >= times.back())  return values.back();
            for (size_t i = 0; i + 1 < times.size(); ++i) {
                if (time >= times[i] && time <= times[i+1]) {
                    float span = times[i+1] - times[i];
                    float a = span > 0.0001f ? (time - times[i]) / span : 0.0f;
                    return glm::mix(values[i], values[i+1], a);
                }
            }
            return values.back();
        };
        auto sampleQuat = [](const std::vector<float>& times, const std::vector<glm::quat>& values,
                              float time, const glm::quat& fallback) -> glm::quat {
            if (times.empty()) return fallback;
            if (time <= times.front()) return values.front();
            if (time >= times.back())  return values.back();
            for (size_t i = 0; i + 1 < times.size(); ++i) {
                if (time >= times[i] && time <= times[i+1]) {
                    float span = times[i+1] - times[i];
                    float a = span > 0.0001f ? (time - times[i]) / span : 0.0f;
                    return glm::normalize(glm::slerp(values[i], values[i+1], a));
                }
            }
            return values.back();
        };

        std::vector<glm::mat4> local(n, glm::mat4(1.0f));
        for (size_t i = 0; i < n; ++i) {
            const Joint3D& j = skeleton.joints[i];
            glm::vec3 bindPos, bindScale; glm::quat bindRot;
            {
                // Decompose the rest-pose local transform once so a
                // joint with no keyframes in this particular clip (e.g.
                // it's not animated) still gets a sensible T/R/S to
                // rebuild from, instead of defaulting to identity.
                glm::vec3 skew; glm::vec4 persp;
                glm::decompose(j.localBindTransform, bindScale, bindRot, bindPos, skew, persp);
            }
            glm::vec3 pos = bindPos, scale = bindScale; glm::quat rot = bindRot;
            if (i < clip.jointTracks.size()) {
                const JointKeyframes& kf = clip.jointTracks[i];
                pos   = sampleVec3(kf.posTimes,   kf.posValues,   t, bindPos);
                rot   = sampleQuat(kf.rotTimes,   kf.rotValues,   t, bindRot);
                scale = sampleVec3(kf.scaleTimes, kf.scaleValues, t, bindScale);
            }
            local[i] = glm::translate(glm::mat4(1.0f), pos) * glm::mat4_cast(rot) * glm::scale(glm::mat4(1.0f), scale);
        }

        // Walk parent-before-child (skeletons are stored so a parent's
        // index is always < its children's -- both fastgltf and ufbx nodes
        // come out of the file in that order, and model_loaders_3d.h
        // preserves it) to accumulate world matrices in one pass.
        for (size_t i = 0; i < n; ++i) {
            int p = skeleton.joints[i].parentIndex;
            outWorldMatrices[i] = (p >= 0) ? outWorldMatrices[p] * local[i] : local[i];
        }
    }

    // ==================================================================
    // ModelCache3D -- de-duplicating GPU model loader, keyed by file
    // path, mirroring Texture::Manager's path-cache pattern in
    // texture.h. Lets many entities share one GPU upload of the same
    // .obj/.fbx/.gltf/.glb file (via Mesh3DRef::modelPath) instead of
    // re-parsing and re-uploading it once per entity.
    //
    // Format dispatch (by extension) is the loadModel3D() definition
    // further down this file (see ModelLoaderDetail3D, and the
    // dispatcher right after it) -- .obj keeps using the tinyobjloader
    // path already in this file, .fbx goes through ufbx, .gltf/.glb
    // through fastgltf. All three converge on the same ObjModel output, so
    // renderObj() above needs no format-specific branches at all.
    //
    // NOTE: loadModel3D() is only forward-declared here and defined
    // later in the file (after ModelLoaderDetail3D::loadGltf/loadFbx
    // and Engine3D::loadObjModel are all visible), since it dispatches
    // to all three. This used to live in a separate model_loaders_3d.h
    // that got #included mid-namespace; that file's contents now live
    // directly in this header instead.
    // ==================================================================

    class ModelCache3D {
    public:
        // Returns nullptr if loading failed (bad path, unsupported/corrupt
        // file, etc). Non-owning pointer into the cache -- valid until
        // Clear()/destructor.
        ObjModel* GetOrLoad(SDL_GPUDevice* device, DefaultGpuResources& defaults, const std::string& path) {
            if (path.empty()) return nullptr;
            auto it = m_models.find(path);
            if (it != m_models.end()) return it->second.get();

            auto model = std::make_unique<ObjModel>();
            bool ok = loadModel3D(device, path, defaults, *model, nullptr, nullptr); // model_loaders_3d.h
            if (!ok) {
                std::cerr << "[Engine3D] ModelCache3D: failed to load " << path << std::endl;
                return nullptr;
            }
            ObjModel* raw = model.get();
            m_models.emplace(path, std::move(model));
            return raw;
        }

        // Cached skeleton/animation data for skinned formats (glTF/FBX),
        // empty for plain .obj. Same key (file path) as GetOrLoad above.
        Skeleton3D* GetSkeleton(const std::string& path) {
            auto it = m_skeletons.find(path);
            return it != m_skeletons.end() ? &it->second : nullptr;
        }
        void SetSkeleton(const std::string& path, Skeleton3D skel) {
            m_skeletons[path] = std::move(skel);
        }

        void Clear(SDL_GPUDevice* device) {
            for (auto& kv : m_models) destroyObjModel(device, *kv.second);
            m_models.clear();
            m_skeletons.clear();
        }

    private:
        std::unordered_map<std::string, std::unique_ptr<ObjModel>> m_models;
        std::unordered_map<std::string, Skeleton3D> m_skeletons;
    };
};

// ============================================================
// Engine3D::Examples
// ------------------------------------------------------------
// The old commented-out "draw a single hardcoded triangle with
// raw SDL_GPU calls" block from main.cpp, split into discrete,
// individually callable steps so it's easy to follow (and reuse)
// as a minimal SDL_GPU pipeline reference separate from the real
// renderObj() path above.
// ============================================================
namespace Engine3D::Examples {

    struct TriangleVertex {
        float x, y;
        float r, g, b, a;
    };

    struct TriangleResources {
        SDL_GPUShader* vertexShader = nullptr;
        SDL_GPUShader* fragmentShader = nullptr;
        SDL_GPUGraphicsPipeline* pipeline = nullptr;
        SDL_GPUBuffer* vertexBuffer = nullptr;
        bool isLoaded = false;
    };

    // Step 1: compile the two SPIR-V shaders used by the triangle.
    // Expects assets/shaders/triangle.vert.spv and triangle.frag.spv,
    // each a plain `layout(location=0) in vec2 pos; layout(location=1)
    // in vec4 color;` vertex shader and a passthrough fragment shader.
    inline bool step1_CreateShaders(SDL_GPUDevice* device, const std::string& shaderDir, TriangleResources& out) {
        out.vertexShader = Engine3D::loadShaderSPV(device, shaderDir + "triangle.vert.spv",
                                                    SDL_GPU_SHADERSTAGE_VERTEX, 0, 0);
        out.fragmentShader = Engine3D::loadShaderSPV(device, shaderDir + "triangle.frag.spv",
                                                      SDL_GPU_SHADERSTAGE_FRAGMENT, 0, 0);
        return out.vertexShader && out.fragmentShader;
    }

    // Step 2: build the graphics pipeline (vertex layout: vec2 pos +
    // vec4 color, no depth test needed for a single flat triangle).
    inline bool step2_CreatePipeline(SDL_GPUDevice* device, SDL_GPUTextureFormat colorFormat, TriangleResources& out,
                                      SDL_GPUTextureFormat depthFormat = SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT) {
        if (!out.vertexShader || !out.fragmentShader) return false;

        SDL_GPUVertexBufferDescription vbDesc = {};
        vbDesc.slot = 0;
        vbDesc.pitch = sizeof(TriangleVertex);
        vbDesc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

        SDL_GPUVertexAttribute attrs[2] = {};
        attrs[0].location = 0; attrs[0].buffer_slot = 0;
        attrs[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
        attrs[0].offset = offsetof(TriangleVertex, x);

        attrs[1].location = 1; attrs[1].buffer_slot = 0;
        attrs[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
        attrs[1].offset = offsetof(TriangleVertex, r);

        SDL_GPUColorTargetDescription colorTarget = {};
        colorTarget.format = colorFormat;
        colorTarget.blend_state.enable_blend = false;

        SDL_GPUGraphicsPipelineCreateInfo info = {};
        info.vertex_shader = out.vertexShader;
        info.fragment_shader = out.fragmentShader;
        info.vertex_input_state.num_vertex_buffers = 1;
        info.vertex_input_state.vertex_buffer_descriptions = &vbDesc;
        info.vertex_input_state.num_vertex_attributes = 2;
        info.vertex_input_state.vertex_attributes = attrs;
        info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        info.target_info.num_color_targets = 1;
        info.target_info.color_target_descriptions = &colorTarget;
        // This pipeline is used as a fallback smoke test, drawn into the
        // SAME render pass as the real obj pipeline (see main.cpp), which
        // always has a depth attachment bound. A pipeline built without a
        // matching depth-stencil target is render-pass-incompatible with
        // that pass and Vulkan's validation layer will reject every draw
        // call with VUID-vkCmdDraw-renderPass-02684. Depth test/write stay
        // off since the triangle doesn't need them -- only the *format*
        // needs to match what the render pass declares.
        info.target_info.has_depth_stencil_target = true;
        info.target_info.depth_stencil_format = depthFormat;
        info.depth_stencil_state.enable_depth_test = false;
        info.depth_stencil_state.enable_depth_write = false;
        info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
        info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
        info.rasterizer_state.enable_depth_clip = true;

        out.pipeline = SDL_CreateGPUGraphicsPipeline(device, &info);
        return out.pipeline != nullptr;
    }

    // Step 3: create the vertex buffer (empty, sized for 3 vertices).
    inline bool step3_CreateVertexBuffer(SDL_GPUDevice* device, TriangleResources& out) {
        SDL_GPUBufferCreateInfo bufferInfo = {};
        bufferInfo.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
        bufferInfo.size = sizeof(TriangleVertex) * 3;
        out.vertexBuffer = SDL_CreateGPUBuffer(device, &bufferInfo);
        return out.vertexBuffer != nullptr;
    }

    // Step 4: upload the actual triangle vertex data (a red/green/blue
    // tricolor triangle) into the buffer created in step 3.
    inline bool step4_UploadVertexData(SDL_GPUDevice* device, TriangleResources& out) {
        if (!out.vertexBuffer) return false;

        TriangleVertex verts[3] = {
            { -0.5f, -0.5f,  1.0f, 0.0f, 0.0f, 1.0f },
            {  0.5f, -0.5f,  0.0f, 1.0f, 0.0f, 1.0f },
            {  0.0f,  0.5f,  0.0f, 0.0f, 1.0f, 1.0f },
        };

        SDL_GPUTransferBufferCreateInfo tbInfo = {};
        tbInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        tbInfo.size = sizeof(verts);
        SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(device, &tbInfo);
        if (!transfer) return false;

        void* mapped = SDL_MapGPUTransferBuffer(device, transfer, false);
        if (mapped) {
            std::memcpy(mapped, verts, sizeof(verts));
            SDL_UnmapGPUTransferBuffer(device, transfer);
        }

        SDL_GPUCommandBuffer* uploadCmd = SDL_AcquireGPUCommandBuffer(device);
        if (uploadCmd) {
            SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(uploadCmd);
            SDL_GPUTransferBufferLocation src = {}; src.transfer_buffer = transfer; src.offset = 0;
            SDL_GPUBufferRegion dst = {}; dst.buffer = out.vertexBuffer; dst.offset = 0; dst.size = sizeof(verts);
            SDL_UploadToGPUBuffer(copyPass, &src, &dst, false);
            SDL_EndGPUCopyPass(copyPass);
            SDL_SubmitGPUCommandBuffer(uploadCmd);
        }
        SDL_ReleaseGPUTransferBuffer(device, transfer);
        out.isLoaded = true;
        return true;
    }

    // Convenience: runs steps 1-4 in order.
    inline bool setupTriangleExample(SDL_GPUDevice* device, const std::string& shaderDir,
                                      SDL_GPUTextureFormat colorFormat, TriangleResources& out) {
        if (!step1_CreateShaders(device, shaderDir, out)) return false;
        if (!step2_CreatePipeline(device, colorFormat, out)) return false;
        if (!step3_CreateVertexBuffer(device, out)) return false;
        if (!step4_UploadVertexData(device, out)) return false;
        return true;
    }

    // Step 5: the actual draw call, issued inside an already-active
    // render pass (same convention as Engine3D::renderObj).
    inline void step5_Render(SDL_GPURenderPass* pass, const TriangleResources& res) {
        if (!res.isLoaded || !res.pipeline) return;
        SDL_BindGPUGraphicsPipeline(pass, res.pipeline);
        SDL_GPUBufferBinding binding = {};
        binding.buffer = res.vertexBuffer;
        binding.offset = 0;
        SDL_BindGPUVertexBuffers(pass, 0, &binding, 1);
        SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
    }

    inline void destroyTriangleExample(SDL_GPUDevice* device, TriangleResources& res) {
        if (!device) return;
        if (res.vertexBuffer) { SDL_ReleaseGPUBuffer(device, res.vertexBuffer); res.vertexBuffer = nullptr; }
        if (res.pipeline) { SDL_ReleaseGPUGraphicsPipeline(device, res.pipeline); res.pipeline = nullptr; }
        if (res.vertexShader) { SDL_ReleaseGPUShader(device, res.vertexShader); res.vertexShader = nullptr; }
        if (res.fragmentShader) { SDL_ReleaseGPUShader(device, res.fragmentShader); res.fragmentShader = nullptr; }
        res.isLoaded = false;
    }

} // namespace Engine3D::Examples



namespace Engine3D {

namespace ModelLoaderDetail3D {

    inline std::string dirOf(const std::string& path) {
        size_t slash = path.find_last_of("/\\");
        return (slash == std::string::npos) ? "./" : path.substr(0, slash + 1);
    }

    inline std::string extOf(const std::string& path) {
        size_t dot = path.find_last_of('.');
        if (dot == std::string::npos) return "";
        std::string ext = path.substr(dot + 1);
        for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
        return ext;
    }

    // ---------------------------------------------------------
    // glTF / GLB, via fastgltf (0.8/0.9-series API: Parser +
    // Expected<Asset>, fastgltf::math types, primitive.findAttribute()).
    // NOTE: fastgltf's public API has changed across minor versions a
    // few times (pre-0.6 used a different Parser interface entirely).
    // This targets the API shipped by vcpkg's fastgltf port as of this
    // writing (0.9.x) -- if your vendored version differs, check
    // fastgltf's own migration notes for the handful of renamed calls.
    // ---------------------------------------------------------
    inline bool loadGltf(SDL_GPUDevice* device, const std::string& path,
                          Engine3D::DefaultGpuResources& defaults,
                          Engine3D::ObjModel& outModel,
                          Engine3D::Skeleton3D* outSkeleton,
                          std::vector<Engine3D::AnimationClip3D>* outAnimations) {
        std::string baseDir = dirOf(path);

        auto bufferResult = fastgltf::GltfDataBuffer::FromPath(path);
        if (bufferResult.error() != fastgltf::Error::None) {
            std::cerr << "[Engine3D] fastgltf: failed to open " << path << std::endl;
            return false;
        }

        fastgltf::Parser parser;
        constexpr auto options = fastgltf::Options::LoadExternalBuffers |
                                  fastgltf::Options::LoadExternalImages |
                                  fastgltf::Options::DecomposeNodeMatrices;
        auto assetResult = parser.loadGltf(bufferResult.get(), std::filesystem::path(baseDir), options);
        if (assetResult.error() != fastgltf::Error::None) {
            std::cerr << "[Engine3D] fastgltf: failed to parse " << path
                       << " (error " << (int)assetResult.error() << ")" << std::endl;
            return false;
        }
        fastgltf::Asset& asset = assetResult.get();

        std::vector<Engine3D::Vertex3D> vertices;
        std::unordered_map<int, std::vector<Uint32>> indicesByMaterial;
        glm::vec3 bmin( std::numeric_limits<float>::max());
        glm::vec3 bmax(-std::numeric_limits<float>::max());

        // ---- Node -> world matrix (for baking static/unskinned mesh
        // geometry into model space, same reasoning as the cgltf path
        // this replaced). fastgltf::Options::DecomposeNodeMatrices above
        // guarantees every node exposes .transform as a TRS
        // (fastgltf::TRS), never a raw matrix, which keeps this simple.
        std::function<glm::mat4(size_t)> nodeWorldMatrix = [&](size_t nodeIdx) -> glm::mat4 {
            fastgltf::Node& node = asset.nodes[nodeIdx];
            glm::mat4 local(1.0f);
            if (auto* trs = std::get_if<fastgltf::TRS>(&node.transform)) {
                glm::vec3 t(trs->translation[0], trs->translation[1], trs->translation[2]);
                glm::quat r(trs->rotation[3], trs->rotation[0], trs->rotation[1], trs->rotation[2]); // fastgltf: x,y,z,w
                glm::vec3 s(trs->scale[0], trs->scale[1], trs->scale[2]);
                local = glm::translate(glm::mat4(1.0f), t) * glm::mat4_cast(r) * glm::scale(glm::mat4(1.0f), s);
            }
            for (size_t pi = 0; pi < asset.nodes.size(); ++pi) {
                for (size_t child : asset.nodes[pi].children) {
                    if (child == nodeIdx) return nodeWorldMatrix(pi) * local;
                }
            }
            return local; // no parent found -- root node
        };

        auto readAccessorVec3 = [&](size_t accessorIdx, size_t i) -> glm::vec3 {
            fastgltf::math::fvec3 v = fastgltf::getAccessorElement<fastgltf::math::fvec3>(asset, asset.accessors[accessorIdx], i);
            return glm::vec3(v.x(), v.y(), v.z());
        };
        auto readAccessorVec2 = [&](size_t accessorIdx, size_t i) -> glm::vec2 {
            fastgltf::math::fvec2 v = fastgltf::getAccessorElement<fastgltf::math::fvec2>(asset, asset.accessors[accessorIdx], i);
            return glm::vec2(v.x(), v.y());
        };

        for (size_t mi = 0; mi < asset.meshes.size(); ++mi) {
            fastgltf::Mesh& mesh = asset.meshes[mi];

            glm::mat4 world(1.0f);
            for (size_t ni = 0; ni < asset.nodes.size(); ++ni) {
                if (asset.nodes[ni].meshIndex && *asset.nodes[ni].meshIndex == mi) { world = nodeWorldMatrix(ni); break; }
            }
            glm::mat3 normalMat = glm::mat3(glm::transpose(glm::inverse(world)));

            for (auto& prim : mesh.primitives) {
                if (prim.type != fastgltf::PrimitiveType::Triangles) continue;

                auto* posIt = prim.findAttribute("POSITION");
                if (posIt == prim.attributes.end()) continue;
                size_t posAccIdx = posIt->accessorIndex;

                auto* normIt = prim.findAttribute("NORMAL");
                auto* uvIt = prim.findAttribute("TEXCOORD_0");
                bool skinned = prim.findAttribute("JOINTS_0") != prim.attributes.end();

                size_t vertCount = asset.accessors[posAccIdx].count;
                Uint32 baseVertex = (Uint32)vertices.size();
                for (size_t v = 0; v < vertCount; ++v) {
                    Engine3D::Vertex3D vert{};
                    glm::vec3 pos = readAccessorVec3(posAccIdx, v);
                    if (!skinned) pos = glm::vec3(world * glm::vec4(pos, 1.0f));
                    vert.position = pos;

                    if (normIt != prim.attributes.end()) {
                        glm::vec3 nrm = readAccessorVec3(normIt->accessorIndex, v);
                        vert.normal = skinned ? nrm : glm::normalize(normalMat * nrm);
                    } else {
                        vert.normal = glm::vec3(0, 1, 0);
                    }
                    if (uvIt != prim.attributes.end()) {
                        vert.texcoord = readAccessorVec2(uvIt->accessorIndex, v);
                    }
                    bmin = glm::min(bmin, vert.position);
                    bmax = glm::max(bmax, vert.position);
                    vertices.push_back(vert);
                }

                int matId = prim.materialIndex ? (int)*prim.materialIndex : -1;

                if (prim.indicesAccessor) {
                    fastgltf::Accessor& idxAcc = asset.accessors[*prim.indicesAccessor];
                    fastgltf::iterateAccessor<std::uint32_t>(asset, idxAcc, [&](std::uint32_t idx) {
                        indicesByMaterial[matId].push_back(idx + baseVertex);
                    });
                } else {
                    for (size_t v = 0; v < vertCount; ++v) indicesByMaterial[matId].push_back(baseVertex + (Uint32)v);
                }
            }
        }

        if (vertices.empty()) {
            std::cerr << "[Engine3D] " << path << " (gltf) produced no geometry." << std::endl;
            return false;
        }

        std::vector<Uint32> flatIndices;
        std::vector<Engine3D::SubMesh3D> submeshes;
        for (auto& kv : indicesByMaterial) {
            Engine3D::SubMesh3D sub;
            sub.materialIndex = kv.first;
            sub.indexOffset = (Uint32)flatIndices.size();
            sub.indexCount = (Uint32)kv.second.size();
            flatIndices.insert(flatIndices.end(), kv.second.begin(), kv.second.end());
            submeshes.push_back(sub);
        }

        outModel.vertexBuffer = Engine3D::createAndUploadBuffer(device, SDL_GPU_BUFFERUSAGE_VERTEX,
            vertices.data(), (Uint32)(vertices.size() * sizeof(Engine3D::Vertex3D)));
        outModel.indexBuffer = Engine3D::createAndUploadBuffer(device, SDL_GPU_BUFFERUSAGE_INDEX,
            flatIndices.data(), (Uint32)(flatIndices.size() * sizeof(Uint32)));
        if (!outModel.vertexBuffer || !outModel.indexBuffer) return false;

        outModel.vertexCount = (Uint32)vertices.size();
        outModel.indexCount = (Uint32)flatIndices.size();
        outModel.submeshes = std::move(submeshes);
        outModel.boundsMin = bmin;
        outModel.boundsMax = bmax;

        // ---- Materials ----
        outModel.materials.reserve(asset.materials.size());
        for (size_t mi = 0; mi < asset.materials.size(); ++mi) {
            fastgltf::Material& gm = asset.materials[mi];
            Engine3D::Material3D mat;
            mat.name = !gm.name.empty() ? std::string(gm.name) : ("material_" + std::to_string(mi));
            auto& bcf = gm.pbrData.baseColorFactor;
            mat.diffuseColor = glm::vec3(bcf[0], bcf[1], bcf[2]);

            if (gm.pbrData.baseColorTexture.has_value()) {
                size_t texIdx = gm.pbrData.baseColorTexture->textureIndex;
                auto& tex = asset.textures[texIdx];
                if (tex.imageIndex.has_value()) {
                    fastgltf::Image& img = asset.images[*tex.imageIndex];
                    if (auto* uriSource = std::get_if<fastgltf::sources::URI>(&img.data)) {
                        mat.diffuseTexturePath = baseDir + std::string(uriSource->uri.path());
                        SDL_GPUTexture* gtex = nullptr; SDL_GPUSampler* gsamp = nullptr;
                        if (Engine3D::loadTextureToGPU(device, mat.diffuseTexturePath, &gtex, &gsamp)) {
                            mat.gpuTexture = gtex; mat.gpuSampler = gsamp; mat.ownsTexture = true; mat.ownsSampler = true;
                        }
                    }
                    // Embedded (glb-packed or data-URI) images: sources::Array/Vector/BufferView
                    // aren't handled here -- external .bin-referenced or loose-file textures (the
                    // overwhelmingly common case for a Blender glTF export with "Separate" textures)
                    // are. Decode embedded images through SDL_image's memory-buffer load path
                    // (IMG_Load_IO over an SDL_IOStream on the raw bytes) as a follow-up if you
                    // need fully self-contained .glb files with baked-in textures.
                }
            }
            if (!mat.gpuTexture) { mat.gpuTexture = defaults.whiteTexture; mat.gpuSampler = defaults.linearSampler; }
            outModel.materials.push_back(mat);
        }

        // ---- Skeleton (first skin only) ----
        if (outSkeleton && !asset.skins.empty()) {
            fastgltf::Skin& skin = asset.skins[0];
            std::unordered_map<size_t, int> nodeToJoint;
            for (size_t j = 0; j < skin.joints.size(); ++j) nodeToJoint[skin.joints[j]] = (int)j;

            outSkeleton->joints.resize(skin.joints.size());
            for (size_t j = 0; j < skin.joints.size(); ++j) {
                size_t nodeIdx = skin.joints[j];
                fastgltf::Node& node = asset.nodes[nodeIdx];
                Engine3D::Joint3D joint;
                joint.name = !node.name.empty() ? std::string(node.name) : ("joint_" + std::to_string(j));

                joint.parentIndex = -1;
                for (size_t pi = 0; pi < asset.nodes.size(); ++pi)
                    for (size_t child : asset.nodes[pi].children)
                        if (child == nodeIdx && nodeToJoint.count(pi)) { joint.parentIndex = nodeToJoint[pi]; }

                if (auto* trs = std::get_if<fastgltf::TRS>(&node.transform)) {
                    glm::vec3 t(trs->translation[0], trs->translation[1], trs->translation[2]);
                    glm::quat r(trs->rotation[3], trs->rotation[0], trs->rotation[1], trs->rotation[2]);
                    glm::vec3 s(trs->scale[0], trs->scale[1], trs->scale[2]);
                    joint.localBindTransform = glm::translate(glm::mat4(1.0f), t) * glm::mat4_cast(r) * glm::scale(glm::mat4(1.0f), s);
                }
                if (skin.inverseBindMatrices.has_value()) {
                    fastgltf::math::fmat4x4 m = fastgltf::getAccessorElement<fastgltf::math::fmat4x4>(
                        asset, asset.accessors[*skin.inverseBindMatrices], j);
                    joint.inverseBindMatrix = glm::make_mat4(&m.col(0).x());
                }
                outSkeleton->joints[j] = joint;
            }

            // ---- Animations ----
            if (outAnimations) {
                for (size_t ai = 0; ai < asset.animations.size(); ++ai) {
                    fastgltf::Animation& ga = asset.animations[ai];
                    Engine3D::AnimationClip3D clip;
                    clip.name = !ga.name.empty() ? std::string(ga.name) : ("clip_" + std::to_string(ai));
                    clip.jointTracks.resize(skin.joints.size());

                    for (auto& ch : ga.channels) {
                        if (!ch.nodeIndex.has_value()) continue;
                        auto it = nodeToJoint.find(*ch.nodeIndex);
                        if (it == nodeToJoint.end()) continue;
                        int jointIdx = it->second;
                        fastgltf::AnimationSampler& samp = ga.samplers[ch.samplerIndex];
                        fastgltf::Accessor& inputAcc = asset.accessors[samp.inputAccessor];
                        size_t count = inputAcc.count;
                        Engine3D::JointKeyframes& kf = clip.jointTracks[jointIdx];

                        for (size_t k = 0; k < count; ++k) {
                            float time = fastgltf::getAccessorElement<float>(asset, inputAcc, k);
                            clip.duration = std::max(clip.duration, time);
                            if (ch.path == fastgltf::AnimationPath::Translation) {
                                glm::vec3 v = readAccessorVec3(samp.outputAccessor, k);
                                kf.posTimes.push_back(time); kf.posValues.push_back(v);
                            } else if (ch.path == fastgltf::AnimationPath::Rotation) {
                                fastgltf::math::fvec4 q = fastgltf::getAccessorElement<fastgltf::math::fvec4>(asset, asset.accessors[samp.outputAccessor], k);
                                kf.rotTimes.push_back(time); kf.rotValues.push_back(glm::quat(q.w(), q.x(), q.y(), q.z()));
                            } else if (ch.path == fastgltf::AnimationPath::Scale) {
                                glm::vec3 v = readAccessorVec3(samp.outputAccessor, k);
                                kf.scaleTimes.push_back(time); kf.scaleValues.push_back(v);
                            }
                        }
                    }
                    outAnimations->push_back(std::move(clip));
                }
            }
        }

        outModel.objFilePath = path;
        outModel.isLoaded = true;
        return true;
    }

    // ---------------------------------------------------------
    // FBX, via ufbx
    // ---------------------------------------------------------
    inline bool loadFbx(SDL_GPUDevice* device, const std::string& path,
                         Engine3D::DefaultGpuResources& defaults,
                         Engine3D::ObjModel& outModel,
                         Engine3D::Skeleton3D* outSkeleton,
                         std::vector<Engine3D::AnimationClip3D>* outAnimations) {
        ufbx_load_opts opts{};
        // ufbx_coordinate_axes fields are right/up/front, NOT x/y/z --
        // and per ufbx.h's own comment, "front" is the OPPOSITE of
        // forward. Right-handed Y-up with -Z forward (the convention
        // the rest of this engine's glm/GPU math assumes) means
        // front = +Z. Fixed from an earlier version of this file that
        // used the wrong type name (`ufbx_axes` -- doesn't exist,
        // correct type is `ufbx_coordinate_axes`) and treated it as a
        // plain (x,y,z) triple instead of (right,up,front).
        opts.target_axes = ufbx_coordinate_axes{
            UFBX_COORDINATE_AXIS_POSITIVE_X,  // right
            UFBX_COORDINATE_AXIS_POSITIVE_Y,  // up
            UFBX_COORDINATE_AXIS_POSITIVE_Z,  // front (opposite of forward -- forward is -Z)
        };
        opts.target_unit_meters = 1.0f; // normalize whatever unit scale the DCC exported to meters, matches Physics3D's 1 unit = 1 meter
        ufbx_error error;
        ufbx_scene* scene = ufbx_load_file(path.c_str(), &opts, &error);
        if (!scene) {
            std::cerr << "[Engine3D] ufbx_load_file failed for " << path << ": " << error.description.data << std::endl;
            return false;
        }

        std::string baseDir = dirOf(path);
        std::vector<Engine3D::Vertex3D> vertices;
        std::unordered_map<int, std::vector<Uint32>> indicesByMaterial;
        glm::vec3 bmin( std::numeric_limits<float>::max());
        glm::vec3 bmax(-std::numeric_limits<float>::max());

        // Map every ufbx bone node we see to a stable joint index, built
        // up as we encounter skinned meshes below.
        std::unordered_map<ufbx_node*, int> nodeToJoint;

        for (size_t mi = 0; mi < scene->nodes.count; ++mi) {
            ufbx_node* node = scene->nodes.data[mi];
            if (!node->mesh) continue;
            ufbx_mesh* mesh = node->mesh;

            bool skinned = mesh->skin_deformers.count > 0;
            glm::mat4 world(1.0f);
            if (!skinned) {
                ufbx_matrix m = node->geometry_to_world;
                world = glm::mat4(
                    m.m00, m.m10, m.m20, 0.0f,
                    m.m01, m.m11, m.m21, 0.0f,
                    m.m02, m.m12, m.m22, 0.0f,
                    m.m03, m.m13, m.m23, 1.0f);
            }
            glm::mat3 normalMat = glm::mat3(glm::transpose(glm::inverse(world)));

            // Triangulate every face (ufbx faces can be n-gons) into a
            // temp index buffer, then bucket by the face's assigned
            // material, same shape as the obj/gltf paths above.
            // mesh->max_triangles isn't a real field -- the correct
            // member (per ufbx.h) is max_face_triangles: the largest
            // number of triangles any single face in this mesh will
            // triangulate into, which is exactly the per-call buffer
            // size ufbx_triangulate_face() expects.
            std::vector<uint32_t> triIndices(mesh->max_face_triangles * 3);
            for (size_t fi = 0; fi < mesh->faces.count; ++fi) {
                ufbx_face face = mesh->faces.data[fi];
                uint32_t numTris = ufbx_triangulate_face(triIndices.data(), triIndices.size(), mesh, face);
                int matId = -1;
                if (mesh->face_material.count > fi) {
                    uint32_t mIdx = mesh->face_material.data[fi];
                    if (mIdx < mesh->materials.count) matId = (int)(mesh->materials.data[mIdx] - scene->materials.data[0]);
                }

                for (uint32_t t = 0; t < numTris * 3; ++t) {
                    uint32_t vIdx = triIndices[t];
                    Engine3D::Vertex3D vert{};
                    ufbx_vec3 p = mesh->vertex_position[vIdx];
                    glm::vec3 pos((float)p.x, (float)p.y, (float)p.z);
                    if (!skinned) pos = glm::vec3(world * glm::vec4(pos, 1.0f));
                    vert.position = pos;

                    if (mesh->vertex_normal.exists) {
                        ufbx_vec3 n = mesh->vertex_normal[vIdx];
                        glm::vec3 nrm((float)n.x, (float)n.y, (float)n.z);
                        vert.normal = skinned ? nrm : glm::normalize(normalMat * nrm);
                    } else {
                        vert.normal = glm::vec3(0, 1, 0);
                    }
                    if (mesh->vertex_uv.exists) {
                        ufbx_vec2 uv = mesh->vertex_uv[vIdx];
                        vert.texcoord = glm::vec2((float)uv.x, 1.0f - (float)uv.y);
                    }

                    bmin = glm::min(bmin, vert.position);
                    bmax = glm::max(bmax, vert.position);
                    Uint32 outIdx = (Uint32)vertices.size();
                    vertices.push_back(vert);
                    indicesByMaterial[matId].push_back(outIdx);
                }
            }

            // ---- Skeleton, from this mesh's first skin deformer ----
            if (outSkeleton && skinned && outSkeleton->joints.empty()) {
                ufbx_skin_deformer* skin = mesh->skin_deformers.data[0];
                outSkeleton->joints.reserve(skin->clusters.count);
                for (size_t ci = 0; ci < skin->clusters.count; ++ci) {
                    ufbx_skin_cluster* cluster = skin->clusters.data[ci];
                    nodeToJoint[cluster->bone_node] = (int)outSkeleton->joints.size();
                    Engine3D::Joint3D joint;
                    joint.name = std::string(cluster->bone_node->name.data, cluster->bone_node->name.length);
                    ufbx_matrix ibm = cluster->geometry_to_bone;
                    joint.inverseBindMatrix = glm::mat4(
                        ibm.m00, ibm.m10, ibm.m20, 0.0f,
                        ibm.m01, ibm.m11, ibm.m21, 0.0f,
                        ibm.m02, ibm.m12, ibm.m22, 0.0f,
                        ibm.m03, ibm.m13, ibm.m23, 1.0f);
                    outSkeleton->joints.push_back(joint);
                }
                // Second pass: parent indices + local bind transforms,
                // now that every bone node has a stable index.
                for (size_t ci = 0; ci < skin->clusters.count; ++ci) {
                    ufbx_node* boneNode = skin->clusters.data[ci]->bone_node;
                    int idx = nodeToJoint[boneNode];
                    Engine3D::Joint3D& joint = outSkeleton->joints[idx];
                    joint.parentIndex = (boneNode->parent && nodeToJoint.count(boneNode->parent)) ? nodeToJoint[boneNode->parent] : -1;
                    ufbx_matrix lm = boneNode->node_to_parent;
                    joint.localBindTransform = glm::mat4(
                        lm.m00, lm.m10, lm.m20, 0.0f,
                        lm.m01, lm.m11, lm.m21, 0.0f,
                        lm.m02, lm.m12, lm.m22, 0.0f,
                        lm.m03, lm.m13, lm.m23, 1.0f);
                }
            }
        }

        if (vertices.empty()) {
            std::cerr << "[Engine3D] " << path << " (fbx) produced no geometry." << std::endl;
            ufbx_free_scene(scene);
            return false;
        }

        std::vector<Uint32> flatIndices;
        std::vector<Engine3D::SubMesh3D> submeshes;
        for (auto& kv : indicesByMaterial) {
            Engine3D::SubMesh3D sub;
            sub.materialIndex = kv.first;
            sub.indexOffset = (Uint32)flatIndices.size();
            sub.indexCount = (Uint32)kv.second.size();
            flatIndices.insert(flatIndices.end(), kv.second.begin(), kv.second.end());
            submeshes.push_back(sub);
        }

        outModel.vertexBuffer = Engine3D::createAndUploadBuffer(device, SDL_GPU_BUFFERUSAGE_VERTEX,
            vertices.data(), (Uint32)(vertices.size() * sizeof(Engine3D::Vertex3D)));
        outModel.indexBuffer = Engine3D::createAndUploadBuffer(device, SDL_GPU_BUFFERUSAGE_INDEX,
            flatIndices.data(), (Uint32)(flatIndices.size() * sizeof(Uint32)));
        if (!outModel.vertexBuffer || !outModel.indexBuffer) { ufbx_free_scene(scene); return false; }

        outModel.vertexCount = (Uint32)vertices.size();
        outModel.indexCount = (Uint32)flatIndices.size();
        outModel.submeshes = std::move(submeshes);
        outModel.boundsMin = bmin;
        outModel.boundsMax = bmax;

        outModel.materials.reserve(scene->materials.count);
        for (size_t mi = 0; mi < scene->materials.count; ++mi) {
            ufbx_material* fm = scene->materials.data[mi];
            Engine3D::Material3D mat;
            mat.name = std::string(fm->name.data, fm->name.length);
            ufbx_vec3 diffuse = fm->fbx.diffuse_color.value_vec3;
            mat.diffuseColor = glm::vec3((float)diffuse.x, (float)diffuse.y, (float)diffuse.z);
            if (fm->fbx.diffuse_color.texture_enabled && fm->fbx.diffuse_color.texture) {
                std::string texPath = std::string(fm->fbx.diffuse_color.texture->filename.data, fm->fbx.diffuse_color.texture->filename.length);
                if (!texPath.empty()) {
                    mat.diffuseTexturePath = baseDir + std::filesystem::path(texPath).filename().string();
                    SDL_GPUTexture* tex = nullptr; SDL_GPUSampler* samp = nullptr;
                    if (Engine3D::loadTextureToGPU(device, mat.diffuseTexturePath, &tex, &samp)) {
                        mat.gpuTexture = tex; mat.gpuSampler = samp; mat.ownsTexture = true; mat.ownsSampler = true;
                    }
                }
            }
            if (!mat.gpuTexture) { mat.gpuTexture = defaults.whiteTexture; mat.gpuSampler = defaults.linearSampler; }
            outModel.materials.push_back(mat);
        }

        // ---- Animations: baked at a fixed sample rate rather than
        // walked curve-by-curve. ufbx exposes each bone's raw FBX
        // animation curves (which can be non-uniformly keyed, use
        // different interpolation types per key, etc.), but re-deriving
        // exact FBX curve evaluation isn't worth it here -- ufbx already
        // does that internally via ufbx_evaluate_transform(), so we just
        // sample it at 30Hz per bone and store that as ordinary
        // JointKeyframes. Costs more memory than sparse original keys,
        // but stays completely format-agnostic downstream (the same
        // sampleAnimationPose() in engine3d.h drives glTF and FBX
        // clips identically), and 30Hz is well above what a Mixamo
        // mocap clip actually needs to look correct.
        if (outAnimations && outSkeleton && !outSkeleton->joints.empty()) {
            constexpr float SAMPLE_RATE = 30.0f;
            for (size_t si = 0; si < scene->anim_stacks.count; ++si) {
                ufbx_anim_stack* stack = scene->anim_stacks.data[si];
                Engine3D::AnimationClip3D clip;
                clip.name = std::string(stack->name.data, stack->name.length);
                clip.duration = (float)stack->time_end - (float)stack->time_begin;
                clip.jointTracks.resize(outSkeleton->joints.size());

                int sampleCount = std::max(2, (int)(clip.duration * SAMPLE_RATE));
                for (auto& kv : nodeToJoint) {
                    ufbx_node* boneNode = kv.first;
                    int jointIdx = kv.second;
                    Engine3D::JointKeyframes& kf = clip.jointTracks[jointIdx];
                    kf.posTimes.reserve(sampleCount); kf.posValues.reserve(sampleCount);
                    kf.rotTimes.reserve(sampleCount); kf.rotValues.reserve(sampleCount);
                    kf.scaleTimes.reserve(sampleCount); kf.scaleValues.reserve(sampleCount);

                    for (int s = 0; s < sampleCount; ++s) {
                        double t = (double)stack->time_begin + (double)s / SAMPLE_RATE;
                        ufbx_transform xf = ufbx_evaluate_transform(stack->anim, boneNode, t);
                        float rel = (float)(t - (double)stack->time_begin);
                        kf.posTimes.push_back(rel);
                        kf.posValues.push_back(glm::vec3((float)xf.translation.x, (float)xf.translation.y, (float)xf.translation.z));
                        kf.rotTimes.push_back(rel);
                        kf.rotValues.push_back(glm::quat((float)xf.rotation.w, (float)xf.rotation.x, (float)xf.rotation.y, (float)xf.rotation.z));
                        kf.scaleTimes.push_back(rel);
                        kf.scaleValues.push_back(glm::vec3((float)xf.scale.x, (float)xf.scale.y, (float)xf.scale.z));
                    }
                }
                outAnimations->push_back(std::move(clip));
            }
        }

        outModel.objFilePath = path;
        outModel.isLoaded = true;
        ufbx_free_scene(scene);
        return true;
    }

} // namespace ModelLoaderDetail3D

    // ---------------------------------------------------------
    // loadModel3D -- the definition promised by the forward
    // declaration further up this file. This used to live in
    // model_loaders_3d.h; now that that file's contents have been
    // folded directly into engine3d.h (inside ModelLoaderDetail3D,
    // above), this dispatcher has to live here too, after
    // loadObjModel/loadGltf/loadFbx are all visible. Picks a loader
    // by file extension; unknown extensions fail with a log message
    // rather than silently no-op'ing.
    // ---------------------------------------------------------
    inline bool loadModel3D(SDL_GPUDevice* device, const std::string& path,
                             DefaultGpuResources& defaults, ObjModel& outModel,
                             Skeleton3D* outSkeleton,
                             std::vector<AnimationClip3D>* outAnimations) {
        std::string ext = ModelLoaderDetail3D::extOf(path);
        if (ext == "obj") {
            // .obj has no skeleton/animation data; outSkeleton/outAnimations
            // are simply left untouched for this format.
            return loadObjModel(device, path, defaults, outModel);
        } else if (ext == "gltf" || ext == "glb") {
            return ModelLoaderDetail3D::loadGltf(device, path, defaults, outModel, outSkeleton, outAnimations);
        } else if (ext == "fbx") {
            return ModelLoaderDetail3D::loadFbx(device, path, defaults, outModel, outSkeleton, outAnimations);
        }
        std::cerr << "[Engine3D] loadModel3D: unsupported file extension '" << ext << "' for " << path << std::endl;
        return false;
    }

    inline void renderGridMap3D(SDL_GPUCommandBuffer* cmd,
                                SDL_GPURenderPass* pass,
                                SDL_GPUGraphicsPipeline* pipeline,
                                const ObjModel& cellModel,  // unit cube model to instance
                                const glm::vec3& entityPos,
                                const std::vector<struct Components::GridCell3D>& cells,
                                float cellW, float cellH, float cellD,
                                const glm::mat4& view,
                                const glm::mat4& projection,
                                const DefaultGpuResources& defaults) {
        if (!cellModel.isLoaded || !pipeline || cells.empty()) return;
        
        SDL_BindGPUGraphicsPipeline(pass, pipeline);
        SDL_GPUBufferBinding vBinding = {};
        vBinding.buffer = cellModel.vertexBuffer;
        vBinding.offset = 0;
        SDL_BindGPUVertexBuffers(pass, 0, &vBinding, 1);
        SDL_GPUBufferBinding iBinding = {};
        iBinding.buffer = cellModel.indexBuffer;
        iBinding.offset = 0;
        SDL_BindGPUIndexBuffer(pass, &iBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
        
        // Bind default white texture
        SDL_GPUTextureSamplerBinding texBinding = {};
        texBinding.texture = defaults.whiteTexture;
        texBinding.sampler = defaults.linearSampler;
        SDL_BindGPUFragmentSamplers(pass, 0, &texBinding, 1);
        
        ObjFragmentUniforms fUniforms{};
        fUniforms.baseColor = glm::vec4(0.6f, 0.8f, 1.0f, 1.0f);
        SDL_PushGPUFragmentUniformData(cmd, 0, &fUniforms, sizeof(fUniforms));
        
        for (const auto& cell : cells) {
            Transform3D t;
            t.position = entityPos + glm::vec3(
                cell.x * cellW + cell.offX,
                cell.y * cellH + cell.offY,
                cell.z * cellD + cell.offZ
            );
            t.scale = glm::vec3(cellW, cellH, cellD);
            
            glm::mat4 modelMat = t.toMatrix();
            ObjVertexUniforms vUniforms{};
            vUniforms.mvp = projection * view * modelMat;
            vUniforms.model = modelMat;
            SDL_PushGPUVertexUniformData(cmd, 0, &vUniforms, sizeof(vUniforms));
            
            for (const auto& sub : cellModel.submeshes) {
                SDL_DrawGPUIndexedPrimitives(pass, sub.indexCount, 1, sub.indexOffset, 0, 0);
            }
        }
    }
}

// ------------------------------------------------------------------
// GPU skinning note: sampleAnimationPose() (above, in engine3d.h)
// produces one world-space glm::mat4 per joint every frame. That's
// everything a vertex shader needs to do real per-vertex GPU skinning
// -- multiply each bone matrix by the joint's inverseBindMatrix,
// upload the resulting array as a uniform/storage buffer, and in the
// vertex shader blend up to 4 bone matrices per vertex by
// joint indices + weights (which cgltf/ufbx both expose per-vertex,
// not yet threaded into Vertex3D here since that also means growing
// Vertex3D's layout and the createObjPipeline() vertex attributes to
// match -- a deliberate follow-up rather than baked in silently).
// Until then, skinned models render in bind pose (T-pose) through
// renderObj(), while Skeleton3D/AnimationClip3D data is fully usable
// today for CPU-side purposes: Physics3D's per-bone rigged collider
// rig (physics3d.h), attaching props/cameras to a named bone, and any
// gameplay logic that just needs "where is this bone right now".
// ------------------------------------------------------------------
