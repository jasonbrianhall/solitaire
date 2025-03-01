#ifdef _WIN32

#include "audiomanager.h"
#include <iostream>
#include <windows.h>
#include <mmsystem.h>
#include <mmreg.h>
#include <vector>
#include <mutex>
#include <map>
#include <memory>
#pragma comment(lib, "winmm.lib")

// A self-contained sound object that manages its own resources
class Sound {
public:
    Sound(const std::vector<uint8_t>& data, std::shared_ptr<std::promise<void>> promise)
        : soundData(data), completionPromise(promise), hWaveOut(NULL), isPlaying(false) {
        ZeroMemory(&waveHeader, sizeof(WAVEHDR));
    }
    
    ~Sound() {
        cleanup();
    }
    
    void cleanup() {
        if (hWaveOut) {
            // Stop playback
            waveOutReset(hWaveOut);
            
            // Unprepare header if it was prepared
            if (waveHeader.dwFlags & WHDR_PREPARED) {
                waveOutUnprepareHeader(hWaveOut, &waveHeader, sizeof(WAVEHDR));
            }
            
            // Close the device
            waveOutClose(hWaveOut);
            hWaveOut = NULL;
        }
        
        // Signal completion promise if not already done
        if (completionPromise) {
            try {
                completionPromise->set_value();
            } catch (...) {
                // Promise might have already been fulfilled
            }
            completionPromise = nullptr;
        }
        
        isPlaying = false;
    }

    // Deleted copy operations
    Sound(const Sound&) = delete;
    Sound& operator=(const Sound&) = delete;
    
    // Data members
    std::vector<uint8_t> soundData;
    std::shared_ptr<std::promise<void>> completionPromise;
    HWAVEOUT hWaveOut;
    WAVEHDR waveHeader;
    bool isPlaying;
};

// Forward declaration of callback
void CALLBACK waveOutProc(HWAVEOUT hwo, UINT uMsg, DWORD_PTR dwInstance, DWORD_PTR dwParam1, DWORD_PTR dwParam2);

class WindowsAudioPlayer : public AudioPlayer {
public:
    WindowsAudioPlayer() : volume_(1.0f) {
        InitializeCriticalSection(&cs_);
    }
    
    ~WindowsAudioPlayer() override {
        shutdown();
        DeleteCriticalSection(&cs_);
    }
    
    bool initialize() override {
        return waveOutGetNumDevs() > 0;
    }
    
    void shutdown() override {
        EnterCriticalSection(&cs_);
        
        // Clean up all active sounds
        for (auto& pair : activeSounds_) {
            pair.second->cleanup();
        }
        
        activeSounds_.clear();
        
        LeaveCriticalSection(&cs_);
    }
    
