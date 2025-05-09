#include <Adafruit_Fingerprint.h>
#include <HardwareSerial.h>
#include <SPI.h>
#include <SD.h>
#include <MFRC522.h>
#include <WiFi.h>
#include "time.h"
#include <PubSubClient.h>
#include <vector>
#include <string> 
#include <Wire.h>
#include <U8g2lib.h>

// OLED and Button Definitions
#define BUTTON_UP    14
#define BUTTON_DOWN  27
#define BUTTON_OK    26

U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

// Setup for UART2: RX = GPIO17, TX = GPIO16
HardwareSerial mySerial(2);
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&mySerial);

// SD card CS pin
#define SD_CS 5

#define SS_PIN 15     // SDA pin
#define RST_PIN 4   // Reset pin

MFRC522 mfrc522(SS_PIN, RST_PIN);

// Replace with your WiFi credentials
const char* ssid     = "vishal";
const char* password = "12345678";

// NTP Server
const char* ntpServer = "pool.ntp.org";

// Set timezone offset (in seconds)
const long  gmtOffset_sec = 19800; // IST = UTC + 5 hours 30 minutes = 5*3600 + 30*60 = 19800
const int   daylightOffset_sec = 0; // No daylight savings in India

// MQTT broker settings
const char* mqtt_server = "192.168.164.159";  // Your MQTT broker IP
const int mqtt_port = 1883;  // MQTT port (default is 1883)

// MQTT Topics
const char* mqtt_new_entry = "esp32/new_entry"; // MQTT topic to publish to flask server
const char* mqtt_new_exit = "court/access"; 	// MQTT topic to publish to flask server
const char* capture_topic = "camera/trigger"; 	// MQTT topic to publish to esp32-cam
const char* ack_topic = "camera/ack"; 			// MQTT topic to receive from esp32-cam
const char* metadata_topic = "camera/metadata"; 	// MQTT topic to publish to flask server

WiFiClient espClient;
PubSubClient client(espClient);

// Store extracted details
String Name, Age, Gender, DOB, A_No, Image, P_No, Address;
String currentDate = "";
String currentTime = "";
String currentDay = "";

bool captureTriggered = false; 
int globalFingerprintID; 

// Timeout variables
unsigned long ackTimeout = 0;
const long ackTimeoutDuration = 10000; // 10 seconds

enum CameraState { CAM_IDLE, CAM_WAITING_ACK, CAM_SUCCESS, CAM_FAILURE };
CameraState cameraState = CAM_IDLE;
int captureRetries = 0;
const int MAX_RETRIES = 3;

// Menu Configuration
const char* menuItems[] = {"Entry Process", "Exit Process"};
const int menuLength = sizeof(menuItems) / sizeof(menuItems[0]);
int selectedMenuItem = 0;

enum SystemState { 
  MAIN_MENU, 
  IN_ENTRY, 
  IN_EXIT,
  POST_ENTRY,
  POST_EXIT
};
SystemState currentState = MAIN_MENU;

unsigned long processEndTime = 0;
const unsigned long returnWindow = 3000; // 3-second window

// Function to identify fingerprint
int getFingerprintID() {
  uint8_t p = finger.getImage();
  if (p != FINGERPRINT_OK) return -1;

  p = finger.image2Tz();
  if (p != FINGERPRINT_OK) return -1;

  p = finger.fingerSearch();
  if (p != FINGERPRINT_OK) return -1;

  return finger.fingerID;
}

