/* This sketch connects to the iopappstore and loads the assigned firmware down. The assignment is done on the server based on the MAC address of the board

    On the server, you need PHP script "IOTappStory.php" and the bin files are in the .\bin folder

    This work is based on the ESPhttpUpdate examples

    To add new constants in WiFiManager search for "NEW CONSTANTS" and insert them according the "boardName" example

  Copyright (c) [2016] [Andreas Spiess]

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.

*/

/*
   Setup till done: Blink
   ON: green LED completely on
   OFF: Green LED blinking with very short on-time
   Setup: very fast blinking green LED

*/

#define VERSION "V1.0"
#define FIRMWARE "WemosShieldClock " VERSION

#define SERIALDEBUG         // Serial is used to present debugging messages 
#define REMOTEDEBUGGING     // telnet is used to present
//#define BOOTSTATISTICS    // send bootstatistics to Sparkfun

#define LEDS_INVERSE   // LEDS on = GND

#include <credentials.h>
#include <ESP8266WiFi.h>
#include <EEPROM.h>
#include <ESP8266httpUpdate.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266mDNS.h>
#include "RemoteDebug.h"        //https://github.com/JoaoLopesF/RemoteDebug
#include <WiFiManager.h>        //https://github.com/kentaylor/WiFiManager
#include <Ticker.h>
#include <SNTPtime.h>
#include <Wire.h>  // Include Wire if you're using I2C
#include <SFE_MicroOLED.h>  // Include the SFE_MicroOLED library

extern "C" {
#include "user_interface.h" // this is for the RTC memory read/write functions
}


// -------- PIN DEFINITIONS ------------------
#ifdef ARDUINO_ESP8266_ESP01           // Generic ESP's 
#define GPIO0 0
#define LEDgreen 13
//#define LEDred 12
#define RELAYPIN 15
#else
#define GPIO0 D3
#define LEDgreen D7
//#define LEDred D6
#define PIRpin D5
#define RELAYPIN D6
#endif

//---------- CODE DEFINITIONS ----------
#define MAXDEVICES 5
#define STRUCT_CHAR_ARRAY_SIZE 50  // length of config variables
#define SERVICENAME "WemosShieldClock"  // name of the MDNS service used in this group of ESPs
#define DELAYSEC 7 *60  // 7 minutes
#define MAX_WIFI_RETRIES 50

#define PIN_RESET 255  //
#define DC_JUMPER 0  // I2C Addres: 0 - 0x3C, 1 - 0x3D

#define RTCMEMBEGIN 68
#define MAGICBYTE 85



//-------- SERVICES --------------

WiFiServer server(80);
Ticker blink;

// remoteDebug
#ifdef REMOTEDEBUGGING
RemoteDebug Debug;
#endif


MicroOLED oled(PIN_RESET, DC_JUMPER);  // I2C Example
SNTPtime NTPch("ch.pool.ntp.org");


//--------- ENUMS AND STRUCTURES  -------------------

typedef struct {
  char ssid[STRUCT_CHAR_ARRAY_SIZE];
  char password[STRUCT_CHAR_ARRAY_SIZE];
  char boardName[STRUCT_CHAR_ARRAY_SIZE];
  char IOTappStory1[STRUCT_CHAR_ARRAY_SIZE];
  char IOTappStoryPHP1[STRUCT_CHAR_ARRAY_SIZE];
  char IOTappStory2[STRUCT_CHAR_ARRAY_SIZE];
  char IOTappStoryPHP2[STRUCT_CHAR_ARRAY_SIZE];
  // insert NEW CONSTANTS according boardname example HERE!
  char magicBytes[4];
} strConfig;

strConfig config = {
  mySSID,
  myPASSWORD,
  "WemosShieldClock",
  "192.168.0.200",
  "/IOTappStory/IOTappStoryv20.php",
  "iotappstory.org",
  "/ota/esp8266-v1.php",
  "CFG"  // Magic Bytes
};

typedef struct {
  byte markerFlag;
  int bootTimes;
} rtcMemDef __attribute__((aligned(4)));
rtcMemDef rtcMem;

/*
   The structure contains following fields:
   struct strDateTime
   {
   byte hour;
   byte minute;
   byte second;
   int year;
   byte month;
   byte day;
   byte dayofWeek;
   boolean valid;
   };
*/
strDateTime dateTime;

//---------- VARIABLES ----------

String boardName, IOTappStory1, IOTappStoryPHP1, IOTappStory2, IOTappStoryPHP2; // add NEW CONSTANTS according boardname example

long delayCount = -1;

char boardMode = 'N';  // Normal operation or Configuration mode?

