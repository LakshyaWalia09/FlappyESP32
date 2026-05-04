#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WebServer.h>
#include <exception>

const char *ssid = "FlappyBird-ESP32";
WebServer server(80);

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// --- PIN DEFINITIONS ---
#define BUTTON_PIN 4
#define RESET_BUTTON_PIN 5
#define BUZZER_PIN 13
#define LED_R 14
#define LED_G 27
#define LED_B 12

// --- BASE GAME DEFAULTS ---
#define BIRD_X_POS 32
#define BIRD_SIZE 7
#define NUM_PIPES 3
#define PIPE_WIDTH 12
#define NUM_SCORES 10

// --- MUTABLE GAME MECHANICS (Controllable via Web) ---
float currentGravity = 0.3;
float currentFlapForce = -3.0;
float basePipeSpeed = 1.0;
int basePipeGap = 32;

// Progressive variables
float activePipeSpeed;
int activePipeGap;
bool isNightMode = false;

float birdY, birdVelocity;
int score;
String playerName = "Player";
bool gameOver;
bool newHighScoreSet;
bool webFlapRequested = false;

Preferences preferences;

class GameException : public std::exception {
  String msg;
public:
  GameException(const String &m) : msg(m) {}
  const char* what() const noexcept override { return msg.c_str(); }
};

struct ScoreEntry {
  String name;
  int score;
};

template <typename NameT, typename ScoreT, size_t SIZE>
class Leaderboard {
private:
  ScoreEntry entries[SIZE];

  String keyName(const char* base, size_t idx) const {
    return String(base) + String(idx);
  }

public:
  Leaderboard() {
    for (size_t i = 0; i < SIZE; ++i) {
      entries[i].name = "Player";
      entries[i].score = 0;
    }
  }

  void addScore(ScoreT sc, const NameT &nm) {
    int pos = -1;
    for (size_t i = 0; i < SIZE; ++i) {
      if (sc > entries[i].score) {
        pos = i;
        break;
      }
    }
    if (pos != -1) {
      for (size_t i = SIZE - 1; i > (size_t)pos; --i) entries[i] = entries[i - 1];
      entries[pos].score = sc;
      entries[pos].name = nm;
    }
  }

  void load(const char *ns = "flappy-scores") {
    preferences.begin(ns, true);
    for (size_t i = 0; i < SIZE; ++i) {
      entries[i].score = preferences.getInt(keyName("score", i).c_str(), 0);
      entries[i].name = preferences.getString(keyName("name", i).c_str(), "Player");
    }
    preferences.end();
  }

  void save(const char *ns = "flappy-scores") {
    preferences.begin(ns, false);
    preferences.clear();
    for (size_t i = 0; i < SIZE; ++i) {
      preferences.putInt(keyName("score", i).c_str(), entries[i].score);
      preferences.putString(keyName("name", i).c_str(), entries[i].name);
    }
    preferences.end();
  }

  const ScoreEntry* getEntries() const { return entries; }

  void clear() {
    for (size_t i = 0; i < SIZE; ++i) {
      entries[i].score = 0;
      entries[i].name = "Player";
    }
  }
};

Leaderboard<String,int,NUM_SCORES> leaderboard;

struct Pipe {
  float x;
  int gapY;
  bool scored;
};
Pipe pipes[NUM_PIPES];

long lastFlapDebounceTime = 0;
long lastResetDebounceTime = 0;
long debounceDelay = 150;

// Helper function for COMMON ANODE LEDs
void setRGB(int r, int g, int b) {
  analogWrite(LED_R, 255 - r);
  analogWrite(LED_G, 255 - g);
  analogWrite(LED_B, 255 - b);
}

void loadHighScores() { leaderboard.load(); }
void saveHighScores() { leaderboard.save(); }

void updateHighScores(int newScore, String newName) {
  if (newScore > leaderboard.getEntries()[0].score && newScore > 0) {
    newHighScoreSet = true;
  }
  leaderboard.addScore(newScore, newName);
  saveHighScores();
}

// --- WEB SERVER HANDLERS ---

