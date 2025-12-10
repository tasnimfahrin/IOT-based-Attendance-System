/*
  IoT Attendance System - ESP8266 (NodeMCU) - FIXED
  - Hosts a simple webpage where 15 students choose their ID and submit attendance
  - Sends attendance to Google Sheets via Apps Script web app
  - Shows last scanned student + count of today's presents on 16x2 I2C LCD
*/

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include <LiquidCrystal_I2C.h>

// ---------- CONFIG ----------
const char* STA_SSID = "Classroom Attendance";           
const char* STA_PASSWORD = "Software3rd";      

const char* AP_SSID = "Attendance_AP";                
const char* AP_PASSWORD = "12345678";                 


// Google Apps Script Web App URL (deployed as Web app)
const char* GOOGLE_SCRIPT_URL = "https://script.google.com/macros/s/AKfycbyscqK2Ad9Bf7v-ocL6eEqbTNRbOXtyPbiUcN97mSYFy7Dh8JnO2hs3x2a6h0YHZsPmmg/exec";
// I2C LCD address and size (adjust if your module uses a different address)
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Web server on port 80
ESP8266WebServer server(80);

// Student dataset (15)
struct Student {
  const char* id;
  const char* name;
};

Student students[] = {
  {"222-134-002","Bipresh Kumar Das Talukder"},
  {"222-134-007","Tasnim Tabassum Fahrin"},
  {"222-134-008","Ayesha Siddika Urme"},
  {"222-134-012","Md.Naahe Uddin Laskar"},
  {"222-134-013","Mahbub Khandaker"},
  {"222-134-014","Nahida Ahmed Chowdhury"},
  {"222-134-015","Md. Shakil Talukder"},
  {"222-134-025","Khadiza Sultana Chowdhury Rimi"},
  {"222-134-026","Md. Absaruzzaman Omi"},
  {"222-134-027","Mahfuj Hussain"},
  {"222-134-028","Naima Rahman"},
  {"222-134-030","Md. Fardeen Hussain"},
  {"222-134-033","Sayara Zarin Chowdhury"},
  {"213-134-021","Md. Mushfiqur Rahman"},
  {"213-134-022","Musharof Ahmed"}
};
const int STUDENT_COUNT = sizeof(students)/sizeof(students[0]);

// Attendance counts (reset on reboot — for persistent counts use EEPROM or Sheet queries)
int todaysCount = 0;
String lastStudentName = "-";

// Forward declarations
void handleRoot();
void handleSubmit();
void notFoundHandler();
void updateLCD();
void sendToGoogle(const String& id, const String& name);
String urlEncode(const String &str);

void handleRoot() {
  String page = "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1'>";
  page += "<title>Attendance</title>";
  page += "<style>body{font-family:Arial, sans-serif;margin:10px} label,select,button{font-size:16px} button{padding:10px 16px}</style>";
  page += "</head><body>";
  page += "<h3>Attendance Submission</h3>";
  page += "<form action='/submit' method='POST'>";
  page += "<label for='sid'>Select Student:</label><br>";
  page += "<select name='studentId' id='sid'>";
  for (int i=0; i<STUDENT_COUNT; ++i) {
    page += "<option value='";
    page += students[i].id;
    page += "'>";
    page += students[i].id;
    page += " - ";
    page += students[i].name;
    page += "</option>";
  }
  page += "</select><br><br>";
  page += "<button type='submit'>Present</button>";
  page += "</form>";
  page += "<hr><p>Last scanned: " + lastStudentName + "</p>";
  page += "<p>Today's present count: " + String(todaysCount) + "</p>";
  page += "</body></html>";

  server.send(200, "text/html", page);
}

