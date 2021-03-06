/*
 * Requires Arduino IDE 1.6.7 or later for running OTA updates. 
 * OTA update requires Python 2.7, OTA IDE plugin, ...  http://esp8266.github.io/Arduino/versions/2.1.0-rc2/doc/ota_updates/ota_updates.html#arduino-ide
 * Adafruit Huzzah ESP8266-E12, 4MB flash, uploads with 3MB SPIFFS (3MB filesystem of total 4MB) -- note that SPIFFS upload packages up everything from the "data" folder and uploads it via serial (same procedure as uploading sketch) or OTA. however, OTA is disabled by default
 * Use Termite serial terminal software for debugging
 */

 /* NOTE  
  *  ESP8266 stores last set SSID and PWD in reserved flash area 
  *  connect to SSID huzzah, and call http://192.168.4.1/api?ssid=<ssid>&pwd=<password> to set new SSID/PASSWORD 
  *        
  *        
  *        
  * TODO       
  *     Web contents should provide   
  *     1) get quickly started by configuring WIFI
  *     2) activate some demo scenarios (showcase fancy illumination patterns)
  *     3) providing help about available API calls
  *     4) firmware updates (load from github or upload to device? or both?)
  *     5) smartconfig https://tzapu.com/esp8266-smart-config-esp-touch-arduino-ide/
  *     6) see also https://github.com/tzapu/WiFiManager/blob/master/WiFiManager.cpp how others deal with Wifi config
  *     7) switch to fastlib for neopixels?
  */

boolean debug = true;
//#define DEBUG_ESP_HTTP_SERVER true

#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
//#include <ESP8266mDNS.h>
//#include <WiFiUdp.h>
//#include <ArduinoOTA.h>
#include <Wire.h>
#include <FS.h>
#include <Adafruit_NeoPixel.h>
#include <math.h>
#include <StreamString.h>

#define FIRMWARE_VERSION __DATE__ " " __TIME__ 

#define DEFAULT_HOSTNAME "ufo"
#define DEFAULT_APSSID "ufo"
#define DEFAULT_SSID "ufo"
#define DEFAULT_PWD "ufo"

// ESP8266 Huzzah Pin usage
#define PIN_NEOPIXEL_LOGO 2
#define PIN_NEOPIXEL_LOWERRING 4
#define PIN_NEOPIXEL_UPPERRING 5
#define PIN_FACTORYRESET 15


