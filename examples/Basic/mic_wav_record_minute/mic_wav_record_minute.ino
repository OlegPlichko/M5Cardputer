/*
 * M5Cardputer Voice Notes Recorder
 * 
 * This sketch allows for recording, saving, and playback of voice notes
 * using the M5Stack Cardputer's built-in microphone and speaker.
 * 
 * Hardware: M5Stack Cardputer
 * Dependencies:
 * - M5GFX library
 * - M5Cardputer library
 * 
 * Controls:
 * - Button A: Start/Stop recording
 * - Keyboard: 
 *   - Up/Down arrows (;/.) to navigate files
 *   - Enter to play selected file
 *   - Delete to remove selected file
 */

 #include <M5Cardputer.h>
 #include <SPI.h>
 #include <SD.h>
 
 // SD Card pins definition
 #define SD_SPI_SCK_PIN  (40)
 #define SD_SPI_MISO_PIN (39)
 #define SD_SPI_MOSI_PIN (14)
 #define SD_SPI_CS_PIN   (12)
 
 // Recording parameters
 #define CHUNK_LENGTH 240
 #define CHUNKS_PER_WRITE 512
 #define TOTAL_CHUNKS 4000  // For ~60 seconds: (240 * 4000) / 16000 = 60 seconds
 
 // Constants for recording
 static constexpr const size_t record_length = CHUNK_LENGTH;
 static constexpr const size_t record_number = CHUNKS_PER_WRITE;
 static constexpr const size_t record_size = record_number * record_length;
 static constexpr const size_t record_samplerate = 16000;
 
 // UI variables
 static int16_t prev_y[record_length];
 static int16_t prev_h[record_length];
 
 // Recording variables
 static int16_t* rec_data;
 static uint32_t file_counter = 0;
 static uint8_t selectedFileIndex = 0;
 static std::vector<String> wavFiles;
 
 // File management variables
 static File recording_file;
 static bool is_recording = false;
 static uint32_t chunks_recorded = 0;
 
 // WAV file header definition
 struct WAVHeader {
     char riff[4]           = {'R', 'I', 'F', 'F'};
     uint32_t fileSize      = 0;
     char wave[4]           = {'W', 'A', 'V', 'E'};
     char fmt[4]            = {'f', 'm', 't', ' '};
     uint32_t fmtSize       = 16;
     uint16_t audioFormat   = 1;
     uint16_t numChannels   = 1;
     uint32_t sampleRate    = record_samplerate;
     uint32_t byteRate      = record_samplerate * sizeof(int16_t);
     uint16_t blockAlign    = sizeof(int16_t);
     uint16_t bitsPerSample = 16;
     char data[4]           = {'d', 'a', 't', 'a'};
     uint32_t dataSize      = 0;
 };
 
 // Function declarations
 bool startWAVFile();
 bool writeChunkToFile(int16_t* data, size_t dataSize);
 void finishRecording();
 void scanAndDisplayWAVFiles();
 void updateDisplay(std::vector<String> wavFiles, uint8_t selectedFileIndex);
 bool playSelectedWAVFile(const String& fileName);
 void playWAV();
 String formatDuration(uint32_t seconds);
 
 void setup() {
     auto cfg = M5.config();
 
     // Initialize M5Cardputer
     M5Cardputer.begin(cfg, true);  // Enable keyboard
     Serial.begin(115200);
     
     // Setup display
     M5Cardputer.Display.startWrite();
     M5Cardputer.Display.setRotation(1);
     M5Cardputer.Display.setTextDatum(top_center);
     M5Cardputer.Display.setTextColor(WHITE);
     M5Cardputer.Display.setFont(&fonts::FreeSansBoldOblique12pt7b);
     M5Cardputer.Display.fillScreen(BLACK);
     M5Cardputer.Display.drawString("Voice Notes Recorder", M5Cardputer.Display.width() / 2, 10);
     M5Cardputer.Display.drawString("Initializing...", M5Cardputer.Display.width() / 2, 50);
 
     // Initialize SD Card
     SPI.begin(SD_SPI_SCK_PIN, SD_SPI_MISO_PIN, SD_SPI_MOSI_PIN, SD_SPI_CS_PIN);
     
     if (!SD.begin(SD_SPI_CS_PIN, SPI, 25000000)) {
         Serial.println("Card failed or not present");
         M5Cardputer.Display.fillScreen(BLACK);
         M5Cardputer.Display.drawString("SD Card Error!", M5Cardputer.Display.width() / 2, 50);
         while (1) delay(100);
     }
     
     // Print SD card info
     uint8_t cardType = SD.cardType();
     if (cardType == CARD_NONE) {
         Serial.println("No SD card attached");
         M5Cardputer.Display.fillScreen(BLACK);
         M5Cardputer.Display.drawString("No SD Card!", M5Cardputer.Display.width() / 2, 50);
         while (1) delay(100);
     }
     
     Serial.print("SD Card Type: ");
     if (cardType == CARD_MMC) {
         Serial.println("MMC");
     } else if (cardType == CARD_SD) {
         Serial.println("SDSC");
     } else if (cardType == CARD_SDHC) {
         Serial.println("SDHC");
     } else {
         Serial.println("UNKNOWN");
     }
     
     uint64_t cardSize = SD.cardSize() / (1024 * 1024);
     Serial.printf("SD Card Size: %lluMB\n", cardSize);
 
     // Allocate memory for recording
     rec_data = (typeof(rec_data))heap_caps_malloc(record_size * sizeof(int16_t), MALLOC_CAP_8BIT);
     if (!rec_data) {
         Serial.println("Failed to allocate memory");
         M5Cardputer.Display.fillScreen(BLACK);
         M5Cardputer.Display.drawString("Memory Error!", M5Cardputer.Display.width() / 2, 50);
         while (1) delay(100);
     }
     
     memset(rec_data, 0, record_size * sizeof(int16_t));
     
     // Initialize audio components
     M5Cardputer.Speaker.setVolume(255);
     M5Cardputer.Speaker.end();
     M5Cardputer.Mic.begin();
 
     // Show welcome screen
     M5Cardputer.Display.fillScreen(BLACK);
     M5Cardputer.Display.drawString("Voice Notes Recorder", M5Cardputer.Display.width() / 2, 10);
     M5Cardputer.Display.setTextFont(1);
     M5Cardputer.Display.drawString("Press BtnA to start recording", M5Cardputer.Display.width() / 2, 40);
     M5Cardputer.Display.drawString("Use ; and . keys to navigate", M5Cardputer.Display.width() / 2, 60);
     M5Cardputer.Display.drawString("Enter to play, Delete to remove", M5Cardputer.Display.width() / 2, 80);
     delay(2000);
     
     // Scan for existing WAV files
     scanAndDisplayWAVFiles();
     updateDisplay(wavFiles, selectedFileIndex);
 }
 
 void loop() {
     M5Cardputer.update();
     
     // Handle Button A press to start/stop recording
     if (M5Cardputer.BtnA.wasClicked()) {
         M5Cardputer.Display.clear();
         if (!is_recording) {
             // Start new recording
             if (M5Cardputer.Mic.isEnabled() && startWAVFile()) {
                 is_recording = true;
                 Serial.println("Start recording");
                 chunks_recorded = 0;
                 
                 // Show recording UI
                 M5Cardputer.Display.fillScreen(BLACK);
                 M5Cardputer.Display.drawString("Recording...", M5Cardputer.Display.width() / 2, 3);
             }
         } else {
             // Stop current recording
             finishRecording();
             updateDisplay(wavFiles, selectedFileIndex);
         }
     }
 
     // Handle recording if active
     if (is_recording && M5Cardputer.Mic.isEnabled()) {
         // Show recording indicator
         M5Cardputer.Display.fillCircle(20, 15, 8, RED);
         
         // Calculate and display recording duration
         int seconds = (chunks_recorded * CHUNK_LENGTH) / record_samplerate;
         M5Cardputer.Display.setTextFont(1);
         M5Cardputer.Display.fillRect(130, 3, 100, 20, BLACK);
         M5Cardputer.Display.drawString(formatDuration(seconds), 180, 10);
         
         if (chunks_recorded < TOTAL_CHUNKS) {
             auto data = &rec_data[0];  // Reuse the same buffer
             if (M5Cardputer.Mic.record(data, record_length, record_samplerate)) {
                 static constexpr int shift = 6;
                 // Display waveform visualization
                 int32_t w = M5Cardputer.Display.width();
                 if (w > record_length - 1) {
                     w = record_length - 1;
                 }
                 for (int32_t x = 0; x < w; ++x) {
                     M5Cardputer.Display.writeFastVLine(x, prev_y[x], prev_h[x], TFT_BLACK);
                     int32_t y1 = (data[x] >> shift);
                     int32_t y2 = (data[x + 1] >> shift);
                     if (y1 > y2) {
                         int32_t tmp = y1;
                         y1 = y2;
                         y2 = tmp;
                     }
                     int32_t y = ((M5Cardputer.Display.height()) >> 1) + y1;
                     int32_t h = ((M5Cardputer.Display.height()) >> 1) + y2 + 1 - y;
                     prev_y[x] = y;
                     prev_h[x] = h;
                     M5Cardputer.Display.writeFastVLine(x, prev_y[x], prev_h[x], WHITE);
                 }
                 
                 if (writeChunkToFile(data, record_length)) {
                     chunks_recorded++;
                 }
             }
         } else {
             // Max recording time reached
             finishRecording();
             updateDisplay(wavFiles, selectedFileIndex);
         }
         M5Cardputer.Display.display();
     }
     
     // Handle keyboard input for file navigation
     if (M5Cardputer.Keyboard.isChange()) {
         if (M5Cardputer.Keyboard.isPressed()) {
             Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();
             char pressedKey = 0;
             
             // Get pressed character key
             for (auto i : status.word) {
                 pressedKey = i;
                 break;
             }
             
             // Handle navigation keys
             if (pressedKey == ';' || pressedKey == 'k') {  // Up navigation
                 if (!wavFiles.empty()) {
                     selectedFileIndex = (selectedFileIndex == 0) ? wavFiles.size() - 1 : selectedFileIndex - 1;
                     updateDisplay(wavFiles, selectedFileIndex);
                 }
             }
             if (pressedKey == '.' || pressedKey == 'j') {  // Down navigation
                 if (!wavFiles.empty()) {
                     selectedFileIndex = (selectedFileIndex + 1) % wavFiles.size();
                     updateDisplay(wavFiles, selectedFileIndex);
                 }
             }
 
             // Handle delete key
             if (status.del) {
                 if (!wavFiles.empty()) {
                     String filePath = "/" + wavFiles[selectedFileIndex];
                     if (SD.remove(filePath.c_str())) {
                         Serial.printf("Deleted file: %s\n", filePath.c_str());
                         wavFiles.erase(wavFiles.begin() + selectedFileIndex);
                         if (selectedFileIndex >= wavFiles.size() && !wavFiles.empty()) {
                             selectedFileIndex--;
                         }
                         updateDisplay(wavFiles, selectedFileIndex);
                     } else {
                         Serial.printf("Failed to delete file: %s\n", filePath.c_str());
                     }
                 }
             }
 
             // Handle enter key for playback
             if (status.enter) {
                 if (!wavFiles.empty()) {
                     playSelectedWAVFile(wavFiles[selectedFileIndex]);
                 }
             }
         }
     }
     
     // Regular file list refresh
     static unsigned long lastScanTime = 0;
     if (millis() - lastScanTime > 2000) {  // Scan every 2 seconds
         scanAndDisplayWAVFiles();
         lastScanTime = millis();
     }
 }
 
 // Format seconds as MM:SS
 String formatDuration(uint32_t seconds) {
     char buffer[10];
     sprintf(buffer, "%02d:%02d", seconds / 60, seconds % 60);
     return String(buffer);
 }
 
 // Start recording a new WAV file
 bool startWAVFile() {
     // Create directories if they don't exist
     if (!SD.exists("/voice_notes")) {
         SD.mkdir("/voice_notes");
     }
     
     // Generate filename with timestamp
     char filename[64];
     unsigned long timestamp = millis() / 1000;  // Use seconds since start as part of filename
     snprintf(filename, sizeof(filename), "/voice_notes/note_%lu_%d.wav", timestamp, file_counter++);
     
     recording_file = SD.open(filename, FILE_WRITE);
     if (!recording_file) {
         Serial.printf("Failed to open file for writing: %s\n", filename);
         return false;
     }
 
     // Write WAV header with pre-calculated size
     WAVHeader header;
     size_t totalDataSize = TOTAL_CHUNKS * CHUNK_LENGTH * sizeof(int16_t);
     header.fileSize = 36 + totalDataSize;
     header.dataSize = totalDataSize;
     recording_file.write((uint8_t*)&header, sizeof(WAVHeader));
     return true;
 }
 
 // Write a chunk of audio data to the WAV file
 bool writeChunkToFile(int16_t* data, size_t dataSize) {
     if (!recording_file) return false;
     return recording_file.write((uint8_t*)data, dataSize * sizeof(int16_t)) == dataSize * sizeof(int16_t);
 }
 
 // Finish recording and update the WAV file header
 void finishRecording() {
     if (recording_file) {
         // Update WAV header with actual recorded size
         size_t actualDataSize = chunks_recorded * CHUNK_LENGTH * sizeof(int16_t);
         WAVHeader header;
         header.fileSize = 36 + actualDataSize;
         header.dataSize = actualDataSize;
         
         // Seek to beginning of file and write the updated header
         recording_file.seek(0);
         recording_file.write((uint8_t*)&header, sizeof(WAVHeader));
         recording_file.close();
     }
     is_recording = false;
     chunks_recorded = 0;
     Serial.println("Recording finished");
 }
 
 // Scan SD card for WAV files
 void scanAndDisplayWAVFiles() {
     static std::vector<String> previousWavFiles;
 
     // Scan for WAV files in the voice_notes directory
     File dir = SD.open("/voice_notes");
     if (!dir) {
         // If directory doesn't exist, create it
         SD.mkdir("/voice_notes");
         dir = SD.open("/voice_notes");
         if (!dir) {
             Serial.println("Failed to open directory");
             return;
         }
     }
     
     wavFiles.clear();
     while (File entry = dir.openNextFile()) {
         if (!entry.isDirectory() && String(entry.name()).endsWith(".wav")) {
             wavFiles.push_back(String(entry.name()));
         }
         entry.close();
     }
     dir.close();
 
     // Also check root directory for backward compatibility
     dir = SD.open("/");
     if (dir) {
         while (File entry = dir.openNextFile()) {
             if (!entry.isDirectory() && String(entry.name()).endsWith(".wav")) {
                 wavFiles.push_back(String(entry.name()));
             }
             entry.close();
         }
         dir.close();
     }
 
     // If file list has changed, update the display
     if (wavFiles != previousWavFiles) {
         previousWavFiles = wavFiles;
         updateDisplay(wavFiles, selectedFileIndex);
     }
 }
 
 // Update the display with the file list
 void updateDisplay(std::vector<String> wavFiles, uint8_t selectedFileIndex) {
     M5Cardputer.Display.fillScreen(BLACK);
     M5Cardputer.Display.setFont(&fonts::FreeSansBoldOblique12pt7b);
     M5Cardputer.Display.drawString("Voice Notes", M5Cardputer.Display.width() / 2, 3);
     
     // Display instruction if no files
     if (wavFiles.empty()) {
         M5Cardputer.Display.setTextFont(1);
         M5Cardputer.Display.setTextColor(YELLOW);
         M5Cardputer.Display.drawString("No voice notes found", M5Cardputer.Display.width() / 2, 40);
         M5Cardputer.Display.drawString("Press BtnA to start recording", M5Cardputer.Display.width() / 2, 60);
         return;
     }
 
     // Calculate which files to display (scrolling view)
     const uint8_t maxVisibleFiles = 5;
     uint8_t startIndex = 0;
     
     // Adjust startIndex for scrolling
     if (selectedFileIndex >= maxVisibleFiles) {
         startIndex = selectedFileIndex - maxVisibleFiles + 1;
     }
     
     // Display file list
     M5Cardputer.Display.setTextFont(1);
     for (size_t i = startIndex; i < startIndex + maxVisibleFiles && i < wavFiles.size(); i++) {
         uint16_t color = (i == selectedFileIndex) ? YELLOW : WHITE;
         M5Cardputer.Display.setTextColor(color);
         
         // Format filename for display (remove path and extension)
         String displayName = wavFiles[i];
         int slashPos = displayName.lastIndexOf('/');
         if (slashPos >= 0) {
             displayName = displayName.substring(slashPos + 1);
         }
         
         int dotPos = displayName.lastIndexOf('.');
         if (dotPos >= 0) {
             displayName = displayName.substring(0, dotPos);
         }
         
         M5Cardputer.Display.drawString(displayName, M5Cardputer.Display.width() / 2, 32 + (i - startIndex) * 20);
     }
     
     // Display navigation instructions at bottom
     M5Cardputer.Display.setTextColor(0x7BEF);  // Light blue
     M5Cardputer.Display.drawString("BtnA: Record | ;/.: Navigate | Enter: Play", M5Cardputer.Display.width() / 2, M5Cardputer.Display.height() - 15);
 }
 
 // Play the selected WAV file
 bool playSelectedWAVFile(const String& fileName) {
     String filePath;
     
     // Check if fileName has a path
     if (fileName.startsWith("/")) {
         filePath = fileName;
     } else {
         // Check in voice_notes directory first
         if (SD.exists("/voice_notes/" + fileName)) {
             filePath = "/voice_notes/" + fileName;
         } else {
             // Then check in root directory
             filePath = "/" + fileName;
         }
     }
     
     Serial.printf("Playing WAV file: %s\n", filePath.c_str());
 
     File file = SD.open(filePath.c_str());
     if (!file) {
         Serial.printf("Failed to open WAV file: %s\n", filePath.c_str());
         return false;
     }
 
     // Skip the header of the WAV file (44 bytes)
     file.seek(44);
 
     // Read file in chunks to handle larger files
     size_t totalBytesRead = 0;
     size_t maxBytes = record_size * sizeof(int16_t);
     size_t bytesRead = file.read((uint8_t*)rec_data, maxBytes);
     file.close();
 
     if (bytesRead == 0) {
         Serial.println("Failed to read WAV file data");
         return false;
     }
 
     playWAV();
     Serial.println("Playback finished");
     return true;
 }
 
 // Play the loaded WAV data
 void playWAV() {
     M5Cardputer.Display.fillScreen(BLACK);
     M5Cardputer.Mic.end();
     M5Cardputer.Speaker.begin();
     
     // Show playback UI
     M5Cardputer.Display.fillTriangle(20, 15-8, 20, 15+8, 36, 15, 0x1c9f);
     M5Cardputer.Display.drawString("Playing...", M5Cardputer.Display.width() / 2, 3);
     
     static constexpr int shift = 6;
     for (uint16_t i = 0; i < record_number; i++) {
         auto data = &rec_data[i * record_length];
         
         // Play the audio chunk
         M5Cardputer.Speaker.playRaw(data, record_length, record_samplerate);
         
         // Wait for playback to complete
         do {
             delay(1);
             M5Cardputer.update();
             
             // Check for key press to stop playback
             if (M5Cardputer.BtnA.wasClicked() || 
                 (M5Cardputer.Keyboard.isPressed() && 
                  M5Cardputer.Keyboard.keysState().enter)) {
                 M5Cardputer.Speaker.stop();
                 break;
             }
         } while (M5Cardputer.Speaker.isPlaying());
         
         // Check again if we should stop playback
         if (M5Cardputer.BtnA.isPressed() || 
             (M5Cardputer.Keyboard.isPressed() && 
              M5Cardputer.Keyboard.keysState().enter)) {
             break;
         }
         
         // Display waveform visualization
         if (i >= 2) {
             data = &rec_data[(i - 2) * record_length];
             int32_t w = M5Cardputer.Display.width();
             if (w > record_length - 1) {
                 w = record_length - 1;
             }
             for (int32_t x = 0; x < w; ++x) {
                 M5Cardputer.Display.writeFastVLine(x, prev_y[x], prev_h[x], TFT_BLACK);
                 int32_t y1 = (data[x] >> shift);
                 int32_t y2 = (data[x + 1] >> shift);
                 if (y1 > y2) {
                     int32_t tmp = y1;
                     y1 = y2;
                     y2 = tmp;
                 }
                 int32_t y = ((M5Cardputer.Display.height()) >> 1) + y1;
                 int32_t h = ((M5Cardputer.Display.height()) >> 1) + y2 + 1 - y;
                 prev_y[x] = y;
                 prev_h[x] = h;
                 M5Cardputer.Display.writeFastVLine(x, prev_y[x], prev_h[x], WHITE);
             }
         }
         M5Cardputer.Display.fillTriangle(20, 15-8, 20, 15+8, 36, 15, 0x1c9f);
         M5Cardputer.Display.drawString("Playing...", M5Cardputer.Display.width() / 2, 3);
     }
 
     // Restore microphone for recording
     M5Cardputer.Speaker.end();
     M5Cardputer.Mic.begin();
     M5Cardputer.Display.clear();
     updateDisplay(wavFiles, selectedFileIndex);
 }