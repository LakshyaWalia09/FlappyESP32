# IoT Flappy Bird (ESP32) 🐦

A Flappy Bird clone for the ESP32 that features a Wi-Fi web interface for remote control and leaderboard tracking.

## ⚡ Features

* **OLED Gameplay:** Smooth graphics on a 128x64 SSD1306 display.


* **IoT Web Control:** Connect via Wi-Fi to "FlappyBird-ESP32" to play the game from your phone browser.


* **Persistent Scores:** Saves the top 10 high scores and player names to flash memory, surviving power cycles.



## 🛠 Hardware Setup

* **ESP32 Board**
* **SSD1306 OLED Display** (I2C)
* **2x Push Buttons**

### Pinout

| Component | GPIO Pin | Function |
| --- | --- | --- |
| **Game Button** | **4** | Flap / Jump |
| **Reset Button** | **5** | Clear Leaderboard |
| **OLED** | **21 (SDA) / 22 (SCL)** | Display |

## 🚀 How to Run

1. Install **Adafruit GFX** and **Adafruit SSD1306** libraries in Arduino IDE.
2. Upload the code to your ESP32.
3.  **To Play:** Use the physical button on GPIO 4 and/or connect to the Wi-Fi AP **"FlappyBird-ESP32"** to set your username.
4. **High Scores:** Persistenlty stored on the board. The web page gives a list of top scorers!.