Adafruit_NeoPixel neopixel_logo = Adafruit_NeoPixel(4, PIN_NEOPIXEL_LOGO, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel neopixel_lowerring = Adafruit_NeoPixel(15, PIN_NEOPIXEL_LOWERRING, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel neopixel_upperring = Adafruit_NeoPixel(15, PIN_NEOPIXEL_UPPERRING, NEO_GRB + NEO_KHZ800);

byte redcountUpperring = 5;
byte redcountLowerring = 10;

boolean logo = true;
byte whirlr = 255;
byte whirlg = 255;
byte whirlb = 255;
boolean whirl = true;

//boolean restart = false;

ESP8266WebServer        httpServer ( 80 );

boolean wifiStationOK = false;
boolean wifiAPisConnected = false;
boolean wifiConfigMode;
long uploadSize = 0;

//format bytes
String formatBytes(size_t bytes){
  if (bytes < 1024){
    return String(bytes)+"B";
  } else if(bytes < (1024 * 1024)){
    return String(bytes/1024.0)+"KB";
  } else if(bytes < (1024 * 1024 * 1024)){
    return String(bytes/1024.0/1024.0)+"MB";
  } else {
    return String(bytes/1024.0/1024.0/1024.0)+"GB";
  }
}

// HTTP not found response
void handleNotFound() {
	String message = "File Not Found\n\n";
	message += "URI: ";
	message += httpServer.uri();
	message += "\nMethod: ";
	message += ( httpServer.method() == HTTP_GET ) ? "GET" : "POST";
	message += "\nArguments: ";
	message += httpServer.args();
	message += "\n";

	for ( uint8_t i = 0; i < httpServer.args(); i++ ) {
		message += " " + httpServer.argName ( i ) + ": " + httpServer.arg ( i ) + "\n";
	}

	httpServer.send ( 404, "text/plain", message );
} 


// handle REST api call to set new WIFI credentials; note that URL encoding can be used
// api?ssid=<ssid>&pwd=<password>
boolean newWifi = false;;
String newWifiSSID;
String newWifiPwd;
String newWifiHostname;

void handleSmartConfig() {
   if (wifiConfigMode) {
      if (WiFi.smartConfigDone()) {
         if (debug) {
           Serial.println("SMARTCONFIG SUCCEEDED - New Wifi settings: " + WiFi.SSID() + " / " + WiFi.psk());
           Serial.println("Restarting....");
           Serial.flush();
         }
         ESP.restart();
      }
   }
}

void handleNewWifiSettings() {
   if (newWifi) {
       // if SSID is given, also update wifi credentials
       if (newWifiSSID.length()) {
          WiFi.mode(WIFI_STA);
          WiFi.begin(newWifiSSID.c_str(), newWifiPwd.c_str() );
       } 
       //newWifi = false;
       //newWifiPwd = String();
       //newWifiSSID = String();
       //newWifiHostname = String();
       if (debug) {
         Serial.println("New Wifi settings: " + newWifiSSID + " / " + newWifiPwd);
         Serial.println("Restarting....");
         Serial.flush();
       }

      ESP.restart();

   }
}

// Handle Factory reset and enable Access Point mode to enable configuration
void handleFactoryReset() {
  if (digitalRead(PIN_FACTORYRESET)) {
    if (millis() < 10000) {  // ignore reset button during first 10 seconds of reboot to avoid looping resets due to fast booting of ESP8266
      return;
    }
    if (debug) {
      Serial.println("*********FACTORYRESET***********");
      //WiFi.printDiag(Serial); 
    }
    WiFi.disconnect(false); //disconnect and disable station mode; delete old config
    // default IP address for Access Point is 192.168.4.1
    //WiFi.softAPConfig(IPAddress(192,168,4,1), IPAddress(192,168,4,1), IPAddress(255,255,255,0)); // IP, gateway, netmask -- NOT STORED TO FLASH!!
    WiFi.softAP(DEFAULT_APSSID); // default is open Wifi
    //WiFi.mode(WIFI_AP);
    if (debug) {
      Serial.println("Wifi reset to SSID: " + WiFi.SSID() + " pass: " + WiFi.psk());
      Serial.println("Wifi config mode enabled: Access point enabled at open Wifi SSID: " DEFAULT_APSSID);
      Serial.println("Restarting....");
      //WiFi.printDiag(Serial); 
      Serial.flush();
    }  
    ESP.restart();

  }
}

void infoHandler() {
  
  String json = "{";
  json += "\"heap\":\""+String(ESP.getFreeHeap())+"\"";
  json += ", \"ssid\":\""+String(WiFi.SSID())+"\"";
  json += ", \"ipaddress\":\""+WiFi.localIP().toString()+"\"";
  json += ", \"ipgateway\":\""+WiFi.gatewayIP().toString()+"\"";
  json += ", \"ipdns\":\""+WiFi.dnsIP().toString()+"\"";
  json += ", \"ipsubnetmask\":\""+WiFi.subnetMask().toString()+"\"";
  json += ", \"macaddress\":\""+WiFi.macAddress()+"\"";
  json += ", \"hostname\":\""+WiFi.hostname()+"\"";
  json += ", \"apipaddress\":\""+WiFi.softAPIP().toString()+"\"";
  json += ", \"apconnectedstations\":\""+String(WiFi.softAPgetStationNum())+"\"";
  json += ", \"wifiautoconnect\":\""+String(WiFi.getAutoConnect())+"\"";
  json += ", \"firmwareversion\":\""+String(FIRMWARE_VERSION)+"\"";
  json += "}";
  httpServer.send(200, "text/json", json);
  httpServer.sendHeader("cache-control", "private, max-age=0, no-cache, no-store");
  json = String();
}

void apiHandler() {
  if (httpServer.hasArg("logo")) {
    Serial.println("LED arg found" + httpServer.arg("logo"));
    if (httpServer.arg("logo").equals("on")) {
      Serial.println("lets turn logoed on");
      logo = true;
    } else if (httpServer.arg("logo").equals("red")) {
      neopixel_logo.setPixelColor(0, 255, 0, 0); 
      neopixel_logo.setPixelColor(1, 255, 0, 0); 
      neopixel_logo.setPixelColor(2, 255, 0, 0); 
      neopixel_logo.setPixelColor(3, 255, 0, 0); 
    } else if (httpServer.arg("logo").equals("green")) {
      neopixel_logo.setPixelColor(0, 0, 255, 0); 
      neopixel_logo.setPixelColor(1, 0, 255, 0); 
      neopixel_logo.setPixelColor(2, 0, 255, 0); 
      neopixel_logo.setPixelColor(3, 0, 255, 0); 
    } else {
      Serial.println("lets turn logoed off");
      logo = false;
    }
  }

  // to quickly set the RGB colors of the logo remotely
  if (httpServer.hasArg("logoled")) {
    byte led = byte(httpServer.arg("logoled").toInt());    
    byte r = byte(httpServer.arg("r").toInt());    
    byte g = byte(httpServer.arg("g").toInt());    
    byte b = byte(httpServer.arg("b").toInt());    
    neopixel_logo.setPixelColor(led, r, g, b); 
  }
  
  if (httpServer.hasArg("whirl")) {
    Serial.println("WHIRL arg found" + httpServer.arg("whirl"));
    String ws = httpServer.arg("whirl");
    if (ws.equals("r")) {
      redcountUpperring = 14;
      redcountLowerring = 15;
      whirl = true;
      Serial.println("whirl red");      
    } else if (ws.equals("g")) {
      redcountUpperring = 0;
      redcountLowerring = 1;
      whirl = true;
      Serial.println("whirl green");      
    } else if (ws.equals("b")) {
      whirl = true;
      Serial.println("whirl blue");      
    } else if (ws.equals("off")) {
      whirl = false;
      Serial.println("whirl off");      
    } else if (ws.equals("on")) {
      whirl = true;
      Serial.println("whirl on");      
    }

  }

  // note its required to provide both arguments SSID and PWD
  if (httpServer.hasArg("ssid") && httpServer.hasArg("pwd")) {
    newWifi = true;
    newWifiSSID = httpServer.arg("ssid");
    newWifiPwd = httpServer.arg("pwd");
  } 
  if (httpServer.hasArg("hostname")) {
    newWifi = true;
    newWifiHostname = httpServer.arg("hostname");
  }

  httpServer.send(200);
  httpServer.sendHeader("cache-control", "private, max-age=0, no-cache, no-store");
}

void generateHandler() {
  if (httpServer.hasArg("size")) {
    Serial.println("size arg found" + httpServer.arg("size"));
    long bytes = httpServer.arg("size").toInt();
    String top = "<html><header>Generator</header><body>sending " + String(bytes) + " bytes of additional payload.<p>";
    String end = "</body></html>";
    httpServer.setContentLength(bytes+top.length()+end.length());
    httpServer.send(200);
    httpServer.sendHeader("cache-control", "private, max-age=0, no-cache, no-store");
    String chunk = "";
    httpServer.sendContent(top);
    String a = String("a");
    while (bytes > 0) {
      chunk = String("");
      long chunklen = bytes<4096?bytes:4096;
      while (chunk.length() <= chunklen) {
        chunk += a;
      }
      httpServer.sendContent(chunk);
      bytes-=chunklen;
    }
    httpServer.sendContent(end);
  }  
}

void updatePostHandler() {
    // handler for the /update form POST (once file upload finishes)
    //httpServer.sendHeader("Connection", "close");
    //httpServer.sendHeader("Access-Control-Allow-Origin", "*");
    StreamString error;
    if (Update.hasError()) {
      Update.printError(error);
    }
    String response = "<html>" 
                        "<head>"  
                          "<title>IU Webmaster redirect</title>"
                          "<META http-equiv='refresh' content='10;URL=/'>"
                        "</head>"  
                        "<body>"
                          "<center>";
    response+=             (Update.hasError())?error:"Upload succeeded! " + String(uploadSize); 
    response+=            " bytes transferred<p>Device is being rebooted. Redirecting to homepage in 10 seconds...</center>"  
                        "</body>"  
                      "</html>";
    httpServer.send(200, "text/html", response);
    ESP.restart();
}

String parseFileName(String &path) {
  String filename;
  int lastIndex = path.lastIndexOf('\\');
  if (lastIndex < 0) {
    lastIndex = path.lastIndexOf('/');
  } 
  if (lastIndex > 0) {
    filename = path.substring(lastIndex+1);
  } else {
    filename = path;
  }

  filename.toLowerCase();
  return filename;
}


File uploadFile;
void updatePostUploadHandler() {
  // handler for the file upload, get's the sketch bytes, and writes
  // them through the Update object
  HTTPUpload& upload = httpServer.upload();
  String filename = parseFileName(upload.filename);
  
  if (filename.endsWith(".bin")) { // handle firmware upload
    if(upload.status == UPLOAD_FILE_START){
      //WiFiUDP::stopAll(); needed for MDNS or the like?
      if (debug) Serial.println("Update: " + upload.filename);
      uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
      if(!Update.begin(maxSketchSpace)){//start with max available size
        if (debug) Update.printError(Serial);
      }
    } else if(upload.status == UPLOAD_FILE_WRITE){
      if (debug) Serial.print(".");
      if(Update.write(upload.buf, upload.currentSize) != upload.currentSize){
        if (debug) Update.printError(Serial);
      }
    } else if(upload.status == UPLOAD_FILE_END){
      uploadSize = Update.size();
      if(Update.end(true)){ //true to set the size to the current progress
        if (debug) Serial.println("Update Success - uploaded: " + String(upload.totalSize) + ".... rebooting now!");
      } else {
        if (debug) Update.printError(Serial);
      }
      if (debug) Serial.setDebugOutput(false);
    } else if(upload.status == UPLOAD_FILE_ABORTED){
      uploadSize = Update.size();
      Update.end();
      if (debug) Serial.println("Update was aborted");
    }
  } else { // handle file upload
    if(upload.status == UPLOAD_FILE_START){
      if (debug) Serial.println("uploading to SPIFFS: /" + filename);
      uploadFile = SPIFFS.open("/" + filename, "w");
    } else if(upload.status == UPLOAD_FILE_WRITE){
      if (debug) Serial.print(".");
      if(uploadFile.write(upload.buf, upload.currentSize) != upload.currentSize){
        if (debug) Serial.println("ERROR writing file " + String(uploadFile.name()) + "to SPIFFS.");
        uploadFile.close();
      }
    } else if(upload.status == UPLOAD_FILE_END){
      uploadSize = upload.totalSize;
      if(uploadFile.size() == upload.totalSize){ 
        if (debug) Serial.println("Upload to SPIFFS Succeeded - uploaded: " + String(upload.totalSize) + ".... rebooting now!");
      } else {
        if (debug) Serial.println("Upload to SPIFFS FAILED: " + String(uploadFile.size()) + " bytes of " + String(upload.totalSize));
      }
      uploadFile.close();
    } else if(upload.status == UPLOAD_FILE_ABORTED){
      uploadSize = upload.totalSize;
      uploadFile.close();
      if (debug) Serial.println("Upload to SPIFFS was aborted");
    }
  }
  
  yield();
}

/*
void indexHtmlHandler() {
    httpServer.send(200, "text/html", String(indexHtml));
    httpServer.sendHeader("cache-control", "private, max-age=0, no-cache, no-store");
}*/

// handles GET request to "/update" and redirects to "index.html/#!pagefirmwareupdate"
void updateHandler() {
    httpServer.send(301, "text/html", "/#!pagefirmwareupdate");
    httpServer.sendHeader("cache-control", "private, max-age=0, no-cache, no-store");
}


void WiFiEvent(WiFiEvent_t event) {
  if(debug) {
    switch(event) {
        case WIFI_EVENT_STAMODE_GOT_IP:
            Serial.println("WiFi connected. IP address: " + String(WiFi.localIP().toString()) + " hostname: "+ WiFi.hostname() + "  SSID: " + WiFi.SSID());
            wifiStationOK = true;
            break;
        case WIFI_EVENT_STAMODE_DISCONNECTED:
            Serial.println("WiFi client lost connection");
            wifiStationOK = false;            
            break;
        case WIFI_EVENT_STAMODE_CONNECTED:
            Serial.println("WiFi client connected");
            break;
        case WIFI_EVENT_STAMODE_AUTHMODE_CHANGE:
            Serial.println("WiFi client authentication mode changed.");
            break;
        //case WIFI_STAMODE_DHCP_TIMEOUT:                             THIS IS A NEW CONSTANT ENABLE WITH UPDATED SDK
          //  Serial.println("WiFi client DHCP timeout reached.");
            //break;
        case WIFI_EVENT_SOFTAPMODE_STACONNECTED:
            Serial.println("WiFi accesspoint: new client connected. Clients: "  + String(WiFi.softAPgetStationNum()));
             if (WiFi.softAPgetStationNum() > 0) {
               wifiAPisConnected = true;
             }
            break;
        case WIFI_EVENT_SOFTAPMODE_STADISCONNECTED:
            Serial.println("WiFi accesspoint: client disconnected. Clients: " + String(WiFi.softAPgetStationNum()));
             if (WiFi.softAPgetStationNum() > 0) {
               wifiAPisConnected = true;
             } else {
               wifiAPisConnected = false;
             }
             break;
        case WIFI_EVENT_SOFTAPMODE_PROBEREQRECVED:
            //Serial.println("WiFi accesspoint: probe request received.");
            break; 
      }
  }
}

void printSpiffsContents() {
  if (debug)
  {
    Dir dir = SPIFFS.openDir("/");
    while (dir.next()) {    
      String fileName = dir.fileName();
      size_t fileSize = dir.fileSize();
      Serial.printf("FS File: %s, size: %s\n", fileName.c_str(), formatBytes(fileSize).c_str());
    }
    Serial.printf("\n");
  }
}

void setupSerial() {
  // checking availability of serial connection
  int serialtimeout = 5000; //ms
  Serial.begin ( 115200 );
  while(!Serial) {
    if (serialtimeout >0) {
      serialtimeout -= 50; 
    } else { 
      debug = false;
      break;
    }
    delay(50);
  }

  if (debug) {
    Serial.println("");
    Serial.println("");
    Serial.println("");
    Serial.println("Welcome to Dynatrace UFO!");
    Serial.println("UFO Firmware Version: " FIRMWARE_VERSION);
    Serial.println("ESP8266 Bootversion: " +String(ESP.getBootVersion()));
    Serial.println("ESP8266 SDK Version: " +String(ESP.getSdkVersion()));
    Serial.println("Resetinfo: " + ESP.getResetInfo());
  }
}

// initialization routines
void setup ( void ) {
  setupSerial();

  pinMode(PIN_FACTORYRESET, INPUT); //, INPUT_PULLUP); use INPUT_PULLUP in case we put reset to ground; currently reset is doing a 3V signal

  // setup neopixel
  neopixel_logo.begin();
  neopixel_logo.setPixelColor(0, 0, 100, 255); // http://ufo/api?logoled=0&r=0&g=100&b=255
  neopixel_logo.setPixelColor(1, 125, 255, 0); // http://ufo/api?logoled=1&r=125&g=255&b=0
  neopixel_logo.setPixelColor(2, 0, 255, 0); // http://ufo/api?logoled=2&r=0&g=255&b=0
  neopixel_logo.setPixelColor(3, 255, 0, 255); // http://ufo/api?logoled=3&r=255&g=0&b=255
  neopixel_logo.show();
  
  neopixel_lowerring.begin();
  neopixel_lowerring.clear();
  neopixel_lowerring.show();
  neopixel_upperring.begin();
  neopixel_upperring.clear();
  neopixel_upperring.show();

  // initialize ESP8266 file system
  SPIFFS.begin();
  printSpiffsContents();

  // initialize Wifi based on stored config
  WiFi.onEvent(WiFiEvent);
  WiFi.hostname(DEFAULT_HOSTNAME); // note, the hostname is not persisted in the ESP config like the SSID. so it needs to be set every time WiFi is started
  wifiConfigMode = WiFi.getMode() & WIFI_AP;
  if (wifiConfigMode) 
    WiFi.beginSmartConfig();

  if (debug) {
    Serial.println("Connecting to Wifi SSID: " + WiFi.SSID() + " as host "+ WiFi.hostname());
    if (wifiConfigMode) {
      Serial.println("WiFi Configuration Mode - Access Point IP address: " + WiFi.softAPIP().toString());
    }
    //WiFi.printDiag(Serial);
  }

  // setup all web server routes; make sure to use / last
  #define STATICFILES_CACHECONTROL "private, max-age=0, no-cache, no-store"
  httpServer.on ( "/api", apiHandler );
  httpServer.on ( "/info", infoHandler );
  httpServer.on ( "/gen", generateHandler );
  //httpServer.serveStatic("/index.html", SPIFFS, "/index.html", STATICFILES_CACHECONTROL);
  //httpServer.serveStatic("/app.js", SPIFFS, "/app.js", STATICFILES_CACHECONTROL);
  httpServer.serveStatic("/phonon.min.css", SPIFFS, "/phonon.min.css");
  httpServer.serveStatic("/phonon.min.js", SPIFFS, "/phonon.min.js");
  httpServer.serveStatic("/forms.min.css", SPIFFS, "/forms.min.css");
  httpServer.serveStatic("/forms.min.js", SPIFFS, "/forms.min.js");
  httpServer.serveStatic("/icons.min.css", SPIFFS, "/icons.min.css");
  httpServer.serveStatic("/lists.min.css", SPIFFS, "/lists.min.css");
  httpServer.serveStatic("/phonon-base.min.css", SPIFFS, "/phonon-base.min.css");
  httpServer.serveStatic("/phonon-core.min.js", SPIFFS, "/phonon-core.min.js");
  httpServer.serveStatic("/font.woff", SPIFFS, "/font.woff");
  httpServer.serveStatic("/font.eot", SPIFFS, "/font.eot");
  httpServer.serveStatic("/font.svg", SPIFFS, "/font.svg");
  httpServer.serveStatic("/font.ttf", SPIFFS, "/font.ttf");
  if (wifiConfigMode) { 
      httpServer.serveStatic("/", SPIFFS, "/wifisettings.html", STATICFILES_CACHECONTROL);
  } else {
      httpServer.serveStatic("/", SPIFFS, "/index.html", STATICFILES_CACHECONTROL);
  }
  //httpServer.on ( "/", HTTP_GET, handleRoot );
  httpServer.onNotFound ( handleNotFound );

  // register firmware update HTTP server: 
  //    To upload through terminal you can use: curl -F "image=@ufo.ino.bin" ufo.local/update  
  //    Windows power shell use DOESNT WORK YET: wget -Method POST -InFile ufo.ino.bin -Uri ufo.local/update   
  //httpServer.on("/", indexHtmlHandler);
  //httpServer.on("/update", HTTP_GET, indexHtmlHandler);
  httpServer.on("/update", HTTP_GET, updateHandler);
  httpServer.on("/update", HTTP_POST, updatePostHandler, updatePostUploadHandler);

  // start webserver
  httpServer.begin();
}

void neopixelSetColor(Adafruit_NeoPixel &neopixel, byte r, byte g, byte b ) {
  for (unsigned short i = 0; i < neopixel.numPixels(); i++) {
    neopixel.setPixelColor(i, r, g, b);
  }
}

void neopixelWhirlRed(Adafruit_NeoPixel &neopixel, byte redcount, byte whirlpos) {
  unsigned short p;
  for (unsigned short i = 0; i < neopixel.numPixels(); i++) {
     p = whirlpos + i;
     if (redcount > 0) {
       neopixel.setPixelColor((p % neopixel.numPixels()), 255, 0, 0);
       redcount--;
     } else {
       neopixel.setPixelColor((p % neopixel.numPixels()), 0, 255, 0);
     }
  }
}

unsigned int tick = 0;
byte whirlpos = 0;
byte wheelcolor = 0;

void loop ( void ) {
  tick++;

  handleFactoryReset();
  handleNewWifiSettings();
  handleSmartConfig();
  httpServer.handleClient();

  yield();

  // adjust logo brightness (on/off right now)
  if (logo) {
    neopixel_logo.setBrightness(255);
  } else {
    neopixel_logo.setBrightness(0);
  }
 
  yield();
  neopixel_logo.show();

  neopixelWhirlRed(neopixel_upperring, redcountUpperring, whirlpos);
  neopixelWhirlRed(neopixel_lowerring, redcountLowerring, whirlpos);
  if (tick % 50 == 0) {
     if (whirl) whirlpos++;
  }
  yield();

/*
  if (whirl) {
    byte p;
    byte r;
    byte g;
    byte b;
    for (byte i = 0; i<6; i++) {
      p = i + whirlpos;      
      if (i == 0 || i == 5) {
        r = whirlr/4;
        g = whirlg/4;
        b = whirlb/4;
      } else if (i == 1 || i == 4) {
        r = whirlr/2;
        g = whirlg/2;
        b = whirlb/2;
      } else {
        r = whirlr;
        g = whirlg;
        b = whirlb;
      }
      neopixel_upperring.setPixelColor((p % 15), r, g, b);
      neopixel_lowerring.setPixelColor((p % 15), r, g, b);
    }
    if (tick % 50 == 0) {
      whirlpos++;
    }
  } else {
      neopixelSetcolor(neopixel_upperring, 0, 0, 0);
      neopixelSetcolor(neopixel_lowerring, 0, 0, 0);
  }*/

  // show AP mode in blue to tell user to configure WIFI; especially after RESET
  // blinking alternatively in blue when no client is connected to AP; 
  // binking both rings in blue when at least one client is connected to AP
  if (wifiConfigMode) {
    if(wifiAPisConnected && (tick % 1000 > 400)) {
      neopixelSetColor(neopixel_upperring, 0, 0, 255);
      neopixelSetColor(neopixel_lowerring, 0, 0, 255);
    } else {
      if (tick % 2000 > 1000) {
        neopixelSetColor(neopixel_upperring, 0, 0, 255);
        neopixelSetColor(neopixel_lowerring, 0, 0, 0);
      } else {
        neopixelSetColor(neopixel_lowerring, 0, 0, 255);
        neopixelSetColor(neopixel_upperring, 0, 0, 0);
      }
    }
  } else { // blink yellow while not connected to wifi when it should
    if (!wifiStationOK && (tick % 1000 > 750)) {
      neopixelSetColor(neopixel_upperring, 50, 50, 0);
      neopixelSetColor(neopixel_lowerring, 50, 50, 0);
    }
  }

  
  yield();
  neopixel_upperring.show();
  neopixel_lowerring.show();
}



