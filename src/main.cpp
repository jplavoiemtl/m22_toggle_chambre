#include <Arduino.h>
#include "secrets.h"

#include <Button2.h>
#include <PageBuilder.h>
#include <Timer.h>
#include <TFT_eSPI.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <WebServer.h>
typedef WebServer WiFiWebServer;
#include <AutoConnect.h>
#include "time.h"
#include "wifigreen.h"
#include "wifired.h"
#include "wifiorange.h"

const char TOG_OUT[] = "m18toggle";       //MQTT topic de commande de toogle
const char PAYLOAD_SAL[] = "salon";       //toggle lampe salon
const char PAYLOAD_STA[] = "statue";      //toggle lampe statue
const char PAYLOAD_CUI[] = "cuisine";     //toggle lampe cuisine
const char PAYLOAD_GAL[] = "galerie";     //toggle lampe galerie
const char SALON_ON[] = "sa_on";
const char SALON_OFF[] = "sa_of";
const char STATUE_ON[] = "st_on";
const char STATUE_OFF[] = "st_of";
const char CUISINE_ON[] = "cu_on";
const char CUISINE_OFF[] = "cu_of";
const char GALERIE_ON[] = "ga_on";
const char GALERIE_OFF[] = "ga_of";

// Create a client class to connect to the MQTT server
WiFiClient espClient;
PubSubClient client(espClient);

#define BUTTON_1        35
#define BUTTON_2        0

TFT_eSPI tft = TFT_eSPI(135, 240);  
TFT_eSprite img = TFT_eSprite(&tft);
TFT_eSprite wifiSprite = TFT_eSprite(&tft);

bool activeScreen = false;          //button pressed flag for active screen

Button2 btn1(BUTTON_1);             //right button
Button2 btn2(BUTTON_2);             //left button

Timer t_mqtt;                                    //Timer MQTT retry connection
const int period_mqtt = 15000;                   //retry delay MQTT
Timer t_NTP;                                     //Timer for time sync over NTP
const int period_NTP = 1800000;                  //sync time delay every 30 min 1800000 s
Timer t_LCDON;                                   //Timer for LCD ON
const int period_LCDON = 180000;                  //Time LCD to be ON, 30000: 30 sec

int afterEvent;                                  //for LCD ON timer to kill repeat events

//A true RGB565 colour picker: https://chrishewett.com/blog/true-rgb565-colour-picker/
//RGB565 is used to represent colours in 16 bits, rather than the 24bit (RGB888)
#define TFT_DARKJPMAROON  0x2000
#define TFT_DARKJPYELLOW  0xAC40  //0xA3C3  0xA323  0xBCC0
#define TFT_DARKJPORANGE  0xb340  //0xDB80  0xD340
#define TFT_DARKJPRED     0x5800  
#define TFT_DARKJPPURPLE  0x1002 
#define TFT_DARKJPNAVY    0x0005  //0x0008
#define TFT_DARKJPGREY    0x18a3  //0x3166

#ifndef TFT_DISPOFF
#define TFT_DISPOFF 0x28
#endif

#ifndef TFT_SLPIN
#define TFT_SLPIN   0x10
#endif

#define TFT_MOSI            19
#define TFT_SCLK            18
#define TFT_CS              5
#define TFT_DC              16
#define TFT_RST             23

#define TFT_BL              4     // Display backlight control pin
#define TFT_BACKLIGHT_OFF   LOW   // Level to turn OFF back-light


WiFiWebServer server;
AutoConnect portal(server);
AutoConnectConfig config;

char timeStringBuff[12];   
int x = 0;                  //TFT variable

// Array of device names
String Device[] = {"salon", "statue", "cuisine", "galerie"};
const int DeviceCount = sizeof(Device) / sizeof(Device[0]); // Calculate the number of devices
int deviceIndex = 0;                                        // Array index variable
// Declare a boolean array to store the status of each device
String deviceStatus[DeviceCount] = {"OFF"};                   // Initialize all elements to OFF



//******************************************************************************************************************
void exitOTAStart() {
  Serial.println("OTA started");
}

void exitOTAProgress(unsigned int amount, unsigned int sz) {
  Serial.printf("OTA in progress: received %d bytes, total %d bytes\n", sz, amount);
}

void exitOTAEnd() {
  Serial.println("OTA ended");
}

void exitOTAError(uint8_t err) {
  Serial.printf("OTA error occurred %d\n", err);
}


//******************************************************************************************************************
void turnOFFLCD() {
  Serial.println("Turn OFF LCD"); 
  digitalWrite(TFT_BL, TFT_BACKLIGHT_OFF);      //turn backlight off 
  activeScreen = false;                         //get ready for next command cycle
}


