#include <Wire.h>
#include <WiFi.h>
#include <time.h>
#include <WebServer.h>
#include <FS.h>
#include <SPIFFS.h>
#include <LiquidCrystal_I2C.h>

// Replace with your network credentials
const char* ssid = "Wifi_SSID";
const char* password = "Wifi_Password";

// Time zone offset in seconds for WIB (UTC+7)
const long GMT_OFFSET_SEC = 7 * 3600;
const int DAYLIGHT_OFFSET_SEC = 0;

// NTP server
const char* ntpServer = "pool.ntp.org";

// LCD address and size (adjust according to your LCD module)
LiquidCrystal_I2C lcd(0x27, 16, 2); // Change address and size if necessary

// Relay control pin (adjust based on your setup)
const int relayPin = 18; // GPIO 18 for relay control

// Create an instance of the web server
WebServer server(80);

// Global variables
bool relayState = false;
String scheduledTime = "";
int scheduledDuration = 0;

// Function prototypes
void connectToWiFi();
String getIndexHTML();
void updateLCDTime();
void checkScheduledActivation();
void scheduleRelayActivation(String scheduledTime, int durationSeconds);
void toggleRelay();

void setup() {
  // Initialize Serial Monitor
  Serial.begin(115200);

  // Initialize I2C communication for the LCD
  Wire.begin();
  lcd.init();
  lcd.backlight();

  // Connect to WiFi
  connectToWiFi();

  // Initialize time
  configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, ntpServer);

  // Initialize SPIFFS
  if (!SPIFFS.begin(true)) {
    Serial.println("An Error has occurred while mounting SPIFFS");
    return;
  }

  // Relay setup
  pinMode(relayPin, OUTPUT);
  digitalWrite(relayPin, LOW); // Ensure relay is initially off
  relayState = false;

  // Set up the LCD display
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Initializing...");

  // Set up the web server routes
  server.on("/", HTTP_GET, [](){
    server.sendHeader("Connection", "close");
    server.send(200, "text/html", getIndexHTML());
  });

  server.on("/time", HTTP_GET, [](){
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
      server.send(200, "text/plain", "Failed to obtain time");
      return;
    }
    char timeStr[32];
    strftime(timeStr, sizeof(timeStr), "%H:%M:%S", &timeinfo);
    server.send(200, "text/plain", timeStr);
  });

  server.on("/style.css", HTTP_GET, [](){
    File file = SPIFFS.open("/style.css", "r");
    if (!file) {
      server.send(404, "text/plain", "Style CSS file not found");
      return;
    }
    server.streamFile(file, "text/css");
    file.close();
  });

  server.on("/script.js", HTTP_GET, [](){
    File file = SPIFFS.open("/script.js", "r");
    if (!file) {
      server.send(404, "text/plain", "Script JS file not found");
      return;
    }
    server.streamFile(file, "application/javascript");
    file.close();
  });

  server.on("/submit", HTTP_POST, [](){
    if (server.hasArg("time") && server.hasArg("duration")) {
      String time = server.arg("time");
      String duration = server.arg("duration");

      Serial.println("Received Time: " + time);
      Serial.println("Received Duration: " + duration);

      // Parse duration into minutes and seconds
      int minutes = duration.substring(0, 2).toInt();
      int seconds = duration.substring(3).toInt();

      // Calculate total seconds
      int totalSeconds = minutes * 60 + seconds;

      // Schedule relay activation
      scheduleRelayActivation(time, totalSeconds);

      server.send(200, "text/plain", "Schedule set successfully");
    } else {
      server.send(400, "text/plain", "Invalid Request");
    }
  });

  server.on("/toggle", HTTP_GET, [](){
    toggleRelay();
    server.send(200, "text/plain", relayState ? "Relay is ON" : "Relay is OFF");

    // Update LCD with current relay status
    lcd.setCursor(0, 1);
    lcd.print(relayState ? "Relay is ON " : "Relay is OFF");
  });

  server.begin();
}

void loop() {
  // Handle web server
  server.handleClient();

  // Update LCD with current time
  updateLCDTime();

  // Check scheduled activation
  checkScheduledActivation();

  // Optional: Handle other tasks here
}

void connectToWiFi() {
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected.");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

  // Update LCD with WiFi connection status
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("WiFi Connected");
}

