#ifdef _WIN32

#include "audiomanager.h"
#include <iostream>
#include <windows.h>
#include <mmsystem.h>
#include <mmreg.h>
#include <vector>
#include <atomic>
#include <mutex>
#pragma comment(lib, "winmm.lib")

// Static playing flag to prevent multiple simultaneous sounds
static std::atomic<bool> s_isPlaying(false);
static std::mutex s_mutex;
static std::vector<uint8_t> s_soundBuffer;

// Forward declaration
void CALLBACK waveOutProc(HWAVEOUT hwo, UINT uMsg, DWORD_PTR dwInstance, DWORD_PTR dwParam1, DWORD_PTR dwParam2);

class WindowsAudioPlayer : public AudioPlayer {
public:
    WindowsAudioPlayer() : volume_(1.0f), hWaveOut_(NULL) {
    }
    
    ~WindowsAudioPlayer() override {
        shutdown();
    }
    
    bool initialize() override {
        // Just check if audio devices are available
        return waveOutGetNumDevs() > 0;
    }
    
    void shutdown() override {
        std::lock_guard<std::mutex> lock(s_mutex);
        
        // Close any open wave device
        if (hWaveOut_ != NULL) {
            waveOutReset(hWaveOut_);
            waveOutClose(hWaveOut_);
            hWaveOut_ = NULL;
        }
        
        // Reset state
        s_isPlaying = false;
        s_soundBuffer.clear();
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
        
        // Check if we can play a sound now
        bool expectedPlaying = false;
        if (!s_isPlaying.compare_exchange_strong(expectedPlaying, true)) {
            // Already playing, just fulfill the promise
            if (completionPromise) {
                completionPromise->set_value();
            }
            return;
        }
        
        std::lock_guard<std::mutex> lock(s_mutex);
        
        try {
            // Store a copy of the data
            s_soundBuffer = data;
            
            // Get the WAV header info
            WAVEFORMATEX wfx = {0};
            
            // Validate RIFF/WAVE headers
            if (memcmp(data.data(), "RIFF", 4) != 0 || memcmp(data.data() + 8, "WAVE", 4) != 0) {
                std::cerr << "Invalid WAV file: missing RIFF/WAVE header" << std::endl;
                s_isPlaying = false;
                if (completionPromise) {
                    completionPromise->set_value();
                }
                return;
            }
            
            // Extract format info
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
                s_isPlaying = false;
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
            
            // Close any previous device
            if (hWaveOut_ != NULL) {
                waveOutReset(hWaveOut_);
                waveOutClose(hWaveOut_);
                hWaveOut_ = NULL;
            }
            
            // Open a new waveOut device
            MMRESULT result = waveOutOpen(&hWaveOut_, WAVE_MAPPER, &wfx, 
                                         (DWORD_PTR)waveOutProc, 
                                         (DWORD_PTR)completionPromise.get(), 
                                         CALLBACK_FUNCTION);
            
            if (result != MMSYSERR_NOERROR) {
                std::cerr << "Failed to open waveOut device, error: " << result << std::endl;
                hWaveOut_ = NULL;
                s_isPlaying = false;
                if (completionPromise) {
                    completionPromise->set_value();
                }
                return;
            }
            
            // Set up the WAVEHDR
            ZeroMemory(&waveHeader_, sizeof(WAVEHDR));
            waveHeader_.lpData = (LPSTR)(s_soundBuffer.data() + dataOffset);
            waveHeader_.dwBufferLength = static_cast<DWORD>(s_soundBuffer.size() - dataOffset);
            waveHeader_.dwUser = (DWORD_PTR)completionPromise.get();
            
            // Prepare the header
            result = waveOutPrepareHeader(hWaveOut_, &waveHeader_, sizeof(WAVEHDR));
            if (result != MMSYSERR_NOERROR) {
                std::cerr << "Failed to prepare waveOut header, error: " << result << std::endl;
                waveOutClose(hWaveOut_);
                hWaveOut_ = NULL;
                s_isPlaying = false;
                if (completionPromise) {
                    completionPromise->set_value();
                }
                return;
            }
            
            // Write the data
            result = waveOutWrite(hWaveOut_, &waveHeader_, sizeof(WAVEHDR));
            if (result != MMSYSERR_NOERROR) {
                std::cerr << "Failed to write wave data, error: " << result << std::endl;
                waveOutUnprepareHeader(hWaveOut_, &waveHeader_, sizeof(WAVEHDR));
                waveOutClose(hWaveOut_);
                hWaveOut_ = NULL;
                s_isPlaying = false;
                if (completionPromise) {
                    completionPromise->set_value();
                }
                return;
            }
            
            // The sound is now playing, and the callback will handle completion
        }
        catch (const std::exception& e) {
            std::cerr << "Exception in playSound: " << e.what() << std::endl;
            s_isPlaying = false;
            if (completionPromise) {
                completionPromise->set_value();
            }
        }
    }
    
    void setVolume(float volume) override {
        volume_ = volume;
        
        // Apply volume if we have an open device
        if (hWaveOut_ != NULL) {
            DWORD volumeValue = static_cast<DWORD>(volume_ * 0xFFFF);
            DWORD stereoVolume = volumeValue | (volumeValue << 16);
            waveOutSetVolume(hWaveOut_, stereoVolume);
        }
    }
    
private:
    float volume_;
    HWAVEOUT hWaveOut_;
    WAVEHDR waveHeader_;
};

// Callback function for waveOut
void CALLBACK waveOutProc(HWAVEOUT hwo, UINT uMsg, DWORD_PTR dwInstance, DWORD_PTR dwParam1, DWORD_PTR dwParam2) {
    if (uMsg == WOM_DONE) {
        // Get the promise pointer from the instance data
        std::shared_ptr<std::promise<void>>* completionPromisePtr = 
            reinterpret_cast<std::shared_ptr<std::promise<void>>*>(dwInstance);
        
        std::lock_guard<std::mutex> lock(s_mutex);
        
        // Clean up wave resources
        WAVEHDR* waveHdr = reinterpret_cast<WAVEHDR*>(dwParam1);
        if (waveHdr) {
            if (waveHdr->dwFlags & WHDR_PREPARED) {
                waveOutUnprepareHeader(hwo, waveHdr, sizeof(WAVEHDR));
            }
        }
        
        // Reset playing state
        s_isPlaying = false;
        
        // Signal completion if the promise exists
        if (completionPromisePtr && *completionPromisePtr) {
            try {
                (*completionPromisePtr)->set_value();
            }
            catch (const std::exception& e) {
                std::cerr << "Exception in waveOutProc: " << e.what() << std::endl;
            }
        }
    }
}

// Factory function implementation for Windows
std::unique_ptr<AudioPlayer> createAudioPlayer() {
    return std::make_unique<WindowsAudioPlayer>();
}

#endif // _WIN32
