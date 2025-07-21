# 🏁 Zman Ritza

## 📘 Overview
This project implements a **race timing system** using an **ESP32** and an **MFRC522 RFID reader**. Each participant carries an RFID tag. When scanned, the system logs their finish time, assigns a placement, and updates a live browser-based interface via Wi-Fi.

---

## 🚀 Features
- 📡 ESP32 creates a local Wi-Fi hotspot (`RFID_Monitor`)
- 🕒 Start a race and track finish times
- 🏁 Assigns placement to each participant based on scan order
- 🔄 Real-time table of finishers in the browser
- 💾 Saves data (Preferences)
- ✍️ Web-based interface to write numbers to RFID tags
- 📥 Export results as a CSV file
- 🔄 Fully reset system from web UI

---

## 🛠 Hardware Requirements
- ESP32 Dev Module  
- MFRC522 RFID Reader (SPI interface)
- RFID Tags 
- Optional: Power bank for portable setup

---

## 🔌 Wiring (ESP32 ↔ MFRC522)

| MFRC522 Pin | ESP32 Pin |
|-------------|-----------|
| SDA (SS)    | GPIO 5    |
| SCK         | GPIO 18   |
| MOSI        | GPIO 23   |
| MISO        | GPIO 19   |
| RST         | GPIO 21   |
| 3.3V        | 3.3V      |
| GND         | GND       |

---

## 🌐 How to Use
1. Power on the ESP32.
2. Connect to Wi-Fi:  
    **SSID:** `RFID_Monitor`  
    **Password:** `12345678`
3. In a browser, open [http://192.168.4.1](http://192.168.4.1)
4. Setup:
   1. Click "Write to tag"
   2. enter the ID of the runner
   3. Click "Write" while placing the tag next to the reader.
   4. Wait for confirmation. If faild, try again :).
5. On the race day:
   1. Click “🏁 Start Race”.
   2. Scan RFID tags to record finish time and placement.
   3. “📥 Export CSV” to download results.
   4. “🔄 Reset” clears all race data.

---

## 📁 Code Structure

### `setup()`
- Initializes RFID, SPI, Wi-Fi AP
- Loads data from flash (`Preferences`)
- Registers HTTP routes

### `loop()`
- Handles HTTP requests
- Scans RFID only if race is active
- Updates in-memory and saved data for new scans

---

## 🌐 Web Endpoints

| Route           | Description                            |
|----------------|----------------------------------------|
| `/`            | Main UI (live table, controls)         |
| `/start`       | Start the race                         |
| `/reset`       | Reset all data                         |
| `/data`        | JSON with all tags                     |
| `/race-time`   | JSON with race time                    |
| `/last-scan`   | JSON of last scanned tag               |
| `/write-ui`    | HTML form to enter tag number          |
| `/write?num=X` | Writes `X` to next presented tag       |
| `/export`      | Downloads CSV of results               |

---

## 🧠 Logic Summary

### `TagInfo` Struct

| Field         | Description                              |
|---------------|------------------------------------------|
| `number`      | ID written on tag (string)               |
| `endSeen`     | Timestamp in `millis()`                  |
| `endRecorded` | True if already scanned                  |
| `place`       | 1st, 2nd, etc., based on order of scan   |

### Persistent Keys (Preferences)

| Key                     | Purpose                         |
|-------------------------|---------------------------------|
| `count`                 | Number of scanned tags          |
| `tag{i}_number`         | Tag content                     |
| `tag{i}_place`          | Finishing place                 |
| `tag{i}_end`            | Time tag was scanned            |
| `tag{i}_rec`            | Whether already recorded        |
| `raceStarted`           | Race active state               |
| `startRaceTime`         | Timestamp when race began       |
