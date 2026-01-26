#include "pyramid.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <sys/stat.h>
#include <vector>
#include <zip.h>

#ifdef _WIN32
#include <direct.h>
#endif

// ============================================================================
// ZIP FILE EXTRACTION
// ============================================================================

bool PyramidGame::extractFileFromZip(const std::string &zipFilePath,
                                      const std::string &fileName,
                                      std::vector<uint8_t> &fileData) {
  int errCode = 0;
  zip_t *archive = zip_open(zipFilePath.c_str(), 0, &errCode);

  if (!archive) {
    zip_error_t zipError;
    zip_error_init_with_code(&zipError, errCode);
    std::cerr << "Failed to open ZIP archive: " << zip_error_strerror(&zipError)
              << std::endl;
    zip_error_fini(&zipError);
    return false;
  }

  // Find file in archive
  zip_int64_t index = zip_name_locate(archive, fileName.c_str(), 0);
  if (index < 0) {
    std::cerr << "File not found in ZIP: " << fileName << std::endl;
    zip_close(archive);
    return false;
  }

  // Open file
  zip_file_t *file = zip_fopen_index(archive, index, 0);
  if (!file) {
    std::cerr << "Failed to open file in ZIP: " << zip_strerror(archive)
              << std::endl;
    zip_close(archive);
    return false;
  }

  // Get file size
  zip_stat_t stat;
  if (zip_stat_index(archive, index, 0, &stat) < 0) {
    std::cerr << "Failed to get file stats: " << zip_strerror(archive)
              << std::endl;
    zip_fclose(file);
    zip_close(archive);
    return false;
  }

  // Read file
  fileData.resize(stat.size);

  zip_int64_t bytesRead = zip_fread(file, fileData.data(), stat.size);
  if (bytesRead < 0 || static_cast<zip_uint64_t>(bytesRead) != stat.size) {
    std::cerr << "Failed to read file: " << zip_file_strerror(file)
              << std::endl;
    zip_fclose(file);
    zip_close(archive);
    return false;
  }

  // Cleanup
  zip_fclose(file);
  zip_close(archive);

  std::cout << "Extracted file: " << fileData.size() << " bytes" << std::endl;
  return true;
}

// ============================================================================
// AUDIO INITIALIZATION
// ============================================================================

bool PyramidGame::initializeAudio() {
  // Don't initialize if sound is disabled
  if (!sound_enabled_) {
    return false;
  }

  if (sounds_zip_path_.empty()) {
    sounds_zip_path_ = "sounds.zip";
  }

  std::cout << "Audio system initialized (stub)" << std::endl;
  return true;
}

bool PyramidGame::loadSoundFromZip(GameSoundEvent event,
                                    const std::string &soundFileName) {
  // Extract sound file from ZIP
  std::vector<uint8_t> soundData;
  if (!extractFileFromZip(sounds_zip_path_, soundFileName, soundData)) {
    std::cerr << "Failed to extract: " << soundFileName << std::endl;
    return false;
  }

  // Get file extension for format
  std::string format;
  size_t dotPos = soundFileName.find_last_of('.');
  if (dotPos != std::string::npos) {
    format = soundFileName.substr(dotPos + 1);
    // Convert to lowercase
    std::transform(format.begin(), format.end(), format.begin(),
                   [](unsigned char c) { return std::tolower(c); });
  } else {
    std::cerr << "Sound file has no extension: " << soundFileName << std::endl;
    return false;
  }

  // Validate format
  if (format != "wav" && format != "mp3") {
    std::cerr << "Unsupported audio format: " << format << std::endl;
    return false;
  }

  std::cout << "Loaded sound: " << soundFileName << " (" << soundData.size()
            << " bytes)" << std::endl;
  return true;
}

void PyramidGame::playSound(GameSoundEvent event) {
  if (!sound_enabled_) {
    return;
  }

  // Convert event to string for debugging
  const char *event_name = "Unknown";
  switch (event) {
    case GameSoundEvent::CardFlip:
      event_name = "CardFlip";
      break;
    case GameSoundEvent::CardPlace:
      event_name = "CardPlace";
      break;
    case GameSoundEvent::CardRemove:
      event_name = "CardRemove";
      break;
    case GameSoundEvent::WinGame:
      event_name = "WinGame";
      break;
    case GameSoundEvent::DealCard:
      event_name = "DealCard";
      break;
    case GameSoundEvent::Firework:
      event_name = "Firework";
      break;
    case GameSoundEvent::NoMatch:
      event_name = "NoMatch";
      break;
  }

  // Stub implementation - would play sound via audio system
  // std::cout << "Play sound: " << event_name << std::endl;
}

void PyramidGame::cleanupAudio() {
  if (sound_enabled_) {
    sound_enabled_ = false;
    std::cout << "Audio system shutdown" << std::endl;
  }
}

// ============================================================================
// AUDIO PATH MANAGEMENT
// ============================================================================

bool PyramidGame::setSoundsZipPath(const std::string &path) {
  std::string original_path = sounds_zip_path_;
  sounds_zip_path_ = path;

  // If sound is enabled, try to reload
  if (sound_enabled_) {
    cleanupAudio();

    if (!initializeAudio()) {
      // Restore original path on failure
      sounds_zip_path_ = original_path;
      initializeAudio();
      return false;
    }
  }

  saveSettings();
  return true;
}

void PyramidGame::checkAndInitializeSound() {
  if (sound_enabled_) {
    // Attempt to load default sounds
    bool sounds_loaded = true;

    // Try to load common sounds
    sounds_loaded &=
        loadSoundFromZip(GameSoundEvent::CardFlip, "flip.wav");
    sounds_loaded &=
        loadSoundFromZip(GameSoundEvent::CardPlace, "place.wav");
    sounds_loaded &=
        loadSoundFromZip(GameSoundEvent::CardRemove, "remove.wav");
    sounds_loaded &=
        loadSoundFromZip(GameSoundEvent::DealCard, "deal.wav");
    sounds_loaded &=
        loadSoundFromZip(GameSoundEvent::WinGame, "win.wav");
    sounds_loaded &=
        loadSoundFromZip(GameSoundEvent::Firework, "firework.wav");

    if (!sounds_loaded) {
      std::cerr << "Warning: Not all sounds loaded successfully" << std::endl;
      // Don't disable sound entirely, just warn
    }

    std::cout << "Sound system ready" << std::endl;
  }
}
