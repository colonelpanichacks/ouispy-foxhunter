#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>
#include <NimBLEDevice.h>
#include <NimBLEScan.h>
#include <NimBLEAdvertisedDevice.h>
#include <esp_wifi.h>
#include <vector>
#include <algorithm>
#include <cstring>

// Hardware configuration
#define BUZZER_PIN 3
#define BUZZER_FREQ 2000
#define BUZZER_DUTY 127
#define LED_PIN 21

// Network configuration
const char* AP_SSID = "foxhunter";
const char* AP_PASSWORD = "foxhunter";
const unsigned long CONFIG_TIMEOUT = 20000; // 20 seconds

// Operating modes
enum OperatingMode {
    CONFIG_MODE,
    TRACKING_MODE
};

// Global variables
OperatingMode currentMode = CONFIG_MODE;
AsyncWebServer server(80);
Preferences preferences;
NimBLEScan* pBLEScan;

String targetMAC = "";
unsigned long configStartTime = 0;
unsigned long lastConfigActivity = 0;
unsigned long modeSwitchScheduled = 0;
unsigned long deviceResetScheduled = 0;
unsigned long lastBeepTime = 0;
bool targetDetected = false;
int currentRSSI = -100;
unsigned long lastTargetSeen = 0;
bool firstDetection = true;
bool sessionFirstDetection = true; // Only beep once per hunting session

// Persistent settings
bool buzzerEnabled = true;
bool ledEnabled = true;

// Simple beep state
bool isBeeping = false;
unsigned long lastBeepStart = 0;
unsigned long beepDuration = 50;  // 50ms beep duration for fast response

// Serial output synchronization - avoid concurrent writes
volatile bool newTargetDetected = false;

// BLE Scan results storage
struct ScannedDevice {
    String mac;
    int rssi;
    String alias;
};
std::vector<ScannedDevice> scanResults;
bool scanInProgress = false;

// Alias management
String getAlias(String mac) {
    preferences.begin("aliases", true);
    String alias = preferences.getString(mac.c_str(), "");
    preferences.end();
    return alias;
}

void setAlias(String mac, String alias) {
    preferences.begin("aliases", false);
    if (alias.length() > 0) {
        preferences.putString(mac.c_str(), alias);
    } else {
        preferences.remove(mac.c_str());
    }
    preferences.end();
}

// Extract MAC address from "ALIAS (MAC)" format or return as-is
String extractMAC(String input) {
    input.trim();
    
    // Check if input contains parentheses (ALIAS (MAC) format)
    int openParen = input.indexOf('(');
    int closeParen = input.indexOf(')');
    
    if (openParen != -1 && closeParen != -1 && closeParen > openParen) {
        // Extract MAC from parentheses
        String mac = input.substring(openParen + 1, closeParen);
        mac.trim();
        return mac;
    }
    
    // No parentheses, assume it's just a MAC address
    return input;
}


int calculateBeepInterval(int rssi) {
    // REAL-TIME foxhunting intervals
    // RSSI ranges: -95 (very weak) to -30 (very strong)
    if (rssi >= -35) {
        return map(rssi, -35, -25, 25, 10); // 25ms to 10ms - INSANE SPEED
    } else if (rssi >= -45) {
        return map(rssi, -45, -35, 50, 25); // 50ms to 25ms - VERY FAST
    } else if (rssi >= -55) {
        return map(rssi, -55, -45, 100, 50); // 100ms to 50ms - FAST
    } else if (rssi >= -65) {
        return map(rssi, -65, -55, 200, 100); // 200ms to 100ms - MEDIUM
    } else if (rssi >= -75) {
        return map(rssi, -75, -65, 500, 200); // 500ms to 200ms - SLOW
    } else if (rssi >= -85) {
        return map(rssi, -85, -75, 1000, 500); // 1000ms to 500ms - VERY SLOW
    } else {
        return 3000; // 3000ms max for very weak signals
    }
}

// LED control functions (inverted logic for Xiao ESP32-S3)
void ledOn() {
    if (ledEnabled) {
        digitalWrite(LED_PIN, LOW);  // LOW = LED ON for Xiao ESP32-S3
    }
}

void ledOff() {
    if (ledEnabled) {
        digitalWrite(LED_PIN, HIGH); // HIGH = LED OFF for Xiao ESP32-S3
    }
}

// Buzzer functions
void singleBeep() {
    if (buzzerEnabled) {
        ledcWrite(0, BUZZER_DUTY);
    }
    ledOn();
    delay(100);
    if (buzzerEnabled) {
        ledcWrite(0, 0);
    }
    ledOff();
}

void ascendingBeeps() {
    // Ready signal - 2 fast ascending beeps with close melodic notes
    if (buzzerEnabled) {
        ledcWriteTone(0, 1900);
        ledcWrite(0, BUZZER_DUTY);
    }
    ledOn();
    delay(150);
    if (buzzerEnabled) {
        ledcWrite(0, 0);
    }
    ledOff();
    delay(50);
    
    if (buzzerEnabled) {
        ledcWriteTone(0, 2200);
        ledcWrite(0, BUZZER_DUTY);
    }
    ledOn();
    delay(150);
    if (buzzerEnabled) {
        ledcWrite(0, 0);
    }
    ledOff();
    
    // Reset to proximity frequency and ENSURE buzzer is OFF
    if (buzzerEnabled) {
        ledcWriteTone(0, 1000);  // Set to 1kHz for consistency with proximity beeps
        ledcWrite(0, 0);  // Make sure buzzer is completely off
    }
    
    // Add delay to prevent interference with proximity beeps
    delay(500);
}

void handleProximityBeeping() {
    unsigned long currentTime = millis();
    int beepInterval = calculateBeepInterval(currentRSSI);
    
    // Ultra close - solid beep (continuous)
    if (currentRSSI >= -25) {
        if (buzzerEnabled) {
            ledcWriteTone(0, 1000);
            ledcWrite(0, BUZZER_DUTY);
        }
        ledOn();
        isBeeping = true;
        Serial.println("DEBUG: Solid beep mode");
        return;
    }
    
    // Regular proximity beeping with aggressive timing
    if (isBeeping) {
        // Check if beep duration is over (50ms)
        if (currentTime - lastBeepStart >= beepDuration) {
            // Turn off beep
            if (buzzerEnabled) {
                ledcWrite(0, 0);
            }
            ledOff();
            isBeeping = false;
            Serial.println("DEBUG: Beep OFF");
        }
    } else {
        // Check if it's time for next beep
        if (currentTime - lastBeepStart >= beepInterval) {
            // Start new beep
            if (buzzerEnabled) {
                ledcWriteTone(0, 1000);
                ledcWrite(0, BUZZER_DUTY);
            }
            ledOn();
            isBeeping = true;
            lastBeepStart = currentTime;
            Serial.print("DEBUG: Beep ON, RSSI: ");
            Serial.print(currentRSSI);
            Serial.print(", interval: ");
            Serial.println(beepInterval);
        }
    }
}

void threeSameToneBeeps() {
    // Three beeps at same tone for initial detection - using 1kHz for consistency
    for (int i = 0; i < 3; i++) {
        if (buzzerEnabled) {
            ledcWriteTone(0, 1000); // Same 1kHz tone as proximity beeps
            ledcWrite(0, BUZZER_DUTY);
        }
        ledOn();
        delay(100);
        if (buzzerEnabled) {
            ledcWrite(0, 0);
        }
        ledOff();
        delay(50);
    }
    
    // Ensure buzzer is OFF (frequency already at 1kHz)
    if (buzzerEnabled) {
        ledcWrite(0, 0);
    }
    
    // Add delay to prevent interference with proximity beeps
    delay(500);
}

// Configuration storage
void saveConfiguration() {
    preferences.begin("tracker", false);
    preferences.putString("targetMAC", targetMAC);
    preferences.putBool("buzzerEnabled", buzzerEnabled);
    preferences.putBool("ledEnabled", ledEnabled);
    preferences.end();
    Serial.println("Configuration saved to NVS");
}

void loadConfiguration() {
    preferences.begin("tracker", true);
    targetMAC = preferences.getString("targetMAC", "");
    buzzerEnabled = preferences.getBool("buzzerEnabled", true);
    ledEnabled = preferences.getBool("ledEnabled", true);
    preferences.end();
    
    if (targetMAC.length() > 0) {
        targetMAC.toUpperCase(); // Ensure consistent case for comparison
        Serial.println("Configuration loaded from NVS");
        Serial.println("Target MAC: " + targetMAC);
    }
    Serial.println("Buzzer enabled: " + String(buzzerEnabled ? "Yes" : "No"));
    Serial.println("LED enabled: " + String(ledEnabled ? "Yes" : "No"));
}

