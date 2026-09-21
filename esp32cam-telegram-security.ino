/*
  ESP32-CAM Telegram Security Camera ("Smart Sentry")
  Author: Faycal Toumi

  Motion (HC-SR501 PIR) -> photo (ESP32-CAM) -> Telegram alert.
  Remote control through Telegram bot commands.
  See README.md for wiring, setup and the command list.
*/

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "esp_camera.h"
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>

// ===========================
// 1. CREDENTIALS (kept out of the repo)
// Copy secrets.h.example to secrets.h and fill in your own values.
// ===========================
#include "secrets.h"
const char* ssid = WIFI_SSID;
const char* password = WIFI_PASSWORD;
// BOT_TOKEN and CHAT_ID are defined in secrets.h

// ===========================
// 2. TIMERS, FLAGS & SENSOR SETUP
// ===========================
#define PIR_PIN 13
volatile bool motionDetected = false;

// Cooldown Variables
unsigned long lastPhotoTime = 0;
const unsigned long cooldownTime = 300000; // 5 minutes in milliseconds
bool isFirstTrigger = true; 

// Mute Variables
bool isMuted = false; 
unsigned long muteDuration = 0;   // How long to stay muted (0 = forever)
unsigned long muteStartTime = 0;  // When the mute started

// Manual Photo Flag
bool requestPhoto = false;

// Telegram Polling Timer
int botRequestDelay = 1000;
unsigned long lastTimeBotRan;

// ===========================
// 3. CAMERA PINS (AI Thinker)
// ===========================
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

WiFiClientSecure client;
UniversalTelegramBot bot(BOT_TOKEN, client);

// Interrupt function for the PIR sensor
void IRAM_ATTR detectsMovement() {
  motionDetected = true;
}

// Memory buffer functions required by UniversalTelegramBot
camera_fb_t *fb = NULL;
int currentByte = 0;

bool isMoreDataAvailable() {
  return (currentByte < fb->len);
}

byte getNextByte() {
  byte b = fb->buf[currentByte];
  currentByte++;
  return b;
}

// ===========================
// 4. CAPTURE & SEND FUNCTION
// ===========================
void captureAndSendPhoto(String alertMessage) {
  Serial.println("Capturing image...");
  fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Camera capture failed");
    bot.sendMessage(CHAT_ID, "❌ Error: Camera failed to capture image.", "");
    return;
  }

  currentByte = 0;
  bot.sendMessage(CHAT_ID, alertMessage, "");
  
  bot.sendPhotoByBinary(CHAT_ID, "image/jpeg", fb->len,
                        isMoreDataAvailable, getNextByte,
                        nullptr, nullptr);

  esp_camera_fb_return(fb); // Free memory
}

// ===========================
// 5. TELEGRAM COMMAND HANDLER
// ===========================
void handleNewMessages(int numNewMessages) {
  for (int i = 0; i < numNewMessages; i++) {
    String chat_id = String(bot.messages[i].chat_id);
    if (chat_id != CHAT_ID) continue; // Ignore unauthorized users

    String text = bot.messages[i].text;
    Serial.println("Received command: " + text);

    if (text == "/photo") {
      requestPhoto = true; // Tell the main loop to snap a picture ASAP
    } 
    else if (text.startsWith("/mute")) {
      String timeStr = text.substring(5); // Grab whatever is typed after "/mute"
      timeStr.trim(); // Remove extra spaces
      
      isMuted = true;
      muteStartTime = millis();

      if (timeStr.length() > 0) {
        int minutes = timeStr.toInt();
        if (minutes > 0) {
          muteDuration = minutes * 60000; // Convert minutes to milliseconds
          bot.sendMessage(CHAT_ID, "🔇 Muted for " + String(minutes) + " minute(s).", "");
        } else {
          bot.sendMessage(CHAT_ID, "⚠️ Invalid number. Use '/mute 5' or just '/mute' for indefinite.", "");
          isMuted = false; // Cancel mute if invalid
        }
      } else {
        muteDuration = 0; // 0 means muted indefinitely
        bot.sendMessage(CHAT_ID, "🔇 Muted indefinitely. Type /resume to wake me up.", "");
      }
    } 
    else if (text == "/resume") {
      isMuted = false;
      muteDuration = 0;
      lastPhotoTime = 0;     // Wipes the 5-minute cooldown clock!
      isFirstTrigger = true; // Guarantees an instant photo on next movement
      bot.sendMessage(CHAT_ID, "🔊 RESUMED. Cooldown reset! Watching for movement...", "");
    } 
    else if (text == "/start" || text == "/help") {
      String welcome = "🛡️ Smart Sentry Commands:\n\n";
      welcome += "📸 /photo : Instantly snap and send a picture\n";
      welcome += "🔇 /mute : Disable alerts indefinitely\n";
      welcome += "⏱️ /mute X : Disable alerts for X minutes (e.g., /mute 15)\n";
      welcome += "🔊 /resume : Enable alerts & reset cooldown timer\n";
      bot.sendMessage(CHAT_ID, welcome, "");
    }
  }
}

