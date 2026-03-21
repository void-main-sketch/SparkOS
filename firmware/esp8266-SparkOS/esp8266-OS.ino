/*
 * SWILL OS for ESP8266 with 0.96" OLED (SSD1306, I2C) and 3 buttons
 * 
 * Features:
 * - Main menu with Settings, Games, Calculator, Console, Time display
 * - Settings: WiFi configuration (WiFiManager), timezone adjustment, reset WiFi
 * - Games: Dodge and Pong (2-button control, 3rd button to exit)
 * - Calculator: simple increment/decrement (can be extended)
 * - Console: system info (WiFi status, IP, timezone)
 * - Time sync via NTP (background)
 * 
 * Pins:
 * - SDA D1 (GPIO5), SCL D2 (GPIO4)
 * - Buttons: D5 (UP), D6 (DOWN), D7 (EXIT/SELECT)
 * 
 * Required libraries:
 * - U8g2 (for OLED)
 * - WiFiManager (for easy WiFi setup)
 * - NTPClient (for time)
 * - ESP8266WiFi (built-in)
 */

#include <ESP8266WiFi.h>
#include <WiFiManager.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <U8g2lib.h>
#include <Wire.h>

// OLED display (128x64, I2C)
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

// Button pins
#define BTN_UP    D5   // GPIO14
#define BTN_DOWN  D6   // GPIO12
#define BTN_EXIT  D7   // GPIO13

// NTP client
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 0, 60000);  // UTC+0, update every 60s

// System state
enum AppState {
  STATE_MENU,
  STATE_SETTINGS,
  STATE_GAME_MENU,
  STATE_GAME_DODGE,
  STATE_GAME_PONG,
  STATE_CALCULATOR,
  STATE_CONSOLE
};
AppState currentState = STATE_MENU;

// Menu variables
const char* mainMenuItems[] = {"Settings", "Games", "Calculator", "Console", "Time"};
int menuIndex = 0;
const int menuSize = 5;

const char* settingsItems[] = {"WiFi Config", "Set Timezone", "Reset WiFi", "Back"};
int settingsIndex = 0;
const int settingsSize = 4;

const char* gamesItems[] = {"Dodge", "Pong", "Back"};
int gamesIndex = 0;
const int gamesSize = 3;

// Calculator
double calcValue = 0;
String calcDisplay = "0";

// Timezone offset (hours)
int timezoneOffset = 0;

// Game variables (global for reset on enter)
// Dodge
int dodgePlayerX = 64;
int dodgeObstacleX = 0;
int dodgeObstacleY = 0;
int dodgeScore = 0;
bool dodgeActive = true;
// Pong
int pongBallX = 64, pongBallY = 32;
int pongBallVX = 1, pongBallVY = 1;
int pongPaddleY = 32;
int pongScore = 0;
bool pongActive = true;

// -------------------------------------------------------------------
// Setup
// -------------------------------------------------------------------
void setupPins() {
  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);
  pinMode(BTN_EXIT, INPUT_PULLUP);
}

void setupWiFi() {
  WiFiManager wifiManager;
  wifiManager.setTimeout(180);
  if (!wifiManager.autoConnect("ESP-OS")) {
    Serial.println("WiFi connection failed");
  } else {
    Serial.println("WiFi connected");
    timeClient.begin();
    timeClient.setTimeOffset(timezoneOffset * 3600);
    timeClient.update();
  }
}

void resetWiFi() {
  WiFiManager wifiManager;
  wifiManager.resetSettings();
  ESP.restart();
}

// -------------------------------------------------------------------
// Drawing functions
// -------------------------------------------------------------------
void drawMenu() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(0, 10, "Main Menu");
  for (int i = 0; i < menuSize; i++) {
    int y = 22 + i * 10;
    if (i == menuIndex) {
      u8g2.drawStr(2, y, ">");
      u8g2.drawStr(12, y, mainMenuItems[i]);
    } else {
      u8g2.drawStr(12, y, mainMenuItems[i]);
    }
  }
  // Time display in top right corner
  if (WiFi.status() == WL_CONNECTED) {
    timeClient.update();
    String timeStr = timeClient.getFormattedTime();
    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.drawStr(128 - 6 * timeStr.length(), 7, timeStr.c_str());
  } else {
    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.drawStr(128 - 30, 7, "No WiFi");
  }
  u8g2.sendBuffer();
}

