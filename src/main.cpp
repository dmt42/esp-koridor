#define ESP8266
#include <Arduino.h>
#if defined(ESP32) || defined(LIBRETINY)
#include <AsyncTCP.h>
#include <WiFi.h>
#elif defined(ESP8266)
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#elif defined(TARGET_RP2040) || defined(TARGET_RP2350) || defined(PICO_RP2040) || defined(PICO_RP2350)
#include <RPAsyncTCP.h>
#include <WiFi.h>
#endif
#include <Adafruit_MCP23X17.h>      //MCP GPIO expander
#include <PubSubClient.h>         // Mqtt stuff
#include <NTPClient.h>            // NTP stuff
#include <ESP8266WiFi.h>          // WiiFi stack
#include <ESPAsyncTCP.h>
#include "ESPAsyncWebServer.h"
#include <DNSServer.h>
#include <ESPAsyncWiFiManager.h>
#include "LittleFS.h"
#include <ArduinoJson.h>          // Working with config file
#include <ArduinoOTA.h> // OTA update
#include <ESPping.h>

/*
MCP23017 	Pin Arduino IDE
21 	A0 	0
22 	A1 	1
23 	A2 	2
24 	A3 	3
25 	A4 	4
26 	A5 	5
27 	A6 	6
28 	A7 	7
1 	B0 	8
2 	B1 	9
3 	B2 	10
4 	B3 	11
5 	B4 	12
6 	B5 	13
7 	B6 	14
8 	B7 	15
*/

// MCP23XXX input pins is attached to
#define PIR_BATH 1
#define PIR_KOR 0
#define PIR_TAMBUR 2
//#define SONIC
// Sonic pins
#define PIR_ZAL 12 //ESP8266
//#define PIN_ECHO 12 //ESP8266
#define PIN_TRIG 15 // MCP23017

#define LAMP_KOR_N 8
#define LAMP_KOR_L 9
#define LAMP_BATH 10
#define LAMP_TAMB 11
#define LEDPIN 14 //ESP8266
#define LAMP_ON LOW //coz relay engaged on low signal :((
#define LAMP_OFF HIGH

#define MSG_BUFFER_SIZE	(80) // Various conversations
#define U_PART U_FS
size_t content_len;


//GPIO expander
Adafruit_MCP23X17 mcp;

// NTP клиент
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "ntp.rdtc.ru", 0, 60000);

// DNS сервер
DNSServer dns;

// Веб сервер
AsyncWebServer server(80);

// Клиент WIFI
WiFiClient espClient;

// Клиент MQTT
PubSubClient client(espClient);


/*******************************************                Глобальные переменные              ***************************************/
uint32_t onSec, on3Sec, on30Sec, netSec, on10ms, lastMsg, uptime, now;
bool nightMode = false, intFlag = false, shouldReboot = false, resetFlag=false, pwmOff = false, pwmOn = false; // Флаги ESP
uint16_t dutyCycle;
int saveCfg = 0; //Flags
uint16_t tambDelay, bathDelay, korDelay, zalDelay, pwLed=511;
uint8_t tz = 7; // Time zone settings
String hostName, ipString, deviceMAC = "00:00:00:00:00:00";           // Empty MAC; // ID устройства ="ESP"+mac address ip в виде строки
StaticJsonDocument<2048> cfg; // json object
const char *filename = "/config.json";         // Name of json config file
char msg[MSG_BUFFER_SIZE];
char topic[100];
char MQTT_Client;
const char* http_username = "esp8266";
const char* http_password = "12345";
const char* mqtt_server = "example.com"; // Initial MQTT server address
const char* mqtt_login = "login"; // Initial MQTT login
const char* mqtt_password = "password"; // Initial MQTT password
const char* net_refresh = "600" ; // Initial Net send interval
const char* myAP = "ESP_koridor"; // Initial AP name
const char* mqtt_status = "<font class=ofln>Offline</font>";
const char* ip_address = "0.0.0.0"; // Initial IP
const char* cmd_reboot = "reboot"; // keyword command in system topic
const char* cmd_update = "update"; // keyword command in system topic
const char* cmd_lightsoff ="lightsoff"; // keyword command switch off all lights in system topic
const char* cmd_ledoff ="ledoff"; // keyword command switch off led in koridor
const char* cmd_ledonday ="ledonday"; // keyword command switch oo led in koridor
const char* cmd_ledonight ="ledonight"; // keyword command switch oo led in koridor
const char* cmd_koron ="koron"; // keyword command switch on the light in koridor
const char* cmd_koroff ="koroff"; // keyword command switch off the light in koridor
const char* cmd_prihon ="prihon"; // keyword command switch on the light in koridor
const char* cmd_prihoff ="prihoff"; // keyword command switch off the light in koridor
const char* cmd_bathoff ="bathoff"; // keyword command switch off the light in bath
const char* cmd_bathon ="bathon"; // keyword command switch off the light in bath
const char* cmd_tamboff ="tamboff"; // keyword command switch off the light in tambur
const char* cmd_tambon ="tambon"; // keyword command switch off the light in tambur
const char* cmd_night ="night"; // keyword command night mode
const char* cmd_day ="day"; // keyword command day mode
const char* espId = "ESP_koridor";
const char* cmdTopic = "smart/ESP_koridor/cmd";
const char* state = "smart/ESP_koridor/tele";
const char* tamb_on = "t_on";
const char* tamb_off = "t_off";
const char* kor_on = "k_on";
const char* kor_off = "k_off";
const char* pih_on = "p_on";
const char* pih_off = "p_off";
const char* bath_on = "b_on";
const char* bath_off = "b_off";
const char* bdelay = "300";
const char* zdelay = "30";
const char* lmax = "3000";
const char* ulogin = "smarthouse";
const char* userver = "192.168.1.3";
const char* upass = "123456";
//const char* cur_dist = "1.3";
const char* ntp_server = "62.231.161.9";