void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); 
  Serial.begin(115200);
  
  pinMode(PIR_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIR_PIN), detectsMovement, RISING);

  // ========================================================
  // 1. INITIALIZE CAMERA FIRST (To secure its RAM budget)
  // ========================================================
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM; config.pin_d1 = Y3_GPIO_NUM; config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM; config.pin_d4 = Y6_GPIO_NUM; config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM; config.pin_d7 = Y9_GPIO_NUM; config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM; config.pin_vsync = VSYNC_GPIO_NUM; config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM; config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM; config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000; config.pixel_format = PIXFORMAT_JPEG;

  // FORCED LOW-MEMORY CONFIGURATION
  config.frame_size = FRAMESIZE_VGA; // 640x480 resolution
  config.jpeg_quality = 15;          // Higher number = smaller file size / less RAM used
  config.fb_count = 1;               // Strict 1 frame buffer
  
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x\n", err);
  } else {
    Serial.println("Camera successfully initialized!");
  }

  // ========================================================
  // 2. CONNECT TO WI-FI & TELEGRAM AFTER CAMERA IS SAFE
  // ========================================================
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  client.setCACert(TELEGRAM_CERTIFICATE_ROOT);
  
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) { 
    delay(1000); 
    Serial.print(".");
  }
  Serial.println("\nConnected to Wi-Fi!");
  
  bot.sendMessage(CHAT_ID, "✅ Smart Sentry Armed! Type /help to see commands.", "");
}

void loop() {
  // 1. Check for Telegram commands
  if (millis() - lastTimeBotRan > botRequestDelay) {
    int numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    while (numNewMessages) {
      handleNewMessages(numNewMessages);
      numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    }
    lastTimeBotRan = millis();
  }

  // 2. Check if timed Mute has expired
  if (isMuted && muteDuration > 0) {
    if (millis() - muteStartTime >= muteDuration) {
      isMuted = false;
      muteDuration = 0;
      
      // --- THE FIX: Wipe the cooldown clock clean! ---
      lastPhotoTime = 0;     
      isFirstTrigger = true; 
      // -----------------------------------------------

      bot.sendMessage(CHAT_ID, "⏱️ Mute duration expired! Sentry Mode is back ONLINE and ready.", "");
    }
  }

  // 3. Handle Manual /photo Command
  if (requestPhoto) {
    captureAndSendPhoto("📸 Manual Photo Request:");
    requestPhoto = false; 
  }

  // 4. Handle Motion Detection
  if (motionDetected) {
    if (!isMuted) {
      unsigned long currentTime = millis();
      
      if ((currentTime - lastPhotoTime >= cooldownTime) || isFirstTrigger) {
        
        captureAndSendPhoto("🚨 ALERT: Motion Detected! (5-min cooldown active)");
        
        lastPhotoTime = currentTime;
        isFirstTrigger = false; 
      }
    }
    // Always clear the flag so it's ready for the next event
    motionDetected = false; 
  }
}
