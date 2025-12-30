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

#define BUTTON_PIN 4
#define RESET_BUTTON_PIN 5

#define BIRD_X_POS 32
#define BIRD_SIZE 7
#define GRAVITY 0.3
#define FLAP_FORCE -3.0
#define PIPE_WIDTH 12
#define PIPE_GAP 28
#define PIPE_SPEED 1
#define NUM_PIPES 3
#define NUM_SCORES 10

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
  const char* what() const noexcept override {
    return msg.c_str();
  }
};

struct ScoreEntry {
  String name;
  int score;
};

template <typename NameT, typename ScoreT, size_t SIZE>
class Leaderboard {
private:
  ScoreEntry entries[SIZE];
  mutable int topScoreAccessCount = 0;

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
    if (sc < 0) throw GameException("Score cannot be negative");
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

  ScoreT getTopScore() const {
    ++topScoreAccessCount;
    return entries[0].score;
  }

  const ScoreEntry* getEntries() const { return entries; }

  void clear() {
    for (size_t i = 0; i < SIZE; ++i) {
      entries[i].score = 0;
      entries[i].name = "Player";
    }
  }

  template <typename A, typename B, size_t C>
  friend void printLeaderboard(const Leaderboard<A,B,C>& lb);
};

template <typename A, typename B, size_t C>
void printLeaderboard(const Leaderboard<A,B,C>& lb) {
  Serial.println("---- Leaderboard ----");
  const ScoreEntry* e = lb.getEntries();
  for (size_t i = 0; i < C; ++i) {
    if (e[i].score > 0) {
      Serial.print(String(i+1) + ". ");
      Serial.print(e[i].name);
      Serial.print(" - ");
      Serial.println(e[i].score);
    }
  }
}

Leaderboard<String,int,NUM_SCORES> leaderboard;

struct Pipe {
  int x;
  int gapY;
  bool scored;
};
Pipe pipes[NUM_PIPES];

long lastFlapDebounceTime = 0;
long lastResetDebounceTime = 0;
long debounceDelay = 150;

void loadHighScores() { leaderboard.load(); }
void saveHighScores() { leaderboard.save(); }

void updateHighScores(int newScore, String newName) {
  try {
    leaderboard.addScore(newScore, newName);
    newHighScoreSet = true;
    saveHighScores();
  } catch (const std::exception &e) {
    Serial.println(e.what());
  }
}

void handleRoot() {
  String html = "<!DOCTYPE html><html><head><title>Flappy Bird Setup</title>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<style>body{font-family: Arial; text-align: center; background:#f0f0f0;}";
  html += "div{background:white; margin:20px auto; padding:20px; border-radius:10px; max-width:400px;}";
  html += "input,button{width:90%; padding:10px; margin:10px 0; border-radius:5px; border:1px solid #ccc; font-size:16px;}";
  html += "button,input[type=submit]{background:#4CAF50; color:white; cursor:pointer;}";
  html += "h1,h2{margin:0 0 10px;} ol{text-align:left;}</style></head><body>";

  html += "<div><h1>Flappy Bird Control</h1>";
  html += "<form action='/save' method='post'><input type='text' name='playerName' value='" + playerName + "'><input type='submit' value='Save Name'></form>";
  html += "<form action='/fly' method='post'><button>FLAP!</button></form></div>";

  html += "<div><h2>Leaderboard</h2><ol>";
  const ScoreEntry* entries = leaderboard.getEntries();
  for (int i = 0; i < NUM_SCORES; i++) {
    if (entries[i].score > 0) html += "<li>" + entries[i].name + " - " + String(entries[i].score) + "</li>";
  }
  html += "</ol></div></body></html>";
  server.send(200, "text/html", html);
}

void handleSave() {
  if (server.hasArg("playerName")) playerName = server.arg("playerName");
  handleRoot();
}

void handleFly() {
  webFlapRequested = true;
  handleRoot();
}