void handleSubmit() {
  if (server.method() != HTTP_POST && server.method() != HTTP_GET) {
    server.send(405, "text/plain", "Method not allowed");
    return;
  }

  String studentId = server.hasArg("studentId") ? server.arg("studentId") : "";
  String studentName = "-";
  for (int i=0;i<STUDENT_COUNT;i++){
    if (studentId == students[i].id) {
      studentName = students[i].name;
      break;
    }
  }

  // only update local state if a valid student was found
  if (studentName != "-") {
    lastStudentName = studentName;
    todaysCount++;
    updateLCD();
  } else {
    // invalid id submitted
    Serial.println("Invalid studentId submitted: " + studentId);
  }

  // show immediate confirmation page
  String resp = "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1'>";
  resp += "<title>Submitted</title></head><body>";
  if (studentName != "-") {
    resp += "<h3>Marked Present</h3>";
    resp += "<p>" + studentId + " - " + studentName + "</p>";
  } else {
    resp += "<h3>Error</h3><p>Unknown student ID: " + studentId + "</p>";
  }
  resp += "<p><a href='/'>Back</a></p>";
  resp += "</body></html>";
  server.send(200, "text/html", resp);

  // Fire-and-forget send to Google Sheets
  if (studentName != "-") sendToGoogle(studentId, studentName);
}

void updateLCD() {
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Last: ");
  String shortName = lastStudentName;
  if (shortName.length() > 10) shortName = shortName.substring(0, 10);
  char buf0[17];
  shortName.toCharArray(buf0, sizeof(buf0));
  lcd.print(buf0);

  lcd.setCursor(0,1);
  lcd.print("Today: ");
  char buf1[6];
  snprintf(buf1, sizeof(buf1), "%d", todaysCount);
  lcd.print(buf1);
}

void sendToGoogle(const String& id, const String& name) {
  // Build URL
  String url = String(GOOGLE_SCRIPT_URL) + "?";
  url += "studentId=" + urlEncode(id);
  url += "&name=" + urlEncode(name);
  url += "&status=present&source=esp8266";

  // Secure client
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient https;

  if (https.begin(client, url)) {
    int httpCode = https.GET();

    if (httpCode > 0) {
      String payload = https.getString();
      Serial.println("Google response: " + String(httpCode) + " " + payload);
    } else {
      Serial.println("Google send failed, error: " + https.errorToString(httpCode));
    }

    https.end();
  } else {
    Serial.println("Unable to start HTTPS connection");
  }
}

// URL-encode helper
String urlEncode(const String &str) {
  String encoded = "";
  char c;
  char bufHex[4];
  for (size_t i = 0; i < str.length(); i++) {
    c = str[i];
    if (('a' <= c && c <= 'z') ||
        ('A' <= c && c <= 'Z') ||
        ('0' <= c && c <= '9') ||
        c == '-' || c == '_' || c == '.' || c == '~') {
      encoded += c;
    } else {
      sprintf(bufHex, "%%%02X", (unsigned char)c);
      encoded += bufHex;
    }
  }
  return encoded;
}

void notFoundHandler(){
  server.send(404, "text/plain", "Not found");
}

void setup() {
  Serial.begin(115200);
  delay(100);
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0,0);
  lcd.print("Booting...");
  lcd.setCursor(0,1);
  lcd.print("Connecting WiFi");

  // Connect to WiFi - FIXED: Use correct variable names
  WiFi.mode(WIFI_STA);
  WiFi.begin(STA_SSID, STA_PASSWORD);

  Serial.print("Connecting to: ");
  Serial.println(STA_SSID);

  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 30) {
    delay(500);
    Serial.print(".");
    tries++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println();
    Serial.print("Connected. IP: ");
    Serial.println(WiFi.localIP());
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("IP:");
    char ipBuf[16];
    WiFi.localIP().toString().toCharArray(ipBuf, sizeof(ipBuf));
    lcd.setCursor(4,0);
    lcd.print(ipBuf);
    lcd.setCursor(0,1);
    lcd.print("Ready");
  } else {
    Serial.println();
    int status = WiFi.status();
    Serial.print("WiFi Failed. status=");
    Serial.println(status);
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("WiFi Failed");
    lcd.setCursor(0,1);
    lcd.print("Check SSID");
  }

  // Configure web server routes
  server.on("/", HTTP_GET, handleRoot);
  server.on("/submit", HTTP_POST, handleSubmit);
  server.on("/submit", HTTP_GET, handleSubmit);
  server.onNotFound(notFoundHandler);
  server.begin();
  Serial.println("HTTP server started");
}

void loop() {
  server.handleClient();
  delay(2);
}