volatile unsigned long buttonEntry, buttonTime;
volatile bool buttonChanged = false;
volatile int greenTimesOff = 0;
volatile int redTimesOff = 0;
volatile int greenTimes = 0;
volatile int redTimes = 0;

unsigned long infoEntry;

bool relayState = 0;

int delayCounter = 0; // counts every second

// Use these variables to set the initial time
int hours = 11;
int minutes = 50;
int seconds = 30;

// How fast do you want the clock to spin? Set this to 1 for fun.
// Set this to 1000 to get _about_ 1 second timing.
const int CLOCK_SPEED = 1000;

// Global variables to help draw the clock face:
const int MIDDLE_Y = oled.getLCDHeight() / 2;
const int MIDDLE_X = oled.getLCDWidth() / 2;

int CLOCK_RADIUS;
int POS_12_X, POS_12_Y;
int POS_3_X, POS_3_Y;
int POS_6_X, POS_6_Y;
int POS_9_X, POS_9_Y;
int S_LENGTH;
int M_LENGTH;
int H_LENGTH;

unsigned long lastDraw = 0;



//---------- FUNCTIONS ----------
void loopWiFiManager(void);
void readFullConfiguration(void);
bool readRTCmem(void);
void printRTCmem(void);
void switchRelay(bool);
bool handleWiFi(void);

//---------- OTHER .H FILES ----------
#include <ESP_Helpers.h>
#include "WiFiManager_Helpers.h"
#include <SparkfunReport.h>


//-------------------------- SETUP -----------------------------------------

void setup() {
  Serial.begin(115200);
  for (int i = 0; i < 5; i++) DEBUG_PRINTLN("");
  DEBUG_PRINTLN("Start " FIRMWARE);


  // ----------- PINS ----------------
  pinMode(GPIO0, INPUT_PULLUP);  // GPIO0 as input for Config mode selection

#ifdef LEDgreen
  pinMode(LEDgreen, OUTPUT);
  digitalWrite(LEDgreen, LEDOFF);
#endif
#ifdef LEDred
  pinMode(LEDred, OUTPUT);
  digitalWrite(LEDred, LEDOFF);
#endif

  blink.detach();

digitalWrite(RELAYPIN,HIGH);

  // ------------- INTERRUPTS ----------------------------
  attachInterrupt(GPIO0, ISRbuttonStateChanged, CHANGE);


  Serial.print("GPIO0 ");
  Serial.println(GPIO0);
  //------------- LED and DISPLAYS ------------------------
  LEDswitch(GreenBlink);


  // --------- BOOT STATISTICS ------------------------
  // read and increase boot statistics (optional)
  readRTCmem();
  rtcMem.bootTimes++;
  writeRTCmem();
  printRTCmem();


  //---------- SELECT BOARD MODE -----------------------------

  system_rtc_mem_read(RTCMEMBEGIN + 100, &boardMode, 1);   // Read the "boardMode" flag RTC memory to decide, if to go to config
  if (boardMode == 'C') configESP();


  DEBUG_PRINTLN("------------- Normal Mode -------------------");
  REMOTEDEBUG_PRINTLN("------------- Normal Mode -------------------");

  // --------- START WIFI --------------------------
  readFullConfiguration();
  WiFi.mode(WIFI_STA);
  WiFi.begin();

  if (!isNetworkConnected()) {
    DEBUG_PRINTLN("");
    DEBUG_PRINTLN("No Connection. Try to connect with saved PW");
    WiFi.begin(config.ssid, config.password);  // if password forgotten by firmwware try again with stored PW
    if (!isNetworkConnected()) espRestart('C', "Going into Configuration Mode"); // still no success
  }
  else {
    DEBUG_PRINTLN("");
    DEBUG_PRINTLN("WiFi connected");
    getMACaddress();
    printMacAddress();
    DEBUG_PRINT("IP Address: ");
    DEBUG_PRINTLN(WiFi.localIP());

#ifdef REMOTEDEBUGGING
    remoteDebugSetup();
    REMOTEDEBUG_PRINTLN(config.boardName);
#endif

#ifdef BOOTSTATISTICS
    sendSparkfun();   // send boot statistics to sparkfun
#endif

    // Register host name in WiFi and mDNS
    String hostNameWifi = boardName;   // boardName is device name
    hostNameWifi.concat(".local");
    WiFi.hostname(hostNameWifi);
    if (MDNS.begin(config.boardName)) {
      DEBUG_PRINT("* MDNS responder started. http://");
      DEBUG_PRINTLN(hostNameWifi);
      MDNS.addService("SERVICENAME", "tcp", 8080);
    } else espRestart('C', "No Credentials");

    // ----------- SPECIFIC SETUP CODE ----------------------------
    while (!NTPch.setSNTPtime()) Serial.print("!"); // set internal clock
    Serial.println();
    Serial.println("Time set");

    dateTime = NTPch.getTime(1.0, 1); // get time from internal clock
    NTPch.printDateTime(dateTime);


    oled.begin();     // Initialize the OLED
    oled.clear(PAGE); // Clear the display's internal memory
    oled.clear(ALL);  // Clear the library's display buffer
    oled.display();   // Display what's in the buffer (splashscreen)

    initClockVariables();

    oled.clear(ALL);
    drawFace();
    drawArms(hours, minutes, seconds);
    oled.display(); // display the memory buffer drawn

    // ----------- END SPECIFIC SETUP CODE ----------------------------

  }  // End WiFi necessary

  LEDswitch(None);

    pinMode(GPIO0, INPUT_PULLUP);  // GPIO0 as input for Config mode selection
  DEBUG_PRINTLN("setup done");
}
//--------------- LOOP ----------------------------------
void loop() {
  //-------- Standard Block ---------------
  if (buttonChanged && buttonTime > 4000) espRestart('C', "Going into Configuration Mode");  // long button press > 4sec
  if (buttonChanged && buttonTime > 500 && buttonTime < 4000) IOTappStory(); // long button press > 1sec
  buttonChanged = false;
#ifdef REMOTEDEBUGGING
  Debug.handle();
  yield();
  //-------- End Standard Block ---------------

  // ------- Debug Message --------
  if (Debug.ative(Debug.INFO) && (millis() - infoEntry) > 5000) {
    Debug.printf("Firmware: %s", FIRMWARE);
    Debug.printf(" buttonChanged: %d", buttonChanged);
    Debug.printf(" buttonTime: %d", buttonTime);

    Debug.print("Heap ");
    Debug.println(ESP.getFreeHeap());
    infoEntry = millis();
  }
#endif
  // ------------------------------------

  if (lastDraw + CLOCK_SPEED < millis())
  {
    lastDraw = millis();
    // Add a second, update minutes/hours if necessary:
    updateTimeNet();

    // Draw the clock:
    oled.clear(PAGE);  // Clear the buffer
    drawFace();  // Draw the face to the buffer
    drawArms(hours, minutes, seconds);  // Draw arms to the buffer
    oled.display(); // Draw the memory buffer
  }
}
//------------------------- END LOOP --------------------------------------------