float duration_us, distance_cm, dist;

// Метрики MQTT
static const char metrics[4][8]  = {"syst","wanip","rssi","uptm"};

// Первичные значения MQTT пакета
 String mqttValues[]  = {
  /*0 syst*/"ready",
  /*1 wanip*/"0.0.0.0",
  /*2 rssi*/"0",
  /*3 uptime*/"0",
};


/* MQTT reconnect */
void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
     // Attempt to connect
    if (client.connect(espId,mqtt_login,mqtt_password)) {
      mqtt_status = "<font class=onln>Online</font>";
      Serial.println(" connected!");
       client.subscribe(cmdTopic);
      } 
    else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
      if (resetFlag == true) { // Get reset command
        ESP.reset();
      }
    }
  }
}

/* Send metrics to mosquitto */
void sendMqtt() {
  reconnect();
  for (uint8_t i=0;i<sizeof(metrics)/sizeof(*metrics);++i) {
    const char* nam = metrics[i];
    String mqTopic = String(mqtt_login) + "/" + String(espId) + "/" + String(nam);
    client.publish(mqTopic.c_str(),mqttValues[i].c_str());
  }
}

void sendMqtt_state(const char* pl) {
    reconnect();
    client.publish(state,pl);
  }


// выключаем ВЕСЬ свет
void lightsOff() {
  mcp.digitalWrite(LAMP_KOR_L,LAMP_OFF);
  mcp.digitalWrite(LAMP_KOR_N,LAMP_OFF);
  mcp.digitalWrite(LAMP_BATH,LAMP_OFF);
  mcp.digitalWrite(LAMP_TAMB,LAMP_OFF);
}

// включаем свет в коридоре
void korOn() {
  mcp.digitalWrite(LAMP_KOR_L,LAMP_ON);
  sendMqtt_state(kor_on);
}

// выключаем свет в коридоре
void korOff() {
  mcp.digitalWrite(LAMP_KOR_L,LAMP_OFF);
  sendMqtt_state(kor_off);
}

// включаем свет в коридоре
void prihOn() {
  mcp.digitalWrite(LAMP_KOR_N,LAMP_ON);
  sendMqtt_state(pih_on);
}

// выключаем свет в коридоре
void prihOff() {
  mcp.digitalWrite(LAMP_KOR_N,LAMP_OFF);
  sendMqtt_state(pih_off);
}

// включаем свет в ванной
void bathOn() {
  mcp.digitalWrite(LAMP_BATH,LAMP_ON);
  sendMqtt_state(bath_on);
}

// выключаем свет в ванной
void bathOff() {
  mcp.digitalWrite(LAMP_BATH,LAMP_OFF);
  sendMqtt_state(bath_off);
}
// включаем свет в тамбуре
void tambOn() {
  mcp.digitalWrite(LAMP_TAMB,LAMP_ON);
  sendMqtt_state(tamb_on);
}

