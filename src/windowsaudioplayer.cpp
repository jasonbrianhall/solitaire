#ifdef _WIN32

#include "audiomanager.h"
#include <iostream>
#include <map>
#include <mmreg.h>
#include <mmsystem.h>
#include <mutex>
#include <vector>
#include <windows.h>
#pragma comment(lib, "winmm.lib")

// Structure to track active sound playbacks
struct ActiveSound {
  HWAVEOUT hWaveOut;
  WAVEHDR waveHeader;
  std::vector<uint8_t>
      soundData; // Keep a copy of the data to ensure it remains valid
  std::shared_ptr<std::promise<void>> completionPromise;
  bool isCompleted;

  ActiveSound() : hWaveOut(NULL), isCompleted(false) {
    ZeroMemory(&waveHeader, sizeof(WAVEHDR));
  }
};

// Global sound manager to track active sounds
class SoundManager {
public:
  SoundManager() : nextSoundId_(1) {}

  // Add a new sound and return its ID
  uint32_t addSound(ActiveSound sound) {
    std::lock_guard<std::mutex> lock(mutex_);
    uint32_t soundId = nextSoundId_++;
    activeSounds_[soundId] = std::move(sound);
    return soundId;
  }

  bool canAddMoreSounds() const { return activeSounds_.size() < 16; }

  // Get a sound by ID
  ActiveSound *getSound(uint32_t soundId) {
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
    if (it != activeSounds_.end()) {
      it->second.isCompleted = true;
      if (it->second.completionPromise) {
        it->second.completionPromise->set_value();
      }
    }
  }

  // Clean up completed sounds (call periodically)
  void cleanupCompletedSounds() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = activeSounds_.begin(); it != activeSounds_.end();) {
      if (it->second.isCompleted) {
        // Clean up resources
        waveOutUnprepareHeader(it->second.hWaveOut, &it->second.waveHeader,
                               sizeof(WAVEHDR));
        waveOutClose(it->second.hWaveOut);
        it = activeSounds_.erase(it);
      } else {
        ++it;
      }
    }
  }

  // Clean up all sounds (for shutdown)
  void cleanupAllSounds() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto &pair : activeSounds_) {
      ActiveSound &sound = pair.second;
      if (!sound.isCompleted) {
        waveOutReset(sound.hWaveOut);
        if (sound.completionPromise) {
          sound.completionPromise->set_value();
        }
      }
      waveOutUnprepareHeader(sound.hWaveOut, &sound.waveHeader,
                             sizeof(WAVEHDR));
      waveOutClose(sound.hWaveOut);
    }
    activeSounds_.clear();
  }

private:
  std::mutex mutex_;
  std::map<uint32_t, ActiveSound> activeSounds_;
  uint32_t nextSoundId_;
};

// Global sound manager instance
SoundManager g_SoundManager;

// Callback function for Windows waveOut completion
void CALLBACK WaveOutProc(HWAVEOUT hwo, UINT uMsg, DWORD_PTR dwInstance,
                          DWORD_PTR dwParam1, DWORD_PTR dwParam2) {
  if (uMsg == WOM_DONE) {
    // Get the sound ID from the instance data
    uint32_t soundId = static_cast<uint32_t>(dwInstance);
    g_SoundManager.completeSound(soundId);
  }
}

class WindowsAudioPlayer : public AudioPlayer {
public:
  WindowsAudioPlayer() : volume_(1.0f), cleanupTimerId_(0) {
    // Set up a timer to periodically clean up completed sounds
    cleanupTimerId_ = SetTimer(NULL, 0, 1000, &CleanupTimerProc);
  }

