#ifdef _WIN32

#include "audiomanager.h"
#include <iostream>
#include <windows.h>
#include <mmsystem.h>
#include <mmreg.h>
#include <vector>
#include <mutex>
#include <map>
#include <stdexcept>
#pragma comment(lib, "winmm.lib")

// Forward declarations
void CALLBACK WaveOutProc(HWAVEOUT hwo, UINT uMsg, DWORD_PTR dwInstance, DWORD_PTR dwParam1, DWORD_PTR dwParam2);
void CALLBACK CleanupTimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime);
void CALLBACK StuckSoundTimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime);

// Structure to track active sound playbacks
struct ActiveSound {
    HWAVEOUT hWaveOut;
    WAVEHDR waveHeader;
    std::vector<uint8_t> soundData; // Keep a copy of the data to ensure it remains valid
    std::shared_ptr<std::promise<void>> completionPromise;
    bool isCompleted;
    DWORD startTime; // Time when sound started playing (for timeout detection)
    
    ActiveSound() : hWaveOut(NULL), isCompleted(false), startTime(0) {
        ZeroMemory(&waveHeader, sizeof(WAVEHDR));
    }
    
    ~ActiveSound() {
        // Safety cleanup in case something wasn't properly released
        if (hWaveOut) {
            if (waveHeader.dwFlags & WHDR_PREPARED) {
                waveOutUnprepareHeader(hWaveOut, &waveHeader, sizeof(WAVEHDR));
            }
            waveOutClose(hWaveOut);
            hWaveOut = NULL;
        }
    }

    // Disable copying
    ActiveSound(const ActiveSound&) = delete;
    ActiveSound& operator=(const ActiveSound&) = delete;

    // Allow moving
    ActiveSound(ActiveSound&& other) noexcept {
        hWaveOut = other.hWaveOut;
        waveHeader = other.waveHeader;
        soundData = std::move(other.soundData);
        completionPromise = std::move(other.completionPromise);
        isCompleted = other.isCompleted;
        startTime = other.startTime;
        
        // Reset the moved-from object
        other.hWaveOut = NULL;
        ZeroMemory(&other.waveHeader, sizeof(WAVEHDR));
        other.isCompleted = true;
    }

    ActiveSound& operator=(ActiveSound&& other) noexcept {
        if (this != &other) {
            // Clean up own resources first
            if (hWaveOut) {
                if (waveHeader.dwFlags & WHDR_PREPARED) {
                    waveOutUnprepareHeader(hWaveOut, &waveHeader, sizeof(WAVEHDR));
                }
                waveOutClose(hWaveOut);
            }
            
            // Move from other
            hWaveOut = other.hWaveOut;
            waveHeader = other.waveHeader;
            soundData = std::move(other.soundData);
            completionPromise = std::move(other.completionPromise);
            isCompleted = other.isCompleted;
            startTime = other.startTime;
            
            // Reset the moved-from object
            other.hWaveOut = NULL;
            ZeroMemory(&other.waveHeader, sizeof(WAVEHDR));
            other.isCompleted = true;
        }
        return *this;
    }
};

// Global sound manager to track active sounds
class SoundManager {
public:
    SoundManager() : nextSoundId_(1), activeSoundCount_(0) {}
    
    // Add a new sound and return its ID
    uint32_t addSound(ActiveSound sound) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // Limit concurrent sounds - immediately complete if we hit the limit
        if (activeSounds_.size() >= MAX_CONCURRENT_SOUNDS) {
            if (sound.completionPromise) {
                sound.completionPromise->set_value();
            }
            return 0; // Return 0 to indicate no sound was added
        }
        
