#include "M5Cardputer.h"
#include "FS.h"
#include "SD.h"

// Constants for WAV recording
#define SAMPLE_RATE 16000      // 16kHz sample rate
#define BITS_PER_SAMPLE 16     // 16-bit audio
#define CHANNELS 1             // Mono
#define BUFFER_SIZE 4000       // 0.25 seconds of audio at 16kHz (reduced buffer for better responsiveness)
#define MAX_RECORDING_TIME 60  // Maximum recording time in seconds
#define SCREEN_WIDTH 135       // Display width for visualization
#define SCREEN_HEIGHT 240      // Display height

// Key definitions
#define KEY_UP              ';'
#define KEY_DOWN            '.'
#define KEY_PLAY            KEY_ENTER
#define KEY_DELETE          KEY_BACKSPACE

// WAV file header structure (44 bytes)
typedef struct WAVHeader {
    // RIFF header
    char riff[4] = {'R', 'I', 'F', 'F'};              // "RIFF"
    uint32_t fileSize = 0;                            // File size - 8
    char wave[4] = {'W', 'A', 'V', 'E'};              // "WAVE"
    
    // fmt subchunk
    char fmt[4] = {'f', 'm', 't', ' '};               // "fmt "
    uint32_t fmtSize = 16;                            // Size of fmt chunk (16 bytes)
    uint16_t audioFormat = 1;                         // PCM = 1
    uint16_t numChannels = CHANNELS;                  // Mono = 1, Stereo = 2
    uint32_t sampleRate = SAMPLE_RATE;                // Sample rate
    uint32_t byteRate = SAMPLE_RATE * CHANNELS * (BITS_PER_SAMPLE / 8); // Bytes per second
    uint16_t blockAlign = CHANNELS * (BITS_PER_SAMPLE / 8); // Bytes per sample * channels
    uint16_t bitsPerSample = BITS_PER_SAMPLE;         // Bits per sample
    
    // data subchunk
    char data[4] = {'d', 'a', 't', 'a'};              // "data"
    uint32_t dataSize = 0;                            // Size of data chunk
} WAVHeader;

// Global variables
int16_t audioBuffer[BUFFER_SIZE];
WAVHeader wavHeader;
File wavFile;
String currentFileName = "";
bool isRecording = false;
bool isPlaying = false;
int recordingDuration = 0;
unsigned long recordingStartTime = 0;
int fileCounter = 1;
int16_t waveformBuffer[SCREEN_WIDTH]; // For visualization
int selectedFileIndex = 0;
int totalFiles = 0;
String fileList[100]; // Assuming max 100 files

// Function prototypes
void startRecording();
void stopRecording();
void updateWaveform(int16_t sample);
void drawWaveform();
void drawRecordingUI();
void listWAVFiles();
void playSelectedFile();
void displayFileList();
void handleKeys();
int countWAVFiles();
String getIncrementalFileName();

void setup() {
    // Initialize M5Cardputer
    M5Cardputer.begin();
    M5Cardputer.Display.setRotation(1); // Fix screen orientation
    M5Cardputer.Display.fillScreen(BLACK);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(WHITE);
    M5Cardputer.Display.setCursor(0, 0);
    M5Cardputer.Display.println("WAV Recorder");
    
    // Initialize SD card
    if (!SD.begin()) {
        M5Cardputer.Display.println("SD Card Mount Failed");
        delay(2000);
        return;
    }
    
    // Check SD card type
    uint8_t cardType = SD.cardType();
    if (cardType == CARD_NONE) {
        M5Cardputer.Display.println("No SD card inserted");
        delay(2000);
        return;
    }
    
    // List available WAV files
    listWAVFiles();
    displayFileList();
    
    M5Cardputer.Display.println("Press A to record, ENTER to play selected file");
    M5Cardputer.Display.println("Press ; / . keys to navigate files");
}