void handleRoot() {
  String status = gameOver ? "<span style='color:red; font-weight:bold;'>GAME OVER</span>" : "<span style='color:green; font-weight:bold;'>PLAYING</span>";
  
  String html = "<!DOCTYPE html><html><head><title>Flappy Dashboard</title>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<style>body{font-family: Arial; text-align: center; background:#222; color:#fff;}";
  html += "div.panel{background:#333; margin:15px auto; padding:20px; border-radius:10px; max-width:400px; box-shadow: 0 4px 8px rgba(0,0,0,0.5);}";
  html += "input,button{width:90%; padding:10px; margin:8px 0; border-radius:5px; border:none; font-size:16px;}";
  html += "button,input[type=submit]{background:#4CAF50; color:white; cursor:pointer; font-weight:bold;}";
  html += ".input-group{display:flex; justify-content:space-between; align-items:center; margin-bottom:10px;}";
  html += ".input-group input{width:50%;}";
  html += ".danger{background:#e74c3c !important;} .info{background:#3498db !important;}";
  html += "h2{margin-top:0; border-bottom:1px solid #555; padding-bottom:10px;} ol{text-align:left;}</style></head><body>";
  
  html += "<h1>ESP32 God Mode</h1>";

  // Panel 1: Live Status & Controls
  html += "<div class='panel'><h2>Live Status</h2>";
  html += "<p>Status: " + status + " | Current Score: <strong>" + String(score) + "</strong></p>";
  html += "<form action='/saveName' method='post'><div class='input-group'><label>Player Name:</label><input type='text' name='playerName' value='" + playerName + "'></div><input type='submit' class='info' value='Save Name'></form>";
  html += "<form action='/fly' method='post'><button>FLAP BIRD</button></form></div>";

  // Panel 2: Physics Engine Engine
  html += "<div class='panel'><h2>Physics Engine</h2>";
  html += "<form action='/settings' method='post'>";
  html += "<div class='input-group'><label>Gravity:</label><input type='number' step='0.1' name='gravity' value='" + String(currentGravity) + "'></div>";
  html += "<div class='input-group'><label>Flap Force:</label><input type='number' step='0.1' name='flapForce' value='" + String(currentFlapForce) + "'></div>";
  html += "<div class='input-group'><label>Base Speed:</label><input type='number' step='0.1' name='pipeSpeed' value='" + String(basePipeSpeed) + "'></div>";
  html += "<div class='input-group'><label>Base Gap:</label><input type='number' name='pipeGap' value='" + String(basePipeGap) + "'></div>";
  html += "<input type='submit' class='info' value='Apply Mechanics'>";
  html += "</form></div>";

  // Panel 3: Leaderboard
  html += "<div class='panel'><h2>Leaderboard</h2><ol>";
  const ScoreEntry* entries = leaderboard.getEntries();
  for (int i = 0; i < NUM_SCORES; i++) {
    if (entries[i].score > 0) html += "<li>" + entries[i].name + " - " + String(entries[i].score) + "</li>";
  }
  html += "</ol></div>";

  // Panel 4: Danger Zone
  html += "<div class='panel' style='border:2px solid #e74c3c;'><h2>Danger Zone</h2>";
  html += "<form action='/resetAll' method='post'><button class='danger'>FACTORY RESET (Wipe Data & Settings)</button></form></div>";

  html += "</body></html>";
  server.send(200, "text/html", html);
}

void handleSaveName() {
  if (server.hasArg("playerName")) playerName = server.arg("playerName");
  handleRoot();
}

void handleFly() {
  webFlapRequested = true;
  handleRoot();
}

void handleSettings() {
  if (server.hasArg("gravity")) currentGravity = server.arg("gravity").toFloat();
  if (server.hasArg("flapForce")) currentFlapForce = server.arg("flapForce").toFloat();
  if (server.hasArg("pipeSpeed")) basePipeSpeed = server.arg("pipeSpeed").toFloat();
  if (server.hasArg("pipeGap")) basePipeGap = server.arg("pipeGap").toInt();
  handleRoot();
}

void handleFactoryReset() {
  // Wipe Leaderboard
  leaderboard.clear();
  saveHighScores();
  
  // Restore Default Physics
  currentGravity = 0.3;
  currentFlapForce = -3.0;
  basePipeSpeed = 1.0;
  basePipeGap = 32;
  
  // Restart Game
  gameOver = true;
  handleRoot();
}

// --- GAME LOGIC ---

void resetGame() {
  birdY = SCREEN_HEIGHT / 2;
  birdVelocity = 0;
  score = 0;
  gameOver = false;
  newHighScoreSet = false;
  
  activePipeGap = basePipeGap; 
  activePipeSpeed = basePipeSpeed;
  isNightMode = false;
  display.invertDisplay(false);
  setRGB(0, 0, 0); 

  for (int i = 0; i < NUM_PIPES; i++) {
    pipes[i].x = SCREEN_WIDTH + i * (SCREEN_WIDTH / 1.5);
    pipes[i].gapY = random(15, SCREEN_HEIGHT - activePipeGap - 15);
    pipes[i].scored = false;
  }
}

void drawGame() {
  display.clearDisplay();
  display.fillRect(BIRD_X_POS, (int)birdY, BIRD_SIZE, BIRD_SIZE, SSD1306_WHITE);
  for (int i = 0; i < NUM_PIPES; i++) {
    display.fillRect((int)pipes[i].x, 0, PIPE_WIDTH, pipes[i].gapY, SSD1306_WHITE);
    display.fillRect((int)pipes[i].x, pipes[i].gapY + activePipeGap, PIPE_WIDTH, SCREEN_HEIGHT - (pipes[i].gapY + activePipeGap), SSD1306_WHITE);
  }
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(2,2);
  display.print(playerName + ": " + String(score));
  display.display();
}

