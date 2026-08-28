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

#ifndef TINYOBJLOADER_IMPLEMENTATION
#define TINYOBJLOADER_IMPLEMENTATION
#endif
#include <tiny_obj_loader.h>

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
        glm::vec3 position{0.0f, 1.5f, -4.5f};
        float yawDeg = 90.0f;    // 0 = looking down +X; 90 = looking down +Z
        float pitchDeg = 0.0f;
        float fovDeg = 60.0f;
        float nearPlane = 0.1f;
        float farPlane = 100.0f;

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