void resetGame() {
  birdY = SCREEN_HEIGHT / 2;
  birdVelocity = 0;
  score = 0;
  gameOver = false;
  newHighScoreSet = false;

  for (int i = 0; i < NUM_PIPES; i++) {
    pipes[i].x = SCREEN_WIDTH + i * (SCREEN_WIDTH / 1.5);
    pipes[i].gapY = random(15, SCREEN_HEIGHT - PIPE_GAP - 15);
    pipes[i].scored = false;
  }
}

void drawGame() {
  display.clearDisplay();
  display.fillRect(BIRD_X_POS, (int)birdY, BIRD_SIZE, BIRD_SIZE, SSD1306_WHITE);
  for (int i = 0; i < NUM_PIPES; i++) {
    display.fillRect(pipes[i].x, 0, PIPE_WIDTH, pipes[i].gapY, SSD1306_WHITE);
    display.fillRect(pipes[i].x, pipes[i].gapY + PIPE_GAP, PIPE_WIDTH, SCREEN_HEIGHT - (pipes[i].gapY + PIPE_GAP), SSD1306_WHITE);
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
  printLeaderboard(leaderboard);
}

void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(RESET_BUTTON_PIN, INPUT_PULLUP);

  try { loadHighScores(); } catch (...) {}

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    try { throw GameException("Display failed"); }
    catch (const std::exception &e) {
      Serial.println(e.what());
      for(;;) delay(1000);
    }
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  WiFi.softAP(ssid);
  IPAddress IP = WiFi.softAPIP();
  display.setCursor(0,10); display.println("Connect to Wi-Fi:");
  display.setCursor(0,20); display.println(ssid);
  display.setCursor(0,40); display.print("IP: "); display.println(IP);
  display.display();
  delay(4000);

  server.on("/", HTTP_GET, handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/fly", HTTP_POST, handleFly);
  server.begin();

  randomSeed(analogRead(0));
  resetGame();
}

void loop() {
  server.handleClient();

  if (gameOver) {
    if (webFlapRequested) {
      resetGame();
      webFlapRequested = false;
      return;
    }
    if (digitalRead(BUTTON_PIN) == LOW) {
      if ((millis() - lastFlapDebounceTime) > debounceDelay) {
        resetGame();
        lastFlapDebounceTime = millis();
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

  if (digitalRead(BUTTON_PIN) == LOW || webFlapRequested) {
    if ((millis() - lastFlapDebounceTime) > debounceDelay) {
      birdVelocity = FLAP_FORCE;
      lastFlapDebounceTime = millis();
      webFlapRequested = false;
    }
  }

  birdVelocity += GRAVITY;
  birdY += birdVelocity;

  for (int i = 0; i < NUM_PIPES; i++) {
    pipes[i].x -= PIPE_SPEED;
    if (pipes[i].x < -PIPE_WIDTH) {
      pipes[i].x = SCREEN_WIDTH;
      pipes[i].gapY = random(15, SCREEN_HEIGHT - PIPE_GAP - 15);
      pipes[i].scored = false;
    }
  }

  if (birdY < 0 || birdY + BIRD_SIZE > SCREEN_HEIGHT) gameOver = true;

  for (int i = 0; i < NUM_PIPES; i++) {
    if (BIRD_X_POS + BIRD_SIZE > pipes[i].x && BIRD_X_POS < pipes[i].x + PIPE_WIDTH) {
      if (birdY < pipes[i].gapY || birdY + BIRD_SIZE > pipes[i].gapY + PIPE_GAP) gameOver = true;
    }
  }

  for (int i = 0; i < NUM_PIPES; i++) {
    if (pipes[i].x < BIRD_X_POS && !pipes[i].scored) {
      score++;
      pipes[i].scored = true;
    }
  }

  if (gameOver) {
    handleGameOver();
    return;
  }

  drawGame();
  delay(20);
}