void setup_wifi() {
  // Connect to Wi-Fi
  Serial.println();
  Serial.print("Connecting to Wi-Fi...");
  WiFi.begin(ssid, password, 6);

  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }
  Serial.println("Connected to Wi-Fi!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

// Function to read user details from a_d.csv based on Fingerprint ID
void getUserDetailsFromCSV(int fingerprintID) {
  if (!SD.begin(SD_CS)) {
    Serial.println("SD card initialization failed!");
    return;
  }

  File file = SD.open("/a_d.csv");
  if (!file) {
    Serial.println("Failed to open a_d.csv");
    return;
  }

  bool found = false;
  while (file.available()) {
    String line = file.readStringUntil('\n');
    line.trim();
    if (line.startsWith("F_ID")) continue; // Skip header

    int index = line.indexOf(',');
    if (index == -1) continue;

    int fid = line.substring(0, index).toInt();

    if (fid == fingerprintID) {
      Serial.println("✅ User Found:");
      Serial.println("-------------");

      // Print each field nicely
      int fieldIndex = 0;
      int prevComma = -1;
      int nextComma;

      String labels[] = {"F_ID", "Name", "Age", "Gender", "DOB", "A_No", "Image", "P_No", "Address"};

      for (int i = 0; i < 8; i++) {
        nextComma = line.indexOf(',', prevComma + 1);
        String field = line.substring(prevComma + 1, nextComma);
        Serial.print(labels[i]);
        Serial.print(": ");
        Serial.println(field);
        prevComma = nextComma;
      }

      // Last field (Address)
      String lastField = line.substring(prevComma + 1);
      Serial.print(labels[8]);
      Serial.print(": ");
      Serial.println(lastField);

      found = true;
      break;
    }
  }

  if (!found) {
    Serial.println("❌ User not found for the given fingerprint ID.");
  }

  file.close();
}

// 🧠 FUNCTION: Read RFID UID with timeout (5 seconds)
String readRFIDTag() {
  unsigned long startTime = millis();
  while (millis() - startTime < 5000) {  // Retry for 5 seconds
    if (!mfrc522.PICC_IsNewCardPresent()) {
      delay(100);
      continue;
    }

    if (!mfrc522.PICC_ReadCardSerial()) {
      Serial.println("Card present, but failed to read serial.");
      u8g2.clearBuffer();
  u8g2.drawStr(2, 15, "Card Present,");
  u8g2.drawStr(2, 30, "But failed to read");
  u8g2.sendBuffer();
      delay(100);
      continue;
    }

    String uidStr = "";
    for (byte i = 0; i < mfrc522.uid.size; i++) {
      uidStr += String(mfrc522.uid.uidByte[i], HEX);
    }

    uidStr.toUpperCase();
    mfrc522.PICC_HaltA();  // Halt the card
    return uidStr;
  }

  // Timeout
  return "";
}

bool readDetailsFromCSV(int fid) {
  File csvFile = SD.open("/a_d.csv");
  if (!csvFile) {
    Serial.println("Failed to open a_d.csv");
    return false;
  }

  while (csvFile.available()) {
    String line = csvFile.readStringUntil('\n');
    line.trim();

    if (line.length() == 0) continue;

    int firstComma = line.indexOf(',');
    if (firstComma == -1) continue;

    // Get current F_ID from line
    String fidStr = line.substring(0, firstComma);
    fidStr.trim();
    int currentFID = fidStr.toInt();

    // Compare as integers
    if (currentFID == fid) {
      // Parse the rest of the fields
      int index = 0;
      int lastIndex = 0;
      String values[9];
      for (int i = 0; i < 8; i++) {
        index = line.indexOf(',', lastIndex);
        if (index == -1) break;
        values[i] = line.substring(lastIndex, index);
        lastIndex = index + 1;
      }
      values[8] = line.substring(lastIndex); // Last field

      Name = values[1];
      Age = values[2];
      Gender = values[3];
      DOB = values[4];
      A_No = values[5];
      Image = values[6];
      P_No = values[7];
      Address = values[8];

      csvFile.close();
      return true;
    }
  }

  csvFile.close();
  return false;
}

void switchToRFID() {
  digitalWrite(SD_CS, HIGH);    // Disable SD
  digitalWrite(SS_PIN, LOW);    // Enable RFID

  mfrc522.PCD_Init();
  mfrc522.PCD_SetAntennaGain(mfrc522.RxGain_max);  // Optional boost
  delay(50);
}

void switchToSD() {
  digitalWrite(SS_PIN, HIGH);   // Disable RFID
  digitalWrite(SD_CS, LOW);     // Enable SD
  SD.begin(SD_CS);              // Re-init SD
  delay(50);
}

void entryProcess() {
	
  // ==== Fingerprint Scanning ====
  int id = getFingerprintID();
  if (id != -1) {
	globalFingerprintID = id;  // Store in global variable
    Serial.print("Fingerprint matched! ID: ");
    Serial.println(id);
    u8g2.clearBuffer();
  u8g2.drawStr(2, 15, "Fingerprint matched!");
  u8g2.sendBuffer();
  } else {
    Serial.println("No match found.");
    u8g2.clearBuffer();
  u8g2.drawStr(2, 15, "No match found");
  u8g2.sendBuffer();
    delay(2000);
    Serial.println("Place a registered finger to identify...");
    return;
  }
  
  // ==== Getting user details from fingerprint Id
  
  getUserDetailsFromCSV(globalFingerprintID);

  delay(1000);

  // ==== Time ====
  time();
  String timestamp = currentTime;
  
  client.loop();
  
  // Send metadata only on success
        String metadata = String(globalFingerprintID) + "," + timestamp;
        client.publish(metadata_topic, metadata.c_str());
        delay(1000);
        client.publish(capture_topic, "capture");
        Serial.println("Capture command sent");
        u8g2.clearBuffer();
  u8g2.drawStr(2, 15, "Camera capturing");
  
  u8g2.sendBuffer();
        client.subscribe(ack_topic);
          client.loop();

        
  
  
  // ==== RFID Issueing
  switchToRFID();  // Before reading RFID
  Serial.println("Waiting for RFID card (5s timeout)...");
  u8g2.clearBuffer();
  u8g2.drawStr(2, 15, "RFID Card");
  u8g2.drawStr(2, 30, "Issuing...");
  u8g2.sendBuffer();
  String rfidTag = readRFIDTag();
  switchToSD();    // Switch back after reading
    client.loop();


  if (rfidTag == "") {
  Serial.println("RFID read failed or tag not present.");
  u8g2.clearBuffer();
  u8g2.drawStr(2, 15, "RFID read");
  u8g2.drawStr(2, 30, "failed");
  u8g2.sendBuffer();
  return;
  }
  Serial.print("RFID Tag Detected: ");
  u8g2.clearBuffer();
  u8g2.drawStr(2, 15, "RFID Detected.");
  u8g2.sendBuffer();
  Serial.println(rfidTag);
    client.loop();



  // Read details from a_d.csv
  if (!readDetailsFromCSV(globalFingerprintID)) {
    Serial.println("F_ID not found in a_d.csv!");
    u8g2.clearBuffer();
  u8g2.drawStr(2, 15, "F_ID not found");
  u8g2.drawStr(2, 30, "in a_d.csv!");
  u8g2.sendBuffer();
    return;
  }
  delay(1000);

    client.loop();



  // Create full log entry
  String logEntry = String("\n") + globalFingerprintID + "," + Name + "," + Age + "," + Gender + "," + DOB + "," +
                    A_No + "," + Image + "," + P_No + "," + Address + "," + rfidTag + "," +
                    currentDate + "," + timestamp + ",," + currentDay + "," + globalFingerprintID + "_" + timestamp + ".jpg" + "\n";
                    
  /* // Modify in entryProcess()
String logEntry = String("\n") + 
  globalFingerprintID + "," + 
  Name + "," + 
  (cameraState == CAM_SUCCESS ? "HAS_IMAGE" : "NO_IMAGE") + "," + // Add capture status
  // ... rest of fields*/
  
  delay(1000);

  // Append to log.csv
  File logFile = SD.open("/log.csv", FILE_APPEND);
  if (logFile) {
    logFile.print(logEntry);
    logFile.close();
    Serial.println("Log entry added:");
    u8g2.clearBuffer();
  u8g2.drawStr(2, 15, "Log Entry Added.");
  u8g2.sendBuffer();
    Serial.println(logEntry);
  } else {
    Serial.println("Failed to open log.csv for writing");
    u8g2.clearBuffer();
  u8g2.drawStr(2, 15, "Failed to open");
  u8g2.drawStr(2, 30, "log.csv for writing.");
  u8g2.sendBuffer();
  }
  
  // ==== Sending New Log Entry To Flask Server ====
  send_log_entry(logEntry);
  
  // Reset flag after full cycle
  captureTriggered = false;

  u8g2.clearBuffer();
  u8g2.drawStr(2, 15, "Entry Sucessful.");
  u8g2.drawStr(2, 30, "You can go.");
  u8g2.sendBuffer();
  delay(1000);
 
}

// The Exit process functions
// ========== FUNCTION 3 ==========
bool find_and_update_exit_time(String rfidTag, String exitTime) {
  File file = SD.open("/log.csv", FILE_READ);
  if (!file) {
    Serial.println("Cannot open log.csv");
    return false;
  }

  std::vector<String> lines;
  while (file.available()) {
    String line = file.readStringUntil('\n');
    line.trim(); // Remove extra \r or whitespace
    if (line.length() > 0) {
      lines.push_back(line);
    }
  }
  file.close();

  bool updated = false;
  for (int i = 1; i < lines.size(); i++) { // Skip header
    String line = lines[i];
    std::vector<String> fields;
    int start = 0, end = 0;

    for (int j = 0; j < 14; j++) {
      end = line.indexOf(',', start);
      if (end == -1) end = line.length();
      fields.push_back(line.substring(start, end));
      start = end + 1;
    }

    // Match RFID and check if exit time is empty
    if (fields[9].equalsIgnoreCase(rfidTag) && fields[12].length() == 0) {
      fields[12] = exitTime;
      line = "";
      for (int k = 0; k < fields.size(); k++) {
        line += fields[k];
        if (k < fields.size() - 1) line += ",";
      }
      lines[i] = line;
      updated = true;
      break;
    }
  }

  if (updated) {
    File writeFile = SD.open("/log.csv", FILE_WRITE);
    if (!writeFile) {
      Serial.println("Failed to open log.csv for writing");
      return false;
    }

    // Clear previous content
    writeFile.seek(0);
    writeFile.print("");

    // Write updated lines
    for (String& l : lines) {
      l.trim();
      if (l.length() > 0) {
        writeFile.println(l);
      }
    }
    writeFile.close();
  }

  return updated;
}

// ========== FUNCTION 4 ==========
void exitProcess() {
  String rfid = readRFIDTag();
  if (rfid == "") {
    Serial.println("No RFID detected.");
    u8g2.clearBuffer();
  u8g2.drawStr(2, 15, "No RFID Detected.");
  u8g2.sendBuffer();
    return;
  }

  Serial.println("RFID: " + rfid);
  u8g2.clearBuffer();
  u8g2.drawStr(2, 15, "RFID Detected.");
  u8g2.sendBuffer();
  
  time();

  String exitTime = currentTime;
  if (exitTime == "") return;

  if (find_and_update_exit_time(rfid, exitTime)) {
    Serial.println("Exit time updated successfully.");
  } else {
    Serial.println("RFID not found or already exited.");
  }
  
  // Build the CSV exit message with only RFID, Date, Ex_Time, Day filled
  String message = rfid + "," + currentDate + "," + currentTime + "," + currentDay;
  
  sendExitLog(message);
  u8g2.clearBuffer();
  u8g2.drawStr(2, 15, "Exit successful.");
  u8g2.drawStr(2, 30, "You can go.");
  u8g2.sendBuffer();
  delay(1000);
  
}

void time() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time.");
    return;
  }

  char timeStr[9];   // HH:MM:SS
  char dateStr[11];  // YYYY-MM-DD
  char dayStr[10];   // Day name (e.g., Monday)

  strftime(timeStr, sizeof(timeStr), "%H:%M:%S", &timeinfo);
  strftime(dateStr, sizeof(dateStr), "%Y-%m-%d", &timeinfo);
  strftime(dayStr, sizeof(dayStr), "%A", &timeinfo);

  currentTime = String(timeStr);
  currentDate = String(dateStr);
  currentDay = String(dayStr);

  Serial.println("Current Time: " + currentTime);
  Serial.println("Current Date: " + currentDate);
  Serial.println("Current Day: " + currentDay);
}