bool handleWiFi() {
  WiFiClient client = server.available();
  if (!client) return false;
  else {
    DEBUG_PRINTLN("new client");
    int timeOut = 10000;
    while (!client.available() && timeOut-- > 0) delay(1);
    if (timeOut <= 0) return false;
    else {
      // Read the first line of the request
      String request = client.readStringUntil('\r');
      DEBUG_PRINT("Request "); DEBUG_PRINTLN(request);
      client.flush();

      // Match the request
      if (request.indexOf("/SWITCH=ON") != -1) {
        delayCount = DELAYSEC;    // Set timer to count back
        switchRelay(ON);
      }
      if (request.indexOf("/SWITCH=OFF") != -1) {
        delayCount = 0;
        switchRelay(OFF);
      }
      if (request.indexOf("/STATUS") != -1) {
        DEBUG_PRINTLN("Status request ");
        REMOTEDEBUG_PRINTLN("STATUS request received ");
      }
      // Return the response
      client.println("HTTP/1.1 200 OK");
      client.println("Content-Type: text/html");
      client.println(""); //  do not forget this one
      client.println("<!DOCTYPE HTML>");
      client.println("<html><h2>");
      client.print("BOARD: ");
      client.print(config.boardName);
      client.print(" STATUS: ");
      if (relayState == ON) {
        client.print("On ");
        client.print(delayCount);
        client.print(" sec");
      }
      else client.println("Off ");

      client.println("<br><br>");
      client.println("Click <a href=\"/SWITCH=ON\">here</a> turn the SWITCH on pin 12 ON<br>");
      client.println("Click <a href=\"/SWITCH=OFF\">here</a> turn the SWITCH on pin 12 OFF<br>");
      client.println("Click <a href=\"/STATUS\">here</a> get status<br>");
      client.println("</h2></html>");
      delay(1);
      DEBUG_PRINTLN("Client disconnected");
      DEBUG_PRINTLN("");
      return true;
    }
  }
}

void readFullConfiguration() {
  readConfig();  // configuration in EEPROM
  // insert NEW CONSTANTS according switchName1 example
}


bool readRTCmem() {
  bool ret = true;
  system_rtc_mem_read(RTCMEMBEGIN, &rtcMem, sizeof(rtcMem));
  if (rtcMem.markerFlag != MAGICBYTE) {
    rtcMem.markerFlag = MAGICBYTE;
    rtcMem.bootTimes = 0;
    system_rtc_mem_write(RTCMEMBEGIN, &rtcMem, sizeof(rtcMem));
    ret = false;
  }
  return ret;
}