void loop() {
    M5Cardputer.update();
    
    // Handle key presses for recording/playback
    handleKeys();
    
    // If recording, handle the recording process
    if (isRecording) {
        // Calculate elapsed time
        unsigned long currentTime = millis();
        unsigned long elapsedTime = currentTime - recordingStartTime;
        int newDuration = elapsedTime / 1000;
        
        // Update UI if the second has changed
        if (newDuration != recordingDuration) {
            recordingDuration = newDuration;
            drawRecordingUI();
        }
        
        // Check if maximum recording time reached
        if (recordingDuration >= MAX_RECORDING_TIME) {
            stopRecording();
            listWAVFiles();
            displayFileList();
        }
        
        // Read audio data
        size_t bytesRead = 0;
        esp_err_t result = i2s_read(I2S_NUM_0, audioBuffer, BUFFER_SIZE * sizeof(int16_t), &bytesRead, 0);
        int samplesRead = bytesRead / sizeof(int16_t);
        
        if (samplesRead > 0) {
            // Write to file
            wavFile.write((const uint8_t*)audioBuffer, bytesRead);
            
            // Update waveform visualization (simplified - just take one sample per BUFFER_SIZE/SCREEN_WIDTH)
            for (int i = 0; i < SCREEN_WIDTH && i < samplesRead; i += samplesRead/SCREEN_WIDTH) {
                updateWaveform(audioBuffer[i]);
            }
            
            // Update waveform display every few buffers to avoid excessive redrawing
            static int updateCounter = 0;
            if (++updateCounter % 4 == 0) {
                drawWaveform();
            }
        }
    }
    
    delay(10); // Small delay to prevent CPU hogging
}

// Start recording a new WAV file
void startRecording() {
    // Generate incremental filename
    currentFileName = getIncrementalFileName();
    
    M5Cardputer.Display.fillScreen(BLACK);
    M5Cardputer.Display.setCursor(0, 0);
    M5Cardputer.Display.println("Recording to: " + currentFileName);
    
    // Initialize microphone
    M5Cardputer.Mic.begin();
    
    // Prepare WAV header
    wavHeader.fileSize = 0;  // Will be updated when recording stops
    wavHeader.dataSize = 0;  // Will be updated when recording stops
    
    // Open file for writing
    wavFile = SD.open("/" + currentFileName, FILE_WRITE);
    if (!wavFile) {
        M5Cardputer.Display.println("Failed to open file for writing");
        M5Cardputer.Mic.end();
        return;
    }
    
    // Write header (will be updated when recording stops)
    wavFile.write((const uint8_t*)&wavHeader, sizeof(WAVHeader));
    
    // Reset recording state
    recordingDuration = 0;
    recordingStartTime = millis();
    
    // Clear waveform buffer
    for (int i = 0; i < SCREEN_WIDTH; i++) {
        waveformBuffer[i] = 0;
    }
    
    isRecording = true;
    drawRecordingUI();
}

// Stop recording and finalize WAV file
void stopRecording() {
    if (!isRecording) return;
    
    isRecording = false;
    
    // End microphone session
    M5Cardputer.Mic.end();
    
    // Calculate final sizes
    uint32_t dataSize = wavFile.size() - sizeof(WAVHeader);
    uint32_t fileSize = wavFile.size() - 8;
    
    // Update the header with the correct sizes
    wavFile.seek(4);
    wavFile.write((const uint8_t*)&fileSize, 4);
    
    wavFile.seek(40);
    wavFile.write((const uint8_t*)&dataSize, 4);
    
    wavFile.close();
    
    M5Cardputer.Display.fillScreen(BLACK);
    M5Cardputer.Display.setCursor(0, 0);
    M5Cardputer.Display.println("Recording saved: " + currentFileName);
    M5Cardputer.Display.println("Size: " + String(dataSize) + " bytes");
    M5Cardputer.Display.println("Duration: " + String(recordingDuration) + " seconds");
    
    delay(2000);
}

// Update waveform buffer with new sample
void updateWaveform(int16_t sample) {
    // Shift all elements one position to the left
    for (int i = 0; i < SCREEN_WIDTH - 1; i++) {
        waveformBuffer[i] = waveformBuffer[i + 1];
    }
    
    // Add new sample to the end
    waveformBuffer[SCREEN_WIDTH - 1] = sample;
}