//******************************************************************************************************************
void printLocalTime(){
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    Serial.println("Failed to obtain time 1");
    return;
  }
  //Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S zone %Z %z ");
  Serial.println(&timeinfo, "%B %d %H:%M:%S ");
  //strftime(timeStringBuff, sizeof(timeStringBuff), "%H:%M:%S ", &timeinfo);
  //Serial.println(timeStringBuff);
}


//******************************************************************************************************************
void setTimezone(String timezone){
  Serial.printf("  Setting Timezone to %s\n",timezone.c_str());
  setenv("TZ",timezone.c_str(),1);  //  Now adjust the TZ.  Clock settings are adjusted to show the new local time
  tzset();
}


//******************************************************************************************************************
void initTime(String timezone){
  struct tm timeinfo;

  Serial.println("Setting up time");
  configTime(0, 0, "pool.ntp.org");    // First connect to NTP server, with 0 TZ offset
  if(!getLocalTime(&timeinfo)){
    Serial.println("  Failed to obtain time");
    return;
  }
  Serial.println("  Got the time from NTP");
  // Now we can set the real timezone
  setTimezone(timezone);
}


//******************************************************************************************************************
//Refresh internal clock with NTP time if module is online
void syncTime(){
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Sync NTP time");
    initTime("EST5EDT,M3.2.0,M11.1.0");         // Set time zone for Montreal
    printLocalTime(); 
  }  
}


//******************************************************************************************************************
//Display time in the top wifiSprite.  getLocalTime use the internal esp32 clock.  Will synchronize over NTP
//server every hour.
void displayLocalTime(){
  struct tm timeinfo;
  char timeString[9];         // HH:MM:SS format requires 8 characters + 1 for null terminator

  if(!getLocalTime(&timeinfo)){
    return;
  }
  strftime(timeString, sizeof(timeString), "%H:%M:%S", &timeinfo);
  wifiSprite.fillRect(0,0,102,31,TFT_BLACK);
  wifiSprite.drawString(timeString, 8, 6);
  wifiSprite.pushSprite(0,0);  
}


//******************************************************************************************************************
//Main screen display
void screenMain(){
  img.fillSprite(TFT_DARKJPNAVY);
  img.setFreeFont(&FreeSans9pt7b);   
  img.drawString("M22 Lampes", 5, 25);
  img.setFreeFont(&FreeSans18pt7b);        
  img.drawString(deviceStatus[deviceIndex], 25, 70);   
  img.drawString(Device[deviceIndex], 5, 125);              
  img.setFreeFont(&FreeSans9pt7b); 
  img.drawString(WiFi.localIP().toString(), 0, 189);   
  img.pushSprite(0,32);  
}


//******************************************************************************************************************
//Command screen display
void screenCommand(){
  img.fillSprite(TFT_DARKJPMAROON);
  img.setFreeFont(&FreeSans12pt7b);        
  img.drawString("Commande", 5, 50); 
  img.setFreeFont(&FreeSans9pt7b);     
  img.drawString(WiFi.localIP().toString(), 0, 189);   
  img.pushSprite(0,32);  
}


//******************************************************************************************************************
//No MQTT connection screen display
void screenNOmqtt(){
  img.fillSprite(TFT_BLACK);
  img.setFreeFont(&FreeSans12pt7b);        
  img.drawString("No MQTT", 5, 50);   
  img.setFreeFont(&FreeSans9pt7b); 
  img.drawString(WiFi.localIP().toString(), 0, 189);   
  img.pushSprite(0,32);  
}


//******************************************************************************************************************
//No Wifi screen display
void screenNOwifi(){
  img.fillSprite(TFT_BLACK);
  img.setFreeFont(&FreeSans12pt7b);        
  img.drawString("No WiFi", 5, 50);   
  img.pushSprite(0,32);  
}