String getIndexHTML() {
  return R"(
  <!DOCTYPE html>
  <html lang="en">
  <head>
      <meta charset="UTF-8">
      <meta name="viewport" content="width=device-width, initial-scale=1.0">
      <title>Relay Control</title>
      <link rel="stylesheet" href="/style.css">
      <script src="https://code.jquery.com/jquery-3.6.0.min.js"></script>
      <style>
        body {
          font-family: 'Arial', sans-serif;
          background-color: #f4f4f4;
          margin: 0;
          padding: 0;
        }
        .container {
          background-color: #fff;
          max-width: 400px;
          margin: 40px auto;
          padding: 20px;
          box-shadow: 0 4px 8px rgba(0,0,0,0.1);
          border-radius: 8px;
          text-align: center;
        }
        .form-group {
          margin-bottom: 15px;
        }
        label {
          display: block;
          margin-bottom: 5px;
        }
        input[type="time"],
        input[type="text"] {
          width: calc(100% - 20px);
          padding: 10px;
          margin-bottom: 10px;
          border-radius: 4px;
          border: 1px solid #ddd;
          font-size: 16px;
        }
        #submitBtn, #manualToggle {
          width: calc(100% - 20px);
          padding: 10px;
          border: none;
          background-color: #007bff;
          color: white;
          border-radius: 4px;
          cursor: pointer;
        }
        #submitBtn:hover, #manualToggle:hover {
          background-color: #0056b3;
        }
        #response {
          margin-top: 20px;
          font-size: 16px;
          color: green;
        }
      </style>
  </head>
  <body>
      <div class="container">
          <h1>Relay Control</h1>
          <form id="relayForm">
              <div class="form-group">
                  <label for="time">Schedule Time:</label>
                  <input type="time" id="time" name="time" required>
              </div>
              <div class="form-group">
                  <label for="duration">Duration (minutes:seconds):</label>
                  <input type="text" id="duration" name="duration" pattern="\d{1,2}:\d{2}" required placeholder="MM:SS">
                  <small>Format: MM:SS</small>
              </div>
              <button type="button" id="submitBtn">Set Schedule</button>
          </form>
          <button type="button" id="manualToggle">Toggle Relay</button>
          <div id="response"></div>
      </div>
      <script>
        $(document).ready(function() {
          $('#submitBtn').click(function() {
            var time = $('#time').val();
            var duration = $('#duration').val();
            $.post('/submit', { time: time, duration: duration }, function(response) {
              $('#response').text(response).css('color', 'green');
            }).fail(function() {
              $('#response').text('Failed to set schedule').css('color', 'red');
            });
          });

          $('#manualToggle').click(function() {
            $.get('/toggle', function(response) {
              $('#response').text(response).css('color', 'green');
            }).fail(function() {
              $('#response').text('Failed to toggle relay').css('color', 'red');
            });
          });
        });
      </script>
  </body>
  </html>
  )";
}


void updateLCDTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
    return;
  }
  char timeStr[32];
  strftime(timeStr, sizeof(timeStr), "%H:%M:%S", &timeinfo);

  // Update LCD with current time
  lcd.setCursor(0, 0);
  lcd.print("Time: ");
  lcd.print(timeStr);
}

void scheduleRelayActivation(String time, int durationSeconds) {
  // Set the scheduled time and duration
  scheduledTime = time;
  scheduledDuration = durationSeconds;

  Serial.print("Relay scheduled for: ");
  Serial.println(scheduledTime);
  Serial.print("Duration: ");
  Serial.print(scheduledDuration);
  Serial.println(" seconds");
}

void checkScheduledActivation() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
    return;
  }
  char currentTime[6];
  strftime(currentTime, sizeof(currentTime), "%H:%M", &timeinfo);

  if (scheduledTime == String(currentTime)) {
    // Activate relay for the scheduled duration
    digitalWrite(relayPin, HIGH);
    relayState = true;
    delay(scheduledDuration * 1000);
    digitalWrite(relayPin, LOW);
    relayState = false;
    scheduledTime = ""; // Reset scheduled time
  }
}

void toggleRelay() {
  relayState = !relayState;
  digitalWrite(relayPin, relayState ? HIGH : LOW);
}
