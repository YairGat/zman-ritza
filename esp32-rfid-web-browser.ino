#include <SPI.h>
#include <MFRC522.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>

#define SS_PIN  5
#define RST_PIN 21

MFRC522 rfid(SS_PIN, RST_PIN);
WebServer server(80);
Preferences prefs;

const char* AP_SSID = "RFID_Monitor";
const char* AP_PASS = "12345678";

const int MAX_TAGS = 40;

struct TagInfo {
  String number;
  unsigned long endSeen = 0;
  bool endRecorded = false;
  int place = 0; // חדש
};

TagInfo tags[MAX_TAGS];
int tagCount = 0;

bool raceStarted = false;
unsigned long startRaceTime = 0;
volatile bool isWriting = false;

String lastScannedNumber = "";
unsigned long lastScannedTime = 0;

void saveTags() {
  prefs.putUInt("count", tagCount);
  for (int i = 0; i < tagCount; i++) {
    String prefix = "tag" + String(i) + "_";
    prefs.putString((prefix + "number").c_str(), tags[i].number);
    prefs.putInt((prefix + "place").c_str(), tags[i].place);
    prefs.putULong((prefix + "end").c_str(), tags[i].endSeen);
    prefs.putBool((prefix + "rec").c_str(), tags[i].endRecorded);
  }
}

void setup() {
  Serial.begin(9600);
  SPI.begin();
  rfid.PCD_Init();

  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.print("ESP32 IP: ");
  Serial.println(WiFi.softAPIP());

  prefs.begin("tagdata", false);

  // Load tags
  tagCount = prefs.getUInt("count", 0);
  for (int i = 0; i < tagCount; i++) {
    String prefix = "tag" + String(i) + "_";
    tags[i].number = prefs.getString((prefix + "number").c_str(), "");
    tags[i].endSeen = prefs.getULong((prefix + "end").c_str(), 0);
    tags[i].endRecorded = prefs.getBool((prefix + "rec").c_str(), false);
    tags[i].place = prefs.getInt((prefix + "place").c_str(), 0);
  }

  // Load race info
  raceStarted = prefs.getBool("raceStarted", false);
  startRaceTime = prefs.getULong("startRaceTime", 0);

  server.on("/", handlePage);
  server.on("/start", handleStartRace);
  server.on("/data", handleJson);
  server.on("/race-time", handleRaceTime);
  server.on("/last-scan", handleLastScan);
  server.on("/write-ui", handleWritePage);
  server.on("/write", handleWriteAPI);
  server.on("/reset", handleReset);
  server.on("/export", handleExportCSV);
  server.begin();
}

void loop() {
  server.handleClient();
  if (isWriting || !raceStarted) return;

  if (!(rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial())) return;

  MFRC522::MIFARE_Key key;
  for (byte i = 0; i < 6; i++) key.keyByte[i] = 0xFF;
  byte buffer[18];
  byte size = sizeof(buffer);

  MFRC522::StatusCode status = rfid.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, 1, &key, &rfid.uid);
  if (status != MFRC522::STATUS_OK) return;

  status = rfid.MIFARE_Read(1, buffer, &size);
  if (status != MFRC522::STATUS_OK) return;

  String number = "";
  for (int i = 0; i < 16; i++) {
    if (buffer[i] == 0) break;
    number += (char)buffer[i];
  }

  int idx = -1;
  for (int i = 0; i < tagCount; i++) {
    if (tags[i].number == number) {
      idx = i;
      break;
    }
  }

  unsigned long now = millis();
  if (idx == -1 && tagCount < MAX_TAGS) {
    TagInfo& t = tags[tagCount++];
    t.number = number;
    t.endSeen = now;
    t.endRecorded = true;
    t.place = tagCount; // המקום לפי הסדר
    saveTags();
  }

  lastScannedNumber = number;
  lastScannedTime = now;

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
}