//******************************************************************************************************************
//MQTT connection every 15s interval if not connected
//Wifi and MQTT connection status indicator
//MQTT connected: green icon.  No MQTT connection: orange icon.  No wifi: red icon
void reconnectMQTT() {
  yield(); 
  if (WiFi.status() == WL_CONNECTED) {
    if (!client.connected()) {
      Serial.print("Attempting MQTT connection...");
      
      if (client.connect(CLIENT_ID, USERNAME, KEY)) {
        Serial.println(" connected");                                    
        Serial.print("Client state connected, rc=");                                                     
        Serial.println(client.state());  
        client.subscribe(TOG_OUT, 1);                 //Topic de commande
        wifiSprite.fillSprite(TFT_BLACK);
        wifiSprite.pushSprite(0,0);                   //clear the sprite in black
        wifiSprite.pushImage(103,0,32,32,Wifigreen);  //MQTT connected, green icon
        wifiSprite.pushSprite(0,0,TFT_BLACK);

        syncTime();                             //sync NTP time 
        screenMain();
      } else {
        Serial.print("failed, rc=");
        Serial.print(client.state());
        Serial.println(" try again in 15 seconds");
        wifiSprite.fillSprite(TFT_BLACK);
        wifiSprite.pushSprite(0,0);                   
        wifiSprite.pushImage(103,0,32,32,Wifiorange); //no MQTT, orange icon
        wifiSprite.pushSprite(0,0,TFT_BLACK);
        screenNOmqtt();
      } 
    }
  } else {
    Serial.println("No Wifi");
    wifiSprite.fillSprite(TFT_BLACK);
    wifiSprite.pushSprite(0,0);                   
    wifiSprite.pushImage(103,0,32,32,Wifired);    //no wifi, red icon 
    wifiSprite.pushSprite(0,0,TFT_BLACK);   
    screenNOwifi(); 
  }  
}


//******************************************************************************************************************
// Handle MQTT
void checkMQTT() {
  if (!client.connected()) {   
    t_mqtt.update();
  }
  client.loop();                              //handle MQTT client
}


//******************************************************************************************************************
//Fonction appelée par le broker MQTT chaque fois qu'un message est publié sur un topic auquel le module est abonné
void callback(char* topic, byte* payload, unsigned int length) {
  String topicString = topic;
  String payloadString = (char*)payload;
  String fullPayloadString;

  fullPayloadString = payloadString.substring(0,length);
  Serial.println("MQTT full payload " + fullPayloadString);

  if (topicString == TOG_OUT && fullPayloadString == SALON_ON) { 
    deviceStatus[0] = "ON";
  }  

  if (topicString == TOG_OUT && fullPayloadString == SALON_OFF) { 
    deviceStatus[0] = "OFF";
  }  

  if (topicString == TOG_OUT && fullPayloadString == STATUE_ON) { 
    deviceStatus[1] = "ON";
  }  

  if (topicString == TOG_OUT && fullPayloadString == STATUE_OFF) { 
    deviceStatus[1] = "OFF";
  }  

  if (topicString == TOG_OUT && fullPayloadString == CUISINE_ON) { 
    deviceStatus[2] = "ON";
  }  

  if (topicString == TOG_OUT && fullPayloadString == CUISINE_OFF) { 
    deviceStatus[2] = "OFF";
  }   

  if (topicString == TOG_OUT && fullPayloadString == GALERIE_ON) { 
    deviceStatus[3] = "ON";
  }  

  if (topicString == TOG_OUT && fullPayloadString == GALERIE_OFF) { 
    deviceStatus[3] = "OFF";
  }  

  screenMain();
}


//******************************************************************************************************************
// Start MQTT
void startMQTT() {
  client.setServer(SERVERMQTT, SERVERPORT);
  client.setCallback(callback);
  reconnectMQTT();    
}


//******************************************************************************************************************
void processDevice() {
  // Depending on the index, execute specific instructions
  Serial.println("Processing " + Device[deviceIndex]);
  switch (deviceIndex) {
    case 0: // salon
      client.publish(TOG_OUT,PAYLOAD_SAL);          //publish toggle lampe salon
      break;
    case 1: // statue
      client.publish(TOG_OUT,PAYLOAD_STA);          //publish toggle lampe statue
      break;
    case 2: // cuisine
      client.publish(TOG_OUT,PAYLOAD_CUI);          //publish toggle lampe cuisine
      break;
    case 3: // galerie
      client.publish(TOG_OUT,PAYLOAD_GAL);          //publish toggle lampe galerie
      break;
    default:
      // Just in case
      Serial.println("Invalid device index!");
      break;
  }
}


//******************************************************************************************************************
// Buttons functions
void pressed(Button2& btn) {                          //bouton droit
  if (activeScreen && client.connected()) {           //active screen and mqtt connection: publish
    if (btn == btn2) {
      deviceIndex++;
      if (deviceIndex >= DeviceCount) { // Use DeviceCount instead of hard-coded value
          deviceIndex = 0; 
      }         
      Serial.println(Device[deviceIndex]);            // Print the current device name
    } else {                                          //bouton gauche
      processDevice();                                // Call the function with the current index
    }
  }
  digitalWrite(TFT_BL, TFT_BACKLIGHT_ON);             //Turn backlight on 
  activeScreen = true;                                //screen is active and ready for command
  t_LCDON.stop(afterEvent);                           //to prevent multiples instances of timers and reset a new one
  afterEvent = t_LCDON.after(period_LCDON, turnOFFLCD);
}