void drawSettings() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(0, 10, "Settings");
  for (int i = 0; i < settingsSize; i++) {
    int y = 22 + i * 10;
    if (i == settingsIndex) {
      u8g2.drawStr(2, y, ">");
      u8g2.drawStr(12, y, settingsItems[i]);
    } else {
      u8g2.drawStr(12, y, settingsItems[i]);
    }
  }
  u8g2.sendBuffer();
}

void drawGamesMenu() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(0, 10, "Games");
  for (int i = 0; i < gamesSize; i++) {
    int y = 22 + i * 10;
    if (i == gamesIndex) {
      u8g2.drawStr(2, y, ">");
      u8g2.drawStr(12, y, gamesItems[i]);
    } else {
      u8g2.drawStr(12, y, gamesItems[i]);
    }
  }
  u8g2.sendBuffer();
}

void drawGameDodge() {
  u8g2.clearBuffer();
  // Player (rectangle at bottom)
  u8g2.drawBox(dodgePlayerX - 5, 56, 10, 8);
  // Obstacle
  u8g2.drawBox(dodgeObstacleX, dodgeObstacleY, 8, 8);
  // Score
  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawStr(100, 10, String(dodgeScore).c_str());

  if (dodgeActive) {
    dodgeObstacleY += 2;
    if (dodgeObstacleY > 64) {
      dodgeObstacleY = 0;
      dodgeObstacleX = random(0, 120);
      dodgeScore++;
    }
    // Collision detection
    if (dodgeObstacleY + 8 > 56 && dodgeObstacleY < 64 &&
        dodgeObstacleX + 8 > dodgePlayerX - 5 && dodgeObstacleX < dodgePlayerX + 5) {
      dodgeActive = false;
    }
  } else {
    u8g2.setFont(u8g2_font_6x10_tf);
    u8g2.drawStr(30, 32, "Game Over");
    u8g2.drawStr(20, 44, "Press Exit");
  }
  u8g2.sendBuffer();
}

void drawGamePong() {
  u8g2.clearBuffer();
  // Paddle (left side)
  u8g2.drawBox(10, pongPaddleY - 8, 3, 16);
  // Ball
  u8g2.drawDisc(pongBallX, pongBallY, 2);
  // Score
  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawStr(100, 10, String(pongScore).c_str());

  if (pongActive) {
    pongBallX += pongBallVX;
    pongBallY += pongBallVY;
    // Bounce off top/bottom
    if (pongBallY <= 2 || pongBallY >= 62) pongBallVY = -pongBallVY;
    // Bounce off right wall
    if (pongBallX >= 126) pongBallVX = -pongBallVX;
    // Paddle collision
    if (pongBallX <= 15 && pongBallX >= 8 &&
        pongBallY >= pongPaddleY - 10 && pongBallY <= pongPaddleY + 10) {
      pongBallVX = -pongBallVX;
      pongBallX = 16;  // push out to avoid multiple collisions
      pongScore++;
    }
    // Ball left the screen -> game over
    if (pongBallX <= 0) {
      pongActive = false;
    }
  } else {
    u8g2.setFont(u8g2_font_6x10_tf);
    u8g2.drawStr(30, 32, "Game Over");
    u8g2.drawStr(20, 44, "Press Exit");
  }
  u8g2.sendBuffer();
}

void drawCalculator() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(0, 10, "Calculator");
  u8g2.drawHLine(0, 12, 128);
  u8g2.setFont(u8g2_font_9x15_tf);
  u8g2.setCursor(0, 32);
  u8g2.print(calcDisplay);
  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawStr(0, 50, "UP: +1   DOWN: -1");
  u8g2.drawStr(0, 60, "EXIT: Menu");
  u8g2.sendBuffer();
}

