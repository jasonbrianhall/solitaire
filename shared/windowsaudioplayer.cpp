#ifdef _WIN32

#include "audiomanager.h"
#include <iostream>
#include <windows.h>
#include <mmsystem.h>
#include <mmreg.h>
#include <thread>
#include <mutex>
#include <vector>
#include <list>
#include <algorithm>
#include <condition_variable>
#include <atomic>
#include <cstring>
#pragma comment(lib, "winmm.lib")

// Structure to hold WAV data information
struct WavSound {
    std::vector<uint8_t> data;     // Original WAV data
    int16_t* samples;              // Pointer to PCM sample data
    size_t sampleCount;            // Number of samples (considering all channels)
    size_t dataOffset;             // Offset to the data chunk
    uint16_t channels;             // Number of channels
    uint32_t sampleRate;           // Sample rate
    uint16_t bitsPerSample;        // Bits per sample
    bool isPlaying;                // Whether this sound is currently playing
    size_t position;               // Current playback position in frames (not samples)
    float volume;                  // Volume multiplier (0.0 to 1.0)
    std::shared_ptr<std::promise<void>> completionPromise;
    
    WavSound() : samples(nullptr), sampleCount(0), dataOffset(0), 
                 channels(0), sampleRate(0), bitsPerSample(0),
                 isPlaying(false), position(0), volume(1.0f) {}
};

// Audio Mixer class that handles all sound playback through a single output stream
class AudioMixer {
public:
    static AudioMixer& getInstance() {
        static AudioMixer instance;
        return instance;
    }
    
    ~AudioMixer() {
        shutdown();
    }
    
    // Initialize the mixer
    bool initialize() {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (initialized_) {
            return true;
        }
        
        // Check if audio devices are available
        if (waveOutGetNumDevs() == 0) {
#ifdef DEBUG
            std::cerr << "No audio output devices available" << std::endl;
#endif
            return false;
        }
        
        // Set up the output wave format
        wfx_.wFormatTag = WAVE_FORMAT_PCM;
        wfx_.nChannels = OUTPUT_CHANNELS;
        wfx_.nSamplesPerSec = OUTPUT_SAMPLE_RATE;
        wfx_.wBitsPerSample = OUTPUT_BITS_PER_SAMPLE;
        wfx_.nBlockAlign = wfx_.nChannels * wfx_.wBitsPerSample / 8;
        wfx_.nAvgBytesPerSec = wfx_.nSamplesPerSec * wfx_.nBlockAlign;
        wfx_.cbSize = 0;
        
        // Open the wave device
        MMRESULT result = waveOutOpen(&hWaveOut_, WAVE_MAPPER, &wfx_, 
                                     (DWORD_PTR)waveOutProc, (DWORD_PTR)this, CALLBACK_FUNCTION);
        if (result != MMSYSERR_NOERROR) {
#ifdef DEBUG
            std::cerr << "Failed to open wave device, error: " << result << std::endl;
#endif
            return false;
        }
        
        // Create a buffer for mixed audio (500ms of audio)
        bufferSizeSamples_ = (OUTPUT_SAMPLE_RATE * OUTPUT_CHANNELS * BUFFER_DURATION_MS) / 1000;
        for (int i = 0; i < NUM_BUFFERS; i++) {
            buffers_[i].resize(bufferSizeSamples_ * sizeof(int16_t));
            
            // Setup wave headers
            ZeroMemory(&waveHeaders_[i], sizeof(WAVEHDR));
            waveHeaders_[i].lpData = reinterpret_cast<LPSTR>(buffers_[i].data());
            waveHeaders_[i].dwBufferLength = static_cast<DWORD>(buffers_[i].size());
            waveHeaders_[i].dwUser = i;
            
            // Prepare the header
            result = waveOutPrepareHeader(hWaveOut_, &waveHeaders_[i], sizeof(WAVEHDR));
            if (result != MMSYSERR_NOERROR) {
#ifdef DEBUG
                std::cerr << "Failed to prepare header, error: " << result << std::endl;
#endif
                waveOutClose(hWaveOut_);
                return false;
            }
        }
        
        // Start playback thread
        exitThread_ = false;
        mixerThread_ = std::thread(&AudioMixer::mixerThreadFunc, this);
        
        initialized_ = true;
        return true;
    }
    
