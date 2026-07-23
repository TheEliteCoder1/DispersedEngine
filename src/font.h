
#pragma once

#include <string>
#include <unordered_map>
#include <cstdio>
#include <SDL3_ttf/SDL_ttf.h>

// only use namespace and it's manager once you have initialized the sdl ttf lib
namespace Font {    
    class Manager {
        public:
            // Loads a font at a specified point size, caching it if already opened
            bool Load(const std::string& name, const std::string& path, float ptSize) {
                // Generate a unique path-and-size key (e.g., "assets/arial.ttf:24")
                std::string pathSizeKey = path + ":" + std::to_string(ptSize);

                // 1. Check if this exact font size has already been allocated
                auto cacheIt = m_pathToFont.find(pathSizeKey);
                if (cacheIt != m_pathToFont.end()) {
                    m_fonts[name] = cacheIt->second;
                    return true;
                }

                // 2. Open the font asset from disk
                TTF_Font* font = TTF_OpenFont(path.c_str(), ptSize);
                if (!font) {
                    std::printf("TTF_OpenFont(%s, %.1f) failed: %s\n", path.c_str(), ptSize, SDL_GetError());
                    return false;
                }

                // 3. Cache the pointer across both tracking lookups
                m_fonts[name] = font;
                m_pathToFont[pathSizeKey] = font;
                return true;
            }

            // Retrieve a raw pointer to an opened font by its friendly name string
            TTF_Font* Get(const std::string& name) {
                auto it = m_fonts.find(name);
                if (it != m_fonts.end()) {
                    return it->second;
                }
                return nullptr;
            }

            // Unified unloading of an individual font alias
            void Unload(const std::string& name) {
                auto it = m_fonts.find(name);
                if (it == m_fonts.end()) return;

                TTF_Font* targetFont = it->second;
                m_fonts.erase(it);

                // Check if another alias is pointing to this exact open font handle
                bool stillInUse = false;
                for (const auto& kv : m_fonts) {
                    if (kv.second == targetFont) {
                        stillInUse = true;
                        break;
                    }
                }

                // If no logic labels depend on it, close the asset entirely
                if (!stillInUse) {
                    for (auto cacheIt = m_pathToFont.begin(); cacheIt != m_pathToFont.end(); ++cacheIt) {
                        if (cacheIt->second == targetFont) {
                            TTF_CloseFont(targetFont);
                            m_pathToFont.erase(cacheIt);
                            break;
                        }
                    }
                }
            }

            // Clears out all mappings and closes all opened instances.
            // Safe to call more than once (e.g. once explicitly before
            // shutdown, then again automatically by the destructor).
            void Clear() {
                m_fonts.clear();
                
                for (auto& kv : m_pathToFont) {
                    if (kv.second) {
                        TTF_CloseFont(kv.second);
                    }
                }
                m_pathToFont.clear();
                // NOTE: TTF_Quit() is intentionally NOT called here. This
                // manager only owns the fonts it opened, not the library
                // itself - whoever calls TTF_Init() should be the one to
                // call TTF_Quit(), exactly once, after this manager (and
                // everything else using TTF) is done.
            }

            ~Manager() {
                Clear();
            }

        private:
            // Maps a developer-assigned alias ("ui_title") to the open font resource
            std::unordered_map<std::string, TTF_Font*> m_fonts;

            // Tracks open files by their "path:size" signature to guarantee single instance loading
            std::unordered_map<std::string, TTF_Font*> m_pathToFont;
    };
};