// выключаем свет в тамбуре
void tambOff() {
  mcp.digitalWrite(LAMP_TAMB,LAMP_OFF);
  sendMqtt_state(tamb_off);
}

/* Выполняется каждые 50 милисекунд */
void every10mS(){
  on10ms = millis();
  
  if (pwmOn) {
    if (dutyCycle >= pwLed) {
      pwmOn = false;
      dutyCycle = pwLed;
    }
    else {
      dutyCycle++; 
    }
  }

  if (pwmOff) {
    if (dutyCycle <= 0) {
      pwmOff = false;
      dutyCycle = 0;
    }
    else {    
      dutyCycle--;
    }
  }
}

/* Выполняется каждую секунду */
void everySec(){
  onSec = millis();
  // установка ночного режима
  if (resetFlag == true) ESP.reset(); // Получена команда перезагрузки с веб
}

/* Выполняется каждые 3 секунды */
void every3Sec(){
  on3Sec = millis();
 }

/* Выполняется каждые 30 секунд */
void every30Sec() {
  on30Sec = millis();
  mqttValues[2] = String(WiFi.RSSI());
  mqttValues[3] = String(uptime/1000);
  timeClient.update();
}

/* Выполняется каждые с периодом указаным в конфигурации */
void everyNetUpdate() {
  lastMsg = millis();
  Serial.println("Time to update arrived..");
  mqttValues[1] = WiFi.localIP().toString();
  sendMqtt();
  timeClient.update();
  if (Ping.ping(WiFi.gatewayIP()) > 0){
    Serial.printf(" response time : %d/%.2f/%d ms\n", Ping.minTime(), Ping.averageTime(), Ping.maxTime());
  } else {
    resetFlag = true;
  }    
}


/* Callback for topic */
void callback(char* topic, byte* payload, int length) {
  Serial.println("New message arrived..");
  /*Get system*/
  if (strcmp(topic, cmdTopic) == 0) {
    payload[length] = '\0'; // Null terminator used to terminate the char array
    const char* message = (char*)payload;
    if (strcmp (message,cmd_lightsoff) == 0) {
      lightsOff();
      Serial.println("switch all lights off..");
    }
  
    if (strcmp (message,cmd_koron) == 0) korOn();
    if (strcmp (message,cmd_prihon) == 0) prihOn();
    
    if (strcmp (message,cmd_koroff) == 0) korOff();
    if (strcmp (message,cmd_prihoff) == 0) prihOff();

    if (strcmp (message,cmd_bathon) == 0) bathOn();
    if (strcmp (message,cmd_bathoff) == 0) bathOff();

    if (strcmp (message,cmd_tambon) == 0) tambOn();
    if (strcmp (message,cmd_tamboff) == 0) tambOff();
  
    if (strcmp (message,cmd_reboot) == 0) {
      resetFlag = true;
      Serial.println("reboot over mqtt..");
    }
    
    if (strcmp (message,cmd_ledonday) == 0) {
    pwLed=511;
    pwmOn = true; pwmOff = false;
    Serial.println("LED on...");
    }
   
    if (strcmp (message,cmd_ledonight) == 0) {
    pwLed=15;
    pwmOn = true; pwmOff = false;
    Serial.println("LED on...");
    }
    
    if (strcmp (message,cmd_ledoff) == 0) {
    pwmOff = true; pwmOn = false;
    Serial.println("LED off...");
    }  

    if (strcmp (message,cmd_day) == 0) {
    pwLed = 511;
    Serial.println("Day mode");
    }
  }
}

void configModeCallback (AsyncWiFiManager *myWiFiManager) {
  Serial.println("Entered config mode");
  Serial.println(WiFi.softAPIP());
  //if you used auto generated SSID, print it  
  Serial.println(myWiFiManager->getConfigPortalSSID());
  }

