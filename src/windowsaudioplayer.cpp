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
#include <functional>
#pragma comment(lib, "winmm.lib")

// Simple structure to hold sound data and resources
struct Sound {
    HWAVEOUT hWaveOut;
    WAVEHDR waveHeader;
    std::vector<uint8_t> data;
    std::shared_ptr<std::promise<void>> completionPromise;
    bool isPlaying;
    int id;
    
    Sound() : hWaveOut(NULL), isPlaying(false), id(0) {
        ZeroMemory(&waveHeader, sizeof(WAVEHDR));
    }
    
    ~Sound() {
        cleanup();
    }
    
    void cleanup() {
        if (hWaveOut != NULL) {
            // Reset to stop playback
            waveOutReset(hWaveOut);
            
            // Unprepare header if it was prepared
            if (waveHeader.dwFlags & WHDR_PREPARED) {
                waveOutUnprepareHeader(hWaveOut, &waveHeader, sizeof(WAVEHDR));
            }
            
            // Close the device
            waveOutClose(hWaveOut);
            hWaveOut = NULL;
        }
        
        // Signal completion if needed
        if (completionPromise) {
            try {
                completionPromise->set_value();
            } catch (...) {
                // Ignore exceptions from setting a value on an already-satisfied promise
            }
        }
        
        isPlaying = false;
    }
};

// We'll use a singleton manager to track sounds
class SoundManager {
public:
    static SoundManager& getInstance() {
        static SoundManager instance;
        return instance;
    }
    
    // Add a new sound and start playing it
    int playSound(const std::vector<uint8_t>& data, 
                 std::shared_ptr<std::promise<void>> completionPromise) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // Clean up any completed sounds first
        cleanupCompletedSounds();
        
        // Limit number of concurrent sounds
        if (activeSounds_.size() >= MAX_SOUNDS) {
            // Find oldest sound to replace
            int oldestId = -1;
            for (const auto& pair : activeSounds_) {
                if (oldestId == -1 || pair.first < oldestId) {
                    oldestId = pair.first;
                }
            }
            
            // Stop and remove the oldest sound
            if (oldestId != -1) {
                activeSounds_.erase(oldestId);
            }
        }
        
        // Find data chunk in WAV file
        size_t dataOffset = 0;
        for (size_t i = 12; i < data.size() - 8; ) {
            if (memcmp(data.data() + i, "data", 4) == 0) {
                dataOffset = i + 8;
                break;
            }
            
            uint32_t chunkSize = *reinterpret_cast<const uint32_t*>(&data[i + 4]);
            i += 8 + chunkSize;
            
            if (i >= data.size()) break;
        }
        
        if (dataOffset == 0 || dataOffset >= data.size()) {
            if (completionPromise) completionPromise->set_value();
            return -1;
        }
        
        // Setup WAV format
        WAVEFORMATEX wfx = {0};
        wfx.wFormatTag = *reinterpret_cast<const uint16_t*>(&data[20]);
        wfx.nChannels = *reinterpret_cast<const uint16_t*>(&data[22]);
        wfx.nSamplesPerSec = *reinterpret_cast<const uint32_t*>(&data[24]);
        wfx.wBitsPerSample = *reinterpret_cast<const uint16_t*>(&data[34]);
        wfx.nBlockAlign = wfx.nChannels * wfx.wBitsPerSample / 8;
        wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;
        
        // Create a new sound
        std::unique_ptr<Sound> sound = std::make_unique<Sound>();
        sound->data = data;
        sound->completionPromise = completionPromise;
        sound->id = nextSoundId_++;
        sound->isPlaying = true;
        
        // Open the wave device
        MMRESULT result = waveOutOpen(&sound->hWaveOut, WAVE_MAPPER, &wfx, 
                                     (DWORD_PTR)waveOutCallback, 
                                     (DWORD_PTR)sound->id, 
                                     CALLBACK_FUNCTION);
        
        if (result != MMSYSERR_NOERROR) {
            if (completionPromise) completionPromise->set_value();
            return -1;
        }
        