        uint32_t soundId = nextSoundId_++;
        activeSounds_[soundId] = std::move(sound);
        activeSoundCount_++;
        return soundId;
    }
    
    bool canAddMoreSounds() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return activeSounds_.size() < MAX_CONCURRENT_SOUNDS;
    }
    
    // Get a sound by ID
    ActiveSound* getSound(uint32_t soundId) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = activeSounds_.find(soundId);
        if (it != activeSounds_.end()) {
            return &it->second;
        }
        return nullptr;
    }
    
    // Mark a sound as completed
    void completeSound(uint32_t soundId) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = activeSounds_.find(soundId);
        if (it != activeSounds_.end() && !it->second.isCompleted) {
            it->second.isCompleted = true;
            
            // Signal completion to any waiting promises
            if (it->second.completionPromise) {
                try {
                    it->second.completionPromise->set_value();
                } catch (const std::exception& e) {
                    std::cerr << "Exception in completeSound: " << e.what() << std::endl;
                }
            }
        }
    }
    
    // Clean up completed sounds
    void cleanupCompletedSounds() {
        std::lock_guard<std::mutex> lock(mutex_);
        
        for (auto it = activeSounds_.begin(); it != activeSounds_.end();) {
            if (it->second.isCompleted) {
                // Stop and reset the device first
                if (it->second.hWaveOut) {
                    waveOutReset(it->second.hWaveOut);
                }
                
                // Only unprepare if the header was prepared
                if (it->second.waveHeader.dwFlags & WHDR_PREPARED) {
                    MMRESULT result = waveOutUnprepareHeader(it->second.hWaveOut, &it->second.waveHeader, sizeof(WAVEHDR));
                    if (result != MMSYSERR_NOERROR) {
                        std::cerr << "Failed to unprepare header, error: " << result << std::endl;
                    }
                }
                
                // Close the waveOut device
                if (it->second.hWaveOut) {
                    MMRESULT result = waveOutClose(it->second.hWaveOut);
                    if (result != MMSYSERR_NOERROR) {
                        std::cerr << "Failed to close waveOut device, error: " << result << std::endl;
                    }
                    it->second.hWaveOut = NULL;
                }
                
                it = activeSounds_.erase(it);
                activeSoundCount_--;
            } else {
                ++it;
            }
        }
    }
    
    // Check for stuck sounds and force-complete them
    void checkForStuckSounds() {
        std::lock_guard<std::mutex> lock(mutex_);
        DWORD currentTime = GetTickCount();
        
        for (auto& pair : activeSounds_) {
            ActiveSound& sound = pair.second;
            
            // If a sound has been playing for too long, force complete it
            if (!sound.isCompleted && (currentTime - sound.startTime) > SOUND_TIMEOUT_MS) {
                std::cerr << "Detected stuck sound (ID: " << pair.first << "), forcing completion" << std::endl;
                
                // Reset the device to stop playback
                if (sound.hWaveOut) {
                    waveOutReset(sound.hWaveOut);
                }
                
                // Mark as completed and signal the promise
                sound.isCompleted = true;
                if (sound.completionPromise) {
                    try {
                        sound.completionPromise->set_value();
                    } catch (const std::exception& e) {
                        std::cerr << "Exception in checkForStuckSounds: " << e.what() << std::endl;
                    }
                }
            }
        }
    }
    
    // Clean up all sounds (for shutdown)
    void cleanupAllSounds() {
        std::lock_guard<std::mutex> lock(mutex_);
        
        for (auto& pair : activeSounds_) {
            ActiveSound& sound = pair.second;
            
            // Stop playback
            if (sound.hWaveOut) {
                waveOutReset(sound.hWaveOut);
            }
            
            // Signal completion to any waiting promises
            if (sound.completionPromise && !sound.isCompleted) {
                try {
                    sound.completionPromise->set_value();
                } catch (const std::exception& e) {
                    std::cerr << "Exception in cleanupAllSounds: " << e.what() << std::endl;
                }
            }
            
            // Only unprepare if the header was prepared
            if (sound.hWaveOut && (sound.waveHeader.dwFlags & WHDR_PREPARED)) {
                waveOutUnprepareHeader(sound.hWaveOut, &sound.waveHeader, sizeof(WAVEHDR));
            }
            
            // Close the waveOut device
            if (sound.hWaveOut) {
                waveOutClose(sound.hWaveOut);
                sound.hWaveOut = NULL;
            }
        }
        
        activeSounds_.clear();
        activeSoundCount_ = 0;
    }
    
    // Get active sound count for debugging
    size_t getActiveSoundCount() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return activeSoundCount_;
    }
    
private:
    static const size_t MAX_CONCURRENT_SOUNDS = 5;
    static const DWORD SOUND_TIMEOUT_MS = 10000; // 10 seconds
    
    mutable std::mutex mutex_;
    std::map<uint32_t, ActiveSound> activeSounds_;
    uint32_t nextSoundId_;
    size_t activeSoundCount_;
};

// Global sound manager instance
SoundManager g_SoundManager;

