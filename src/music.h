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
            void PlayHit(const std::string& name, float impulse, float pitch = 1.0f) {
                auto it = m_samples.find(name);
                if (it == m_samples.end()) return;
                MIX_Track* track = MIX_CreateTrack(m_mixer);
                if (!track) return;
                MIX_SetTrackAudio(track, it->second);
                float vol = std::clamp(impulse * 0.15f, 0.05f, 1.0f);
                MIX_SetTrackGain(track, vol);
                MIX_SetTrackFrequencyRatio(track, pitch);
                SDL_PropertiesID props = SDL_CreateProperties();
                MIX_PlayTrack(track, props);
                SDL_DestroyProperties(props);
            }
    
            // Plays a regular sound effect once at a given volume (defaults to 1.0f full volume)
            void PlaySfx(const std::string& name, float volume = 1.0f, float pitch = 1.0f) {
                auto it = m_samples.find(name);
                if (it == m_samples.end()) return;
                MIX_Track* track = MIX_CreateTrack(m_mixer);
                if (!track) return;
                MIX_SetTrackAudio(track, it->second);
                MIX_SetTrackGain(track, std::clamp(volume, 0.0f, 1.0f));
                MIX_SetTrackFrequencyRatio(track, pitch);
                SDL_PropertiesID props = SDL_CreateProperties();
                MIX_PlayTrack(track, props);
                SDL_DestroyProperties(props);
            }

            void PlayLoopingSfx(const std::string& name, float volume = 1.0f, float pitch = 1.0f)
            {
                auto sample = m_samples.find(name);
                if(sample == m_samples.end()) return;

                auto existing = m_activeSfx.find(name);
                if(existing != m_activeSfx.end()) return;

                MIX_Track* track = MIX_CreateTrack(m_mixer);
                if(!track) return;

                MIX_SetTrackAudio(track, sample->second);
                MIX_SetTrackGain(track, std::clamp(volume, 0.0f, 1.0f));
                MIX_SetTrackFrequencyRatio(track, pitch);

                SDL_PropertiesID props = SDL_CreateProperties();
                SDL_SetNumberProperty(props, MIX_PROP_PLAY_LOOPS_NUMBER, -1);   // infinite loops

                MIX_SetTrackLoops(track, -1);   // keep for safety
                m_activeSfx[name] = track;
                MIX_PlayTrack(track, props);
                SDL_DestroyProperties(props);
            }

            void StopSfx(const std::string& name)
            {
                auto it = m_activeSfx.find(name);

                if(it == m_activeSfx.end())
                    return;


                MIX_Track* track = it->second;


                if(track)
                {
                    MIX_StopTrack(
                        track,
                        0
                    );
                }


                m_activeSfx.erase(it);
            }

            // Plays a song with an option to loop indefinitely
            void PlaySong(const std::string& name, bool loop = true, float volume = 0.5f, float pitch = 1.0f) {
                auto it = m_samples.find(name);
                if (it == m_samples.end()) return;

                MIX_Track* track = MIX_CreateTrack(m_mixer);
                if (!track) return;

                MIX_SetTrackAudio(track, it->second);
                MIX_SetTrackGain(track, std::clamp(volume, 0.0f, 1.0f));
                MIX_SetTrackFrequencyRatio(track, pitch);

                SDL_PropertiesID props = SDL_CreateProperties();
                if (loop) {
                    // Tell SDL3_mixer to repeat this track infinitely
                    SDL_SetNumberProperty(props, MIX_PROP_PLAY_LOOPS_NUMBER, -1);
                }

                m_activeSongs[name] = track;
                MIX_PlayTrack(track, props);
                SDL_DestroyProperties(props);
            }

            // Stops a song previously started with PlaySong (mirrors StopSfx)
            void StopSong(const std::string& name)
            {
                auto it = m_activeSongs.find(name);

                if(it == m_activeSongs.end())
                    return;


                MIX_Track* track = it->second;


                if(track)
                {
                    MIX_StopTrack(
                        track,
                        0
                    );
                }


                m_activeSongs.erase(it);
            }

            // Unified destruction: wipes out all allocations and the
            // mixer device safely. Safe to call more than once.
            void Clear() {
                m_samples.clear();

                for (auto& kv : m_pathToAudio) {
                    if (kv.second) MIX_DestroyAudio(kv.second);
                }
                m_pathToAudio.clear();
                m_activeSfx.clear();
                m_activeSongs.clear();

                if (m_mixer) {
                    MIX_DestroyMixer(m_mixer);
                    m_mixer = nullptr;
                }

                // NOTE: MIX_Quit() is intentionally NOT called here, same
                // reasoning as font.h - whoever calls MIX_Init() owns
                // calling MIX_Quit() exactly once, not this cache.
            }

            ~Manager() {
                Clear();
            }

        public:
            MIX_Mixer* m_mixer = nullptr;
        private:
            std::unordered_map<std::string, MIX_Audio*> m_samples;
            std::unordered_map<std::string, MIX_Audio*> m_pathToAudio;
            std::unordered_map<std::string, MIX_Track*> m_activeSfx;
            std::unordered_map<std::string, MIX_Track*> m_activeSongs;
    };

};
