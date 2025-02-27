#ifndef _WIN32

#include "audiomanager.h"
#include <iostream>
#include <pulse/pulseaudio.h>
#include <pulse/simple.h>
#include <thread>

class PulseAudioPlayer : public AudioPlayer {
public:
    PulseAudioPlayer() : volume_(1.0f) {
        // Set default sample specification
        sampleSpec_.format = PA_SAMPLE_S16LE;
        sampleSpec_.rate = 44100;
        sampleSpec_.channels = 2;
    }
    
    ~PulseAudioPlayer() override {
        shutdown();
    }
    
    bool initialize() override {
        // Test if we can create a connection to PulseAudio
        int error = 0;
        pa_simple* s = pa_simple_new(
            NULL,               // Use default server
            "SolitaireAudio",   // Application name
            PA_STREAM_PLAYBACK, // Stream direction
            NULL,               // Default device
            "Test",             // Stream description
            &sampleSpec_,       // Sample format
            NULL,               // Default channel map
            NULL,               // Default buffering attributes
            &error              // Error code
        );
        
        if (s) {
            pa_simple_free(s);
            return true;
        } else {
            std::cerr << "Failed to initialize PulseAudio: " << pa_strerror(error) << std::endl;
            return false;
        }
    }
    
    void shutdown() override {
        // No persistent resources to clean up
    }
    
    void playSound(const std::vector<uint8_t>& data, 
                  const std::string& format,
                  std::shared_ptr<std::promise<void>> completionPromise = nullptr) override {
        // Spawn a thread to handle the audio playback to avoid blocking the main thread
        std::thread playbackThread([this, data, format, completionPromise]() {
            // For WAV files, skip the header and set format based on header
            size_t dataOffset = 0;
            pa_sample_spec ss = sampleSpec_;
            
            if (format == "wav" && data.size() >= 44) {
                dataOffset = 44; // Standard WAV header size
                
                // Parse basic WAV header for parameters
                uint32_t sampleRate = *reinterpret_cast<const uint32_t*>(&data[24]);
                uint16_t channels = *reinterpret_cast<const uint16_t*>(&data[22]);
                uint16_t bitsPerSample = *reinterpret_cast<const uint16_t*>(&data[34]);
                
                // Set sample specification based on WAV header
                ss.rate = sampleRate;
                ss.channels = channels;
                
                // Set format based on bits per sample
                if (bitsPerSample == 8) {
                    ss.format = PA_SAMPLE_U8;
                } else if (bitsPerSample == 16) {
                    ss.format = PA_SAMPLE_S16LE;
                } else if (bitsPerSample == 24) {
                    ss.format = PA_SAMPLE_S24LE;
                } else if (bitsPerSample == 32) {
                    ss.format = PA_SAMPLE_S32LE;
                }
            }
            
            int error = 0;
            
            // Create buffer attr with custom size
            pa_buffer_attr bufferAttr;
            bufferAttr.maxlength = static_cast<uint32_t>(-1);
            bufferAttr.tlength = static_cast<uint32_t>(-1);
            bufferAttr.prebuf = static_cast<uint32_t>(-1);
            bufferAttr.minreq = static_cast<uint32_t>(-1);
            bufferAttr.fragsize = static_cast<uint32_t>(-1);
            
            // Open a new playback stream
            pa_simple* s = pa_simple_new(
                NULL,               // Use default server
                "SolitaireAudio",   // Application name
                PA_STREAM_PLAYBACK, // Stream direction
                NULL,               // Default device
                format.c_str(),     // Stream description
                &ss,                // Sample format
                NULL,               // Default channel map
                &bufferAttr,        // Buffer attributes
                &error              // Error code
            );
            
            if (!s) {
                std::cerr << "Failed to open PulseAudio stream: " << pa_strerror(error) << std::endl;
                if (completionPromise) {
                    completionPromise->set_value(); // Fulfill the promise even on error
                }
                return;
            }
            
            // Write audio data to the stream
            if (pa_simple_write(s, data.data() + dataOffset, 
                              data.size() - dataOffset, &error) < 0) {
                std::cerr << "Failed to write audio data: " << pa_strerror(error) << std::endl;
                pa_simple_free(s);
                if (completionPromise) {
                    completionPromise->set_value(); // Fulfill the promise even on error
                }
                return;
            }
            
            // Wait for playback to complete
            if (pa_simple_drain(s, &error) < 0) {
                std::cerr << "Failed to drain audio: " << pa_strerror(error) << std::endl;
            }
            
            // Close the stream
            pa_simple_free(s);
            
            // Signal that playback is complete
            if (completionPromise) {
                completionPromise->set_value();
            }
        });
        
        // If we're using synchronous playback, we'll wait on the future
        // If we're using asynchronous playback, detach the thread
        if (completionPromise) {
            playbackThread.detach();
        } else {
            playbackThread.detach();
        }
    }
    
    void setVolume(float volume) override {
        volume_ = volume;
        
        // Note: PulseAudio volume control is more complex and would require
        // using the PulseAudio context API instead of the simple API
        // For a basic implementation, we don't modify system volume here
    }
    
private:
    float volume_;
    pa_sample_spec sampleSpec_;
};

// Factory function implementation for PulseAudio
std::unique_ptr<AudioPlayer> createAudioPlayer() {
    return std::make_unique<PulseAudioPlayer>();
}

#endif // !_WIN32