  ~WindowsAudioPlayer() override {
    shutdown();
    if (cleanupTimerId_) {
      KillTimer(NULL, cleanupTimerId_);
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

  void playSound(const std::vector<uint8_t> &data, const std::string &format,
                 std::shared_ptr<std::promise<void>> completionPromise =
                     nullptr) override {
    if (format == "wav") {
      // For both synchronous and asynchronous playback, use waveOut* functions
      playWavSound(data, completionPromise);
    } else if (format == "mp3") {
      // For MP3 files, we'd need a more advanced library
      // This basic implementation doesn't support MP3 natively
      std::cerr << "MP3 playback requires additional libraries on Windows"
                << std::endl;
      if (completionPromise) {
        // Signal completion even though we couldn't play
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

  // Timer procedure to clean up completed sounds
  static void CALLBACK CleanupTimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent,
                                        DWORD dwTime) {
    g_SoundManager.cleanupCompletedSounds();
  }

  void playWavSound(const std::vector<uint8_t> &data,
                    std::shared_ptr<std::promise<void>> completionPromise) {

    if (!g_SoundManager.canAddMoreSounds()) {
      std::cerr << "Too many active sounds, skipping playback" << std::endl;
      if (completionPromise) {
        completionPromise->set_value();
      }
      return;
    }

    if (data.size() < 44) {
      std::cerr << "Invalid WAV file: too small" << std::endl;
      if (completionPromise) {
        completionPromise->set_value();
      }
      return;
    }

    WAVEFORMATEX wfx = {0};

    if (memcmp(data.data(), "RIFF", 4) != 0 ||
        memcmp(data.data() + 8, "WAVE", 4) != 0) {
      std::cerr << "Invalid WAV file: missing RIFF/WAVE header" << std::endl;
      if (completionPromise) {
        completionPromise->set_value();
      }
      return;
    }

    uint16_t audioFormat = *reinterpret_cast<const uint16_t *>(&data[20]);
    uint16_t numChannels = *reinterpret_cast<const uint16_t *>(&data[22]);
    uint32_t sampleRate = *reinterpret_cast<const uint32_t *>(&data[24]);
    uint16_t bitsPerSample = *reinterpret_cast<const uint16_t *>(&data[34]);

    size_t dataOffset = 0;
    for (size_t i = 12; i < data.size() - 8;) {
      if (memcmp(data.data() + i, "data", 4) == 0) {
        dataOffset = i + 8;
        break;
      }
      uint32_t chunkSize = *reinterpret_cast<const uint32_t *>(&data[i + 4]);
      i += 8 + chunkSize;
    }

    if (dataOffset == 0 || dataOffset >= data.size()) {
      std::cerr << "Invalid WAV file: data chunk not found" << std::endl;
      if (completionPromise) {
        completionPromise->set_value();
      }
      return;
    }

    wfx.wFormatTag = audioFormat;
    wfx.nChannels = numChannels;
    wfx.nSamplesPerSec = sampleRate;
    wfx.wBitsPerSample = bitsPerSample;
    wfx.nBlockAlign = numChannels * bitsPerSample / 8;
    wfx.nAvgBytesPerSec = sampleRate * wfx.nBlockAlign;

    ActiveSound sound;
    sound.soundData = data;
    sound.completionPromise = completionPromise;

    uint32_t soundId = g_SoundManager.addSound(std::move(sound));

    ActiveSound *activeSound = g_SoundManager.getSound(soundId);
    if (!activeSound) {
      std::cerr << "Failed to retrieve active sound" << std::endl;
      if (completionPromise) {
        completionPromise->set_value();
      }
      return;
    }

    MMRESULT result =
        waveOutOpen(&activeSound->hWaveOut, WAVE_MAPPER, &wfx,
                    (DWORD_PTR)WaveOutProc, soundId, CALLBACK_FUNCTION);

    if (result != MMSYSERR_NOERROR) {
      std::cerr << "Failed to open waveOut device, error code: " << result
                << std::endl;
      g_SoundManager.completeSound(soundId);
      if (completionPromise) {
        completionPromise->set_value();
      }
      return;
    }

    activeSound->waveHeader.lpData =
        (LPSTR)activeSound->soundData.data() + dataOffset;
    activeSound->waveHeader.dwBufferLength =
        activeSound->soundData.size() - dataOffset;
    activeSound->waveHeader.dwFlags = 0;

    result = waveOutPrepareHeader(activeSound->hWaveOut,
                                  &activeSound->waveHeader, sizeof(WAVEHDR));
    if (result != MMSYSERR_NOERROR) {
      std::cerr << "Failed to prepare waveOut header, error code: " << result
                << std::endl;
      waveOutClose(activeSound->hWaveOut);
      g_SoundManager.completeSound(soundId);
      return;
    }

    result = waveOutWrite(activeSound->hWaveOut, &activeSound->waveHeader,
                          sizeof(WAVEHDR));
    if (result != MMSYSERR_NOERROR) {
      std::cerr << "Failed to write wave data, error code: " << result
                << std::endl;
      waveOutUnprepareHeader(activeSound->hWaveOut, &activeSound->waveHeader,
                             sizeof(WAVEHDR));
      waveOutClose(activeSound->hWaveOut);
      g_SoundManager.completeSound(soundId);
      return;
    }
  }
};

// Factory function implementation for Windows
std::unique_ptr<AudioPlayer> createAudioPlayer() {
  return std::make_unique<WindowsAudioPlayer>();
}

#endif // _WIN32