void handleStartRace() {
  raceStarted = true;
  startRaceTime = millis();
  prefs.putBool("raceStarted", true);
  prefs.putULong("startRaceTime", startRaceTime);
  Serial.println("🏁 Race started!");
  server.send(200, "text/plain", "OK");
}

void handleReset() {
  tagCount = 0;
  lastScannedNumber = "";
  lastScannedTime = 0;
  raceStarted = false;
  startRaceTime = 0;

  prefs.clear(); // Clears tags + race info
  server.send(200, "text/plain", "Reset OK");
}

void handleRaceTime() {
  if (!raceStarted) {
    server.send(200, "application/json", "{\"started\":false}");
    return;
  }

  unsigned long elapsed = millis() - startRaceTime;
  unsigned s = elapsed / 1000;
  char buf[10];
  sprintf(buf, "%02u:%02u", (s / 60) % 60, s % 60);

  String json = "{\"started\":true,\"time\":\"" + String(buf) + "\"}";
  server.send(200, "application/json", json);
}

void handleLastScan() {
  if (lastScannedNumber == "") {
    server.send(200, "application/json", "{\"number\":null}");
    return;
  }
  String json = "{\"number\":\"" + lastScannedNumber + "\",\"time\":\"" + msToMMSS(lastScannedTime - startRaceTime) + "\"}";
  server.send(200, "application/json", json);
}

String msToMMSS(unsigned long ms) {
  unsigned s = ms / 1000;
  char buf[6];
  sprintf(buf, "%02u:%02u", (s / 60) % 60, s % 60);
  return buf;
}

void handlePage() {
  String html = "<!DOCTYPE html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>RFID Monitor</title><style>"
    "body{font-family:sans-serif;padding:1em}table{width:100%;border-collapse:collapse}"
    "th,td{border:1px solid #ccc;padding:8px;text-align:center}"
    "th{background:#3f51b5;color:#fff}a,button{display:inline-block;margin:10px;padding:10px 15px;"
    "background:#3f51b5;color:white;text-decoration:none;border:none;border-radius:4px;font-size:16px}"
    "#raceTime{font-size:20px;color:#4CAF50;margin:10px 0;font-weight:bold}"
    "#lastScan{margin:10px 0;color:#2E7D32;font-weight:bold}"
    "</style></head><body><h2>RFID Live Table</h2>"

    "<button id='startBtn' onclick='startRace()'>🏁 Start Race</button>"
    "<span id='startedMsg' style='display:none;'>✅ Race Started!</span>"
    "<div id='raceTime' style='display:none;'>⏱ Time: 00:00</div>"
    "<a href='/write-ui'>✍️ Write to Tag</a>"
    "<button onclick='resetRace()'>🔄 Reset</button>"
    "<a href='/export' download='race_results.csv'>📥 Export CSV</a>"

    "<div id='lastScan'></div>"
    "<table><thead><tr><th>Place</th><th>Number</th><th>Duration</th></tr></thead><tbody id='rows'></tbody></table>"

    "<script>"
    "let raceStarted = " + String(raceStarted ? "true" : "false") + ";"
    "if(raceStarted){"
      "document.addEventListener('DOMContentLoaded',()=>{"
        "document.getElementById('startBtn').style.display='none';"
        "document.getElementById('startedMsg').style.display='inline';"
        "document.getElementById('raceTime').style.display='block';"
      "});"
    "}"
    "function startRace(){"
      "fetch('/start').then(()=>{"
        "document.getElementById('startBtn').style.display='none';"
        "document.getElementById('startedMsg').style.display='inline';"
        "document.getElementById('raceTime').style.display='block';"
        "raceStarted = true;"
      "});"
    "}"
    "function resetRace(){"
      "fetch('/reset').then(()=>location.reload());"
    "}"
    "function updateRaceTime(){"
      "if (!raceStarted) return;"
      "fetch('/race-time').then(r=>r.json()).then(js=>{"
        "if(js.started){"
          "document.getElementById('raceTime').innerText='⏱ Time: ' + js.time;"
        "}"
      "});"
    "}"
    "function updateLastScan(){"
      "fetch('/last-scan').then(r=>r.json()).then(js=>{"
        "if(js.number){"
          "document.getElementById('lastScan').innerText = `✅ Last scanned: ${js.number} at ${js.time}`;"
        "}"
      "});"
    "}"
    "function load(){fetch('/data').then(r=>r.json()).then(js=>{let h='';"
    "js.forEach(t=>{h += `<tr><td>${t.place || '-'}</td><td>${t.number}</td><td>${t.duration || '-'}</td></tr>`;});"
    "document.getElementById('rows').innerHTML=h;});}"
    "setInterval(load, 2000);"
    "setInterval(updateRaceTime, 1000);"
    "setInterval(updateLastScan, 2000);"
    "load();"
    "</script></body></html>";

  server.send(200, "text/html", html);
}