void reconnect() {
  // Loop until we're connected to MQTT
  while (!client.connected()) {
    Serial.print("Connecting to MQTT...");
    if (client.connect("ESP32Client")) {
      Serial.println("connected");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      delay(2000);
    }
  }
}

void send_log_entry(const String& logData) {
	// Ensure the client remains connected to MQTT
  if (!client.connected()) {
    reconnect();
  }

  client.loop();

  // Publish the log entry to MQTT broker
  if (client.publish(mqtt_new_entry, logData.c_str())) {
    Serial.println("Log entry sent to MQTT broker");
  } else {
    Serial.println("Failed to send log entry");
  }
}

void sendExitLog(const String& exitData) {
	
	if (!client.connected()) {
    reconnect();
	}
	client.loop();

  Serial.print("Sending MQTT Message: ");
  Serial.println(exitData);
  
  client.publish(mqtt_new_exit, exitData.c_str());
}

void connectMQTT() {
	client.setKeepAlive(60);  // Add this line
	client.setSocketTimeout(30); 
  while (!client.connected()) {
    Serial.print("[MQTT] Connecting to broker...");
    if (client.connect("ESP32-WROOM")) {
      Serial.println(" connected.");
      client.loop();
      delay(100);
      client.subscribe(ack_topic);
      Serial.printf("[MQTT] Subscribed to: %s\n", ack_topic);
    } else {
      Serial.print(" failed. rc=");
      Serial.print(client.state());
      Serial.println(" retrying in 3s...");
      delay(3000);
    }
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
    String topicStr = String(topic);
    String msg;

    for (int i = 0; i < length; i++) {
        msg += (char)payload[i];
    }

    if (topicStr == ack_topic) {
        if (msg == "image_uploaded") {
            Serial.println("ACK Received: Image Uploaded");
     
        }
        else if (msg == "upload_error") {
            Serial.println("ACK Received: Upload Error");
            
        }
    }
}

// oled display functions =====================
void handleMainMenu() {
  u8g2.clearBuffer();
  drawMenu();
  u8g2.sendBuffer();

  handleMenuNavigation();
}

void handleMenuNavigation() {
  if (digitalRead(BUTTON_UP) == LOW) {
    selectedMenuItem = (selectedMenuItem - 1 + menuLength) % menuLength;
    delay(200);
  }

  if (digitalRead(BUTTON_DOWN) == LOW) {
    selectedMenuItem = (selectedMenuItem + 1) % menuLength;
    delay(200);
  }

  if (digitalRead(BUTTON_OK) == LOW) {
    currentState = (selectedMenuItem == 0) ? IN_ENTRY : IN_EXIT;
    delay(200); // Debounce
  }
}

void executeEntryProcess() {
  showProcessScreen("Running Entry...");
  entryProcess(); // Your existing entry function
  currentState = POST_ENTRY;
  processEndTime = millis();
}

void executeExitProcess() {
  showProcessScreen("Running Exit...");
  exitProcess(); // Your existing exit function
  currentState = POST_EXIT;
  processEndTime = millis();
}

void handlePostProcess() {
   if(millis() - processEndTime <= returnWindow) { 
  showReturnPrompt();
  
  // Check for OK press or timeout
  if (digitalRead(BUTTON_OK) == LOW) {
    currentState = MAIN_MENU;
    delay(200); // Debounce
    return;
  }

  // Check if 3 seconds have passed
  if (millis() - processEndTime >= returnWindow) {
    // Return to respective process
    currentState = (currentState == POST_ENTRY) ? IN_ENTRY : IN_EXIT;
  }
}
}

void showReturnPrompt() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.drawStr(10, 20, "Process Complete!");
  u8g2.drawStr(10, 40, "OK to return");
  
  // Show remaining time
  unsigned long remaining = returnWindow - (millis() - processEndTime);
  u8g2.setCursor(10, 60);
  u8g2.print("Auto-continue in: ");
  u8g2.print(remaining / 1000);
  u8g2.print("s");
  
  u8g2.sendBuffer();
}