/* Async Web Server processor */
String processor(const String& var){
// Return various template values
if(var == "MQTTSERVER") return String(mqtt_server);
else if(var == "MQTTUSER") return String(mqtt_login);
else if(var == "MQTTPASS") return String(mqtt_password);
else if(var == "REFRESH") return String(net_refresh);
else if(var == "S_MQTT") return String(mqtt_status);
else if(var == "IPADDR") return String(ipString);
else if(var == "UPTIME") return String(uptime/1000);
else if(var == "ULOGIN") return String(ulogin);
else if(var == "UPASS") return String(upass);
else if(var == "USERVER") return String(userver);


else if(var == "BDELAY") return String(bdelay);
else if(var == "ZDELAY") return String(zdelay);
else if(var == "FTM") return timeClient.getFormattedTime();


else if(var == "REBOOT") {
  if (saveCfg == 1) return String("SAVE SETTINGS, PLEASE WAIT...");
  else if (saveCfg == 2) return String("UPDATING, PLEASE WAIT...");
  else return String("REBOOTING, PLEASE WAIT...");
}

return String();
}

/* Concatenate prefix and metric to get full mqtt topic*/
char* setTopic(const char* prefix, const char* metric) {
  int bufferSize = strlen(prefix) + strlen(metric) + 1;
  char* sum = new char[ bufferSize ];
  strcpy(sum, prefix);
  strcat(sum, metric);
  return sum;
  delete[] sum;
}


void notFound(AsyncWebServerRequest *request) {
    request->send(404, "text/plain", "Not found");
}

// Read cfg file and put it in json
void readConf() {
// Deserialize the JSON document
  File file = LittleFS.open(filename, "r");
  DeserializationError error = deserializeJson(cfg, file);
  if (error) Serial.println(F("Failed to read file, using default configuration"));
  mqtt_server=cfg["hostname"];
  mqtt_login=cfg["login"];
  mqtt_password=cfg["pass"];
  net_refresh=cfg["refresh"];
   bdelay=cfg["bdelay"];
  zdelay=cfg["zdelay"];
  
  file.close();

}

// Save json config to file
void saveConf(){
  LittleFS.remove(filename);
  File file = LittleFS.open(filename, "a");
  serializeJson(cfg, file);
  file.close();
  resetFlag = true;
  saveCfg = 1;
  Serial.println("wrote ");
  Serial.print("Koridor = ");
  Serial.println(String(cfg["kdelay"])); 
  Serial.print("Bath = ");
  Serial.println(String(cfg["bdelay"])); 
  Serial.print("Tambur = ");
  Serial.println(String(cfg["tdelay"])); 
  //lampDelay=cfg["ldelay"];
}


