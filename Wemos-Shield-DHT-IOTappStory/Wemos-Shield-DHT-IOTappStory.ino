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

#define VERSION "V1.1"
#define FIRMWARE "WEMOS_DHT " VERSION

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

#include "DHT.h"
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
#define RELAYPIN 12
#else
#define GPIO0 D3
#define LEDgreen D7
//#define LEDred D6
#define PIRpin D5
#define RELAYPIN D6
#endif

#define DHTPIN D4     // what pin we're connected to

// Uncomment whatever type you're using!
//#define DHTTYPE DHT11   // DHT 11
#define DHTTYPE DHT22   // DHT 22  (AM2302)
//#define DHTTYPE DHT21   // DHT 21 (AM2301)

#define PIN_RESET 255  //
#define DC_JUMPER 0  // I2C Addres: 0 - 0x3C, 1 - 0x3D

//---------- CODE DEFINITIONS ----------
#define MAXDEVICES 5
#define STRUCT_CHAR_ARRAY_SIZE 50  // length of config variables
#define MAX_WIFI_RETRIES 50

#define RTCMEMBEGIN 68
#define MAGICBYTE 85



//-------- SERVICES --------------

WiFiServer server(80);
Ticker blink;

// remoteDebug
#ifdef REMOTEDEBUGGING
RemoteDebug Debug;
#endif

DHT dht(DHTPIN, DHTTYPE);
MicroOLED oled(PIN_RESET, DC_JUMPER); // Example I2C declaration


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
  "WemosShield",
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

//---------- VARIABLES ----------

String boardName, IOTappStory1, IOTappStoryPHP1, IOTappStory2, IOTappStoryPHP2; // add NEW CONSTANTS according boardname example
int delayTime;   // time till off in seconds; 0 = always on

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


  // ------------- INTERRUPTS ----------------------------
  attachInterrupt(GPIO0, ISRbuttonStateChanged, CHANGE);



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

    IOTappStory();

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
    dht.begin();

    // Before you can start using the OLED, call begin() to init
    // all of the pins and configure the OLED.
    oled.begin();
    oled.clear(ALL); // will clear out the OLED's graphic memory.
    oled.clear(PAGE);  // Clear the display's memory (gets rid of artifacts)

    // ----------- END SPECIFIC SETUP CODE ----------------------------

  }  // End WiFi necessary

  LEDswitch(None);
    pinMode(GPIO0, INPUT_PULLUP);  // GPIO0 as input for Config mode selection

  DEBUG_PRINTLN("setup done");
}
//--------------- LOOP ----------------------------------
void loop() {
  float h, t, f, hif,hic;
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
    Debug.printf(" Temperature: %d", t);
    Debug.println("");
    Debug.print("Heap ");
    Debug.println(ESP.getFreeHeap());
    infoEntry = millis();
  }
#endif
  // ------------------------------------

  // Reading temperature or humidity takes about 250 milliseconds!
  // Sensor readings may also be up to 2 seconds 'old' (its a very slow sensor)
  h = dht.readHumidity();
  // Read temperature as Celsius (the default)
  t = dht.readTemperature();
  // Read temperature as Fahrenheit (isFahrenheit = true)
  f = dht.readTemperature(true);
  // Compute heat index in Fahrenheit (the default)
   hif = dht.computeHeatIndex(f, h);
  // Compute heat index in Celsius (isFahreheit = false)
   hic = dht.computeHeatIndex(t, h, false);

  displayTemp(t);

 /* Serial.print("Humidity: ");
  Serial.print(h);
  Serial.print(" %\t");
  Serial.print("Temperature: ");
  Serial.print(t);
  Serial.print(" *C ");
  Serial.print(f);
  Serial.print(" *F\t");
  Serial.print("Heat index: ");
  Serial.print(hic);
  Serial.print(" *C ");
  Serial.print(hif);
  Serial.println(" *F"); */
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
        delayCount = delayTime;    // Set timer to count back
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

void switchRelay(bool state) {
  if (state) {
    DEBUG_PRINT("Switch On ");
    if (Debug.ative(Debug.INFO)) REMOTEDEBUG_PRINTLN("Switch On ");
    LEDswitch(Green);
    digitalWrite(RELAYPIN, ON);
    relayState = ON;


  } else {
    DEBUG_PRINT("Switch Off ");
    if (Debug.ative(Debug.INFO)) REMOTEDEBUG_PRINTLN("Switch Off ");
    LEDswitch(None);
    digitalWrite(RELAYPIN, OFF);
    relayState = OFF;
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

void displayTemp(float temp) {
  oled.setFontType(2);  // Set the text to medium/7-segment (5 columns, 3 rows worth of characters).
  oled.setCursor(0, 5);
  oled.print(temp);  // Print a float
  oled.setCursor(0, 25);  // Set the text cursor to the upper-left of the screen.
  oled.setFontType(1);
  oled.print("Celsius");
  oled.display(); // Draw to the screen
}