    // Shutdown the mixer
    void shutdown() {
        // Signal the thread to exit and wait for it
        if (mixerThread_.joinable()) {
            exitThread_ = true;
            condVar_.notify_all();
            mixerThread_.join();
        }
        
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (!initialized_) {
            return;
        }
        
        // Reset wave device and unprepare headers
        waveOutReset(hWaveOut_);
        
        for (int i = 0; i < NUM_BUFFERS; i++) {
            if (waveHeaders_[i].dwFlags & WHDR_PREPARED) {
                waveOutUnprepareHeader(hWaveOut_, &waveHeaders_[i], sizeof(WAVEHDR));
            }
        }
        
        // Close the wave device
        waveOutClose(hWaveOut_);
        hWaveOut_ = NULL;
        
        // Complete all pending sounds
        for (auto& sound : sounds_) {
            if (sound.completionPromise) {
                sound.completionPromise->set_value();
            }
        }
        sounds_.clear();
        
        initialized_ = false;
    }
    
    // Add a sound to the mixer
    bool addSound(const std::vector<uint8_t>& data, const std::string& format, 
                 std::shared_ptr<std::promise<void>> completionPromise) {
        // Only support WAV format
        if (format != "wav") {
#ifdef DEBUG
            std::cerr << "Only WAV format is supported" << std::endl;
#endif
            if (completionPromise) completionPromise->set_value();
            return false;
        }
        
        // Basic validation
        if (data.size() < 44 || 
            memcmp(data.data(), "RIFF", 4) != 0 || 
            memcmp(data.data() + 8, "WAVE", 4) != 0) {
#ifdef DEBUG
            std::cerr << "Invalid WAV file" << std::endl;
#endif
            if (completionPromise) completionPromise->set_value();
            return false;
        }
        
        // Create a new sound entry
        WavSound sound;
        sound.data = data;
        sound.completionPromise = completionPromise;
        sound.isPlaying = true;
        sound.position = 0;
        sound.volume = 1.0f;
        
        // Parse the WAV header
        if (!parseWavHeader(sound)) {
            if (completionPromise) completionPromise->set_value();
            return false;
        }
        
#ifdef DEBUG
        std::cout << "WAV file sample rate: " << sound.sampleRate 
                  << ", Output sample rate: " << OUTPUT_SAMPLE_RATE << std::endl;
#endif
        
        // Add the sound to our list
        std::lock_guard<std::mutex> lock(mutex_);
        sounds_.push_back(std::move(sound));
        
        // Signal the mixer thread that we have a new sound
        condVar_.notify_one();
        
        return true;
    }
    
    // Set master volume
    void setVolume(float volume) {
        masterVolume_ = std::max(0.0f, std::min(1.0f, volume));
    }

private:
    // Add a sample rate conversion function
    void resampleSound(const WavSound& sound, float* resampledBuffer, 
                     size_t outputSamples, size_t startPos, size_t& endPos) {
        // Calculate ratio between source and target sample rates
        double ratio = static_cast<double>(sound.sampleRate) / OUTPUT_SAMPLE_RATE;
        
        // Simple linear interpolation resampling
        for (size_t i = 0; i < outputSamples; i++) {
            double srcPos = (startPos + i) * ratio;
            size_t srcPosInt = static_cast<size_t>(srcPos);
            float fraction = static_cast<float>(srcPos - srcPosInt);
            
            // If we've reached the end of the source data
            if (srcPosInt >= sound.sampleCount / sound.channels - 1) {
                endPos = i;
                return;
            }
            
            // For each channel
            for (uint16_t ch = 0; ch < sound.channels; ch++) {
                // Get samples at position and next position
                int16_t sample1 = sound.samples[(srcPosInt * sound.channels) + ch];
                int16_t sample2 = sound.samples[((srcPosInt + 1) * sound.channels) + ch];
                
                // Linear interpolation
                float sample = sample1 * (1.0f - fraction) + sample2 * fraction;
                
                // Store in output buffer, normalize to float [-1.0, 1.0]
                resampledBuffer[(i * OUTPUT_CHANNELS) + (ch % OUTPUT_CHANNELS)] = 
                    sample / 32768.0f;
            }
        }
        
        endPos = outputSamples;
    }
    