void drawConsole() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(0, 10, "Console");
  u8g2.drawHLine(0, 12, 128);
  u8g2.setCursor(0, 24);
  u8g2.print("WiFi: ");
  if (WiFi.status() == WL_CONNECTED) {
    u8g2.print("Connected");
    u8g2.setCursor(0, 36);
    u8g2.print("IP: ");
    u8g2.print(WiFi.localIP().toString());
  } else {
    u8g2.print("Disconnected");
  }
  u8g2.setCursor(0, 48);
  u8g2.print("Timezone: ");
  u8g2.print(timezoneOffset);
  u8g2.sendBuffer();
}

// -------------------------------------------------------------------
// Input handlers
// -------------------------------------------------------------------
void handleMenu() {
  static unsigned long lastPress = 0;
  if (millis() - lastPress < 350) return;

  if (digitalRead(BTN_UP) == LOW) {
    menuIndex = (menuIndex - 1 + menuSize) % menuSize;
    lastPress = millis();
  }
  if (digitalRead(BTN_DOWN) == LOW) {
    menuIndex = (menuIndex + 1) % menuSize;
    lastPress = millis();
  }
  if (digitalRead(BTN_EXIT) == LOW) {
    switch (menuIndex) {
      case 0: currentState = STATE_SETTINGS; settingsIndex = 0; break;
      case 1: currentState = STATE_GAME_MENU; gamesIndex = 0; break;
      case 2: currentState = STATE_CALCULATOR; calcValue = 0; calcDisplay = "0"; break;
      case 3: currentState = STATE_CONSOLE; break;
      case 4: /* Time is always visible, do nothing */ break;
    }
    lastPress = millis();
  }
}

void handleSettings() {
  static unsigned long lastPress = 0;
  if (millis() - lastPress < 350) return;

  if (digitalRead(BTN_UP) == LOW) {
    settingsIndex = (settingsIndex - 1 + settingsSize) % settingsSize;
    lastPress = millis();
  }
  if (digitalRead(BTN_DOWN) == LOW) {
    settingsIndex = (settingsIndex + 1) % settingsSize;
    lastPress = millis();
  }
  if (digitalRead(BTN_EXIT) == LOW) {
    switch (settingsIndex) {
      case 0: // WiFi Config
        {
          WiFiManager wifiManager;
          wifiManager.startConfigPortal("ESP-OS");
          ESP.restart();
        }
        break;
      case 1: // Set Timezone
        timezoneOffset = (timezoneOffset + 1) % 25 - 12;  // -12 .. +12
        timeClient.setTimeOffset(timezoneOffset * 3600);
        break;
      case 2: // Reset WiFi
        resetWiFi();
        break;
      case 3: // Back
        currentState = STATE_MENU;
        break;
    }
    lastPress = millis();
  }
}

void handleGamesMenu() {
  static unsigned long lastPress = 0;
  if (millis() - lastPress < 350) return;

  if (digitalRead(BTN_UP) == LOW) {
    gamesIndex = (gamesIndex - 1 + gamesSize) % gamesSize;
    lastPress = millis();
  }
  if (digitalRead(BTN_DOWN) == LOW) {
    gamesIndex = (gamesIndex + 1) % gamesSize;
    lastPress = millis();
  }
  if (digitalRead(BTN_EXIT) == LOW) {
    switch (gamesIndex) {
      case 0: // Dodge
        currentState = STATE_GAME_DODGE;
        // Reset game
        dodgePlayerX = 64;
        dodgeObstacleX = random(0, 120);
        dodgeObstacleY = 0;
        dodgeScore = 0;
        dodgeActive = true;
        break;
      case 1: // Pong
        currentState = STATE_GAME_PONG;
        // Reset game
        pongBallX = 64; pongBallY = 32;
        pongBallVX = 1; pongBallVY = 1;
        pongPaddleY = 32;
        pongScore = 0;
        pongActive = true;
        break;
      case 2: // Back
        currentState = STATE_MENU;
        break;
    }
    lastPress = millis();
  }
}