// Draw waveform on screen
void drawWaveform() {
    // Clear waveform area
    M5Cardputer.Display.fillRect(0, 50, SCREEN_WIDTH, 50, BLACK);
    
    // Draw center line
    M5Cardputer.Display.drawLine(0, 75, SCREEN_WIDTH, 75, DARKGREY);
    
    // Draw waveform
    for (int i = 0; i < SCREEN_WIDTH - 1; i++) {
        // Map audio values to display height
        int y1 = map(waveformBuffer[i], -32768, 32767, 50, 100);
        int y2 = map(waveformBuffer[i + 1], -32768, 32767, 50, 100);
        
        M5Cardputer.Display.drawLine(i, y1, i + 1, y2, GREEN);
    }
}

// Draw recording UI with progress
void drawRecordingUI() {
    // Display recording status
    M5Cardputer.Display.fillRect(0, 25, SCREEN_WIDTH, 25, BLACK);
    M5Cardputer.Display.setCursor(0, 25);
    
    // Show record indicator
    M5Cardputer.Display.fillCircle(10, 32, 5, RED);
    M5Cardputer.Display.setCursor(20, 30);
    M5Cardputer.Display.print("REC ");
    
    // Show recording time
    M5Cardputer.Display.print(recordingDuration);
    M5Cardputer.Display.print("/");
    M5Cardputer.Display.print(MAX_RECORDING_TIME);
    M5Cardputer.Display.print("s");
    
    // Progress bar
    int progressWidth = map(recordingDuration, 0, MAX_RECORDING_TIME, 0, SCREEN_WIDTH - 20);
    M5Cardputer.Display.drawRect(10, 45, SCREEN_WIDTH - 20, 5, WHITE);
    M5Cardputer.Display.fillRect(10, 45, progressWidth, 5, RED);
}

// List all WAV files on SD card
void listWAVFiles() {
    File root = SD.open("/");
    if (!root) {
        M5Cardputer.Display.println("Failed to open directory");
        return;
    }
    
    if (!root.isDirectory()) {
        M5Cardputer.Display.println("Not a directory");
        return;
    }
    
    // Clear file list
    for (int i = 0; i < 100; i++) {
        fileList[i] = "";
    }
    
    totalFiles = 0;
    fileCounter = 0;
    
    File file = root.openNextFile();
    while (file && totalFiles < 100) {
        String fileName = file.name();
        if (fileName.endsWith(".wav") || fileName.endsWith(".WAV")) {
            fileList[totalFiles] = fileName;
            totalFiles++;
            
            // Extract highest number from file name for incremental naming
            if (fileName.startsWith("recording_")) {
                int underscorePos = fileName.indexOf('_');
                int dotPos = fileName.lastIndexOf('.');
                if (underscorePos >= 0 && dotPos > underscorePos) {
                    String numStr = fileName.substring(underscorePos + 1, dotPos);
                    int fileNum = numStr.toInt();
                    if (fileNum >= fileCounter) {
                        fileCounter = fileNum + 1;
                    }
                }
            }
        }
        file = root.openNextFile();
    }
    
    root.close();
}

// Count WAV files on SD card
int countWAVFiles() {
    File root = SD.open("/");
    if (!root || !root.isDirectory()) {
        return 0;
    }
    
    int count = 0;
    File file = root.openNextFile();
    while (file) {
        String fileName = file.name();
        if (fileName.endsWith(".wav") || fileName.endsWith(".WAV")) {
            count++;
        }
        file = root.openNextFile();
    }
    
    root.close();
    return count;
}

// Generate incremental file name
String getIncrementalFileName() {
    char fileName[20];
    sprintf(fileName, "recording_%03d.wav", fileCounter++);
    return String(fileName);
}

// Display list of files
void displayFileList() {
    M5Cardputer.Display.fillScreen(BLACK);
    M5Cardputer.Display.setCursor(0, 0);
    M5Cardputer.Display.println("WAV Files (" + String(totalFiles) + "):");
    
    int startIdx = max(0, selectedFileIndex - 5);
    int endIdx = min(totalFiles, startIdx + 10);
    
    for (int i = startIdx; i < endIdx; i++) {
        if (i == selectedFileIndex) {
            M5Cardputer.Display.setTextColor(BLACK, WHITE);
        } else {
            M5Cardputer.Display.setTextColor(WHITE, BLACK);
        }
        
        // Truncate filename if too long
        String displayName = fileList[i];
        if (displayName.length() > 20) {
            displayName = displayName.substring(0, 17) + "...";
        }
        
        M5Cardputer.Display.println(displayName);
    }
    
    M5Cardputer.Display.setTextColor(WHITE, BLACK);
    M5Cardputer.Display.println("\nA: Record  ENTER: Play  ;/.: Navigate");
}