void handleJson() {
  String json = "[";
  for (int i = 0; i < tagCount; i++) {
    TagInfo& t = tags[i];
    json += "{\"number\":\"" + t.number + "\",";
    json += "\"duration\":" + (t.endRecorded ? "\"" + msToMMSS(t.endSeen - startRaceTime) + "\"" : "null") + ",";
    json += "\"place\":" + String(t.place) + "}";
    if (i < tagCount - 1) json += ",";
  }
  json += "]";
  server.send(200, "application/json", json);
}

void handleExportCSV() {
  String csv = "place,number,duration\n";
  for (int i = 0; i < tagCount; i++) {
    TagInfo& t = tags[i];
    if (t.endRecorded) {
      csv += String(t.place) + "," + t.number + "," + msToMMSS(t.endSeen - startRaceTime) + "\n";
    }
  }
  server.send(200, "text/csv", csv);
}

void handleWritePage() {
  String html = "<!DOCTYPE html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Write RFID</title><style>"
    "body{font-family:sans-serif;padding:1em;text-align:center}"
    "input,button{padding:10px;margin:10px;width:90%;max-width:300px;font-size:16px}"
    "button{background:#3f51b5;color:white;border:none;border-radius:5px}"
    "</style></head><body><h2>Write to RFID Tag</h2>"
    "<form action='/write' method='GET'>"
    "<input name='num' placeholder='Enter number' required>"
    "<button type='submit'>Start Writing</button></form></body></html>";
  server.send(200, "text/html", html);
}

void handleWriteAPI() {
  if (!server.hasArg("num")) {
    server.send(400, "text/html", "<html><body><h3>❌ Missing number</h3><a href='/write-ui'>← Back</a></body></html>");
    return;
  }

  String number = server.arg("num");

  String html = "<html><head><meta http-equiv='refresh' content='5;url=/write-ui' /></head><body>"
                "<h3>📡 Please present the tag now...</h3><p>This will return in 5 seconds.</p></body></html>";
  server.send(200, "text/html", html);
  delay(100);

  isWriting = true;
  unsigned long start = millis();
  while (millis() - start < 5000) {
    if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
      byte block = 1;
      byte data[16] = {0};
      for (int i = 0; i < 16 && i < number.length(); i++)
        data[i] = number[i];

      MFRC522::MIFARE_Key key;
      for (byte i = 0; i < 6; i++) key.keyByte[i] = 0xFF;

      MFRC522::StatusCode status = rfid.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, block, &key, &rfid.uid);
      if (status == MFRC522::STATUS_OK)
        status = rfid.MIFARE_Write(block, data, 16);

      rfid.PICC_HaltA();
      rfid.PCD_StopCrypto1();
      isWriting = false;

      Serial.print("Write status: ");
      Serial.println(status == MFRC522::STATUS_OK ? "Success" : "Fail");
      return;
    }
  }

  isWriting = false;
  Serial.println("Timeout: No card detected");
}
