#include <Arduino.h>
#include <HardwareSerial.h>
#include <SPI.h>
#include <SD.h>

// --------------------- SD Card Pin Definitions ---------------------
#define SD_CS   PA4    // CS
#define SD_SCK  PA5    // SCK
#define SD_MISO PA6    // MISO
#define SD_MOSI PA7    // MOSI

// --------------------- UART1 for GPS & Debug -----------------------
HardwareSerial Serial1(PA10, PA9); // RX=PA10, TX=PA9

// --------------------- GPS Buffer & Timezone -----------------------
char buffer[128];
int bufferIndex = 0;

// Time zone offset in hours (for IST: +5:30)
const int TZ_HOURS   = 5;
const int TZ_MINUTES = 30;

// --------------------- GPS Data Struct -----------------------
struct GpsData {
  char date[7];    // DDMMYY
  char time[7];    // HHMMSS
  char lat[11];
  char ns;         // 'N' or 'S'
  char lon[11];
  char ew;         // 'E' or 'W'
  float speed;     // in km/h
  char mode;       // 'A'=Active, 'V'=Void
  bool dataReady;  // true if we have a valid RMC parse
} gpsData;

// --------------------- Function: Adjust Time for Timezone ---------------------
void adjustTimeZone(const char* utcTime, char* localTime) {
  // utcTime is "HHMMSS" from the RMC sentence (UTC)
  // localTime will receive "HHMMSS" after adding TZ offset
  int hours   = (utcTime[0] - '0') * 10 + (utcTime[1] - '0');
  int minutes = (utcTime[2] - '0') * 10 + (utcTime[3] - '0');
  int seconds = (utcTime[4] - '0') * 10 + (utcTime[5] - '0');

  // Add timezone offset
  minutes += TZ_MINUTES;
  hours   += TZ_HOURS + (minutes / 60);
  minutes  = minutes % 60;
  hours    = hours % 24;

  // Format back: HHMMSS
  sprintf(localTime, "%02d%02d%02d", hours, minutes, seconds);
}

// --------------------- Function: Parse RMC Sentence ---------------------
void parseRMC(char* sentence) {
  // Example: $GPRMC,HHMMSS,A,lat,NS,lon,EW,speed,angle,date,...
  // or       $GNRMC,HHMMSS,A,lat,NS,lon,EW,speed,angle,date,...
  char* token = strtok(sentence, ",");
  int fieldCount = 0;
  char tempTime[7];

  // Reset dataReady before parsing
  gpsData.dataReady = false;

  while (token != NULL) {
    switch(fieldCount) {
      case 1: // Time (UTC)
        if (strlen(token) >= 6) {
          strncpy(tempTime, token, 6);
          tempTime[6] = '\0';
        }
        break;
      case 2: // Status (A=Active, V=Void)
        if (strlen(token) > 0) {
          gpsData.mode = token[0];
        }
        break;
      case 3: // Latitude
        if (strlen(token) > 0) {
          strncpy(gpsData.lat, token, 10);
          gpsData.lat[10] = '\0';
        }
        break;
      case 4: // N/S
        if (strlen(token) > 0) {
          gpsData.ns = token[0];
        }
        break;
      case 5: // Longitude
        if (strlen(token) > 0) {
          strncpy(gpsData.lon, token, 10);
          gpsData.lon[10] = '\0';
        }
        break;
      case 6: // E/W
        if (strlen(token) > 0) {
          gpsData.ew = token[0];
        }
        break;
      case 7: // Speed in knots
        if (strlen(token) > 0) {
          gpsData.speed = atof(token) * 1.852; // Convert knots -> km/h
        } else {
          gpsData.speed = 0.0;
        }
        break;
      case 9: // Date (DDMMYY)
        if (strlen(token) >= 6) {
          strncpy(gpsData.date, token, 6);
          gpsData.date[6] = '\0';
          gpsData.dataReady = true;
        }
        break;
    }
    token = strtok(NULL, ",");
    fieldCount++;
  }

  // After parsing, adjust time for local time zone
  if (gpsData.dataReady && gpsData.mode == 'A') {
    adjustTimeZone(tempTime, gpsData.time);

    // For debugging on Serial1 (same as your approach)
    Serial1.print(gpsData.date);
    Serial1.print(",");
    Serial1.print(gpsData.time);
    Serial1.print(",");
    Serial1.print(gpsData.lon);
    Serial1.print(gpsData.ew);
    Serial1.print(",");
    Serial1.print(gpsData.lat);
    Serial1.print(gpsData.ns);
    Serial1.print(",");
    Serial1.println(gpsData.speed, 1);
  }
  else {
    // Print "gps signal lost" if mode=V
    if (gpsData.mode == 'V') {
      Serial1.println("gps signal lost");
    }
  }
}

// --------------------- 5-Second Logging Interval ---------------------
unsigned long lastLogTime = 0;
const unsigned long LOG_INTERVAL_MS = 5000;

