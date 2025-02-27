#include "solitaire.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#include <zip.h>
#include <string>
#include <vector>
#include <memory>
#include "audiomanager.h"
#include <cctype>    // Added for std::tolower
#ifdef _WIN32
#include <direct.h>
#endif

// Function to extract a file from a ZIP archive into memory
bool SolitaireGame::extractFileFromZip(const std::string& zipFilePath, const std::string& fileName, std::vector<uint8_t>& fileData) {
    int errCode = 0;
    zip_t* archive = zip_open(zipFilePath.c_str(), 0, &errCode);
    
    if (!archive) {
        zip_error_t zipError;
        zip_error_init_with_code(&zipError, errCode);
        std::cerr << "Failed to open ZIP archive: " << zip_error_strerror(&zipError) << std::endl;
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
    zip_file_t* file = zip_fopen_index(archive, index, 0);
    if (!file) {
        std::cerr << "Failed to open file in ZIP archive: " << zip_strerror(archive) << std::endl;
        zip_close(archive);
        return false;
    }
    
    // Get file size
    zip_stat_t stat;
    if (zip_stat_index(archive, index, 0, &stat) < 0) {
        std::cerr << "Failed to get file stats: " << zip_strerror(archive) << std::endl;
        zip_fclose(file);
        zip_close(archive);
        return false;
    }
    
    // Resize the buffer to fit the file
    fileData.resize(stat.size);
    
    // Read the file content
    zip_int64_t bytesRead = zip_fread(file, fileData.data(), stat.size);
    if (bytesRead < 0 || static_cast<zip_uint64_t>(bytesRead) != stat.size) {
        std::cerr << "Failed to read file: " << zip_file_strerror(file) << std::endl;
        zip_fclose(file);
        zip_close(archive);
        return false;
    }
    
    // Close everything
    zip_fclose(file);
    zip_close(archive);
    
    std::cout << "Successfully extracted file (" << fileData.size() << " bytes)" << std::endl;
    return true;
}

// Custom Audio Manager function to load sound from memory
bool loadSoundFromMemory(SoundEvent event, const std::vector<uint8_t>& data, const std::string& format) {
    // Get file extension to determine format
    if (format != "wav" && format != "mp3") {
        std::cerr << "Unsupported audio format: " << format << std::endl;
        return false;
    }
    
    // Store the sound data
    return AudioManager::getInstance().loadSoundFromMemory(event, data, format);
}


bool SolitaireGame::initializeAudio() {
    // Don't do anything if sound is disabled
    if (!sound_enabled_) {
        return false;
    }
    
    if (sounds_zip_path_.empty()) {
        sounds_zip_path_ = "sounds.zip"; // Default location, can be changed via settings
    }
    
    // Try to initialize the audio system
    if (AudioManager::getInstance().initialize()) {
        // Attempt to load default sounds
        if (loadSoundFromZip(GameSoundEvent::CardFlip, "sounds/flip.wav") &&
            loadSoundFromZip(GameSoundEvent::CardPlace, "sounds/place.wav") &&
            loadSoundFromZip(GameSoundEvent::CardDrag, "sounds/drag.wav") &&
            loadSoundFromZip(GameSoundEvent::StockRefill, "sounds/refill.wav") &&
            loadSoundFromZip(GameSoundEvent::WinGame, "sounds/win.wav") &&
            loadSoundFromZip(GameSoundEvent::DealCards, "sounds/deal.wav")) {
            
            std::cout << "Sound system initialized successfully." << std::endl;
            return true;
        } else {
            std::cerr << "Failed to load all sound effects. Sound will be disabled." << std::endl;
            AudioManager::getInstance().shutdown();
            sound_enabled_ = false;
            
            // Update menu checkbox if window exists
            if (window_) {
                GList* menu_items = gtk_container_get_children(GTK_CONTAINER(vbox_));
                if (menu_items) {
                    GtkWidget* menubar = GTK_WIDGET(menu_items->data);
                    GList* menus = gtk_container_get_children(GTK_CONTAINER(menubar));
                    if (menus) {
                        // First menu should be the Game menu
                        GtkWidget* game_menu_item = GTK_WIDGET(menus->data);
                        GtkWidget* game_menu = gtk_menu_item_get_submenu(GTK_MENU_ITEM(game_menu_item));
                        if (game_menu) {
                            GList* game_menu_items = gtk_container_get_children(GTK_CONTAINER(game_menu));
                            // Find the sound checkbox (should be near the end)
                            for (GList* item = game_menu_items; item != NULL; item = item->next) {
                                if (GTK_IS_CHECK_MENU_ITEM(item->data)) {
                                    GtkWidget* check_item = GTK_WIDGET(item->data);
                                    const gchar* label = gtk_menu_item_get_label(GTK_MENU_ITEM(check_item));
                                    if (label && strstr(label, "Sound") != NULL) {
                                        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(check_item), FALSE);
                                        break;
                                    }
                                }
                            }
                            g_list_free(game_menu_items);
                        }
                        g_list_free(menus);
                    }
                    g_list_free(menu_items);
                }
            }
            
            return false;
        }
    } else {
        std::cerr << "Failed to initialize audio system. Sound will be disabled." << std::endl;
        sound_enabled_ = false;
        return false;
    }
}

