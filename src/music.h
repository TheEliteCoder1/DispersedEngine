#pragma once

#include <string>
#include <unordered_map>
#include <algorithm>
#include <cstdio>
#include <SDL3_mixer/SDL_mixer.h>

// only use namespace and it's manager once you have initialized the sdl mixer lib
namespace MusicAndSfx {
    class Manager {
        public:
            // creates the device for all audio
            bool CreateMixerDevice() {
                m_mixer = MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
                if (!m_mixer) {
                    std::printf("MusicAndSfx Manager Init failed: MIX_CreateMixerDevice failed: %s\n", SDL_GetError());
                    MIX_Quit();
                    return false;
                }
                return true;
            }

            // Updated Load method with a 'stream' flag for background music/large files
            void Load(const std::string& name, const std::string& path, bool stream = false)
            {
                auto pathIt = m_pathToAudio.find(path);
                if (pathIt != m_pathToAudio.end()) {
                    m_samples[name] = pathIt->second;
                    return;
                }

                MIX_Audio* audio = MIX_LoadAudio(m_mixer, path.c_str(), !stream);
                if (audio) {
                    m_samples[name] = audio;
                    m_pathToAudio[path] = audio;
                } else {
                    std::printf("MIX_LoadAudio(%s) failed: %s\n", path.c_str(), SDL_GetError());
                }
            }

            // Play a sound whose volume scales with collision impulse.
            // typically used with the physics engine
            void PlayHit(const std::string& name, float impulse) {
                
                auto it = m_samples.find(name);
                if (it == m_samples.end()) return;

                MIX_Track* track = MIX_CreateTrack(m_mixer);
                if (!track) return;

                MIX_SetTrackAudio(track, it->second);

                float vol = std::clamp(impulse * 0.15f, 0.05f, 1.0f);
                MIX_SetTrackGain(track, vol);

                SDL_PropertiesID props = SDL_CreateProperties();
                MIX_PlayTrack(track, props);
                SDL_DestroyProperties(props);
            }
    
            // Plays a regular sound effect once at a given volume (defaults to 1.0f full volume)
            void PlaySfx(const std::string& name, float volume = 1.0f) {
                auto it = m_samples.find(name);
                if (it == m_samples.end()) return;

                MIX_Track* track = MIX_CreateTrack(m_mixer);
                if (!track) return;

                MIX_SetTrackAudio(track, it->second);
                MIX_SetTrackGain(track, std::clamp(volume, 0.0f, 1.0f));

                SDL_PropertiesID props = SDL_CreateProperties();
                MIX_PlayTrack(track, props);
                SDL_DestroyProperties(props);
            }

            // Plays a song with an option to loop indefinitely
            void PlaySong(const std::string& name, bool loop = true, float volume = 0.5f) {
                auto it = m_samples.find(name);
                if (it == m_samples.end()) return;

                MIX_Track* track = MIX_CreateTrack(m_mixer);
                if (!track) return;

                MIX_SetTrackAudio(track, it->second);
                MIX_SetTrackGain(track, std::clamp(volume, 0.0f, 1.0f));

                SDL_PropertiesID props = SDL_CreateProperties();
                if (loop) {
                    // Tell SDL3_mixer to repeat this track infinitely
                    SDL_SetNumberProperty(props, MIX_PROP_PLAY_LOOPS_NUMBER, -1);
                }
                
                MIX_PlayTrack(track, props);
                SDL_DestroyProperties(props);
            }

            ~Manager() {
                m_samples.clear();

                for (auto& kv : m_pathToAudio) {
                    MIX_DestroyAudio(kv.second);
                }
                m_pathToAudio.clear();

                if (m_mixer) {
                    MIX_DestroyMixer(m_mixer);
                    m_mixer = nullptr;
                }
                
                MIX_Quit();
            }

        private:
            MIX_Mixer* m_mixer;
            std::unordered_map<std::string, MIX_Audio*> m_samples;
            std::unordered_map<std::string, MIX_Audio*> m_pathToAudio;
    };

};