String getASCIIArt() {
    return R"(
                                                                                                                                                                                                                                                                                                                            
                                                                                                                                                                                                                                                                                                                            
                                                                                                                                                                                                                                                                                                                            
                                                                                                                                                                                                                                                                                                                            
                                                                                                                                                                                                                                                                                                                            
                                                                                                                                                                                                                                                                                                                            
                                                                                                                                                                                                                                                                                                                            
                                                                                                                                                                                                                                                                                                                            
                                                                                                                                                                                                                                                                                                                            
                                                                                                                                                                                                                                                                                                                            
                                                                                                                                                                                                                                                                                                                            
                                                                                                                                                                                                                                                                                                                            
                                                                                                                                                                                                                                                                                                                            
                                                                                                                                                                                                                                                                                                                            
                                                                                                                                                                                                                                                                                                                            
                                                                                                                                                                                                                                                                                                                            
                                                                                                                                                                                                                                                                                                                            
                                                                                                                                                                                                                                                                                                                            
                                                                                                                                                                                                                                                                                                                            
                                                                                                                                                                                                                                                                                                                            
                                                                                                                                                                                                                                                                                                                            
                                                                                                                                                                                                                                                                                                                            
                                                                                                                                                                                                                                                                                                                            
                                                                                                                                                                                                                                                                                                                            
                                                                                                                                                                                                                                                                                                                            
                                                                                                                                                                                                                                                                                                                            
                                                                                                                                                                                                                                                                                                                            
                                                                                                                                                                                                                                                                                                                            
                                                                                                                                                                                                                                                                                                                            
                                                                                                                                                                                                                                                                                                                            
                                                                                                                                                                                                                                                                                                                            
                                                                                                                                                                                                                                                                                                                            
                                                                                                                                                                                                                                                                                                                            
                                                                                                                                                                                           @@@@@@@@                                                         @@@@@@@@                                        
                                                                                                                                                                                       @@@ @@@@@@@@@@                                                    @@@@@@@@@@ @@@@                                    
                                              @@@@@                                                           @@@@@                                                                               @@@@ @ @ @@@@@@@@@@@@@                                               @@@@@@@@@@@@ @@@@@@@@                                
                                         @@@@ @@@@@@@@                                                     @@@@@@@@@@@@@                                                                     @@@@ @@@@@@@@@@@@@@@@@@@@@@@@                                          @@@@@@@@@@@@@@@@@@@ @@@@@@@@@                           
                                     @@@@@@@@ @@@@@@@@@@                                                 @@@@@@@@@@@@ @@ @@@@                                                            @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@                                    @@@@@@@@@@@@@@@@@@@@@@@@@@ @@@@@@@@@                       
                                @@@@@@@@@@@@@@@@@@@@@@@@@@@                                           @@@@@@@@@@@@@@@@@@@@@@@@@@@                                                        @@@@@@ @@@@@@@@@          @@@@@@@@@@@@                                @@@@@@@@@@@@@          @@@@@@@@@@@@@@@                       
                           @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@                                      @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@                                                   @@@@@@@@@ @@@               @@@@@@@@@@@@@                          @@@@@@@@@@@@@               @@@@@@@@@@@@@                       
                          @@@ @@@@@@@@@@@@@       @@@@@@@@@@@@@@                                 @@@@@@@@@@@@@@      @@@@@@@@@@@@@@@@@@                                                  @@ @@@@@@@@@                  @@@@@@@@@@@@@@                     @@@@@@@@ @@@@                   @@@@@  @@@@                       
                          @@@@ @@@@@@@@@              @@@@@@@@@@@@                            @@@@@@@@@@@@@              @@@@@@@@@ @@ @                                                  @@@@   @@@@                   @@@@@@@@@@@ @@                     @ @@@@@@@@@@@                    @@@@  @ @@                       
                          @@@@@@@ @@@                   @@@@@@@@@@@@@                       @@@@@@@@@@@@@                  @@@@ @@@@@@@                                                   @@@  @@@@                     @@ @@@@@@@@@@                     @@@@@@@@@ @@@                     @@@  @@ @                       
                          @@@@@  @ @@                   @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@ @@@@@@@@@@                   @@@@  @@@@                                                    @@@  @@@@                     @@@  @@ @                              @ @@@@@                      @@@@ @@@@                       
                           @@@   @@@                     @@@@@@@ @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@                    @@@@   @@@                                                    @@@@ @@@@                    @@@@  @@@@                              @@@@@@@@                    @@@@@@@@@@                       
                           @@@@ @@@@                     @@ @@@@ @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@   @@@                     @@@  @@@@                                                    @@@@ @@@@@                   @@@   @ @                                 @ @@@@@                  @@@@@@@@@@                        
                           @@@@ @@@@                     @@ @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@  @@@                     @@@@ @@@@                                                    @@@@@@@ @@@                @@@@@   @ @                                 @ @ @@@@                @@@@@@@@@ @                        
                           @@@@ @@@@@                   @@@ @ @                                @@@@  @@@@                   @@@@@ @@@@                                                     @@@@@@@@@@@@             @@@@@    @@@@                               @@@@  @@@@@            @@@@@@@  @  @                        
                           @@@@ @@ @@@                 @@@@ @ @                                 @ @   @@@@                 @@@ @@@@@@                                                      @@@ @@@ @@@@@@@@     @@@@@@@@     @@@@                               @@@@   @@@@@@@@    @@@@@@@@ @@ @@@@@                        
                            @@@@@@@@@@@@             @@@@@  @@@                                @@@@   @@@@@              @@@@@@@@@@@@                                                      @@@@@@@   @@@@@@@@@@@@@@@@@        @@@                               @@@      @@@@@@@@@@@@@@@@@  @@ @@@@@                        
                            @@@@ @@ @@@@@@         @@@@@@   @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@     @@@@@@        @@@@@@@ @@ @@ @                                                      @@@@@@@       @@@@@@@@@  @@@@@@@            @@@@@@@@          @@@     @@@@  @    @@@@@@@@@@      @@ @@@@@                        
                            @@ @@@@@ @@@@@@@@@@@@@@@@@@     @@@@@                             @@@@       @@@@@@@@@@@@@@@@@@   @@@@ @@                                                      @@@@@@@       @@@  @@   @@ @@@@@           @@@@  @ @          @ @     @@@@@@ @     @ @           @@ @ @@                         
                            @@ @ @@@  @@ @@@@@@@@@@@@@@@@@@   @@@@@@@ @@@@@@@@ @@@@@@@@@@@@@@@@@@@        @@ @@@@@@@@@@@@@@@ @@@@@@@@                                                      @@ @@@@      @@@@@@@@@@  @@@@@@ @@@        @@@@@@@@@          @@@@@   @@@@   @@@   @@@@@@@@      @@ @@@@                         
                            @@@@ @@@  @@@@     @@@@  @@@@@@     @ @@@@@   @@@@@@@@@        @@@@@@@@@@@@   @ @ @@@@@@@@@  @@@@@@@@@@@@                                                       @@@@@@@  @@@ @@  @@@@@@@@@    @@@@         @@@@@@@   @@@@@   @@@@@@ @@@  @ @@@@@@@@@@@@@@@      @@@@@ @                         
                            @@@@@@@@  @@@@  @@@@@@@@ @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@  @@@@@@@@@@ @@@@@@@@ @ @@@@@@@@@@@@@@@ @@@@                                                        @@@@@@@  @ @ @@  @@@@@@@@@@   @@@@          @@@@@@   @@@@@   @@@@@@ @ @   @@@@@@@@ @@  @@@      @@@@@@@                         
                             @@@@ @@  @ @ @@@@@@@@@@@@@@@@@@@@@@@ @@@@@@@@@@@ @@@@ @@@@@@@@@@@ @@@@@@@@@@@@   @  @@@@@@@ @@@@@@ @@@@                                                        @ @@ @@  @@@@ @  @@@@@@@@@@@@@@@            @@@@@@   @@@@@   @@@@@@ @@@@  @@@  @@@@@@@@@@@      @@@@@@@                         
                             @  @ @@  @@@@@@@@@@@@@@@ @@@@@@@ @@@@@@@@@@@@ @@@  @@@@@@@@@@@@@@@@@ @  @@ @@@@  @  @@@@@ @@@   @@ @@ @                                                        @ @@@@@  @@@ @@  @@@@@ @@ @ @@ @@           @@@@@@   @@@@@   @@@@@@@@@@           @@@@@@       @@@@  @                          
                             @@ @ @@  @@@@@@@@@@@@@@@ @@@@@@@@@@@   @@@@@@ @@@@ @@ @@@@@@@@@@@@@@ @@@@@@@@@@  @@@@@@@@@@     @@@@@ @                                                        @@ @@@@  @@@@    @@@@@@   @@@@@@@           @  @@@   @@@@@   @@@@@@@@@@           @@@@ @       @@@@@@@                          
                             @@@@@@@  @@@@@@    @@@ @ @@ @@@@@@@@@   @@@@@@@@@@ @@@@@@@@@@@ @@@@@ @ @@ @@@@@  @@@@@ @@       @@@@@@                                                          @@@@@@  @@@@    @@@@@@       @@@           @@@@@@   @@@@@   @@@@@@@@@@           @@@@@@       @@@@@@@                          
                             @@ @@@@  @@@@@@@@@@@@@@@ @@@@@@@    @@@@@@ @@@@@@@@@@@@@@ @@@@@@@@@@@@@@@@@@@    @@@@@@@@       @@@@@@                                                          @@@@@@  @@@      @ @ @@@@@@  @@@@ @        @@@@@@   @@@@@   @@@  @@@@@   @@@@@@  @@@@         @@@@@@                           
                              @  @@@  @@@@@@@@ @@@@@@ @@@@@@@    @@@@@@@@@@@@@@@@@@ @@@@@@@@@@@@@ @ @@@@@@    @@@@@@@@@      @@@@@@                                                          @@@@@@   @@@    @@@@ @ @@@@@@@@@@ @        @@@@@@   @@ @@      @ @@@@@@@@@@@@@@  @@@@ @       @@@@@@                           
                              @@ @@@   @@@@@@@@@@@@   @@@@@@@     @@@@@@ @@@@@@@@@@@@@@    @@@@@@@@@@@@@@@    @@@@@@@@@      @@@@@@                                                           @@@@@   @ @    @@@@ @@@@@@@@@@@ @@        @@@@@@   @@@@@      @@@@@@@ @ @@@@@@  @@@@         @@@  @                           
                              @@@@@@      @@@@ @@@       @@@@             @@@ @@@@@      @@@@   @   @@@       @@@  @@@@      @@@@@                                                            @@@@@   @@@     @@@     @@@@@@@           @@@                      @@@@@@@@@    @@@@         @@@@@@                           
                              @@@@@@@        @@       @@@@@   @@@@@@      @@@@@@@@@@@@@@@@   @    @@@@@@@@@@@@              @@ @@@                                                            @@@ @                                                                                        @@@@@@                           
                              @@@@@@@      @@@@@      @ @@@@@ @@@@@@@@@   @@@@@ @@@@ @@@@ @@@@   @@@@@@@@  @@@              @@@@@@                                                            @@@@@@             @@@@@@@@@    @@@   @@@    @@@@@@@@@    @@@@@@@@     @@@@@@@@@             @@@@@                            
                               @  @@@      @@@@       @@@@@ @ @@@@@@@ @   @@@@@@@@@@@@@@@        @@@@@@@@@@@@@              @@@@ @                                                            @@@@@@             @@    @@@    @ @   @ @@@  @@@    @@    @@ @@@@@     @@@    @@@            @ @@@                            
                               @@@@@@      @@@@       @@@@@@@ @@@@@@@@ @@@@@@@@      @@@@      @@@@@@@     @@ @@@@@         @@@@@@                                                            @@@@@@             @@@@@@@@@@@@ @@@   @@@@@  @@@@@@@ @@@@  @@@@@@@@@@@  @@@@@@ @@@@          @ @@@                            
                               @@@@@@     @@@@@      @@@@@@@@ @@@@@@  @@ @@@@@@      @@@@      @@@@@@@@      @@@@@@         @@@@@                                                              @@@@@           @@@@@   @@ @@@ @@@@  @@@@@@@@@@   @@@@@@@@@@   @@@@@@@@@@   @@@@@@         @@@@ @                            
                                 @@@@     @@@@@      @@@@@@@@ @@@@@@  @@@@@@@@@      @@@@      @@@@@@@@@@@@@@@@@@@@         @@@@@                                                              @@@ @           @@ @@@  @@@@@@ @@@@@ @@@ @@@@@@   @@@@@@@@@@   @@ @@@@@@@   @@@@@@         @@@@@@                            
                                @@@@@     @@@@@@@@@@@@@@@@@@@ @@@@@@     @@@@@@     @@@@@@@     @@@@@@@@@@@@@@ @@@@         @ @ @                                                              @@@@@           @@@@@@ @  @@@@ @@@@  @@@@@@@@@@   @@@@@@@@@@   @@@@@@@@@@   @@@@@@         @@@@@@                            
                                @@@ @     @@ @  @      @@@   @@@  @@      @@@@@     @@   @@         @@@@@@@@@  @@           @@ @@                                                              @@@@@              @@@  @  @@@ @@@  @@@@@@@@@@@   @@@ @@@@@@   @@ @@@@@@@   @@ @@@         @@@ @                             
                                @   @        @@@@@@@@@@@@@    @@@@@@    @ @@@@@@@@  @@@@@@@         @@@@@@@@@@@@            @@@@@                                                              @@@@                       @ @@@@@  @ @@ @ @@@@    @@ @@@@@@    @@@@@@@@@                    @@@                             
                                @@@@@      @@@@@@@@@@@@@@@@@@@  @@@@   @@@   @@@@@@  @@@@   @@@@@       @@@@@               @@@@@                                                               @@@                       @@@@@@@  @@@@@@@@@@@     @@@@@@@@    @@@@ @@@@                    @ @                             
                                @@@@@      @@ @@@  @@@ @  @@ @  @@@@   @ @@@@@ @@@@  @@@@   @ @@@@@@   @@@@@@                @@@                                                                @@@               @@@        @@@@   @@@  @@@@@   @@@  @@@@@        @@@@@                   @@@@                             
                                 @@@       @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@ @@@@@@@ @@@@@@@   @ @@@@@@@@@@@@@ @@               @@@                                                                @ @              @@@@@@@@@@@ @@@@   @@@@ @@@@@@@@@@@@@ @@@@  @@@@  @@@@@                   @@@@                             
                                 @@@              @@@@@     @@@@@@@@@@@@@@@@@  @@@@@@@@@@   @@@@@@@@@@@@@@@@@@@@             @ @                                                                @ @              @@@@@@@@@ @  @ @   @@@     @@@@@@@@ @  @ @     @    @ @                   @ @                              
                                 @ @              @@@@@     @@@@@@@@@@@@@@@@@  @@@@@@@@@@   @ @@@@@@@@@@ @@@@@ @             @ @                                                                @@@              @@@@@@@@@@@  @@@   @@@@    @@@@@@@@@@  @@@  @@@@    @@@                   @ @                              
                                 @@@@             @@@@@     @@@@  @@@@@@@ @@@@@@@ @@ @@@@   @ @@@@@@@@@@@@@ @  @@@          @@@@                                                                 @@@                                                                                       @@@                              
                                 @@@@            @@@@@@@      @@@@@@    @@@ @@@@@ @@@@@@@@@@@@@@@@@@@@@@ @@@@@@@@@          @@@                                                                  @ @  @@@    @@@   @@@@   @@@   @@@@@@@@@@@@ @@@@@@@@@@         @@@   @@@@   @@@@@@@@@     @@@                              
                                  @@@  @@@@@@    @@@@ @@      @@@@@@    @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@ @@@@@@@@@ @@@@@@@  @@@                                                                  @@@  @ @    @@@@  @@@@   @@@@  @@@@@@@@  @@ @@ @ @ @@@@        @ @   @@@@   @@  @ @@@@   @@@@                              
                                  @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@ @@    @@@@@@@@@@@ @ @@@@@@@ @@@@ @@@@@@ @@@@@@@@@@@@@@@@@@@@                                                                  @@@  @@@    @@@@  @@@@   @@@@  @@@@@@@@@@@@  @@@@@@@@@@        @@@   @@@@   @@@@@@@@@@   @@@@                              
                                  @ @@@@@  @@@@@@ @@@@@        @@ @@@@@@ @@@@@     @ @@@@@@@@@@ @@  @@@       @@@@@@@@  @@@@@ @                                                                  @ @ @@@     @@@@@@@@@@@  @@@@     @@@ @@   @@@@    @@@@        @@@   @@@@@@ @@    @@@@@@ @@@                               
                                  @@@@ @@@@@@ @@ @@@@@@@@@@@@@@ @@@@@@@@@@@@@@@@@@@@@@@@ @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@   @@@ @                                                                  @@@@@@@@    @@@@@@@@@@@  @@@@@@   @@@@@@   @@@@@   @@@@@@    @@@@@   @@@@@@ @@@@ @@@ @ @ @@@                               
                                  @@@@@@@@@@@@ @@ @          @@@@@@@@@@@                 @@@@@@@ @@@          @ @@@@@@@@@@@@@@                                                                    @@@@@@@@   @@@@@@@@@@@  @@@@ @   @@ @@@   @@@@@   @@@@@@    @@ @@   @@ @@@ @@@@ @ @  @@ @@@                               
                                  @@@@@@@@@@@@@@@@@        @@@@@@@@@                          @@@@@@@@        @ @@@@ @@@@@@@@@                                                                    @@@@@@@    @@@@@@@@@    @@@@@@   @@@@@@   @@@@@   @@@@@@    @@@@@   @@@@@@ @@@@ @@@@   @@@                                
                                   @@@@@@@@@@@@@ @@      @@@@@@@                                @@@@@@@@      @@@ @@@@@@@@@@@@                                                                    @@@@@@@     @ @ @@@@@   @@@ @@   @@@@@    @@@@@    @@@@@    @@@@@   @@@@@@       @@@@@@@@@                                
                                   @@@@@@@@@@@@@@@@@   @@@  @@@@                                 @@@@@@@@@    @@@@@@@@@@@@@@@@                                                                    @ @@@@@     @ @ @@@@@   @ @@ @    @ @@    @@@@@   @@@@ @    @@@@@   @@@  @  @@@  @ @@ @@ @                                
                                   @@@ @@@@@@@@@@@@@ @@@@@@@@@                                      @@@@@@@@ @@@@ @@@  @@@@@@                                                                      @ @@@@    @@@@ @@@@@   @ @@@@   @@@@     @@@@@   @ @@@@    @@@@@   @@@@@@  @ @  @ @@@ @@@                                
                                   @@@@@@@@@@@@@ @@@@@@  @@                                         @@@@@ @@@@@@   @@@@@@@@@@                                                                      @@@@@  @@@@@@@ @@@@    @ @      @ @      @@@ @@@@@@@       @@@@@@@@@ @  @  @@@@@@ @  @@@@                                
                                    @@@@@@@@@@@@ @@@@@@@@@@                                          @@ @@@ @@@@   @@@@@@@@@@                                                                      @@@    @@@ @ @ @@@@    @ @      @ @      @@@@@@@@@ @           @@@@@ @ @@  @@@@ @ @  @ @                                 
                                    @@@  @@@@@   @@@@@ @@@                                            @@ @@@@@@@   @@ @@@ @@@                                                                      @@@@@@ @@@ @@@ @@@@    @@@      @@@       @@@@@@@@@@           @@@@@@@     @@@@ @@@@@@@@                                 
                                    @@@@@@@ @@   @@@@ @@@@                                             @@@@ @@@@   @@@@@@@@@@                                                                       @@@@@   @@@                              @@@@                               @@@   @@@@@                                 
                                    @@@  @@@@@@@@    @@@@    @@@@@@@                       @@@@@@@@@@@  @ @     @@@@@@@@ @@@                                                                        @ @@@  @@@@                              @@@@                               @@@  @@ @@@                                 
                                      @@ @@@@@ @@    @@@@  @@@@@@@@@@                      @@@@@@@@@@@  @@@@    @@ @@@@@ @@@                                                                        @@ @@  @@@@        @@@@                  @@@@                   @@@@        @ @  @@@@@@                                 
                                     @@@ @@@@@ @@    @ @   @@@@   @@@@                     @@       @@   @@@   @@@ @@@@@ @ @                                                                        @@@@@@ @@@         @@@@@@                @@@@@                @@@@@@        @ @  @@@ @                                  
                                     @@@  @@@@ @@    @ @   @@      @@@                     @@       @@   @ @   @@@ @@@@  @ @                                                                        @@@@@@ @@@         @@@@@@@             @@@@@@@@             @@@@ @@@        @@@@ @@@ @                                  
                                     @@@@ @@@@@@@    @ @   @@@@  @@@@@                     @@       @@   @@@   @@@@@@@@  @@@                                                                        @@@@@@ @@@          @@@@@@@@@@@@@@@@@ @@@@@ @@@@@@@@@@@@@@@@@@ @@@@         @@@@@@@@ @                                  
                                     @@ @@@@@@@@     @@@@ @@@@@@@@@@@                      @@@@@@@@@@@@ @@@@     @@@@@@@@@@                                                                          @ @@@ @@@           @@@@@@@@@   @@@@@@@@@@@@@@@@@@@@@  @@@@@@@@@            @@@@@@@@@                                  
                                      @@@ @@ @@@     @@@@@  @@@@@@@@                       @@@@@@@@@@@@@@ @      @@@@@   @@                                                                          @@@@@@@@@             @@@@@@@@@@@@ @@@ @@@@@@@@@@@ @@@@@@@@@@@              @@@@@@@@@                                  
                                      @@@@@@@@@@      @@@@@@                                   @@@   @@@@@@      @@@@@@@@ @                                                                          @@@@@@@@@              @@@@@ @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@              @ @@@@@@@                                  
                                      @@@ @@@@@       @@@@@                                  @@@@@@@  @@@@@       @@@@ @@@@                                                                          @ @@@@@@@              @@ @@@@ @@@@@ @@@     @@ @@@@@@@@@@@@@               @ @@@@ @                                   
                                      @ @@@@@@@@@@    @@@@@                                  @@@ @@@ @@@@@@    @@@@@@@@@@@@                                                                          @@@ @@@@               @@@@@@@@ @@@@@@@@@@@@@@@@@@@@@@@@ @@@@               @ @@@@ @                                   
                                      @@@@@@@@@ @@   @@@@@@                                  @@@@@@@ @@@@@@@   @@ @@@@@@@@@                                                                           @@@@@@@                @@@@@@@@@@@@@@@@@@@@@@@@@@@@@ @@@@@@@               @@@@@@@@                                   
                                      @@@@@@@@@@@@   @@@@@ @@@                                 @@@   @@@ @ @   @@@@@@@@@@@                                                                            @@@@@@@                @@@@@@@@@   @@@@@@@@@@@@@  @@@@@@@@@@               @@@@ @@@                                   
                                       @ @@@@@@@@@   @@ @@@@@@                                        @ @@@ @@ @@@@@@@@@@@                                                                            @ @@@@@              @@@@@@@ @@       @@@@@@@@@@   @@@@@@ @@@              @@@@@@@@                                   
                                       @@@@@@@@@@@ @@ @@@@@@@@@@@@                             @@@    @@@@@@ @@@@@@@@ @@@@                                                                            @ @@@@@            @@@@@@@@@@@@       @@@@@@@@@    @@@@@@@@@@@@@            @@@@ @                                    
                                       @ @@@@@@@@@@@ @@ @ @@@ @@@@@@                        @@@@ @ @   @@@  @ @@@@@@@@@  @                                                                            @@@ @@@          @@@@ @@@@@@@@@       @@@@@@@@     @@@@@@@@@ @@@@@          @@@@@@                                    
                                       @@@@ @@@@ @@@@@@@@   @@@@@@@@ @@@@               @@@@@ @@@@     @@@  @@@@ @@@@@@@@@                                                                             @@@     @@@@@@@@@@@@@ @@@@@@@@    @@@@@@@@@@@@    @@@@@@@@@@@@@@@@@@@@@@      @@@                                    
                                       @@@@ @@@@ @ @ @@@@     @@@@@@@@@@@ @@@@@@@@@ @@@ @@@@@@@@       @@@@ @@@@ @@@  @@@                                                                              @ @     @@      @@@@@@@  @@@@@    @@  @@@@@@@@    @@@@@   @@@@@@@     @@      @ @                                    
                                        @ @ @@@@ @ @ @ @        @@@ @ @@@ @@@@@@ @@ @ @ @@@@  @@       @@@@ @@@@ @@@@@@@@                                                                              @@@     @@@@@@@@@@@@@@@@@@@@@@    @@@@@@@@@@@@    @@@@@@@@@@@@@@@@@@@@@@     @@@                                     
                                        @@@      @ @ @@@         @@@@@@@  @@@@@@@@@ @@@  @  @           @@@ @@@@     @@@@                                                                                               @@@@@ @@@@@@@        @@@@        @@@@@@@@ @@@@              @@@                                     
                                        @@@      @ @ @@@            @@ @@@                @@            @@@ @@@@     @@@                                                                               @ @                @@@@@ @@@@@       @@@@@@@      @@@@@@@@@@@                @ @                                     
                                        @@@      @ @ @ @             @@@ @                              @@@ @@@@     @@@                                                                               @ @                   @@@@@@@@       @@@@@@@      @@@@@@@@@                  @ @                                     
                                        @ @   @@@@ @ @@@               @@@                              @@@ @@@@@@   @@@                                                                               @@@                     @@@@@@     @@@@@@@@@@@    @@@@@@@                    @@@                                     
                                        @ @ @@@ @@ @                                                        @@@@ @@@@@@@                                                                               @@@                     @@@@@@     @@ @@@@  @@    @@@@@@@                    @@@@                                    
                                        @@@@@ @@@@@@@   @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@   @ @@@@@ @@@@                                                                               @@@                   @@@@@@@@     @@@@@@@@@@@    @@@@@@@@@                   @@@                                    
                                        @@@ @@    @ @@ @@@                                             @@  @@ @   @@@@@@                                                                              @@@@                @@@@@ @@@@@        @@@@        @@@@@@@@@@@                 @@@                                    
                                        @@@@@      @ @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@ @ @@      @@@@@                                                                             @ @               @@@@@ @@@@@@@      @@@@@@@@@     @@@@@@@@ @@@@@              @ @                                    
                                        @@@@@@@@@@@@@@@  @                                            @ @@ @@@@@@@@@@@@@@                                                                             @@@      @@@@@@@@@@@@@@@@@@@@@@      @ @@@@@ @     @@@@@@@ @@@@@@@@@@@@@@      @@@@                                   
                                       @@@@@@@@@@@@@@@   @                                            @ @@ @ @@@@@@@@ @@@                                                                             @@@      @@      @@@ @@@  @@@@@      @@@@@@@@@     @@@@@   @@@ @@@     @@      @@@@                                   
                                       @@@@@@@@   @@@@   @                                            @ @@ @ @@   @@@ @@@@                                                                            @ @      @@@@@@@@@@@@@@@@@@@@@@        @@@@@       @@@@@@@@@@@@@@@@@@@@@@       @@@                                   
                                       @@  @@@@@@@@@@@   @                                            @ @@ @ @@@@@@@@  @ @                                                                           @@@@              @@@@@@@@@@@@@@        @@@@        @@@@@@@@@@@@@@@              @@@                                   
                                       @@@ @@@@@@@@@ @   @                                            @ @@ @ @@@@@@@@  @@@                                                                           @@@                 @@@@@@@@@@@@        @@@@@@      @@@@@@@@@@@@                 @ @                                   
                                       @ @ @   @@@   @   @                                            @ @@ @   @@@     @@@                                                                           @@@                  @@ @@@@@ @@        @@@@        @@@@@@@@@@@                  @ @                                   
                                       @   @@@@@@@@@ @   @                                            @ @@ @ @@@@@@@   @ @                                                                           @ @                  @@@@@@@@ @@@       @@@@       @@@@@@@@@@@@                  @@@@                                  
                                      @@@@ @@@@@@@@@@@   @                                            @ @@ @@@@@@@@@@  @@@@                                                                          @@@                  @@@@ @@@@@@@@@@@@@@@@@@@@@@@@@@@ @@@@@@@@@@                 @@@@                                  
                                      @@@@ @@@@   @@@@   @                                            @ @@ @@@@   @@@   @@@                                                                          @@@                  @@@@@@@@@@@ @@@@   @@@@   @@@@@@@@@ @@@@@@@                  @ @                                  
                                      @@@@ @@@@@@@@@@@   @                                            @ @@ @@@@@@@@@@   @@@                                                                         @@@@                  @@@@@ @@@@@@@ @@@@@@@@@@@@@@  @@@@@@@@@@@@@                  @@@                                  
                                      @@@@ @@@@@@@@@ @   @                                            @ @@ @ @@@@@@@@   @ @                                                                         @@@                  @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@ @@@@@@@  @@@                 @ @                                  
                                      @ @@ @         @   @                                            @ @@ @            @@@                                                                         @@@                @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@                @@@@                                 
                                     @@@@@ @@@@@@@@@ @   @                                            @ @@ @ @@@@@@@@   @@@                                                                         @@@              @@@@ @@@@@@@@@@@   @@@@ @@@@@@@@@   @@@@@@@@@@@@@@@@              @@@@                                 
                                     @@@@@ @@@@@@@@@@@   @                                            @ @@ @@@@@@@@@@   @@@@                                                                        @ @              @@@@@@@              @@@@@@@@@@@             @@@@ @@               @ @                                 
                                     @ @ @ @@@    @@@@   @                                            @ @@ @@@@   @@@    @@@                                                                        @@@              @@@@@                 @@@@@@@@                 @@@@@@              @@@                                 
                                     @@@ @ @@@@@@@@@@@   @                                            @ @@ @@@@@@@@@@    @@@                                                                       @@@               @@@@                    @@@@@                    @@@               @@@                                 
                                     @@@ @ @@@@@@@@@ @   @                                            @ @@ @ @@@@@@@@    @@@                                                                       @ @                                       @@@@                                       @ @                                 
                                    @@@  @ @  @@@@   @   @                                            @ @@ @   @@@@      @ @                                                                       @@@                                       @@@@                                       @@@@                                
                                    @@@  @ @@@@@@@@@ @   @                                            @ @@ @ @@@@@@@@    @@@                                                                       @ @                   @@@ @@@             @@@@@@           @@@ @@@@                  @@@@                                
                                    @@@  @ @@@@@@@@@@@   @                                            @ @@ @@@@@@@@@@     @@@                                                                     @ @                    @ @@@ @@@          @@@@@@@          @@ @@@@@@                @@@@ @                                
                                    @@@  @ @@@@  @@@@@   @                                            @ @@ @@@@   @@@     @ @                                                                     @@@@@@@                @@@@@@@ @          @ @ @ @        @@@@@@@@@@@                @@@@@@                                
                                   @@@@@@@ @@@@@@@@@@@   @                                            @ @@ @@@@@@@@@@     @@@                                                                     @@@@@@@                   @ @@@@@@@       @ @ @ @       @@ @@@@@@@ @                @@@@@@                                
                                   @@@@@@@ @@@@@@@@@ @   @                                            @ @@@@@@@@@@@@   @@@@@@                                                                     @@    @                @  @@@ @ @ @@@     @ @ @ @     @@@@@ @@@@ @ @               @@@@@@@@                               
                                   @@@@@@@ @  @@@@   @   @                                            @ @ @@@@@@@@@    @@@@@@                                                                    @@@@   @                @    @@@@@@@ @     @ @ @ @   @@@ @  @@@@  @ @               @@@@@@@@                               
                                   @@@@@@  @@@@@@@@@ @   @                                            @ @  @@@@@@@@@@  @@@@@@@                                                                   @ @@   @                @ @    @@ @@@@@@@  @ @ @ @  @@ @@@@@@@    @ @               @@  @  @                               
                                   @ @@@@  @@@@@@@@@@@   @                                            @ @   @@@@@@@@@  @@@@@@@                                                                   @@@@@  @                @ @     @@@ @ @ @@ @ @ @ @@@@@@ @ @@      @ @               @@  @@@@@                              
                                  @@@@@@@  @@@@  @@@@@   @                                            @ @   @@@   @@@  @@@@@@@                                                                   @@@@@ @@@               @ @       @@@@@@@@@@ @ @ @@ @ @ @@@       @ @               @@  @@@@@                              
                                  @ @@@@@  @@@@@@@@@@@   @                                            @ @   @@@@@@@@@  @@@@@@@                                                                   @ @@@ @@@               @ @         @@ @@@ @ @ @ @@@@@@@@         @ @               @@  @@@@@                              
                                  @@@@@@@  @@@@@@@@@@ @@ @                                            @ @    @@@@@@@   @@@@@@ @                                                                 @@@@@@ @@@               @ @          @@@ @@@   @@@@@ @            @ @              @@@  @@@@@                              
                                  @@@@@ @  @  @@@@@@ @@@ @                                            @ @     @@@@@    @@ @@                                                                    @@@@@@ @@@               @ @            @@@@@@  @@@ @@@            @ @              @@@   @  @                              
                                  @ @@@ @  @@@@@@@@@@  @ @                                            @ @    @@@@@@@@  @@ @@@@@                                                                 @@ @@@ @@@@              @ @              @@ @  @@ @@              @ @              @@    @@@@                              
                                  @ @@@ @  @@@@@@@@@@  @ @                                            @ @   @@@@ @@@@  @@ @@@ @                                                                 @ @@ @ @@@@              @ @               @@@@ @@@                @ @             @@@    @@@@@                             
                                 @@@@@@ @  @@@@  @@@@  @ @                                            @ @   @@@@ @@@@  @@ @@ @@@                                                                @@@@ @ @ @@              @ @                @ @ @ @                @ @             @@@    @@@@@                             
                                 @@@@@  @  @@@@@@@@@   @ @                                            @ @   @@@@@@@@@  @@ @@@@@@                                                                @@@@ @ @ @@              @ @                @ @ @ @                @ @             @@@     @  @                             
                                 @@ @@  @  @ @@@@@@    @ @                                            @ @    @@@@@@@   @@  @@@@@                                                               @@@@  @ @ @@@                                @ @ @ @                @ @             @@      @@@@                             
                                 @ @@@  @  @ @@@@@@    @ @                                            @ @     @@@@@    @@  @@@ @                                                               @@@@  @ @ @@@             @@@@@@@@@@@@@@@@@@@@@@ @@@@@@@@@@@@@@@@@@@@@@             @@      @@@@                             
                                 @ @@@  @  @@@@@@@@@   @ @                                            @ @    @@@@@@@@  @@  @@@@@                                                               @@@@  @ @  @@   @@@@@@@@@@@@                 @@@ @@@                @@@ @@@@@@@@@  @@@       @@@@                            
                                @@@@@   @  @@@@@ @@@   @ @                                            @ @   @@@@ @@@@  @@  @@ @@                                                               @@@@  @ @  @@@@@@@@@ @@@  @@@@@@@@@@@@@@@@@@@@@ @@@@@@@@@@@@@@@@@@@@@@ @@  @@@ @   @@@       @@@@                            
                                @@@@@   @  @@@@  @@@   @ @                                            @ @   @@@@ @@@@  @@   @@@@@                                                              @@@   @ @  @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@ @@@@@@@@@@@@@@@@@@@@@@@@ @@@@@@@@@@@@@@ @@        @@@@                            
                                @ @@@   @  @@@@@@@@@   @@@                                            @@@    @@@@@@@@  @@   @@@@@                                                              @@@   @ @  @@@ @@@@ @@ @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@ @@        @@@@                            
                                @@@@@   @@@@ @@@@@@     @                                             @@@    @@@@@@@@@@@@   @@@@@                                                             @@@@   @ @  @@@@@@@@@@@@@@@@@@@@@@ @@@@@ @@@@@ @@@@ @@@@@@@ @  @ @@@@@@@@@@@@@@@@@@ @@         @@@                            
                                @@@@    @@@@@                                                                       @@@@@   @@@@@                                                             @@@@   @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@ @@@@@@@@@@@@@ @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@ @@@@@@     @@@                            
                                 @@@    @@@ @@@                                                                   @@@@@@@   @@@ @                                                             @@@@   @@@ @@@@@@@@@@@@@@@@@@@@@@@ @@        @@@@ @@@@       @@ @@@@@@@@@@@@@@@@@@@@@@@@@@@@   @@@@                           
                               @@@@@    @@@@@ @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@ @@@@@    @@@@                                                             @@@  @@@@@@@@@@@@@@@@@@@@@ @@@@    @@@@@@@@@@@@@@@@@@@@@@@@@@@@    @@@@  @@@@@@@@@@@@@@@@@@@@@ @@@@                           
                               @@@@   @@@@@@@@@@@@@@        @@@                                   @@@        @@@@@@@@@@@@@@  @@@@@                                                            @@@@@@@@@@@         @@@@ @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@  @@@@@@        @@@@@ @@  @@                           
                               @@@@ @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@  @@ @                                                           @@@@@@@@@              @@@@@@@@@@@@@@@@@@@@@@@@@@  @@@@@@@@@@@@@@@@@@@@@ @@@@              @@@@@@@@@                           
                              @@@@@@@@@@@@@       @@@@@@@@@@@@@ @                               @ @ @@@@@@@@@@@      @@@@@@@@@@@@@                                                           @@@@@@@@                 @@@@@@@@                                   @@@@@@@                  @@@@@@@                           
                              @ @@@@@@@@             @@@@ @@@ @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@  @  @@@@             @@@@@@@@@                                                           @@@ @@@                   @@@@@ @                                   @ @@@@                    @@@@ @@                          
                              @@@@@@@@                 @@@@@@@@@                                 @@  @ @@                  @@@@@@@                                                          @@@@@@@                     @@@@ @                                   @ @@@@                     @@ @@@                          
                              @@@ @@@                    @@@@ @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@ @@@@                    @@@@@@@                                                         @ @ @@                      @@@@ @                                   @ @@@                      @@ @@@                          
                              @ @@@@                     @@@@ @                                   @ @@@@                    @@@ @@@                                                         @@@ @@@                     @@ @ @                                   @ @@@@                    @@@ @@@                          
                             @@@@@@@                     @@ @ @                                   @ @@@@                     @@ @@@                                                         @@@@@@@@                   @@@ @ @                                   @ @@@@@                   @@@@@@@@                         
                             @@@@@@@                     @@ @ @                                   @ @@@@                     @@ @@@                                                         @ @@@@@@@                 @@@@@@ @                                   @ @@@@@@                @@@ @@@@ @                         
                             @@@@@@@@                   @@@@@ @                                   @ @@@@@                   @@@@ @ @                                                        @@@@@@@@@@@             @@@@@@@@ @                                   @ @@@@@@@@            @@@@@@@@@@@@                         
                             @@@@@@@@@                 @@@@@@ @                                   @ @@@@@@                 @@@@@  @                                                        @@@@@ @@@@@@@@@       @@@@@@@@@@@@@                                   @@@@ @@ @@@@@      @@@@@@@@@ @@@@@                         
                             @ @@@@@@@@               @@@@@@@ @                                   @ @@@@@@               @@@@@@@ @@@                                                       @ @@@  @@@@@@@@@@@@@@@@@@@@ @@@@@@@                                   @@@@@ @@@@@@@@@@@@@@@@@@@@@  @@@ @                         
                             @@@@@ @@@@@@@         @@@@@@@@@@ @                                   @ @@ @@@@@@@         @@@@ @@@@ @ @                                                       @ @@@     @@@@ @@@@@@@ @@@@@@@@@@@                                     @@@@@@@@@@@@@@@@@@@@@@@     @@@ @                         
                            @@@@@@  @@@@@@@@@@@@@@@@@@@@@@@@@@@                                   @@@@@@@@@@@@@@@@@@@@@@@@@@@ @@ @@@                                                       @ @@@@@@@     @@@@@@@@@@@@@@@@@                                          @@@@@@@ @@@@@@@@@     @@@@@@@@@                         
                            @@@@@@    @@@@@@@@@@@@@@@@@@@@@@@@@                                   @@@@@@@@@ @@@@@@@@@@@@@@@   @@  @@@                                                      @@@@@@@@@@@@@  @@@@@@ @@@@@@                                                @@@@@@@@@@@@@  @@@@@@@@@@@@@                         
                            @@@@@@@@@      @@@@@@@@@@@@@@@@@                                         @@@@@@@@@@@@@@@@@     @@@@@@@@ @                                                          @@@@@@@@@@@@@@ @@@@@@                                                      @@@@@@ @@@@@@@@@@@@@@@                            
                            @@@@@@@@@@@@@@   @@@@@ @@@@@@                                              @@@@@@@ @@@@@   @@@@@@@@@@@@@@                                                              @@@@@@@@@@@@@@                                                            @@@@@@@@@@@@@@@                                
                               @@@ @@@@@@@@@@@@@@ @@@@                                                     @@@@  @@@@@@@@@@@@@@@@@                                                                      @@@@@@                                                                  @@@@@@@                                     
                                   @@@ @@@@@@@@@@@@                                                           @@@@@@@@@@@@@@@@                                                                                                                                                                                              
                                       @@@@  @@@                                                                @@@@ @@@@@                                                                                                                                                                                                  
                                                                                                                    @                                                                                                                                                                                                       
)";
}