void drawGameOverScreen() {
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(18,0);
  display.println("GAME OVER");
  display.setTextSize(1);
  if (newHighScoreSet) {
    display.setCursor(22,18);
    display.print("NEW HIGH SCORE!");
  }
  display.setCursor(20,30);
  display.print("Score: " + String(score));
  display.setCursor(20,40);
  display.print("IP: " + WiFi.softAPIP().toString());
  display.setCursor(15,54);
  display.print("Press to restart");
  display.display();
}

void handleGameOver() {
  updateHighScores(score, playerName);
  drawGameOverScreen();

  if (newHighScoreSet) {
    setRGB(0, 255, 0); 
    tone(BUZZER_PIN, 523, 150); delay(150); 
    tone(BUZZER_PIN, 659, 150); delay(150); 
    tone(BUZZER_PIN, 784, 150); delay(150); 
    tone(BUZZER_PIN, 1046, 400);            
  } else {
    setRGB(255, 0, 0);
    for(int i = 0; i < 3; i++) {
      tone(BUZZER_PIN, 300, 150); 
      delay(200);                 
    }
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(RESET_BUTTON_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_R, OUTPUT);
  pinMode(LED_G, OUTPUT);
  pinMode(LED_B, OUTPUT);
  setRGB(0, 0, 0); 

  try { loadHighScores(); } catch (...) {}

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("Display failed");
    for(;;) delay(1000);
  }

  WiFi.softAP(ssid);
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0,10); display.println("Connect to Wi-Fi:");
  display.setCursor(0,20); display.println(ssid);
  display.setCursor(0,40); display.print("IP: "); display.println(WiFi.softAPIP());
  display.display();
  delay(4000);

  server.on("/", HTTP_GET, handleRoot);
  server.on("/saveName", HTTP_POST, handleSaveName);
  server.on("/fly", HTTP_POST, handleFly);
  server.on("/settings", HTTP_POST, handleSettings);
  server.on("/resetAll", HTTP_POST, handleFactoryReset);
  server.begin();

  randomSeed(analogRead(0));
  resetGame();
}

void loop() {
  server.handleClient();

  if (gameOver) {
    if (webFlapRequested || digitalRead(BUTTON_PIN) == LOW) {
      if ((millis() - lastFlapDebounceTime) > debounceDelay) {
        resetGame();
        lastFlapDebounceTime = millis();
        webFlapRequested = false;
      }
    }
    if (digitalRead(RESET_BUTTON_PIN) == LOW) {
      if ((millis() - lastResetDebounceTime) > debounceDelay) {
        leaderboard.clear();
        saveHighScores();
        newHighScoreSet = false;
        drawGameOverScreen();
        lastResetDebounceTime = millis();
      }
    }
    return;
  }

  setRGB(0, 0, 0); 

  if (digitalRead(BUTTON_PIN) == LOW || webFlapRequested) {
    if ((millis() - lastFlapDebounceTime) > debounceDelay) {
      birdVelocity = currentFlapForce; // Uses mutable variable
      tone(BUZZER_PIN, 1000, 50); 
      setRGB(0, 0, 255); 
      lastFlapDebounceTime = millis();
      webFlapRequested = false;
    }
  }

  birdVelocity += currentGravity; // Uses mutable variable
  birdY += birdVelocity;

  for (int i = 0; i < NUM_PIPES; i++) {
    pipes[i].x -= activePipeSpeed; // Progressive speed
    if (pipes[i].x < -PIPE_WIDTH) {
      pipes[i].x = SCREEN_WIDTH;
      pipes[i].gapY = random(15, SCREEN_HEIGHT - activePipeGap - 15);
      pipes[i].scored = false;
    }
  }

  if (birdY < 0 || birdY + BIRD_SIZE > SCREEN_HEIGHT) gameOver = true;

  for (int i = 0; i < NUM_PIPES; i++) {
    if (BIRD_X_POS + BIRD_SIZE > pipes[i].x && BIRD_X_POS < pipes[i].x + PIPE_WIDTH) {
      if (birdY < pipes[i].gapY || birdY + BIRD_SIZE > pipes[i].gapY + activePipeGap) gameOver = true;
    }
  }

  for (int i = 0; i < NUM_PIPES; i++) {
    if (pipes[i].x < BIRD_X_POS && !pipes[i].scored) {
      score++;
      pipes[i].scored = true;

      // 📈 PROGRESSIVE DIFFICULTY
      if (score % 5 == 0) {
        if (activePipeGap > 18) activePipeGap -= 2; 
        activePipeSpeed += 0.2; 
        tone(BUZZER_PIN, 2000, 80); 
      }

      // 🌓 DAY/NIGHT MODE TOGGLE
      if (score % 10 == 0) {
        isNightMode = !isNightMode;
        display.invertDisplay(isNightMode);
        tone(BUZZER_PIN, 800, 80); delay(80);
        tone(BUZZER_PIN, 1200, 100);
      }
    }
  }

  if (gameOver) {
    handleGameOver();
    return;
  }

  drawGame();
  delay(20);
}