void printRTCmem() {
  DEBUG_PRINTLN("");
  DEBUG_PRINTLN("rtcMem ");
  DEBUG_PRINT("markerFlag ");
  DEBUG_PRINTLN(rtcMem.markerFlag);
  DEBUG_PRINT("bootTimes ");
  DEBUG_PRINTLN(rtcMem.bootTimes);
}

void initClockVariables()
{
  // Calculate constants for clock face component positions:
  oled.setFontType(0);
  if (MIDDLE_X > MIDDLE_Y) CLOCK_RADIUS = MIDDLE_Y;
  else CLOCK_RADIUS = MIDDLE_X;
  CLOCK_RADIUS = CLOCK_RADIUS - 1;
  POS_12_X = MIDDLE_X - oled.getFontWidth();
  POS_12_Y = MIDDLE_Y - CLOCK_RADIUS + 2;
  POS_3_X  = MIDDLE_X + CLOCK_RADIUS - oled.getFontWidth() - 1;
  POS_3_Y  = MIDDLE_Y - oled.getFontHeight() / 2;
  POS_6_X  = MIDDLE_X - oled.getFontWidth() / 2;
  POS_6_Y  = MIDDLE_Y + CLOCK_RADIUS - oled.getFontHeight() - 1;
  POS_9_X  = MIDDLE_X - CLOCK_RADIUS + oled.getFontWidth() - 2;
  POS_9_Y  = MIDDLE_Y - oled.getFontHeight() / 2;

  // Calculate clock arm lengths
  S_LENGTH = CLOCK_RADIUS - 2;
  M_LENGTH = S_LENGTH * 0.7;
  H_LENGTH = S_LENGTH * 0.5;
}

void updateTimeNet() {
  dateTime = NTPch.getTime(1.0, 1); // get time from internal clock
  hours = dateTime.hour;
  minutes = dateTime.minute;
  seconds = dateTime.second;
}

// Draw the clock's three arms: seconds, minutes, hours.
void drawArms(int h, int m, int s)
{
  double midHours;  // this will be used to slightly adjust the hour hand
  static int hx, hy, mx, my, sx, sy;

  // Adjust time to shift display 90 degrees ccw
  // this will turn the clock the same direction as text:
  h -= 3;
  m -= 15;
  s -= 15;
  if (h <= 0)
    h += 12;
  if (m < 0)
    m += 60;
  if (s < 0)
    s += 60;

  // Calculate and draw new lines:
  s = map(s, 0, 60, 0, 360);  // map the 0-60, to "360 degrees"
  sx = S_LENGTH * cos(PI * ((float)s) / 180);  // woo trig!
  sy = S_LENGTH * sin(PI * ((float)s) / 180);  // woo trig!
  // draw the second hand:
  oled.line(MIDDLE_X, MIDDLE_Y, MIDDLE_X + sx, MIDDLE_Y + sy);

  m = map(m, 0, 60, 0, 360);  // map the 0-60, to "360 degrees"
  mx = M_LENGTH * cos(PI * ((float)m) / 180);  // woo trig!
  my = M_LENGTH * sin(PI * ((float)m) / 180);  // woo trig!
  // draw the minute hand
  oled.line(MIDDLE_X, MIDDLE_Y, MIDDLE_X + mx, MIDDLE_Y + my);

  midHours = minutes / 12; // midHours is used to set the hours hand to middling levels between whole hours
  h *= 5;  // Get hours and midhours to the same scale
  h += midHours;  // add hours and midhours
  h = map(h, 0, 60, 0, 360);  // map the 0-60, to "360 degrees"
  hx = H_LENGTH * cos(PI * ((float)h) / 180);  // woo trig!
  hy = H_LENGTH * sin(PI * ((float)h) / 180);  // woo trig!
  // draw the hour hand:
  oled.line(MIDDLE_X, MIDDLE_Y, MIDDLE_X + hx, MIDDLE_Y + hy);
}

// Draw an analog clock face
void drawFace()
{
  // Draw the clock border
  oled.circle(MIDDLE_X, MIDDLE_Y, CLOCK_RADIUS);

  // Draw the clock numbers
  oled.setFontType(0); // set font type 0, please see declaration in SFE_MicroOLED.cpp
  oled.setCursor(POS_12_X, POS_12_Y); // points cursor to x=27 y=0
  oled.print(12);
  oled.setCursor(POS_6_X, POS_6_Y);
  oled.print(6);
  oled.setCursor(POS_9_X, POS_9_Y);
  oled.print(9);
  oled.setCursor(POS_3_X, POS_3_Y);
  oled.print(3);
}

