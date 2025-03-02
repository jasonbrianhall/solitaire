#include "audiomanager.h"
#include <algorithm>
#include <fstream>
#include <iostream>

AudioPlayer::AudioPlayer() {
  // Empty constructor
}

AudioPlayer::~AudioPlayer() {
  // Empty destructor
}

// Factory function implementation that creates the appropriate
// platform-specific player
std::unique_ptr<AudioPlayer> createAudioPlayer();

// AudioManager singleton implementation
AudioManager &AudioManager::getInstance() {
  static AudioManager instance;
  return instance;
}

AudioManager::AudioManager()
    : volume_(1.0f), muted_(false), initialized_(false) {
  player_ = createAudioPlayer();
}

AudioManager::~AudioManager() { shutdown(); }

bool AudioManager::initialize() {
  std::lock_guard<std::mutex> lock(mutex_);

  if (initialized_) {
    return true;
  }

  if (player_ && player_->initialize()) {
    initialized_ = true;
    return true;
  }

#ifdef DEBUG
  std::cerr << "Failed to initialize audio system" << std::endl;
#endif

  return false;
}

void AudioManager::shutdown() {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!initialized_) {
    return;
  }

  // Clear all loaded sounds
  sounds_.clear();

  if (player_) {
    player_->shutdown();
  }

  initialized_ = false;
}

bool AudioManager::loadSound(SoundEvent event, const std::string &filePath) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!initialized_) {
    return false;
  }

  // Get file extension to determine format
  std::string format = getFileExtension(filePath);
  std::transform(format.begin(), format.end(), format.begin(), ::tolower);

  // Check if format is supported
  if (format != "wav" && format != "mp3") {
    std::cerr << "Unsupported audio format: " << format << std::endl;
    return false;
  }

  // Read file content
  std::ifstream file(filePath, std::ios::binary | std::ios::ate);
  if (!file) {
#ifdef DEBUG
    std::cerr << "Failed to open audio file: " << filePath << std::endl;
#endif
    return false;
  }

  // Get file size
  std::streamsize size = file.tellg();
  file.seekg(0, std::ios::beg);

  // Read file data
  SoundData soundData;
  soundData.format = format;
  soundData.data.resize(size);

  if (!file.read(reinterpret_cast<char *>(soundData.data.data()), size)) {
#ifdef DEBUG
    std::cerr << "Failed to read audio file: " << filePath << std::endl;
#endif
    return false;
  }

  // Store the sound data
  sounds_[event] = std::move(soundData);
  return true;
}

bool AudioManager::loadSoundFromMemory(SoundEvent event,
                                       const std::vector<uint8_t> &data,
                                       const std::string &format) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!initialized_) {
    return false;
  }

  // Check if format is supported
  std::string formatLower = format;
  std::transform(formatLower.begin(), formatLower.end(), formatLower.begin(),
                 ::tolower);

  if (formatLower != "wav" && formatLower != "mp3") {
#ifdef DEBUG
    std::cerr << "Unsupported audio format: " << format << std::endl;
#endif
    return false;
  }

  // Create sound data
  SoundData soundData;
  soundData.format = formatLower;
  soundData.data = data; // Copy the data

  // Store the sound data
  sounds_[event] = std::move(soundData);
  return true;
}

void AudioManager::playSound(SoundEvent event) {
  if (muted_ || !initialized_) {
    return;
  }

  SoundData soundData;

  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sounds_.find(event);
    if (it == sounds_.end()) {
      return;
    }
    soundData = it->second;
  }

  if (player_) {
    player_->playSound(soundData.data, soundData.format);
  }
}

void AudioManager::playSoundAndWait(SoundEvent event) {
  if (muted_ || !initialized_) {
    return;
  }

  SoundData soundData;

  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sounds_.find(event);
    if (it == sounds_.end()) {
      return;
    }
    soundData = it->second;
  }

  if (player_) {
    // Create a promise/future pair to synchronize with playback completion
    auto completionPromise = std::make_shared<std::promise<void>>();
    std::future<void> completionFuture = completionPromise->get_future();

    // Play the sound with the completion promise
    player_->playSound(soundData.data, soundData.format, completionPromise);

    // Wait for the future to be fulfilled (when playback completes)
    completionFuture.wait();
  }
}

void AudioManager::setVolume(float volume) {
  std::lock_guard<std::mutex> lock(mutex_);
  volume_ = std::max(0.0f, std::min(1.0f, volume));

  if (player_) {
    player_->setVolume(volume_);
  }
}

void AudioManager::setMuted(bool muted) {
  std::lock_guard<std::mutex> lock(mutex_);
  muted_ = muted;
}

bool AudioManager::isMuted() const { return muted_; }

bool AudioManager::isAvailable() const { return initialized_; }

std::string AudioManager::getFileExtension(const std::string &filePath) {
  size_t dotPos = filePath.find_last_of('.');
  if (dotPos != std::string::npos) {
    return filePath.substr(dotPos + 1);
  }
  return "";
}