void handleGameDodge() {
  static unsigned long lastMove = 0;
  if (digitalRead(BTN_UP) == LOW && millis() - lastMove > 150) {
    dodgePlayerX -= 5;
    if (dodgePlayerX < 5) dodgePlayerX = 5;
    lastMove = millis();
  }
  if (digitalRead(BTN_DOWN) == LOW && millis() - lastMove > 150) {
    dodgePlayerX += 5;
    if (dodgePlayerX > 123) dodgePlayerX = 123;
    lastMove = millis();
  }
  if (digitalRead(BTN_EXIT) == LOW) {
    currentState = STATE_GAME_MENU;
    delay(200);
  }
}

void handleGamePong() {
  static unsigned long lastMove = 0;
  if (digitalRead(BTN_UP) == LOW && millis() - lastMove > 150) {
    pongPaddleY -= 4;
    if (pongPaddleY < 8) pongPaddleY = 8;
    lastMove = millis();
  }
  if (digitalRead(BTN_DOWN) == LOW && millis() - lastMove > 150) {
    pongPaddleY += 4;
    if (pongPaddleY > 56) pongPaddleY = 56;
    lastMove = millis();
  }
  if (digitalRead(BTN_EXIT) == LOW) {
    currentState = STATE_GAME_MENU;
    delay(200);
  }
}

void handleCalculator() {
  static unsigned long lastPress = 0;
  if (millis() - lastPress < 350) return;

  if (digitalRead(BTN_UP) == LOW) {
    calcValue++;
    calcDisplay = String(calcValue);
    lastPress = millis();
  }
  if (digitalRead(BTN_DOWN) == LOW) {
    calcValue--;
    calcDisplay = String(calcValue);
    lastPress = millis();
  }
  if (digitalRead(BTN_EXIT) == LOW) {
    currentState = STATE_MENU;
    lastPress = millis();
  }
}

void handleConsole() {
  if (digitalRead(BTN_EXIT) == LOW) {
    currentState = STATE_MENU;
    delay(200);
  }
}

// -------------------------------------------------------------------
// Main
// -------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  setupPins();
  Wire.begin(D1, D2);
  u8g2.begin();
  
  // Анимация загрузки
  for (int i = 0; i <= 100; i++) {
    u8g2.clearBuffer();
    
    // Логотип (текстовый)
    u8g2.setFont(u8g2_font_logisoso32_tf);
    u8g2.drawStr(10, 35, "S");
    u8g2.setFont(u8g2_font_7x13_tf);
    u8g2.drawStr(30, 35, "park ");
    
    u8g2.setFont(u8g2_font_6x10_tf);
    u8g2.drawStr(35, 50, "Loading...");
    
    // Прогресс-бар
    u8g2.drawFrame(14, 54, 102, 8);
    u8g2.drawBox(16, 56, i, 4);
    
    // Процент загрузки
    u8g2.setCursor(90, 45);
    u8g2.print(i);
    u8g2.print("%");
    
    u8g2.sendBuffer();
    delay(20);
  }
  
  setupWiFi();
}

void loop() {
  // Background NTP update (every minute)
  if (WiFi.status() == WL_CONNECTED) {
    static unsigned long lastNTP = 0;
    if (millis() - lastNTP > 60000) {
      timeClient.update();
      lastNTP = millis();
    }
  }

  // State machine
  switch (currentState) {
    case STATE_MENU:
      drawMenu();
      handleMenu();
      break;
    case STATE_SETTINGS:
      drawSettings();
      handleSettings();
      break;
    case STATE_GAME_MENU:
      drawGamesMenu();
      handleGamesMenu();
      break;
    case STATE_GAME_DODGE:
      drawGameDodge();
      handleGameDodge();
      break;
    case STATE_GAME_PONG:
      drawGamePong();
      handleGamePong();
      break;
    case STATE_CALCULATOR:
      drawCalculator();
      handleCalculator();
      break;
    case STATE_CONSOLE:
      drawConsole();
      handleConsole();
      break;
  }
  delay(20);  // small debounce
}