        // Set up the header
        sound->waveHeader.lpData = (LPSTR)(sound->data.data() + dataOffset);
        sound->waveHeader.dwBufferLength = static_cast<DWORD>(sound->data.size() - dataOffset);
        sound->waveHeader.dwUser = (DWORD_PTR)sound->id;
        
        // Prepare the header
        result = waveOutPrepareHeader(sound->hWaveOut, &sound->waveHeader, sizeof(WAVEHDR));
        if (result != MMSYSERR_NOERROR) {
            waveOutClose(sound->hWaveOut);
            if (completionPromise) completionPromise->set_value();
            return -1;
        }
        
        // Write the data
        result = waveOutWrite(sound->hWaveOut, &sound->waveHeader, sizeof(WAVEHDR));
        if (result != MMSYSERR_NOERROR) {
            waveOutUnprepareHeader(sound->hWaveOut, &sound->waveHeader, sizeof(WAVEHDR));
            waveOutClose(sound->hWaveOut);
            if (completionPromise) completionPromise->set_value();
            return -1;
        }
        
        // Store the sound
        int soundId = sound->id;
        activeSounds_[soundId] = std::move(sound);
        return soundId;
    }
    
    // Mark a sound as completed
    void completeSound(int soundId) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto it = activeSounds_.find(soundId);
        if (it != activeSounds_.end()) {
            it->second->isPlaying = false;
        }
    }
    
    // Clean up all sounds
    void cleanup() {
        std::lock_guard<std::mutex> lock(mutex_);
        activeSounds_.clear();
    }
    
    // Clean up just finished sounds
    void cleanupCompletedSounds() {
        for (auto it = activeSounds_.begin(); it != activeSounds_.end();) {
            if (!it->second->isPlaying) {
                it = activeSounds_.erase(it);
            } else {
                ++it;
            }
        }
    }

private:
    SoundManager() : nextSoundId_(1) {}
    ~SoundManager() { cleanup(); }
    
    // Deleted copy and move constructors and assignment operators
    SoundManager(const SoundManager&) = delete;
    SoundManager& operator=(const SoundManager&) = delete;
    SoundManager(SoundManager&&) = delete;
    SoundManager& operator=(SoundManager&&) = delete;
    
    static void CALLBACK waveOutCallback(HWAVEOUT hwo, UINT uMsg, DWORD_PTR dwInstance, DWORD_PTR dwParam1, DWORD_PTR dwParam2) {
        if (uMsg == WOM_DONE) {
            int soundId = static_cast<int>(dwInstance);
            SoundManager::getInstance().completeSound(soundId);
        }
    }
    
    std::mutex mutex_;
    std::map<int, std::unique_ptr<Sound>> activeSounds_;
    int nextSoundId_;
    static constexpr int MAX_SOUNDS = 16; // Limit concurrent sounds to avoid resource issues
};

class WindowsAudioPlayer : public AudioPlayer {
public:
    WindowsAudioPlayer() : volume_(1.0f) {}
    
    ~WindowsAudioPlayer() override {
        shutdown();
    }
    
    bool initialize() override {
        return waveOutGetNumDevs() > 0;
    }
    
    void shutdown() override {
        SoundManager::getInstance().cleanup();
    }
    
    void playSound(const std::vector<uint8_t>& data, 
                  const std::string& format,
                  std::shared_ptr<std::promise<void>> completionPromise = nullptr) override {
        if (format != "wav") {
            std::cerr << "Only WAV format is supported" << std::endl;
            if (completionPromise) completionPromise->set_value();
            return;
        }
        
        // Basic validation
        if (data.size() < 44 || 
            memcmp(data.data(), "RIFF", 4) != 0 || 
            memcmp(data.data() + 8, "WAVE", 4) != 0) {
            std::cerr << "Invalid WAV file" << std::endl;
            if (completionPromise) completionPromise->set_value();
            return;
        }
        
        // Start playing the sound
        SoundManager::getInstance().playSound(data, completionPromise);
    }
    
    void setVolume(float volume) override {
        volume_ = volume;
    }
    
private:
    float volume_;
};

// Factory function implementation for Windows
std::unique_ptr<AudioPlayer> createAudioPlayer() {
    return std::make_unique<WindowsAudioPlayer>();
}

#endif // _WIN32