String generateConfigHTML() {
    // Generate random MAC for placeholder
    String randomMAC = "";
    randomSeed(analogRead(0) + micros());
    for (int i = 0; i < 6; i++) {
        if (i > 0) randomMAC += ":";
        byte randByte = random(0, 256);
        if (randByte < 16) randomMAC += "0";
        randomMAC += String(randByte, HEX);
    }
    randomMAC.toLowerCase();

    String html = R"html(
<!DOCTYPE html>
<html>
<head>
    <title>OUI-SPY FOXHUNTER Configuration</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        * { box-sizing: border-box; }
        body {
            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
            margin: 0; 
            padding: 20px;
            background: #0f0f23;
            color: #ffffff;
            position: relative;
            overflow-x: hidden;
        }
        .container {
            max-width: 700px; 
            margin: 0 auto; 
            background: rgba(255, 255, 255, 0.02);
            padding: 40px; 
            border-radius: 16px;
            box-shadow: 0 8px 32px rgba(0, 0, 0, 0.2); 
            backdrop-filter: blur(5px);
            border: 1px solid rgba(255, 255, 255, 0.05);
            position: relative;
            z-index: 1;
        }
        h1 {
            text-align: center;
            margin-bottom: 20px;
            margin-top: 0px;
            font-size: 48px;
            font-weight: 700;
            color: #8a2be2;
            background: -webkit-linear-gradient(45deg, #8a2be2, #4169e1);
            background: -moz-linear-gradient(45deg, #8a2be2, #4169e1);
            background: linear-gradient(45deg, #8a2be2, #4169e1);
            -webkit-background-clip: text;
            -moz-background-clip: text;
            background-clip: text;
            -webkit-text-fill-color: transparent;
            -moz-text-fill-color: transparent;
            letter-spacing: 3px;
        }
        @media (max-width: 768px) {
            h1 {
                font-size: clamp(32px, 8vw, 48px);
                letter-spacing: 2px;
                margin-bottom: 15px;
                text-align: center;
                display: block;
                width: 100%;
            }
            .container {
                padding: 20px;
                margin: 10px;
            }
        }
        .section { 
            margin-bottom: 30px; 
            padding: 25px; 
            border: 1px solid rgba(255, 255, 255, 0.1); 
            border-radius: 12px; 
            background: rgba(255, 255, 255, 0.01); 
            backdrop-filter: blur(3px);
        }
        .section h3 { 
            margin-top: 0; 
            color: #ffffff; 
            font-size: 18px;
            font-weight: 600;
            margin-bottom: 15px;
        }
        textarea { 
            width: 100%; 
            min-height: 120px;
            padding: 15px; 
            border: 1px solid rgba(255, 255, 255, 0.2); 
            border-radius: 8px; 
            background: rgba(255, 255, 255, 0.02);
            color: #ffffff;
            font-family: 'Courier New', monospace;
            font-size: 14px;
            resize: vertical;
        }
        textarea:focus {
            outline: none;
            border-color: #4ecdc4;
            box-shadow: 0 0 0 3px rgba(78, 205, 196, 0.2);
        }
        .toggle-container {
            display: flex;
            flex-direction: column;
            gap: 15px;
        }
        .toggle-item {
            display: flex;
            align-items: center;
            gap: 15px;
            padding: 15px;
            border: 1px solid rgba(255, 255, 255, 0.1);
            border-radius: 8px;
            background: rgba(255, 255, 255, 0.02);
        }
        .toggle-item input[type="checkbox"] {
            width: 20px;
            height: 20px;
            accent-color: #4ecdc4;
            cursor: pointer;
        }
        .toggle-label {
            font-weight: 500;
            color: #ffffff;
            cursor: pointer;
            user-select: none;
        }
        .help-text { 
            font-size: 13px; 
            color: #a0a0a0; 
            margin-top: 8px; 
            line-height: 1.4;
        }
        button { 
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%); 
            color: #ffffff; 
            padding: 14px 28px; 
            border: none; 
            border-radius: 8px; 
            cursor: pointer; 
            font-size: 16px; 
            font-weight: 500;
            margin: 10px 5px; 
            transition: all 0.3s;
        }
        button:hover { 
            transform: translateY(-2px);
            box-shadow: 0 8px 25px rgba(102, 126, 234, 0.4);
        }
        .button-container {
            text-align: center;
            margin-top: 40px;
            padding-top: 30px;
            border-top: 1px solid #404040;
        }
        .status { 
            padding: 15px; 
            border-radius: 8px; 
            margin-bottom: 30px; 
            margin-top: 10px;
            border-left: 4px solid #ff1493;
            background: rgba(255, 20, 147, 0.05);
            color: #ffffff;
            border: 1px solid rgba(255, 20, 147, 0.2);
            text-align: center;
        }
        .scan-btn {
            background: linear-gradient(135deg, #4ecdc4 0%, #44a08d 100%);
            width: 100%;
            margin: 0;
            font-size: 18px;
            padding: 16px;
        }
        .scan-btn:hover {
            box-shadow: 0 8px 25px rgba(78, 205, 196, 0.4);
        }
        .scan-btn:disabled {
            background: #555;
            cursor: not-allowed;
            opacity: 0.6;
        }
        #scanResults {
            margin-top: 20px;
            max-height: 400px;
            overflow-y: auto;
            overflow-x: hidden;
            width: 100%;
        }
        .device-item {
            display: flex;
            justify-content: space-between;
            align-items: center;
            padding: 12px;
            margin-bottom: 8px;
            background: rgba(255, 255, 255, 0.03);
            border: 1px solid rgba(255, 255, 255, 0.1);
            border-radius: 8px;
            transition: all 0.2s;
            gap: 10px;
            flex-wrap: wrap;
        }
        .device-item:hover {
            background: rgba(78, 205, 196, 0.1);
            border-color: #4ecdc4;
        }
        .device-mac {
            cursor: pointer;
            user-select: all;
            flex: 1 1 auto;
            min-width: 0;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            padding: 10px 12px;
            border-radius: 6px;
            transition: all 0.2s;
            border: 1px solid rgba(255, 255, 255, 0.1);
            word-break: break-all;
        }
        .device-mac:hover {
            transform: scale(1.02);
            box-shadow: 0 4px 12px rgba(102, 126, 234, 0.4);
            border-color: rgba(255, 255, 255, 0.3);
        }
        .device-mac:active {
            transform: scale(0.98);
        }
        .device-info {
            display: flex;
            flex-direction: column;
            gap: 3px;
        }
        .device-info > div {
            font-family: 'Courier New', monospace;
            font-size: 13px;
            font-weight: 600;
            color: #ffffff;
            text-align: center;
        }
        .device-alias {
            font-size: 15px;
            font-weight: 700;
            color: #32cd32;
            text-shadow: 0 0 10px rgba(50, 205, 50, 0.5);
        }
        .device-mac-small {
            font-size: 10px;
            color: rgba(255, 255, 255, 0.6);
            font-weight: 400;
        }
        .device-rssi {
            display: flex;
            align-items: center;
            gap: 8px;
            flex-shrink: 0;
            margin-left: auto;
        }
        .edit-alias-btn {
            background: rgba(78, 205, 196, 0.2);
            color: #4ecdc4;
            border: 1px solid #4ecdc4;
            padding: 4px 8px;
            border-radius: 4px;
            cursor: pointer;
            font-size: 11px;
            transition: all 0.2s;
        }
        .edit-alias-btn:hover {
            background: rgba(78, 205, 196, 0.3);
        }
        @media (max-width: 768px) {
            .device-item {
                flex-direction: column;
                align-items: stretch;
                gap: 10px;
            }
            .device-mac {
                width: 100%;
                font-size: 12px;
                padding: 12px 10px;
            }
            .device-rssi {
                width: 100%;
                justify-content: space-between;
                margin-left: 0;
            }
        }
        .rssi-value {
            font-weight: 600;
            padding: 4px 10px;
            border-radius: 6px;
            font-size: 13px;
            white-space: nowrap;
        }
        @media (max-width: 768px) {
            .rssi-value {
                font-size: 12px;
                padding: 6px 10px;
            }
        }
        .rssi-strong {
            background: rgba(34, 197, 94, 0.2);
            color: #22c55e;
            border: 1px solid #22c55e;
        }
        .rssi-medium {
            background: rgba(251, 191, 36, 0.2);
            color: #fbbf24;
            border: 1px solid #fbbf24;
        }
        .rssi-weak {
            background: rgba(239, 68, 68, 0.2);
            color: #ef4444;
            border: 1px solid #ef4444;
        }
        .scanning-indicator {
            text-align: center;
            padding: 20px;
            color: #4ecdc4;
            font-size: 14px;
        }
        .spinner {
            border: 3px solid rgba(78, 205, 196, 0.1);
            border-top: 3px solid #4ecdc4;
            border-radius: 50%;
            width: 30px;
            height: 30px;
            animation: spin 1s linear infinite;
            margin: 10px auto;
        }
        @keyframes spin {
            0% { transform: rotate(0deg); }
            100% { transform: rotate(360deg); }
        }
        .no-devices {
            text-align: center;
            padding: 20px;
            color: #a0a0a0;
            font-style: italic;
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>OUI-SPY FOXHUNTER</h1>
        
        <div class="status">
            Scan for or enter the target MAC address for foxhunting. Beep/LED flash speed is related to RSSI of selected device.
        </div>
        
        <div class="section">
            <h3>BLE Device Scanner</h3>
            <button type="button" class="scan-btn" id="scanBtn" onclick="startScan()">Scan for BLE Devices</button>
            <div id="scanResults"></div>
            <div class="help-text" style="margin-top: 15px;">
                Click scan to discover nearby BLE devices. Click any MAC address button to auto-populate the target field below.
            </div>
        </div>
        
        <form method="POST" action="/save">
            <div class="section">
                <h3>Target MAC Address</h3>
                <textarea name="targetMAC" placeholder="Enter target MAC address:
)html" + randomMAC + R"html(">)html" + targetMAC + R"html(</textarea>
                <div class="help-text">
                    Single MAC address for directional antenna tracking.<br>
                    Format: XX:XX:XX:XX:XX:XX (17 characters with colons)<br>
                    Beep intervals: 50ms (LIGHTNING) to 10s (PAINFULLY SLOW)
                </div>
            </div>
            
            <div class="section">
                <h3>Audio & Visual Settings</h3>
                <div class="toggle-container">
                    <div class="toggle-item">
                        <input type="checkbox" id="buzzerEnabled" name="buzzerEnabled" )html" + String(buzzerEnabled ? "checked" : "") + R"html(>
                        <label class="toggle-label" for="buzzerEnabled">Enable Buzzer</label>
                        <div class="help-text" style="margin-top: 0;">Audio feedback for target proximity</div>
                    </div>
                    <div class="toggle-item">
                        <input type="checkbox" id="ledEnabled" name="ledEnabled" )html" + String(ledEnabled ? "checked" : "") + R"html(>
                        <label class="toggle-label" for="ledEnabled">Enable LED Blinking</label>
                        <div class="help-text" style="margin-top: 0;">Orange LED blinks with same cadence as buzzer</div>
                    </div>
                </div>
            </div>
            
            <div class="button-container">
                <button type="submit">Save Configuration & Start Scanning</button>
                <button type="button" onclick="clearConfig()" style="background: #8b0000; margin-left: 20px;">Clear All Filters</button>
                <button type="button" onclick="deviceReset()" style="background: #4a0000; margin-left: 20px; font-size: 12px;">Device Reset</button>
            </div>
            
            <script>
            function startScan() {
                const scanBtn = document.getElementById('scanBtn');
                const resultsDiv = document.getElementById('scanResults');
                
                scanBtn.disabled = true;
                scanBtn.textContent = 'Scanning...';
                
                resultsDiv.innerHTML = '<div class="scanning-indicator"><div class="spinner"></div>Scanning for BLE devices...<br>This takes about 3 seconds</div>';
                
                // Start the scan and wait for results
                fetch('/scan', { method: 'POST' })
                    .then(response => response.json())
                    .then(data => {
                        scanBtn.disabled = false;
                        scanBtn.textContent = 'Scan for BLE Devices';
                        displayResults(data);
                    })
                    .catch(error => {
                        console.error('Error:', error);
                        scanBtn.disabled = false;
                        scanBtn.textContent = 'Scan for BLE Devices';
                        resultsDiv.innerHTML = '<div class="no-devices">Error scanning. Please try again.</div>';
                    });
            }
            
            function displayResults(devices) {
                const resultsDiv = document.getElementById('scanResults');
                
                if (devices.length === 0) {
                    resultsDiv.innerHTML = '<div class="no-devices">No BLE devices found. Try scanning again.</div>';
                    return;
                }
                
                let html = '';
                devices.forEach(device => {
                    const rssiClass = device.rssi >= -60 ? 'rssi-strong' : device.rssi >= -80 ? 'rssi-medium' : 'rssi-weak';
                    const displayText = device.alias ? device.alias : device.mac;
                    const subText = device.alias ? device.mac : '';
                    const aliasParam = device.alias ? "'" + device.alias.replace(/'/g, "\\'") + "'" : "''";
                    
                    html += `
                        <div class="device-item">
                            <div class="device-mac" onclick="copyToTarget('${device.mac}', ${aliasParam})" title="Click to use as target">
                                <div class="device-info">
                                    ${device.alias ? `<div class="device-alias">${device.alias}</div>` : ''}
                                    <div class="${device.alias ? 'device-mac-small' : ''}">${device.mac}</div>
                                </div>
                            </div>
                            <div class="device-rssi">
                                <span class="rssi-value ${rssiClass}">${device.rssi} dBm</span>
                                <button class="edit-alias-btn" onclick="editAlias('${device.mac}', '${device.alias || ''}')">Alias</button>
                            </div>
                        </div>
                    `;
                });
                
                resultsDiv.innerHTML = html;
            }
            
            function editAlias(mac, currentAlias) {
                const newAlias = prompt('Enter alias for ' + mac + '\\n(Leave empty to remove alias):', currentAlias);
                if (newAlias !== null) {
                    fetch('/set-alias', {
                        method: 'POST',
                        headers: {'Content-Type': 'application/x-www-form-urlencoded'},
                        body: 'mac=' + encodeURIComponent(mac) + '&alias=' + encodeURIComponent(newAlias)
                    })
                    .then(response => response.text())
                    .then(data => {
                        // Update alias in current results without re-scanning
                        fetch('/get-results')
                            .then(response => response.json())
                            .then(devices => {
                                displayResults(devices);
                            })
                            .catch(error => {
                                console.error('Error:', error);
                            });
                    })
                    .catch(error => {
                        console.error('Error:', error);
                        alert('Error saving alias');
                    });
                }
            }
            
            function copyToTarget(mac, alias) {
                const textarea = document.querySelector('textarea[name="targetMAC"]');
                // Format as "ALIAS (MAC)" if alias exists, otherwise just "MAC"
                textarea.value = alias ? alias + ' (' + mac + ')' : mac;
                textarea.focus();
                textarea.scrollIntoView({ behavior: 'smooth', block: 'center' });
                
                // Visual feedback
                textarea.style.borderColor = '#4ecdc4';
                textarea.style.boxShadow = '0 0 0 3px rgba(78, 205, 196, 0.3)';
                setTimeout(() => {
                    textarea.style.borderColor = '';
                    textarea.style.boxShadow = '';
                }, 1000);
            }
            
            function clearConfig() {
                if (confirm('Are you sure you want to clear the target MAC? This action cannot be undone.')) {
                    document.querySelector('textarea[name="targetMAC"]').value = '';
                    fetch('/clear', { method: 'POST' })
                        .then(response => response.text())
                        .then(data => {
                            alert('Target MAC cleared!');
                            location.reload();
                        })
                        .catch(error => {
                            console.error('Error:', error);
                            alert('Error clearing target. Check console.');
                        });
                }
            }
            
            function deviceReset() {
                if (confirm('DEVICE RESET: This will completely wipe all saved data and restart the device. Are you absolutely sure?')) {
                    if (confirm('This action cannot be undone. The device will restart and behave like first boot. Continue?')) {
                        fetch('/device-reset', { method: 'POST' })
                            .then(response => response.text())
                            .then(data => {
                                alert('Device reset initiated! Device restarting...');
                                setTimeout(function() {
                                    window.location.href = '/';
                                }, 5000);
                            })
                            .catch(error => {
                                console.error('Error:', error);
                                alert('Error during device reset. Check console.');
                            });
                    }
                }
            }
    </script>
        </form>
    </div>
</body>
</html>
)html";
    
    return html;
}

// BLE Scan callback for discovery mode
class ScanCallbacks: public NimBLEAdvertisedDeviceCallbacks {
    void onResult(NimBLEAdvertisedDevice* advertisedDevice) {
        String mac = advertisedDevice->getAddress().toString().c_str();
        mac.toUpperCase();
        int rssi = advertisedDevice->getRSSI();
        
        // Check if device already in results
        bool found = false;
        for (auto& device : scanResults) {
            if (device.mac == mac) {
                // Update RSSI if stronger signal
                if (rssi > device.rssi) {
                    device.rssi = rssi;
                }
                // Refresh alias in case it was changed
                device.alias = getAlias(mac);
                found = true;
                break;
            }
        }
        
        // Add new device
        if (!found) {
            ScannedDevice newDevice;
            newDevice.mac = mac;
            newDevice.rssi = rssi;
            newDevice.alias = getAlias(mac);
            scanResults.push_back(newDevice);
        }
    }
};

// Web server handlers
void startConfigMode() {
    currentMode = CONFIG_MODE;
    Serial.println("\n=== STARTING FOXHUNT CONFIG MODE ===");
    Serial.println("SSID: " + String(AP_SSID));
    Serial.println("Password: " + String(AP_PASSWORD));
    Serial.println("Initializing WiFi AP...");
    
    WiFi.mode(WIFI_AP);

    bool apStarted = false;
    if (AP_PASSWORD != nullptr && std::strlen(AP_PASSWORD) >= 8) {
        apStarted = WiFi.softAP(AP_SSID, AP_PASSWORD);
    } else {
        Serial.println("AP password too short (<8 chars) or not set. Starting open AP with custom SSID.");
    }

    if (!apStarted) {
        apStarted = WiFi.softAP(AP_SSID);
    }

    if (!apStarted) {
        Serial.println("Failed to start Access Point!");
    } else {
        WiFi.softAPsetHostname("ouispy-foxhunter");
    }
    delay(2000); // Allow AP to fully initialize
    
    // Set timing AFTER AP initialization
    configStartTime = millis();
    lastConfigActivity = millis();
    
    Serial.println("✓ Access Point created successfully!");
    Serial.println("AP IP address: " + WiFi.softAPIP().toString());
    Serial.println("Config portal: http://" + WiFi.softAPIP().toString());
    Serial.println("==============================\n");
    
    // Web server routes
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        lastConfigActivity = millis();
        request->send(200, "text/html", generateConfigHTML());
    });
    
    server.on("/save", HTTP_POST, [](AsyncWebServerRequest *request){
        lastConfigActivity = millis();
        
        if (request->hasParam("targetMAC", true)) {
            String rawInput = request->getParam("targetMAC", true)->value();
            
            // Extract MAC from "ALIAS (MAC)" format if needed
            targetMAC = extractMAC(rawInput);
            targetMAC.trim();
            targetMAC.toUpperCase(); // Ensure consistent case for comparison
            
            // Process buzzer and LED toggles
            buzzerEnabled = request->hasParam("buzzerEnabled", true);
            ledEnabled = request->hasParam("ledEnabled", true);
            
            Serial.println("Received input: " + rawInput);
            Serial.println("Extracted target MAC: " + targetMAC);
            Serial.println("Buzzer enabled: " + String(buzzerEnabled ? "Yes" : "No"));
            Serial.println("LED enabled: " + String(ledEnabled ? "Yes" : "No"));
            saveConfiguration();
            
            String responseHTML = R"html(
<!DOCTYPE html>
<html>
<head>
    <title>Configuration Saved</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body { 
            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; 
            margin: 0; 
            padding: 20px;
            background: #1a1a1a; 
            color: #e0e0e0;
            text-align: center; 
        }
        .container { 
            max-width: 600px; 
            margin: 0 auto; 
            background: #2d2d2d; 
            padding: 40px; 
            border-radius: 12px; 
            box-shadow: 0 4px 20px rgba(0,0,0,0.3); 
        }
        h1 { 
            color: #ffffff; 
            margin-bottom: 30px; 
            font-weight: 300;
        }
        .success { 
            background: #1a4a3a; 
            color: #4ade80; 
            border: 1px solid #166534; 
            padding: 20px; 
            border-radius: 8px; 
            margin: 30px 0; 
        }
        p { 
            line-height: 1.6; 
            margin: 15px 0;
        }
    </style>
    <script>
        setTimeout(function() {
            document.getElementById('countdown').innerHTML = 'Switching to tracking mode now...';
        }, 5000);
    </script>
</head>
<body>
    <div class="container">
        <h1>Configuration Saved</h1>
        <div class="success">
            <p><strong>Target MAC configured successfully!</strong></p>
            <p id="countdown">Switching to tracking mode in 5 seconds...</p>
        </div>
        <p>The device will now start tracking your target device.</p>
        <p>When the target is found, you'll hear proximity beeps!</p>
    </div>
</body>
</html>
)html";
            
            request->send(200, "text/html", responseHTML);
            
            // Schedule mode switch for 5 seconds from now
            modeSwitchScheduled = millis() + 5000;
            
            Serial.println("Mode switch scheduled for 5 seconds from now");
            Serial.println("==============================\n");
        } else {
            request->send(400, "text/plain", "Missing target MAC");
        }
    });
    
    server.on("/clear", HTTP_POST, [](AsyncWebServerRequest *request){
        lastConfigActivity = millis();
        
        targetMAC = "";
        saveConfiguration();
        Serial.println("Target MAC cleared");
        
        request->send(200, "text/plain", "Target cleared");
    });
    
    server.on("/device-reset", HTTP_POST, [](AsyncWebServerRequest *request){
        lastConfigActivity = millis();
        request->send(200, "text/plain", "Device reset initiated");
        
        // Schedule device reset (non-blocking)
        deviceResetScheduled = millis() + 1000; // 1 second delay
    });
    
    server.on("/scan", HTTP_POST, [](AsyncWebServerRequest *request){
        lastConfigActivity = millis();
        
        if (scanInProgress) {
            request->send(200, "application/json", "[]");
            return;
        }
        
        scanInProgress = true;
        scanResults.clear();
        
        Serial.println("Starting BLE scan for device discovery...");
        
        try {
            // Initialize BLE if not already initialized
            if (!NimBLEDevice::getInitialized()) {
                Serial.println("Initializing BLE...");
                NimBLEDevice::init("");
                delay(500);
            }
            
            // Create scan object
            NimBLEScan* pScan = NimBLEDevice::getScan();
            if (pScan != nullptr) {
                pScan->setAdvertisedDeviceCallbacks(new ScanCallbacks(), false);
                pScan->setActiveScan(true);
                pScan->setInterval(100);
                pScan->setWindow(99);
                pScan->setDuplicateFilter(false);
                
                Serial.println("Starting BLE scan...");
                // Perform 3 second scan (shorter to avoid timeout)
                pScan->start(3, false);
                Serial.println("Scan complete");
                
                // Sort by RSSI (strongest first)
                std::sort(scanResults.begin(), scanResults.end(), [](const ScannedDevice& a, const ScannedDevice& b) {
                    return a.rssi > b.rssi;
                });
            }
        } catch (...) {
            Serial.println("BLE scan error");
            scanInProgress = false;
            request->send(200, "application/json", "[]");
            return;
        }
        
        // Build JSON response
        String json = "[";
        for (size_t i = 0; i < scanResults.size(); i++) {
            if (i > 0) json += ",";
            json += "{\"mac\":\"" + scanResults[i].mac + "\",\"rssi\":" + String(scanResults[i].rssi);
            if (scanResults[i].alias.length() > 0) {
                String escapedAlias = scanResults[i].alias;
                escapedAlias.replace("\"", "\\\"");
                json += ",\"alias\":\"" + escapedAlias + "\"";
            }
            json += "}";
        }
        json += "]";
        
        Serial.println("Found " + String(scanResults.size()) + " devices");
        
        scanInProgress = false;
        request->send(200, "application/json", json);
    });
    
    server.on("/set-alias", HTTP_POST, [](AsyncWebServerRequest *request){
        lastConfigActivity = millis();
        
        if (request->hasParam("mac", true) && request->hasParam("alias", true)) {
            String mac = request->getParam("mac", true)->value();
            String alias = request->getParam("alias", true)->value();
            
            mac.toUpperCase();
            alias.trim();
            
            setAlias(mac, alias);
            
            // Update alias in current scan results
            for (auto& device : scanResults) {
                if (device.mac == mac) {
                    device.alias = alias;
                    break;
                }
            }
            
            Serial.println("Alias set: " + mac + " = " + alias);
            request->send(200, "text/plain", "OK");
        } else {
            request->send(400, "text/plain", "Missing parameters");
        }
    });
    
    server.on("/get-results", HTTP_GET, [](AsyncWebServerRequest *request){
        lastConfigActivity = millis();
        
        // Return current scan results with updated aliases
        String json = "[";
        for (size_t i = 0; i < scanResults.size(); i++) {
            if (i > 0) json += ",";
            json += "{\"mac\":\"" + scanResults[i].mac + "\",\"rssi\":" + String(scanResults[i].rssi);
            if (scanResults[i].alias.length() > 0) {
                String escapedAlias = scanResults[i].alias;
                escapedAlias.replace("\"", "\\\"");
                json += ",\"alias\":\"" + escapedAlias + "\"";
            }
            json += "}";
        }
        json += "]";
        
        request->send(200, "application/json", json);
    });
    
    server.begin();
    Serial.println("Web server started!");
}