// --------------------- SETUP: Combine Your SD Init + GPS Init ---------------------
void setup() {
  // 1) Start Serial1 at 9600 for GPS & debug
  Serial1.begin(9600);
  delay(1000);

  // 2) Print your SD Card messages (from your second code)
  Serial1.println("\nSD Card Test - Level Shifter Version");

  // Configure SPI pins
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH); // Deselect SD card

  // Initialize SPI with conservative settings
  SPI.begin();
  SPI.setClockDivider(SPI_CLOCK_DIV128); // Very slow speed
  SPI.setDataMode(SPI_MODE0);
  SPI.setBitOrder(MSBFIRST);

  Serial1.println("\nPower-up sequence:");
  digitalWrite(SD_CS, HIGH);
  delay(100);

  // Send dummy clock cycles
  for(int i = 0; i < 10; i++) {
    SPI.transfer(0xFF);
    Serial1.print(".");
  }
  Serial1.println(" Done");

  delay(100);

  // Print your SD init debug info
  Serial1.println("\nInitializing SD card...");
  Serial1.println("Using level-shifted configuration:");
  Serial1.println("CS   -> PA4  (with level shifter)");
  Serial1.println("SCK  -> PA5  (with level shifter)");
  Serial1.println("MISO -> PA6  (with level shifter)");
  Serial1.println("MOSI -> PA7  (with level shifter)");
  Serial1.println("VCC  -> 5V   (to module's VCC)");
  Serial1.println("GND  -> GND");

  // Attempt SD begin
  if (!SD.begin(SD_CS)) {
    Serial1.println("\nSD card initialization failed!");
    Serial1.println("\nTroubleshooting for level-shifted module:");
    Serial1.println("1. Power supply:");
    Serial1.println("   - Connect module's VCC to 5V (not 3.3V)");
    Serial1.println("   - Ensure good ground connection");
    Serial1.println("2. Check voltage levels:");
    Serial1.println("   - Module VCC should read ~5V");
    Serial1.println("   - Card VCC should read ~3.3V (from LDO)");
    Serial1.println("3. Level shifter connections:");
    Serial1.println("   - All signals should be properly level shifted");
    Serial1.println("   - No additional pullup resistors needed");
    Serial1.println("4. Try these steps:");
    Serial1.println("   - Power cycle the entire system");
    Serial1.println("   - Inspect the level shifter ICs");
    Serial1.println("   - Check for any burnt components");
    while(1);
  }

  Serial1.println("SD card initialization successful!");

  // Test file operations (from your second code)
  File dataFile = SD.open("test.txt", FILE_WRITE);
  if (dataFile) {
    Serial1.println("\nWriting to test.txt...");
    dataFile.println("Testing SD card with level shifter");
    dataFile.println("Module is working properly!");
    dataFile.close();
    Serial1.println("Write successful!");

    // Read back the file
    dataFile = SD.open("test.txt");
    if (dataFile) {
      Serial1.println("\nReading file contents:");
      while (dataFile.available()) {
        Serial1.write(dataFile.read());
      }
      dataFile.close();
      Serial1.println("\nRead successful!");
    }
  } else {
    Serial1.println("Error opening test file!");
  }

  // 3) Clear GPS data struct
  memset(&gpsData, 0, sizeof(gpsData));
}

// --------------------- LOOP: Read GPS + Log to SD every 5s ---------------------
void loop() {
  // ------------------ A) GPS Parsing (From your first code) ------------------
  while (Serial1.available() > 0) {
    char c = Serial1.read();

    if (c == '$') { // Start of NMEA
      bufferIndex = 0;
    }
    else if (c == '\n' || c == '\r') { // End of NMEA
      if (bufferIndex > 0) {
        buffer[bufferIndex] = '\0';
        // Check if it's RMC
        if (strstr(buffer, "GNRMC") || strstr(buffer, "GPRMC")) {
          parseRMC(buffer);
        }
      }
      bufferIndex = 0;
    }
    else if (bufferIndex < sizeof(buffer) - 1) {
      buffer[bufferIndex++] = c;
    }
  }

  // ------------------ B) Every 5 seconds, Log GPS Data to SD ------------------
  unsigned long currentMillis = millis();
  if (currentMillis - lastLogTime >= LOG_INTERVAL_MS) {
    lastLogTime = currentMillis;

    // Only log if GPS has a valid fix (mode = 'A') and dataReady = true
    if (gpsData.dataReady && gpsData.mode == 'A') {
      File dataFile = SD.open("gps_log.txt", FILE_WRITE);
      if (dataFile) {
        // Format: DATE,TIME,LONG+EW,LAT+NS,SPEED
        dataFile.print(gpsData.date);
        dataFile.print(",");
        dataFile.print(gpsData.time);
        dataFile.print(",");
        dataFile.print(gpsData.lon);
        dataFile.print(gpsData.ew);
        dataFile.print(",");
        dataFile.print(gpsData.lat);
        dataFile.print(gpsData.ns);
        dataFile.print(",");
        dataFile.println(gpsData.speed, 1);
        dataFile.close();

        // Also print to Serial1 so you can debug
        Serial1.println("Data logged to SD (gps_log.txt). Logged line:");
        Serial1.print(gpsData.date);  Serial1.print(",");
        Serial1.print(gpsData.time);  Serial1.print(",");
        Serial1.print(gpsData.lon);   Serial1.print(gpsData.ew);
        Serial1.print(",");
        Serial1.print(gpsData.lat);   Serial1.print(gpsData.ns);
        Serial1.print(",");
        Serial1.println(gpsData.speed, 1);
      }
      else {
        Serial1.println("Error opening gps_log.txt for writing!");
      }
    }
    else if (gpsData.mode == 'V') {
      // If we lost fix, just let us know
      Serial1.println("GPS fix is void (gps signal lost) - Not logging.");
    }
    else {
      // data not ready yet
      Serial1.println("GPS data not ready yet - Not logging.");
    }
  }
}