// Play the selected WAV file
void playSelectedFile() {
    if (selectedFileIndex >= totalFiles || fileList[selectedFileIndex] == "") {
        return;
    }
    
    M5Cardputer.Display.fillScreen(BLACK);
    M5Cardputer.Display.setCursor(0, 0);
    M5Cardputer.Display.println("Playing: " + fileList[selectedFileIndex]);
    
    // Open file for reading
    File file = SD.open("/" + fileList[selectedFileIndex]);
    if (!file) {
        M5Cardputer.Display.println("Failed to open file");
        delay(2000);
        return;
    }
    
    // Initialize speaker
    M5Cardputer.Speaker.begin();
    
    // Skip WAV header
    file.seek(44);
    
    isPlaying = true;
    
    // Create a buffer for playback
    const int PLAY_BUFFER_SIZE = 1024;
    uint8_t playBuffer[PLAY_BUFFER_SIZE];
    
    // Draw playback UI
    M5Cardputer.Display.println("\nPlaying...");
    M5Cardputer.Display.println("Press ENTER to stop");
    
    // Simple progress bar
    M5Cardputer.Display.drawRect(10, 45, SCREEN_WIDTH - 20, 5, WHITE);
    
    size_t bytesRead = 0;
    size_t totalBytesRead = 0;
    size_t fileSize = file.size() - 44;  // Subtract header size
    
    // Read and play in chunks
    while (isPlaying && (bytesRead = file.read(playBuffer, PLAY_BUFFER_SIZE)) > 0) {
        // Send to speaker
        M5Cardputer.Speaker.playRaw(playBuffer, bytesRead, SAMPLE_RATE);
        
        // Update progress bar
        totalBytesRead += bytesRead;
        int progressWidth = map(totalBytesRead, 0, fileSize, 0, SCREEN_WIDTH - 20);
        M5Cardputer.Display.fillRect(10, 45, progressWidth, 5, BLUE);
        
        // Check for stop key
        M5Cardputer.update();
        if (M5Cardputer.Keyboard.isKeyPressed(KEY_PLAY)) {
            isPlaying = false;
            break;
        }
    }
    
    file.close();
    
    // End speaker session
    M5Cardputer.Speaker.end();
    
    isPlaying = false;
    
    // Return to file list
    delay(1000);
    displayFileList();
}

// Handle key inputs
void handleKeys() {
    // A button for recording
    if (M5Cardputer.BtnA.wasPressed()) {
        if (isRecording) {
            stopRecording();
            listWAVFiles();
            displayFileList();
        } else if (!isPlaying) {
            startRecording();
        }
    }
    
    // ENTER key for playback or stop playback
    if (M5Cardputer.Keyboard.isKeyPressed(KEY_PLAY)) {
        if (isPlaying) {
            isPlaying = false;
            M5Cardputer.Speaker.end();
            displayFileList();
        } else if (!isRecording) {
            playSelectedFile();
        }
    }
    
    // Keyboard navigation
    if (M5Cardputer.Keyboard.isKeyPressed(KEY_UP)) {
        if (selectedFileIndex > 0) {
            selectedFileIndex--;
            displayFileList();
        }
    }
    
    if (M5Cardputer.Keyboard.isKeyPressed(KEY_DOWN)) {
        if (selectedFileIndex < totalFiles - 1) {
            selectedFileIndex++;
            displayFileList();
        }
    }
    
    // Delete key functionality
    if (M5Cardputer.Keyboard.isKeyPressed(KEY_DELETE)) {
        if (!isRecording && !isPlaying && totalFiles > 0) {
            // Delete the selected file
            SD.remove("/" + fileList[selectedFileIndex]);
            
            // Refresh file list
            listWAVFiles();
            // Adjust selected index if needed
            if (selectedFileIndex >= totalFiles) {
                selectedFileIndex = max(0, totalFiles - 1);
            }
            displayFileList();
        }
    }
}