// BLE callback for device detection
class MyAdvertisedDeviceCallbacks: public NimBLEAdvertisedDeviceCallbacks {
    void onResult(NimBLEAdvertisedDevice* advertisedDevice) {
        if (currentMode != TRACKING_MODE) return;
        
        String deviceMAC = advertisedDevice->getAddress().toString().c_str();
        deviceMAC.toUpperCase();
        
        // Check if this is our target
        if (deviceMAC == targetMAC) {
            currentRSSI = advertisedDevice->getRSSI();
            lastTargetSeen = millis();
            
            // Set flags for main loop to handle
            targetDetected = true;
            newTargetDetected = true;
            Serial.print("DEBUG: Target detected, RSSI: ");
            Serial.println(currentRSSI);
        }
    }
};

void startTrackingMode() {
    if (targetMAC.length() == 0) {
        Serial.println("No target MAC configured, staying in config mode");
        return;
    }
    
    currentMode = TRACKING_MODE;
    
    // Reset session detection flag for new hunting session
    sessionFirstDetection = true;
    firstDetection = true;
    
    // Stop the web server
    server.end();
    
    Serial.println("\n==============================");
    Serial.println("=== STARTING FOXHUNT TRACKING MODE ===");
    Serial.print("Target MAC: ");
    Serial.println(targetMAC);
    Serial.println("==============================\n");
    
    // Initialize BLE
    NimBLEDevice::init("");
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);
    
    pBLEScan = NimBLEDevice::getScan();
    pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
    pBLEScan->setInterval(16);    // 16ms intervals (maximum speed)
    pBLEScan->setWindow(15);      // 15ms scan window (95% duty cycle)
    pBLEScan->setActiveScan(true);
    pBLEScan->setDuplicateFilter(false);
    
    // Start continuous scanning
    pBLEScan->start(0, nullptr, false);
    
    Serial.println("FOXHUNT REALTIME tracking started!");
    
    // Play startup ready signal
    ascendingBeeps();
}

