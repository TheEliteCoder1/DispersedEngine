#pragma once

#include <string>
#include <vector>
#include <iostream>
#include <SDL3/SDL.h>

#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"

#ifndef TINYOBJLOADER_IMPLEMENTATION
#define TINYOBJLOADER_IMPLEMENTATION
#endif
#include <tiny_obj_loader.h>

namespace Engine3D {
    enum class CanvasMode {
        Mode2D = 0,
        Mode3D
    };
    
    struct Transform3D {
        glm::vec3 position{0.0f, 0.0f, 0.0f};
        glm::vec3 rotation{0.0f, 0.0f, 0.0f};
        glm::vec3 scale{1.0f, 1.0f, 1.0f};
    };

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

        bool ret = tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, filepath.c_str());
        
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
        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, transform.position);
        model = glm::rotate(model, glm::radians(transform.rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
        model = glm::rotate(model, glm::radians(transform.rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
        model = glm::rotate(model, glm::radians(transform.rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
        model = glm::scale(model, transform.scale);

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
};