void drawMenu() {
  u8g2.setFont(u8g2_font_6x12_tr);
  for (int i = 0; i < menuLength; i++) {
    if (i == selectedMenuItem) {
      u8g2.drawStr(5, (i+1)*15, ">");
    }
    u8g2.drawStr(20, (i+1)*15, menuItems[i]);
  }
}

// OLED Display Helpers
void showProcessScreen(const char* message) {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.drawStr(10, 20, message);
  u8g2.drawStr(10, 40, "OK to cancel");
  u8g2.sendBuffer();
}

void showStatusScreen(const char* message, int duration) {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.drawStr(10, 30, message);
  u8g2.sendBuffer();
  delay(duration);
  u8g2.clearDisplay();
}

void setup() {
  Serial.begin(115200);
  Serial.println("Serial initialized"); // Add this line
  delay(1000);

  // Initialize OLED
  Wire.begin(21, 22); // SDA=GPIO21, SCL=GPIO22
  u8g2.begin();

  // Initialize buttons
  pinMode(BUTTON_UP, INPUT_PULLUP);
  pinMode(BUTTON_DOWN, INPUT_PULLUP);
  pinMode(BUTTON_OK, INPUT_PULLUP);

  
  
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawStr(2, 15, "Automated Entry-Exit");
  u8g2.drawStr(2,30, "Log System For");
  u8g2.drawStr(2, 45, "High-Security Zones");
  u8g2.sendBuffer();

  mySerial.begin(57600, SERIAL_8N1, 16, 17);
  delay(2000);  // Sensor connected to GPIO17 (RX), GPIO16 (TX)
  
  // WiFi configuration
  setup_wifi();
  
  // Set up MQTT client
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(mqttCallback);
  client.setBufferSize(2048); // For both devices
  
  // Wait until the client is connected to MQTT
  reconnect();

  // Initialize the fingerprint sensor with baudrate
  finger.begin(57600);
  if (finger.verifyPassword()) {
    Serial.println("Fingerprint sensor found.");
    u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawStr(2, 15, "Fingerprint Sensor");
  u8g2.drawStr(2, 30, "Initialized OK");
  u8g2.sendBuffer();
  delay(500);
  } else {
    Serial.println("Fingerprint sensor not found.");
    u8g2.clearBuffer();
  u8g2.drawStr(2, 15, "Fingerprint Sensor");
  u8g2.drawStr(2, 30, "not found.");
  u8g2.sendBuffer();
    while (1);
  }

  Serial.println("Place a registered finger to identify...");
  

  // SD Card setup
  if (!SD.begin(SD_CS)) {
    Serial.println("SD card initialization failed!");
    u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawStr(2, 15, "SD Card Init");
  u8g2.drawStr(2, 30, "Failed!");
  u8g2.sendBuffer();
  delay(1000);
    return;
  }
  Serial.println("SD card initialized.");
  


  // RFID Initialization
  SPI.begin();               // Init SPI bus
  mfrc522.PCD_Init();        // Init MFRC522 module
  Serial.println("RFID reader ready.");

  // Initialize NTP
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer, "time.nist.gov", "time.google.com");

  
  
  delay(5000);
  
}


void loop() {
  switch(currentState) {
    case MAIN_MENU:
      handleMainMenu();
      break;
      
    case IN_ENTRY:
      executeEntryProcess();
      break;
      
    case IN_EXIT:
      executeExitProcess();
      break;
      
    case POST_ENTRY:
    case POST_EXIT:
      handlePostProcess();
      break;
  }

  // Maintain MQTT connection
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

}