void setup() {
    Serial.begin(115200);
    Serial.println("\n=== OUI-SPY FOXHUNT MODE for Xiao ESP32 S3 ===");
    Serial.println("Hardware: Xiao ESP32 S3");
    Serial.println("Buzzer: GPIO3");
    Serial.println("Target: Single MAC address");
    Serial.println("Mode: REALTIME RSSI-based proximity beeping");
    Serial.println("Range: 5s (WEAK) to 100ms (STRONG)");
    Serial.println("Initializing...\n");
    
    // Setup buzzer - initialize to 1kHz for proximity beeps
    ledcSetup(0, 1000, 8);  // 1kHz default frequency
    ledcAttachPin(BUZZER_PIN, 0);
    
    // Setup LED (inverted logic - HIGH = OFF for Xiao ESP32-S3)
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH);
    
    singleBeep(); // Startup test beep
    
    // Initialize WiFi FIRST before setting MAC
    WiFi.mode(WIFI_MODE_NULL);
    delay(100);
    
    // STEALTH MODE: Full MAC randomization
    uint8_t newMAC[6];
    WiFi.macAddress(newMAC);
    
    Serial.print("Original MAC: ");
    for (int i = 0; i < 6; i++) {
        Serial.printf("%02x", newMAC[i]);
        if (i < 5) Serial.print(":");
    }
    Serial.println();
    
    // STEALTH MODE: Randomize ALL 6 bytes for maximum anonymity
    randomSeed(analogRead(0) + micros());
    for (int i = 0; i < 6; i++) {
        newMAC[i] = random(0, 256);
    }
    // Ensure it's a valid locally administered address
    newMAC[0] |= 0x02; // Set locally administered bit
    newMAC[0] &= 0xFE; // Clear multicast bit
    
    // Set the randomized MAC for both STA and AP modes
    esp_wifi_set_mac(WIFI_IF_STA, newMAC);
    esp_wifi_set_mac(WIFI_IF_AP, newMAC);
    
    Serial.print("Randomized MAC: ");
    for (int i = 0; i < 6; i++) {
        Serial.printf("%02x", newMAC[i]);
        if (i < 5) Serial.print(":");
    }
    Serial.println();
    
    // Load configuration
    loadConfiguration();
    
    // Start in configuration mode
    startConfigMode();
}

