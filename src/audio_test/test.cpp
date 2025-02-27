#include "audiomanager.h"
#include <algorithm> // Added for std::transform
#include <cctype>    // Added for std::tolower
#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <zip.h>

// Function to extract a file from a ZIP archive into memory
bool extractFileFromZip(const std::string &zipFilePath,
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

  // Find the file in the archive
  zip_int64_t index = zip_name_locate(archive, fileName.c_str(), 0);
  if (index < 0) {
    std::cerr << "File not found in ZIP archive: " << fileName << std::endl;
    zip_close(archive);
    return false;
  }

  // Open the file in the archive
  zip_file_t *file = zip_fopen_index(archive, index, 0);
  if (!file) {
    std::cerr << "Failed to open file in ZIP archive: " << zip_strerror(archive)
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

  // Resize the buffer to fit the file
  fileData.resize(stat.size);

  // Read the file content
  zip_int64_t bytesRead = zip_fread(file, fileData.data(), stat.size);
  if (bytesRead < 0 || static_cast<zip_uint64_t>(bytesRead) != stat.size) {
    std::cerr << "Failed to read file: " << zip_file_strerror(file)
              << std::endl;
    zip_fclose(file);
    zip_close(archive);
    return false;
  }

  // Close everything
  zip_fclose(file);
  zip_close(archive);

  std::cout << "Successfully extracted file (" << fileData.size() << " bytes)"
            << std::endl;
  return true;
}

// Custom Audio Manager function to load sound from memory
bool loadSoundFromMemory(SoundEvent event, const std::vector<uint8_t> &data,
                         const std::string &format) {
  // Get file extension to determine format
  if (format != "wav" && format != "mp3") {
    std::cerr << "Unsupported audio format: " << format << std::endl;
    return false;
  }

  // Store the sound data
  return AudioManager::getInstance().loadSoundFromMemory(event, data, format);
}

int main(int argc, char *argv[]) {
  // Check command line arguments
  if (argc != 3) {
    std::cout << "Usage: " << argv[0] << " <zip_file> <wav_file_in_zip>"
              << std::endl;
    std::cout << "Example: " << argv[0] << " sounds.zip music/background.wav"
              << std::endl;
    return 1;
  }

  std::string zipFile = argv[1];
  std::string wavFileInZip = argv[2];

  // Initialize the audio system
  std::cout << "Initializing audio system..." << std::endl;
  if (!AudioManager::getInstance().initialize()) {
    std::cerr << "Failed to initialize audio system. Exiting." << std::endl;
    return 1;
  }
  std::cout << "Audio system initialized successfully." << std::endl;

  // Extract the WAV file from the ZIP archive
  std::cout << "Extracting " << wavFileInZip << " from " << zipFile << "..."
            << std::endl;
  std::vector<uint8_t> wavData;
  if (!extractFileFromZip(zipFile, wavFileInZip, wavData)) {
    std::cerr << "Failed to extract WAV file from ZIP archive. Exiting."
              << std::endl;
    return 1;
  }

  // Get file extension to determine format
  std::string format;
  size_t dotPos = wavFileInZip.find_last_of('.');
  if (dotPos != std::string::npos) {
    format = wavFileInZip.substr(dotPos + 1);
    // Convert to lowercase
    std::transform(format.begin(), format.end(), format.begin(),
                   [](unsigned char c) { return std::tolower(c); });
  }

  // Load the WAV data into the audio manager
  std::cout << "Loading sound data..." << std::endl;
  if (!AudioManager::getInstance().loadSoundFromMemory(SoundEvent::CardPlace,
                                                       wavData, format)) {
    std::cerr << "Failed to load sound data. Exiting." << std::endl;
    return 1;
  }

  std::cout << "Playing sound and waiting for completion..." << std::endl;
  // This will play the sound and block until it's complete
  AudioManager::getInstance().playSoundAndWait(SoundEvent::CardPlace);

  // No need to sleep - playSoundAndWait blocks until completion
  std::cout << "Playback complete." << std::endl;

  // Clean up
  std::cout << "Shutting down audio system..." << std::endl;
  AudioManager::getInstance().shutdown();

  // WAV data is automatically cleaned up when the vector goes out of scope
  std::cout << "Memory cleanup complete." << std::endl;

  std::cout << "Done." << std::endl;
  return 0;
}