void setup() {
  Serial.begin(9600);
  Serial.print("Initializing mcp...");
  if (!mcp.begin_I2C()) {
    Serial.println("Error initialize mcp...");
   while (1);
  }
  else Serial.println("Initialize MCP23017 OK!");
  analogWriteRange(511);
  // configure pin for input
  mcp.pinMode(PIR_TAMBUR, INPUT);
  mcp.pinMode(PIR_KOR, INPUT);
  mcp.pinMode(PIR_BATH, INPUT);
  pinMode(LEDPIN, OUTPUT);
  mcp.pinMode(LAMP_KOR_L, OUTPUT); //B1
  mcp.pinMode(LAMP_KOR_N, OUTPUT); //B0
  mcp.pinMode(LAMP_BATH, OUTPUT); //B2
  mcp.pinMode(LAMP_TAMB, OUTPUT); //B3
  digitalWrite(LEDPIN,LOW);
  hostName = "ESP"+WiFi.macAddress(); // Create the name of device
  hostName.replace(":","");
  timeClient.begin();
  timeClient.setTimeOffset(tz*3600);
  timeClient.setPoolServerName(ntp_server);
  timeClient.update();
  deviceMAC = (WiFi.macAddress());
  if(!LittleFS.begin()){
    Serial.println(F("An Error has occurred while mounting LittleFS"));
    return;
  }
  // Enable OTA update
  ArduinoOTA.begin();
  uptime = now;

/******************WEB config********************/
if (1) {
// root page
server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
     request->send(LittleFS, "/index.html", String(), false, processor);
    });
  // reset page
  server.on("/reset.html", HTTP_GET, [](AsyncWebServerRequest *request){
     request->send(LittleFS, "/reset.html", String(), false, processor);
     resetFlag = true;
  });
  // config page
  server.on("/conf.html", HTTP_GET, [](AsyncWebServerRequest *request){
     request->send(LittleFS, "/conf.html", String(), false, processor);
  });
  // update page
  server.on("/update.html", HTTP_GET, [](AsyncWebServerRequest *request){
     request->send(LittleFS, "/update.html", String(), false, processor);
  });
  // image
  server.on("/progress.gif", HTTP_GET, [](AsyncWebServerRequest *request){
     request->send(LittleFS, "/progress.gif", "img/gif");
  });
  // css file
   server.on("/wst.css", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(LittleFS, "/wst.css", "text/css");
  });
  // HTTP basic authentication
  server.on("/login", HTTP_GET, [](AsyncWebServerRequest *request){
    if(!request->authenticate(http_username, http_password))
        return request->requestAuthentication();
    request->send(LittleFS, "/conf.html", String(), false, processor);
  });
  // Simple Firmware Update Form
  server.on("/update", HTTP_GET, [](AsyncWebServerRequest *request){
  request->send(200, "text/html", "<form method='POST' action='/update' enctype='multipart/form-data'><input type='file' name='update'><input type='submit' value='Update'></form>");
  });
  server.on("/update", HTTP_POST, [](AsyncWebServerRequest *request){
    // resetFlag = !Update.hasError();
    AsyncWebServerResponse *response = request->beginResponse(200, "text/html", !Update.hasError()?"Update success :)  <a href='/reset.html'>Apply changes</a>":"Update fail :(");
    response->addHeader("Connection", "close");
    request->send(response);
  },[](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final){
    if(!index){
      Serial.printf("Update Start: %s\n", filename.c_str());
      content_len = request->contentLength();
      uint32_t maxSketchSpace = ((size_t) &_FS_end - (size_t) &_FS_start);
      // if filename includes littlefs, update the littlefs partition
      int cmd = (filename.indexOf("littlefs") > -1) ? U_PART : U_FLASH;
      Update.runAsync(true);
      Serial.println(cmd);
      
      if (!Update.begin(maxSketchSpace,cmd)){
          Update.printError(Serial);
        }
    }
    if(!Update.hasError()){
      if(Update.write(data, len) != len){
        Update.printError(Serial);
      }
    }
    if(final){
      if(Update.end(true)){
        Serial.printf("Update Success: %uB\n", index+len);
       //delay(3000);
        // resetFlag = true;
      } else {
        Update.printError(Serial);
      }
    }
  });
 // config trap
 server.on("/set_config", HTTP_POST, [](AsyncWebServerRequest *request){
    size_t params = request->params();
    cfg["narodmon"]="0";
     for(size_t i=0;i<params;i++){ // some hardcoded shit, can be resided in processor but im too lazy 
      const AsyncWebParameter *p = request->getParam(i);
      Serial.printf("POST[%s]: %s\n", p->name().c_str(), p->value().c_str());
      cfg[p->name()] = p->value();
      //Serial.printf("json[%s]:%s\n",cfg[p->name()],cfg[p->value()]);
    }
    //delay(1000);
  
  saveConf();
     request->send(LittleFS, "/reset.html", String(), false, processor);
     resetFlag = true;
  });
}
  
readConf();
  Serial.println("reading");
  netSec = String(net_refresh).toInt();
  bathDelay = String(bdelay).toInt();
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);

  //WiFiManager
  AsyncWiFiManager wifiManager(&server,&dns);
  wifiManager.setAPCallback(configModeCallback);
  if(!wifiManager.autoConnect()) {
    Serial.println("failed to connect and hit timeout");
    //reset and try again, or maybe put it to deep sleep
    ESP.reset();
    //delay(1000);
  }
  ipString = String(WiFi.localIP()[0]);
  for (byte octet = 1; octet < 4; ++octet) {
    ipString += '.' + String(WiFi.localIP()[octet]);
  }
  server.onNotFound(notFound);
  server.begin();
  Serial.println("Looping...");
  reconnect();
  lightsOff();
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();
  analogWrite(LEDPIN,dutyCycle);
  /* Обработка периодических событий */
  unsigned long now = millis();
  uptime = now;
  if (now - on10ms >= 10) every10mS(); // Выполняется раз в 100 ms
  if (now - onSec >= 1000) everySec(); // Выполняется раз в сек
  if (now - on3Sec >= 3000) every3Sec(); // Выполняется раз в 3 сек
  if (now - on30Sec >= 30000) every30Sec(); // Выполняется раз в 30 сек
  if (now - lastMsg >= netSec*1000) everyNetUpdate(); // Выполняется каждый (netSec) промежуток времени, заданный в веб интерфейсе 

}