bool SolitaireGame::loadSoundFromZip(GameSoundEvent event, const std::string& soundFileName) {
    // Extract the sound file from the ZIP archive
    std::vector<uint8_t> soundData;
    if (!extractFileFromZip(sounds_zip_path_, soundFileName, soundData)) {
        std::cerr << "Failed to extract sound file from ZIP archive: " << soundFileName << std::endl;
        return false;
    }
    
    // Get file extension to determine format
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
    
    // Map GameSoundEvent to AudioManager's SoundEvent
    SoundEvent audioEvent;
    switch (event) {
        case GameSoundEvent::CardFlip:
            audioEvent = SoundEvent::CardFlip;
            break;
        case GameSoundEvent::CardPlace:
            audioEvent = SoundEvent::CardPlace;
            break;
        case GameSoundEvent::CardDrag:
            audioEvent = SoundEvent::CardDrag;
            break;
        case GameSoundEvent::StockRefill:
            audioEvent = SoundEvent::StockRefill;
            break;
        case GameSoundEvent::WinGame:
            // Reuse an existing sound event since WinGame is not available
            audioEvent = SoundEvent::CardPlace; // Using CardPlace as a substitute
            break;
        case GameSoundEvent::DealCards:
            // Reuse an existing sound event since DealCards is not available
            audioEvent = SoundEvent::CardDrag; // Using CardDrag as a substitute
            break;
        default:
            std::cerr << "Unknown sound event" << std::endl;
            return false;
    }
    
    // Load the sound data into the audio manager
    return AudioManager::getInstance().loadSoundFromMemory(audioEvent, soundData, format);
}

void SolitaireGame::playSound(GameSoundEvent event) {
    if (!sound_enabled_) {
        return;
    }
    
    // Map GameSoundEvent to AudioManager's SoundEvent
    SoundEvent audioEvent;
    switch (event) {
        case GameSoundEvent::CardFlip:
            audioEvent = SoundEvent::CardFlip;
            break;
        case GameSoundEvent::CardPlace:
            audioEvent = SoundEvent::CardPlace;
            break;
        case GameSoundEvent::CardDrag:
            audioEvent = SoundEvent::CardDrag;
            break;
        case GameSoundEvent::StockRefill:
            audioEvent = SoundEvent::StockRefill;
            break;
        case GameSoundEvent::WinGame:
            // Reuse an existing sound event since WinGame is not available
            audioEvent = SoundEvent::CardPlace; // Using CardPlace as a substitute
            break;
        case GameSoundEvent::DealCards:
            // Reuse an existing sound event since DealCards is not available
            audioEvent = SoundEvent::CardDrag; // Using CardDrag as a substitute
            break;
        default:
            return;
    }
    
    // Play the sound asynchronously
    AudioManager::getInstance().playSound(audioEvent);
}

void SolitaireGame::cleanupAudio() {
    if (sound_enabled_) {
        AudioManager::getInstance().shutdown();
        sound_enabled_ = false;
    }
}

bool SolitaireGame::setSoundsZipPath(const std::string& path) {
    // Save original path in case of failure
    std::string original_path = sounds_zip_path_;
    
    // Update path
    sounds_zip_path_ = path;
    
    // If sound is enabled, reload all sounds
    if (sound_enabled_) {
        // First clean up existing audio
        cleanupAudio();
        
        // Try to reinitialize with new path
        if (!initializeAudio()) {
            // If failed, restore original path and reinitialize with that
            sounds_zip_path_ = original_path;
            initializeAudio();
            return false;
        }
    }
    
    // Save the new path to settings
    saveSettings();
    return true;
}