    // Parse a WAV header and set up the sound object
    bool parseWavHeader(WavSound& sound) {
        const auto& data = sound.data;
        
        // Parse basic header info
        sound.channels = *reinterpret_cast<const uint16_t*>(&data[22]);
        sound.sampleRate = *reinterpret_cast<const uint32_t*>(&data[24]);
        sound.bitsPerSample = *reinterpret_cast<const uint16_t*>(&data[34]);
        
        // Only support 16-bit PCM for now (for simplicity)
        if (sound.bitsPerSample != 16) {
#ifdef DEBUG
            std::cerr << "Only 16-bit PCM WAV is supported" << std::endl;
#endif
            return false;
        }
        
        // Find the data chunk
        sound.dataOffset = 0;
        for (size_t i = 12; i < data.size() - 8; ) {
            if (memcmp(data.data() + i, "data", 4) == 0) {
                sound.dataOffset = i + 8;
                break;
            }
            
            uint32_t chunkSize = *reinterpret_cast<const uint32_t*>(&data[i + 4]);
            i += 8 + chunkSize + (chunkSize & 1);
            
            if (i >= data.size()) break;
        }
        
        if (sound.dataOffset == 0 || sound.dataOffset >= data.size()) {
#ifdef DEBUG
            std::cerr << "Data chunk not found in WAV file" << std::endl;
#endif
            return false;
        }
        
        // Calculate number of samples
        size_t dataSize = data.size() - sound.dataOffset;
        sound.sampleCount = dataSize / (sound.bitsPerSample / 8);
        sound.samples = reinterpret_cast<int16_t*>(const_cast<uint8_t*>(&data[sound.dataOffset]));
        
        return true;
    }
    
    // Mix all active sounds into a buffer with resampling
    void mixSounds(int16_t* outputBuffer, size_t sampleCount) {
        // First clear the output buffer
        std::memset(outputBuffer, 0, sampleCount * sizeof(int16_t));
        
        // Create a float buffer for mixing (to avoid clipping)
        std::vector<float> mixBuffer(sampleCount * OUTPUT_CHANNELS, 0.0f);
        
        std::lock_guard<std::mutex> lock(mutex_);
        
        // Process each sound
        auto it = sounds_.begin();
        while (it != sounds_.end()) {
            if (!it->isPlaying) {
                // Clean up completed sounds
                if (it->completionPromise) {
                    it->completionPromise->set_value();
                }
                it = sounds_.erase(it);
                continue;
            }
            
            // Create temporary buffer for resampled data
            std::vector<float> resampledBuffer(sampleCount * OUTPUT_CHANNELS, 0.0f);
            
            // Calculate samples considering sample rate conversion
            size_t samplesResampled = 0;
            resampleSound(*it, resampledBuffer.data(), sampleCount, 
                        it->position, samplesResampled);
            
            // Mix resampled sound to the float buffer
            for (size_t i = 0; i < samplesResampled * OUTPUT_CHANNELS; i++) {
                // Apply volume
                float sample = resampledBuffer[i] * it->volume * masterVolume_;
                
                // Add to mix buffer
                mixBuffer[i] += sample;
            }
            
            // Update position and check if finished
            it->position += samplesResampled;
            
            if (samplesResampled < sampleCount || it->position >= it->sampleCount / it->channels) {
                it->isPlaying = false;
                if (it->completionPromise) {
                    it->completionPromise->set_value();
                }
                it = sounds_.erase(it);
            } else {
                ++it;
            }
        }
        
        // Convert float mix buffer back to int16_t with clipping protection
        for (size_t i = 0; i < sampleCount * OUTPUT_CHANNELS; i++) {
            float sample = mixBuffer[i];
            // Clip to [-1.0, 1.0]
            sample = std::max(-1.0f, std::min(1.0f, sample));
            // Convert to int16_t
            outputBuffer[i] = static_cast<int16_t>(sample * 32767.0f);
        }
    }
    