// Callback function for Windows waveOut completion
void CALLBACK WaveOutProc(HWAVEOUT hwo, UINT uMsg, DWORD_PTR dwInstance, DWORD_PTR dwParam1, DWORD_PTR dwParam2) {
    if (uMsg == WOM_DONE) {
        try {
            // Get the sound ID from the instance data
            uint32_t soundId = static_cast<uint32_t>(dwInstance);
            
            // Mark the sound as completed
            g_SoundManager.completeSound(soundId);
            
            // Force an immediate cleanup
            g_SoundManager.cleanupCompletedSounds();
        } catch (const std::exception& e) {
            std::cerr << "Exception in WaveOutProc: " << e.what() << std::endl;
        }
    }
}

// Timer procedure to clean up completed sounds
void CALLBACK CleanupTimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime) {
    g_SoundManager.cleanupCompletedSounds();
}

// Timer procedure to check for stuck sounds
void CALLBACK StuckSoundTimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime) {
    g_SoundManager.checkForStuckSounds();
}

class WindowsAudioPlayer : public AudioPlayer {
public:
    WindowsAudioPlayer() : volume_(1.0f), cleanupTimerId_(0), stuckSoundTimerId_(0) {
        // Create a timer that will run every 500ms to clean up sounds
        cleanupTimerId_ = SetTimer(NULL, 0, 500, CleanupTimerProc);
        
        // Create a second timer that checks for "stuck" sounds every 5 seconds
        stuckSoundTimerId_ = SetTimer(NULL, 0, 5000, StuckSoundTimerProc);
    }
    
    ~WindowsAudioPlayer() override {
        shutdown();
        
        // Kill all timers
        if (cleanupTimerId_) {
            KillTimer(NULL, cleanupTimerId_);
            cleanupTimerId_ = 0;
        }
        
        if (stuckSoundTimerId_) {
            KillTimer(NULL, stuckSoundTimerId_);
            stuckSoundTimerId_ = 0;
        }
    }
    
    bool initialize() override {
        // Windows doesn't need explicit initialization for basic WAV playback
        // Just check if the Windows Multimedia System is available
        MMRESULT result = waveOutGetNumDevs();
        if (result == 0) {
            std::cerr << "No audio output devices found" << std::endl;
            return false;
        }
        return true;
    }
    
    void shutdown() override {
        // Clean up all active sounds
        g_SoundManager.cleanupAllSounds();
    }
    
    void playSound(const std::vector<uint8_t>& data, 
                  const std::string& format,
                  std::shared_ptr<std::promise<void>> completionPromise = nullptr) override {
        if (format == "wav") {
            // For both synchronous and asynchronous playback, use waveOut* functions
            playWavSound(data, completionPromise);
        }
        else if (format == "mp3") {
            // For MP3 files, we'd need a more advanced library
            // This basic implementation doesn't support MP3 natively
            std::cerr << "MP3 playback requires additional libraries on Windows" << std::endl;
            if (completionPromise) {
                // Signal completion even though we couldn't play
                completionPromise->set_value();
            }
        }
        else {
            std::cerr << "Unsupported audio format: " << format << std::endl;
            if (completionPromise) {
                completionPromise->set_value();
            }
        }
    }
    
    void setVolume(float volume) override {
        volume_ = volume;
        // Note: To implement volume control, you would need to apply it
        // to each active sound using waveOutSetVolume
    }
    
private:
    float volume_;
    UINT_PTR cleanupTimerId_;
    UINT_PTR stuckSoundTimerId_;
    
