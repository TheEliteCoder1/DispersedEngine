#pragma once

#include <string>
#include <unordered_map>
#include <cstdio>
#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

// only use namespace and it's manager once you have initialized the sdl image lib
namespace Texture {
    class Manager {
        public:
            bool SetRenderer(SDL_Renderer* renderer)
            {
                if (!renderer) {
                    std::printf("TextureManager Init failed: Renderer context is null.\n");
                    return false;
                }
                m_renderer = renderer;
                return true;
            }

            // Loads a texture or reuses a cached version if already loaded from the same path
            bool Load(const std::string& name, const std::string& path) {
                if (!m_renderer) return false;

                // 1. Check if the physical file has already been loaded into GPU memory
                auto pathIt = m_pathToTexture.find(path);
                if (pathIt != m_pathToTexture.end()) {
                    m_textures[name] = pathIt->second;
                    return true;
                }

                // 2. Load the image file directly into an optimized hardware texture
                SDL_Texture* texture = IMG_LoadTexture(m_renderer, path.c_str());
                if (!texture) {
                    std::printf("IMG_LoadTexture(%s) failed: %s\n", path.c_str(), SDL_GetError());
                    return false;
                }

                // 3. Cache the texture pointers in both tracking maps
                m_textures[name] = texture;
                m_pathToTexture[path] = texture;
                return true;
            }

            // Retreive a raw pointer to an asset by its name alias for rendering routines
            SDL_Texture* Get(const std::string& name) {
                auto it = m_textures.find(name);
                if (it != m_textures.end()) {
                    return it->second;
                }
                return nullptr;
            }

            // Unified unloading of an individual named alias link
            void Unload(const std::string& name) {
                auto it = m_textures.find(name);
                if (it == m_textures.end()) return;

                SDL_Texture* targetTexture = it->second;
                m_textures.erase(it);

                // Check if any other alias is still using this physical texture data
                bool stillInUse = false;
                for (const auto& kv : m_textures) {
                    if (kv.second == targetTexture) {
                        stillInUse = true;
                        break;
                    }
                }

                // If no other name maps to this texture, we can safely drop it from the cache & VRAM
                if (!stillInUse) {
                    for (auto pathIt = m_pathToTexture.begin(); pathIt != m_pathToTexture.end(); ++pathIt) {
                        if (pathIt->second == targetTexture) {
                            SDL_DestroyTexture(targetTexture);
                            m_pathToTexture.erase(pathIt);
                            break;
                        }
                    }
                }
            }

            // Unified destruction: wipes out all allocations and maps safely
            void Clear() {
                m_textures.clear();
                
                // Destroy actual underlying hardware GPU textures once
                for (auto& kv : m_pathToTexture) {
                    if (kv.second) {
                        SDL_DestroyTexture(kv.second);
                    }
                }
                m_pathToTexture.clear();
            }

            ~Manager() {
                Clear();
            }

        private:
            SDL_Renderer* m_renderer;
            std::unordered_map<std::string, SDL_Texture*> m_textures;
            std::unordered_map<std::string, SDL_Texture*> m_pathToTexture;
    };
};