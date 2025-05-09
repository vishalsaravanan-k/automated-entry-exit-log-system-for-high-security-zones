#include <WiFi.h>
#include <PubSubClient.h>
#include "esp_camera.h"
#include <HTTPClient.h>

// Wi-Fi Credentials
const char* ssid = "vishal";
const char* password = "12345678";

// MQTT Broker
const char* mqtt_server = "192.168.164.159";

// MQTT Topics
const char* capture_topic = "camera/trigger";
const char* ack_topic = "camera/ack";

String currentMetadata = "";

// HTTP Upload URL
const char* serverName = "http://192.168.164.159:5000/upload";

enum CameraState { CAM_IDLE, CAM_CAPTURING, CAM_UPLOADING };
CameraState camState = CAM_IDLE;
camera_fb_t* fb = NULL;

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

// AI-Thinker ESP32-CAM pinmap
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

void setupWiFi() {
  Serial.print("[WIFI] Connecting to ");
  Serial.println(ssid);
  
  WiFi.begin(ssid, password, 6); // Channel 6
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if(WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[WIFI] Connected!");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\n[WIFI] Connection Failed!");
    ESP.restart();
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String msg;
  for(int i=0; i<length; i++) msg += (char)payload[i];
  
  if(String(topic) == capture_topic) {
    if(camState == CAM_IDLE) {
      currentMetadata = msg;
      camState = CAM_CAPTURING;
    }
  }
}

void connectMQTT() {
  while(!mqttClient.connected()) {
    Serial.print("[MQTT] Connecting...");
    
    if(mqttClient.connect("ESP32-CAM", "", "", 0, 0, 0, 0, 1)) {
      mqttClient.subscribe(capture_topic, 1);
      Serial.println("connected!");
      mqttClient.publish(ack_topic, "cam_ready");
    } else {
      Serial.print("failed rc=");
      Serial.print(mqttClient.state());
      Serial.println(", retrying...");
      delay(2000);
    }
  }
}

void setupCamera() {
  camera_config_t config;
  
  // Validate PSRAM availability
  bool hasPsram = psramFound();
  Serial.printf("PSRAM: %s\n", hasPsram ? "Yes" : "No");

  // Camera pin configuration
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  // Adaptive memory settings
  if(hasPsram) {
    config.frame_size = FRAMESIZE_SVGA; // 800x600
    config.jpeg_quality = 12;
    config.fb_count = 2;
    config.fb_location = CAMERA_FB_IN_PSRAM;
  } else {
    config.frame_size = FRAMESIZE_VGA;  // 640x480
    config.jpeg_quality = 10;
    config.fb_count = 1;
    config.fb_location = CAMERA_FB_IN_DRAM;
  }

  esp_err_t err = esp_camera_init(&config);
  if(err != ESP_OK) {
    Serial.printf("Camera init failed: 0x%x\n", err);
    ESP.restart();
  }
}

void handleUpload() {
  static HTTPClient http;
  static bool uploadStarted = false;
  
  if(!uploadStarted) {
    http.begin(serverName);
    http.addHeader("Content-Type", "image/jpeg");
    
    // Generate filename from metadata
    String filename = currentMetadata;
    filename.replace(',', '_');
    filename += ".jpg";
    http.addHeader("X-Filename", filename.c_str());
    
    uploadStarted = true;
  }

  int httpCode = http.sendRequest("POST", fb->buf, fb->len);
  
  if(httpCode > 0) {
    Serial.printf("Uploaded %d bytes\n", fb->len);
    mqttClient.publish(ack_topic, "image_uploaded");
  } else {
    Serial.println("Upload failed");
    mqttClient.publish(ack_topic, "upload_error");
  }
  
  http.end();
  esp_camera_fb_return(fb);
  fb = NULL;
  uploadStarted = false;
  currentMetadata = ""; // Clear metadata after upload
}

void setup() {
  Serial.begin(115200); // Start Serial Monitor
  setupWiFi();          // Connect to Wi-Fi
  setupCamera();        // Initialize Camera
  mqttClient.setServer(mqtt_server, 1883); // Set MQTT Broker
  mqttClient.setCallback(mqttCallback);    // Set MQTT Message Callback
}



void loop() {
  if(!mqttClient.connected()) connectMQTT();
  mqttClient.loop();

  switch(camState) {
    case CAM_IDLE:
      break;

    case CAM_CAPTURING:
    fb = esp_camera_fb_get();
    if(fb) {
    Serial.printf("Captured %d bytes\n", fb->len);
    camState = CAM_UPLOADING;
    } else {
    Serial.println("Capture failed");
    currentMetadata = ""; // Clear metadata on failure
    camState = CAM_IDLE;
    }
    break;

    case CAM_UPLOADING:
      handleUpload();
      camState = CAM_IDLE;
      break;
  }
  
  delay(10);
}