    // Improved playWavSound method
    void playWavSound(const std::vector<uint8_t>& data, 
                     std::shared_ptr<std::promise<void>> completionPromise) {
        // If we can't add more sounds, complete immediately
        if (!g_SoundManager.canAddMoreSounds()) {
            std::cerr << "Too many active sounds, skipping playback" << std::endl;
            if (completionPromise) {
                completionPromise->set_value();
            }
            return;
        }

        // Basic WAV file validation
        if (data.size() < 44) {
            std::cerr << "Invalid WAV file: too small" << std::endl;
            if (completionPromise) {
                completionPromise->set_value();
            }
            return;
        }
        
        try {
            // Check for RIFF/WAVE header
            if (memcmp(data.data(), "RIFF", 4) != 0 || memcmp(data.data() + 8, "WAVE", 4) != 0) {
                std::cerr << "Invalid WAV file: missing RIFF/WAVE header" << std::endl;
                if (completionPromise) {
                    completionPromise->set_value();
                }
                return;
            }
            
            // Extract WAV format information
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
                // Get chunk size and move to next chunk
                uint32_t chunkSize = *reinterpret_cast<const uint32_t*>(&data[i + 4]);
                i += 8 + chunkSize;
                
                // Safety check to prevent infinite loop
                if (i >= data.size()) {
                    break;
                }
            }
            
            // Validate data chunk
            if (dataOffset == 0 || dataOffset >= data.size()) {
                std::cerr << "Invalid WAV file: data chunk not found" << std::endl;
                if (completionPromise) {
                    completionPromise->set_value();
                }
                return;
            }
            
            // Setup WAVEFORMATEX structure
            wfx.wFormatTag = audioFormat;
            wfx.nChannels = numChannels;
            wfx.nSamplesPerSec = sampleRate;
            wfx.wBitsPerSample = bitsPerSample;
            wfx.nBlockAlign = numChannels * bitsPerSample / 8;
            wfx.nAvgBytesPerSec = sampleRate * wfx.nBlockAlign;
            
            // Create a new sound entry
            ActiveSound sound;
            sound.soundData = data; // Copy the data
            sound.completionPromise = completionPromise;
            sound.startTime = GetTickCount(); // Record start time
            
            // Add the sound to the sound manager
            uint32_t soundId = g_SoundManager.addSound(std::move(sound));
            if (soundId == 0) {
                // Sound wasn't added (likely due to too many active sounds)
                return;
            }
            
            // Get a pointer to the active sound
            ActiveSound* activeSound = g_SoundManager.getSound(soundId);
            if (!activeSound) {
                std::cerr << "Failed to retrieve active sound" << std::endl;
                if (completionPromise) {
                    completionPromise->set_value();
                }
                return;
            }
            
            // Open the waveOut device
            MMRESULT result = waveOutOpen(&activeSound->hWaveOut, WAVE_MAPPER, &wfx, 
                                        (DWORD_PTR)WaveOutProc, soundId, 
                                        CALLBACK_FUNCTION);
            
            if (result != MMSYSERR_NOERROR) {
                std::cerr << "Failed to open waveOut device, error code: " << result << std::endl;
                g_SoundManager.completeSound(soundId);
                return;
            }
            
            // Setup the wave header
            activeSound->waveHeader.lpData = (LPSTR)activeSound->soundData.data() + dataOffset;
            activeSound->waveHeader.dwBufferLength = static_cast<DWORD>(activeSound->soundData.size() - dataOffset);
            activeSound->waveHeader.dwFlags = 0;
            
            // Prepare the header
            result = waveOutPrepareHeader(activeSound->hWaveOut, &activeSound->waveHeader, sizeof(WAVEHDR));
            if (result != MMSYSERR_NOERROR) {
                std::cerr << "Failed to prepare waveOut header, error code: " << result << std::endl;
                waveOutClose(activeSound->hWaveOut);
                activeSound->hWaveOut = NULL;
                g_SoundManager.completeSound(soundId);
                return;
            }
            
            // Write the wave data
            result = waveOutWrite(activeSound->hWaveOut, &activeSound->waveHeader, sizeof(WAVEHDR));
            if (result != MMSYSERR_NOERROR) {
                std::cerr << "Failed to write wave data, error code: " << result << std::endl;
                // Need to unprepare the header before closing
                waveOutUnprepareHeader(activeSound->hWaveOut, &activeSound->waveHeader, sizeof(WAVEHDR));
                waveOutClose(activeSound->hWaveOut);
                activeSound->hWaveOut = NULL;
                g_SoundManager.completeSound(soundId);
                return;
            }
            
            // If we got here, the sound is playing and will be cleaned up in the callback
        } catch (const std::exception& e) {
            std::cerr << "Exception in playWavSound: " << e.what() << std::endl;
            if (completionPromise) {
                completionPromise->set_value();
            }
        }
    }
};

// Factory function implementation for Windows
std::unique_ptr<AudioPlayer> createAudioPlayer() {
    try {
        return std::make_unique<WindowsAudioPlayer>();
    } catch (const std::exception& e) {
        std::cerr << "Exception creating WindowsAudioPlayer: " << e.what() << std::endl;
        return nullptr;
    }
}

#endif // _WIN32