    // Thread function for the mixer
    void mixerThreadFunc() {
        while (!exitThread_) {
            // Wait for buffer completion or new sounds
            {
                std::unique_lock<std::mutex> lock(mutex_);
                condVar_.wait(lock, [this] { 
                    return !sounds_.empty() || exitThread_; 
                });
                
                if (exitThread_) break;
            }
            
            // Check which buffer is available
            int bufferIndex = -1;
            for (int i = 0; i < NUM_BUFFERS; i++) {
                if (!(waveHeaders_[i].dwFlags & WHDR_INQUEUE)) {
                    bufferIndex = i;
                    break;
                }
            }
            
            if (bufferIndex != -1) {
                // Mix audio into this buffer
                int16_t* outputBuffer = reinterpret_cast<int16_t*>(buffers_[bufferIndex].data());
                mixSounds(outputBuffer, bufferSizeSamples_ / OUTPUT_CHANNELS);
                
                // Write the buffer to the output device
                MMRESULT result = waveOutWrite(hWaveOut_, &waveHeaders_[bufferIndex], sizeof(WAVEHDR));
                if (result != MMSYSERR_NOERROR) {
#ifdef DEBUG
                    std::cerr << "Failed to write audio buffer, error: " << result << std::endl;
#endif
                }
            }
            
            // Sleep a bit to prevent high CPU usage
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    
    // Static callback for waveOut
    static void CALLBACK waveOutProc(HWAVEOUT hwo, UINT uMsg, DWORD_PTR dwInstance, 
                                    DWORD_PTR dwParam1, DWORD_PTR dwParam2) {
        if (uMsg == WOM_DONE) {
            AudioMixer* mixer = reinterpret_cast<AudioMixer*>(dwInstance);
            mixer->condVar_.notify_one();
        }
    }
    
    // Private constructor for singleton
    AudioMixer() : initialized_(false), hWaveOut_(NULL), 
                  currentBuffer_(0), masterVolume_(1.0f), 
                  exitThread_(false) {}
    
    // Constants
    static constexpr int NUM_BUFFERS = 4;
    static constexpr int BUFFER_DURATION_MS = 50;
    static constexpr int OUTPUT_CHANNELS = 2;
    static constexpr int OUTPUT_SAMPLE_RATE = 44100;
    static constexpr int OUTPUT_BITS_PER_SAMPLE = 16;
    
    // Member variables
    bool initialized_;
    HWAVEOUT hWaveOut_;
    WAVEFORMATEX wfx_;
    std::vector<uint8_t> buffers_[NUM_BUFFERS];
    WAVEHDR waveHeaders_[NUM_BUFFERS];
    size_t bufferSizeSamples_;
    int currentBuffer_;
    float masterVolume_;
    
    std::list<WavSound> sounds_;
    std::mutex mutex_;
    std::condition_variable condVar_;
    std::thread mixerThread_;
    std::atomic<bool> exitThread_;
};

// Windows implementation of AudioPlayer using the mixer
class WindowsAudioPlayer : public AudioPlayer {
public:
    WindowsAudioPlayer() {}
    
    ~WindowsAudioPlayer() override {
        shutdown();
    }
    
    bool initialize() override {
        return AudioMixer::getInstance().initialize();
    }
    
    void shutdown() override {
        AudioMixer::getInstance().shutdown();
    }
    
    void playSound(const std::vector<uint8_t>& data, 
                  const std::string& format,
                  std::shared_ptr<std::promise<void>> completionPromise = nullptr) override {
        AudioMixer::getInstance().addSound(data, format, completionPromise);
    }
    
    void setVolume(float volume) override {
        AudioMixer::getInstance().setVolume(volume);
    }
};

// Factory function implementation for Windows
std::unique_ptr<AudioPlayer> createAudioPlayer() {
    return std::make_unique<WindowsAudioPlayer>();
}

#endif // _WIN32