    void playSound(const std::vector<uint8_t>& data, 
                  const std::string& format,
                  std::shared_ptr<std::promise<void>> completionPromise = nullptr) override {
        
        // Only support WAV format
        if (format != "wav") {
            std::cerr << "Only WAV format is supported" << std::endl;
            if (completionPromise) {
                completionPromise->set_value();
            }
            return;
        }
        
        // Basic validation
        if (data.size() < 44) {
            std::cerr << "Invalid WAV file: too small" << std::endl;
            if (completionPromise) {
                completionPromise->set_value();
            }
            return;
        }
        
        try {
            EnterCriticalSection(&cs_);
            
            // Limit the number of simultaneously playing sounds
            if (activeSounds_.size() >= 10) {
                std::cerr << "Too many active sounds, skipping playback" << std::endl;
                LeaveCriticalSection(&cs_);
                if (completionPromise) {
                    completionPromise->set_value();
                }
                return;
            }
            
            // Create a new sound object
            std::shared_ptr<Sound> sound = std::make_shared<Sound>(data, completionPromise);
            
            // Check for RIFF/WAVE header
            if (memcmp(data.data(), "RIFF", 4) != 0 || memcmp(data.data() + 8, "WAVE", 4) != 0) {
                std::cerr << "Invalid WAV file: missing RIFF/WAVE header" << std::endl;
                LeaveCriticalSection(&cs_);
                if (completionPromise) {
                    completionPromise->set_value();
                }
                return;
            }
            
            // Extract format info
            WAVEFORMATEX wfx = {0};
            uint16_t audioFormat = *reinterpret_cast<const uint16_t*>(&data[20]);
            uint16_t numChannels = *reinterpret_cast<const uint16_t*>(&data[22]);
            uint32_t sampleRate = *reinterpret_cast<const uint32_t*>(&data[24]);
            uint16_t bitsPerSample = *reinterpret_cast<const uint16_t*>(&data[34]);
            
            // Find the data chunk
            size_t dataOffset = 0;
            for (size_t i = 12; i < data.size() - 8; ) {
                if (memcmp(data.data() + i, "data", 4) == 0) {
                    dataOffset = i + 8;
                    break;
                }
                uint32_t chunkSize = *reinterpret_cast<const uint32_t*>(&data[i + 4]);
                i += 8 + chunkSize;
                
                // Safety check
                if (i >= data.size()) {
                    break;
                }
            }
            
            if (dataOffset == 0 || dataOffset >= data.size()) {
                std::cerr << "Invalid WAV file: data chunk not found" << std::endl;
                LeaveCriticalSection(&cs_);
                if (completionPromise) {
                    completionPromise->set_value();
                }
                return;
            }
            
            // Set up the WAVEFORMATEX structure
            wfx.wFormatTag = audioFormat;
            wfx.nChannels = numChannels;
            wfx.nSamplesPerSec = sampleRate;
            wfx.wBitsPerSample = bitsPerSample;
            wfx.nBlockAlign = numChannels * bitsPerSample / 8;
            wfx.nAvgBytesPerSec = sampleRate * wfx.nBlockAlign;
            
            // Open a waveOut device
            MMRESULT result = waveOutOpen(&sound->hWaveOut, WAVE_MAPPER, &wfx, 
                                         (DWORD_PTR)waveOutProc, 
                                         (DWORD_PTR)this, 
                                         CALLBACK_FUNCTION);
                                         
            if (result != MMSYSERR_NOERROR) {
                std::cerr << "Failed to open waveOut device, error: " << result << std::endl;
                LeaveCriticalSection(&cs_);
                if (completionPromise) {
                    completionPromise->set_value();
                }
                return;
            }
            
            // Apply volume
            if (volume_ < 1.0f) {
                DWORD volumeValue = static_cast<DWORD>(volume_ * 0xFFFF);
                DWORD stereoVolume = volumeValue | (volumeValue << 16);
                waveOutSetVolume(sound->hWaveOut, stereoVolume);
            }
            
            // Set up the WAVEHDR
            sound->waveHeader.lpData = (LPSTR)(sound->soundData.data() + dataOffset);
            sound->waveHeader.dwBufferLength = static_cast<DWORD>(sound->soundData.size() - dataOffset);
            sound->waveHeader.dwUser = (DWORD_PTR)sound->hWaveOut;  // Store the device handle for the callback
            
            // Prepare the header
            result = waveOutPrepareHeader(sound->hWaveOut, &sound->waveHeader, sizeof(WAVEHDR));
            if (result != MMSYSERR_NOERROR) {
                std::cerr << "Failed to prepare waveOut header, error: " << result << std::endl;
                waveOutClose(sound->hWaveOut);
                sound->hWaveOut = NULL;
                LeaveCriticalSection(&cs_);
                if (completionPromise) {
                    completionPromise->set_value();
                }
                return;
            }
            
            // Add to active sounds map
            sound->isPlaying = true;
            activeSounds_[sound->hWaveOut] = sound;
            
            // Write the data
            result = waveOutWrite(sound->hWaveOut, &sound->waveHeader, sizeof(WAVEHDR));
            if (result != MMSYSERR_NOERROR) {
                std::cerr << "Failed to write wave data, error: " << result << std::endl;
                
                // Remove from active sounds
                activeSounds_.erase(sound->hWaveOut);
                
                // Clean up resources
                waveOutUnprepareHeader(sound->hWaveOut, &sound->waveHeader, sizeof(WAVEHDR));
                waveOutClose(sound->hWaveOut);
                sound->hWaveOut = NULL;
                
                LeaveCriticalSection(&cs_);
                if (completionPromise) {
                    completionPromise->set_value();
                }
                return;
            }
            
            LeaveCriticalSection(&cs_);
            // The sound is now playing, and the callback will handle completion
        }
        catch (const std::exception& e) {
            std::cerr << "Exception in playSound: " << e.what() << std::endl;
            LeaveCriticalSection(&cs_);
            if (completionPromise) {
                completionPromise->set_value();
            }
        }
    }
    
    void setVolume(float volume) override {
        volume_ = std::max(0.0f, std::min(1.0f, volume));
        
        // Apply to all active devices
        EnterCriticalSection(&cs_);
        
        DWORD volumeValue = static_cast<DWORD>(volume_ * 0xFFFF);
        DWORD stereoVolume = volumeValue | (volumeValue << 16);
        
        for (auto& pair : activeSounds_) {
            if (pair.second->hWaveOut) {
                waveOutSetVolume(pair.second->hWaveOut, stereoVolume);
            }
        }
        
        LeaveCriticalSection(&cs_);
    }
    
    // Helper for the callback function
    void handlePlaybackComplete(HWAVEOUT hwo) {
        EnterCriticalSection(&cs_);
        
        auto it = activeSounds_.find(hwo);
        if (it != activeSounds_.end()) {
            // Get the sound object
            std::shared_ptr<Sound> sound = it->second;
            
            // Clean up resources
            if (sound->waveHeader.dwFlags & WHDR_PREPARED) {
                waveOutUnprepareHeader(hwo, &sound->waveHeader, sizeof(WAVEHDR));
            }
            
            waveOutClose(hwo);
            sound->hWaveOut = NULL;
            sound->isPlaying = false;
            
            // Fulfill the promise
            if (sound->completionPromise) {
                try {
                    sound->completionPromise->set_value();
                } catch (...) {
                    // Promise might have already been fulfilled
                }
                sound->completionPromise = nullptr;
            }
            
            // Remove from active sounds
            activeSounds_.erase(it);
        }
        
        LeaveCriticalSection(&cs_);
    }
    
private:
    float volume_;
    CRITICAL_SECTION cs_;
    std::map<HWAVEOUT, std::shared_ptr<Sound>> activeSounds_;
    
    friend void CALLBACK waveOutProc(HWAVEOUT, UINT, DWORD_PTR, DWORD_PTR, DWORD_PTR);
};

// Callback function for waveOut
void CALLBACK waveOutProc(HWAVEOUT hwo, UINT uMsg, DWORD_PTR dwInstance, DWORD_PTR dwParam1, DWORD_PTR dwParam2) {
    if (uMsg == WOM_DONE) {
        // The instance parameter is a pointer to our WindowsAudioPlayer
        WindowsAudioPlayer* player = reinterpret_cast<WindowsAudioPlayer*>(dwInstance);
        if (player) {
            player->handlePlaybackComplete(hwo);
        }
    }
}

// Factory function implementation for Windows
std::unique_ptr<AudioPlayer> createAudioPlayer() {
    return std::make_unique<WindowsAudioPlayer>();
}

#endif // _WIN32
