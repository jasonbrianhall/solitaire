#ifdef _WIN32

#include "audiomanager.h"
#include <iostream>
#include <windows.h>
#include <mmsystem.h>
#include <mmreg.h>
#pragma comment(lib, "winmm.lib")

// Callback function for Windows PlaySound completion
void CALLBACK WaveOutProc(HWAVEOUT hwo, UINT uMsg, DWORD_PTR dwInstance, DWORD_PTR dwParam1, DWORD_PTR dwParam2) {
    if (uMsg == WOM_DONE) {
        // Get our promise from the instance data
        auto promisePtr = reinterpret_cast<std::shared_ptr<std::promise<void>>*>(dwInstance);
        if (promisePtr && *promisePtr) {
            // Set the promise value to signal completion
            (*promisePtr)->set_value();
            
            // Clean up the memory allocated for the promise pointer
            delete promisePtr;
        }
    }
}

class WindowsAudioPlayer : public AudioPlayer {
public:
    WindowsAudioPlayer() : volume_(1.0f) {}
    
    ~WindowsAudioPlayer() override {
        shutdown();
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
        // No explicit cleanup needed for Windows basic audio
    }
    
    void playSound(const std::vector<uint8_t>& data, 
                  const std::string& format,
                  std::shared_ptr<std::promise<void>> completionPromise = nullptr) override {
        if (format == "wav") {
            if (completionPromise) {
                // For synchronous playback, we need to use waveOut* functions to get callbacks
                playWavWithCallback(data, completionPromise);
            } else {
                // For asynchronous playback, we can use the simpler PlaySound API
                // SND_MEMORY to play from memory, SND_ASYNC to play asynchronously
                PlaySoundA(
                    reinterpret_cast<LPCSTR>(data.data()),
                    NULL,
                    SND_MEMORY | SND_ASYNC | SND_NODEFAULT
                );
            }
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
    }
    
    void setVolume(float volume) override {
        volume_ = volume;
        
        // Windows doesn't provide a simple global volume control for PlaySound
        // For more advanced volume control, we'd need to use waveOut* functions
        // or a more sophisticated audio library
    }
    
private:
    float volume_;
    
    void playWavWithCallback(const std::vector<uint8_t>& data, 
                            std::shared_ptr<std::promise<void>> completionPromise) {
        // This is a simplified implementation that works for basic WAV files
        // A production-quality implementation would need more robust WAV parsing
        
        // Allocate memory for the promise pointer that will be passed to the callback
        auto promisePtr = new std::shared_ptr<std::promise<void>>(completionPromise);
        
        // Set up the wave header (simplified for brevity)
        WAVEFORMATEX wfx = {0};
        wfx.wFormatTag = WAVE_FORMAT_PCM;
        wfx.nChannels = 2;  // Stereo
        wfx.nSamplesPerSec = 44100;  // 44.1kHz
        wfx.wBitsPerSample = 16;  // 16-bit
        wfx.nBlockAlign = wfx.nChannels * wfx.wBitsPerSample / 8;
        wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;
        
        // Open a waveOut device
        HWAVEOUT hWaveOut;
        MMRESULT result = waveOutOpen(&hWaveOut, WAVE_MAPPER, &wfx, 
                                     (DWORD_PTR)WaveOutProc, (DWORD_PTR)promisePtr, 
                                     CALLBACK_FUNCTION);
        
        if (result != MMSYSERR_NOERROR) {
            std::cerr << "Failed to open waveOut device" << std::endl;
            delete promisePtr;
            completionPromise->set_value();  // Signal completion even on failure
            return;
        }
        
        // Set up the wave header
        WAVEHDR waveHeader = {0};
        waveHeader.lpData = (LPSTR)data.data() + 44;  // Skip WAV header
        waveHeader.dwBufferLength = data.size() - 44;
        waveHeader.dwFlags = 0;
        
        // Prepare the header
        result = waveOutPrepareHeader(hWaveOut, &waveHeader, sizeof(WAVEHDR));
        if (result != MMSYSERR_NOERROR) {
            std::cerr << "Failed to prepare waveOut header" << std::endl;
            waveOutClose(hWaveOut);
            delete promisePtr;
            completionPromise->set_value();
            return;
        }
        
        // Write the data
        result = waveOutWrite(hWaveOut, &waveHeader, sizeof(WAVEHDR));
        if (result != MMSYSERR_NOERROR) {
            std::cerr << "Failed to write wave data" << std::endl;
            waveOutUnprepareHeader(hWaveOut, &waveHeader, sizeof(WAVEHDR));
            waveOutClose(hWaveOut);
            delete promisePtr;
            completionPromise->set_value();
            return;
        }
        
        // The callback will handle cleanup and signaling completion
    }
};

// Factory function implementation for Windows
std::unique_ptr<AudioPlayer> createAudioPlayer() {
    return std::make_unique<WindowsAudioPlayer>();
}

#endif // _WIN32