//******************************************************************************************************************
void released(Button2& btn) {
  //Serial.print("released: ");
  //Serial.println(btn.wasPressedFor());
  //printLocalTime();
  //delay(1000);
  if (client.connected()) { 
    screenMain();                                   //go back to main display   
  }
  if (btn.wasPressedFor() > 10000) {                //instead of unpluging power press 10 sec to reset module
    Serial.println("Restarting... ");
    delay(1000);
    ESP.restart();
  }
}


//******************************************************************************************************************
//******************************************************************************************************************
void setup() {
  delay(800);
  Serial.begin(115200);

  // TTGO DISPLAY SETUP
  tft.init();
  tft.setRotation(0);                     //default: 1, vertical: 0
  img.createSprite(135,208);
  img.fillSprite(TFT_BLACK);
  img.pushSprite(0,32);

  wifiSprite.createSprite(135,32);
  wifiSprite.fillSprite(TFT_BLACK);
  wifiSprite.pushSprite(0,0);
  wifiSprite.pushImage(103,0,32,32,Wifired);    //no wifi, red icon 
  wifiSprite.pushSprite(0,0,TFT_BLACK);    
  wifiSprite.setTextColor(TFT_DARKGREY);
  wifiSprite.setFreeFont(&FreeSans12pt7b);  

  //img.setFreeFont(&FreeSans12pt7b);
  img.setFreeFont(&FreeSans9pt7b);
  img.setTextColor(TFT_DARKGREY);  
  img.drawString("Captive portal",10,74);
  img.pushSprite(0,32);  

  if (TFT_BL > 0) { // TFT_BL has been set in the TFT_eSPI library in the User Setup file TTGO_T_Display.h
    pinMode(TFT_BL, OUTPUT); // Set backlight pin to output mode
    digitalWrite(TFT_BL, TFT_BACKLIGHT_ON); // Turn backlight on. 
  }

  delay(500);

  for (int i = 0; i < DeviceCount; i++) {
    deviceStatus[i] = "OFF";
  }  

  // Responder of root page and apply page handled directly from WebServer class.
  server.on("/", []() {
    String content = R"(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8" name="viewport" content="width=device-width, initial-scale=1">
</head>
<body>
m22 toggle lampes salon TTGO &ensp;
__AC_LINK__
</body>
</html>
    )";
    content.replace("__AC_LINK__", String(AUTOCONNECT_LINK(COG_16)));
    server.send(200, "text/html", content);
  });

  Serial.println("Connecting to WiFi");
  config.ota = AC_OTA_BUILTIN;
  config.apid = "m22toggle";
  config.psk = "87654321";        // Set your custom password for the AP
  config.menuItems = config.menuItems | AC_MENUITEM_DELETESSID;   //enable delete credentials of some SSIDs
  config.autoReconnect = true;                    // Attempt automatic reconnection.
  config.reconnectInterval = 1;
  config.portalTimeout = 180000;

  portal.config(config);
  portal.onOTAStart(exitOTAStart);
  portal.onOTAEnd(exitOTAEnd);
  portal.onOTAProgress(exitOTAProgress);
  portal.onOTAError(exitOTAError);
  if (portal.begin()) {
    Serial.println("WiFi connected: " + WiFi.localIP().toString());
  } else {
    Serial.println("No WiFi, restarting... ");
    delay(1000);
    ESP.restart();
  }  

  startMQTT();                                // Start MQTT
  t_mqtt.every(period_mqtt, reconnectMQTT);   // MQTT reconnect timer 
  t_NTP.every(period_NTP, syncTime);          // NTP time sync timer

  btn1.begin(BUTTON_1);
  btn1.setPressedHandler(pressed);
  btn1.setReleasedHandler(released);
  btn2.begin(BUTTON_2);
  btn2.setPressedHandler(pressed);
  btn2.setReleasedHandler(released);

  syncTime();                             //sync NTP time 
  activeScreen = true;                    //screen is active and ready for command
  afterEvent = t_LCDON.after(period_LCDON, turnOFFLCD);
}


//******************************************************************************************************************
//******************************************************************************************************************
void loop() {
  portal.handleClient();
  btn1.loop();
  btn2.loop();  
  checkMQTT();  
  displayLocalTime();
  t_NTP.update();
  t_LCDON.update();
}