void loop() {
    unsigned long currentTime = millis();
    
    // Handle scheduled mode switch
    if (modeSwitchScheduled > 0 && currentTime >= modeSwitchScheduled) {
        modeSwitchScheduled = 0;
        startTrackingMode();
        return;
    }
    
    // Handle scheduled device reset
    if (deviceResetScheduled > 0 && currentTime >= deviceResetScheduled) {
        deviceResetScheduled = 0;
        Serial.println("Device reset triggered");
        
        // Clear NVS and restart
        preferences.begin("tracker", false);
        preferences.clear();
        preferences.end();
        
        delay(1000);
        ESP.restart();
        return;
    }
    
    if (currentMode == CONFIG_MODE) {
        // Check for config timeout only if no recent activity AND no connected clients
        int connectedClients = WiFi.softAPgetStationNum();
        if (currentTime - lastConfigActivity > CONFIG_TIMEOUT && connectedClients == 0) {
            Serial.println("Configuration timeout - switching to tracking mode with saved config");
            startTrackingMode();
        }
    } 
    else if (currentMode == TRACKING_MODE) {
        unsigned long currentTime = millis();
        
        // Handle target detection messages (safe serial output)
        if (newTargetDetected) {
            newTargetDetected = false;
            
            // Only play three same-tone beeps on FIRST detection of hunting session
            if (sessionFirstDetection) {
                threeSameToneBeeps();
                sessionFirstDetection = false;
                Serial.println("TARGET ACQUIRED!");
            } else if (firstDetection) {
                // Silent reacquisition after loss
                firstDetection = false;
                Serial.println("TARGET REACQUIRED!");
            }
        }
        
        // Handle proximity beeping
        if (targetDetected && (currentTime - lastTargetSeen < 5000)) { // Target seen within last 5 seconds
            handleProximityBeeping();
            
            // Print RSSI for visual fox hunting feedback (reduced frequency for real-time performance)
            static unsigned long lastRSSIPrint = 0;
            int printInterval = 2000; // Fixed 2-second intervals - less serial spam
            
            if (currentTime - lastRSSIPrint >= printInterval) {
                Serial.print("RSSI: ");
                Serial.print(currentRSSI);
                Serial.println(" dBm");
                lastRSSIPrint = currentTime;
            }
        } else if (currentTime - lastTargetSeen >= 5000) {
            // Target lost - INSTANT LED OFF for maximum reactivity
            targetDetected = false;
            firstDetection = true; // Reset for next detection
            
            // Turn off beep and LED immediately
            if (buzzerEnabled) {
                ledcWrite(0, 0);
            }
            ledOff();
            isBeeping = false;
            
            Serial.println("TARGET LOST - Searching...");
        }
        
        return;
    }
} 
