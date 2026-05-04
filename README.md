# IoT Flappy Bird (ESP32)

A Flappy Bird clone for the ESP32 with a Wi-Fi web dashboard for remote control, physics tuning, and leaderboard tracking — plus RGB LED and buzzer feedback.

## Features

* **OLED Gameplay:** Smooth graphics on a 128×64 SSD1306 display, with the player's name and current score shown live.

* **IoT Web Dashboard ("God Mode"):** Connect to the Wi-Fi AP **"FlappyBird-ESP32"** and open the IP shown on the OLED in any browser to access:
  * **Live Status** – see current score and game state at a glance.
  * **Remote Flap** – tap *FLAP BIRD* to make the bird jump from your phone or browser.
  * **Player Name** – set your in-game name remotely; it appears on the OLED and leaderboard.
  * **Physics Engine** – adjust gravity, flap force, base pipe speed, and base pipe gap in real time.
  * **Factory Reset (Danger Zone)** – wipe all scores and restore default physics with one button.

* **Persistent Leaderboard:** Saves the top 10 high scores and player names to flash memory (ESP32 `Preferences`), surviving power cycles.

* **Progressive Difficulty:** Every 5 points the pipe gap shrinks and pipe speed increases, making the game harder as you improve.

* **Day / Night Mode:** Every 10 points the display automatically inverts between day and night themes with a distinctive two-tone chime.

* **RGB LED Feedback (Common Anode):**
  * 🔵 **Blue** – flap/jump
  * 🟢 **Green** – new #1 high score achieved
  * 🔴 **Red** – game over

* **Buzzer Audio Feedback:**
  * Short beep on each flap.
  * Rising three-note tone on every difficulty increase (score milestone).
  * Two-tone chime on day/night toggle.
  * Celebratory ascending melody on a new high score.
  * Three descending beeps on game over.

* **New High Score Detection:** Triggers the celebratory LED and melody when the current run beats the #1 spot.

* **Startup Screen:** Displays the Wi-Fi SSID and AP IP address on the OLED at boot so you can connect immediately.

* **Game Over Screen:** Shows the final score, the AP IP address, and a prompt to press the button to restart.

## Hardware Setup

* **ESP32 Board**
* **SSD1306 OLED Display** (I2C, 128×64)
* **2× Push Buttons**
* **Passive Buzzer**
* **Common-Anode RGB LED**

### Pinout

| Component | GPIO Pin | Function |
| --- | --- | --- |
| **Game Button** | **4** | Flap / Jump (also restarts after game over) |
| **Reset Button** | **5** | Clear Leaderboard (physical shortcut) |
| **Buzzer** | **13** | Audio feedback |
| **LED – Red** | **14** | Game over indicator |
| **LED – Green** | **27** | New high score indicator |
| **LED – Blue** | **12** | Flap indicator |
| **OLED SDA** | **21** | I2C Data |
| **OLED SCL** | **22** | I2C Clock |

## How to Run

1. Install the following libraries in Arduino IDE:
   * **Adafruit GFX**
   * **Adafruit SSD1306**
2. Upload `project.cpp.ino` to your ESP32.
3. On boot the OLED shows the Wi-Fi SSID (**FlappyBird-ESP32**) and IP address.
4. **To play physically:** Press the button on GPIO 4 to flap; press GPIO 5 to clear the leaderboard.
5. **To play remotely:** Connect to **FlappyBird-ESP32** and navigate to the IP shown on the OLED. Use the dashboard to set your name, tune physics, flap the bird, and view or reset the leaderboard.
6. **High Scores** are saved automatically and persist across reboots.
