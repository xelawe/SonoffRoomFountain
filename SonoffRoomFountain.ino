/*

    Flash Mode: DIO
    Flash Frequency: 40MHz
    Upload Using: Serial
    CPU Frequency: 80MHz
    Flash Size: 1M (64K SPIFFS)
    Debug Port: Disabled
    Debug Level: None
    Reset Method: ck
    Upload Speed: 115200
    Port: Your COM port connected to sonoff

   1MB flash sizee

   sonoff header
   1 - vcc 3v3
   2 - rx
   3 - tx
   4 - gnd
   5 - gpio 14

   esp8266 connections
   gpio  0 - button
   gpio 12 - relay
   gpio 13 - green led - active low
   gpio 14 - pin 5 on header

*/

#define SONOFF_BUTTON    0
#define SONOFF_RELAY    12
#define SONOFF_LED      13
#define SONOFF_INPUT    14

#define relStateOFF LOW
#define relStateON HIGH

#define BLYNK_PRINT Serial    // Comment this out to disable prints and save space
#include <ESP8266WiFi.h>
#include <BlynkSimpleEsp8266.h>

static bool BLYNK_ENABLED = true;

#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager

#include <EEPROM.h>

#include <WiFiUdp.h>
#include <TimeAlarms.h>

#define EEPROM_SALT 12663
typedef struct {
  int   salt = EEPROM_SALT;
  char  blynkToken[33]  = "";
  char  blynkServer[33] = "blynk-cloud.com";
  char  blynkPort[6]    = "8442";
} WMSettings;

WMSettings settings;

#include <ArduinoOTA.h>

//for LED status
#include <Ticker.h>
Ticker ticker;


const int CMD_WAIT = 0;
const int CMD_BUTTON_CHANGE = 1;
int cmd = CMD_WAIT;

const int CMD_INPUT_CHANGE = 1;
int cmd_inp = CMD_WAIT;

int relayState = relStateOFF;

//inverted button state
int buttonState = HIGH;
int InputState = HIGH;

static long startPress = 0;

/* Don't hardwire the IP address or we won't get the benefits of the pool.
    Lookup the IP address for the host name instead */
IPAddress timeServerIP; // time.nist.gov NTP server address
const char* ntpServerName = "time.nist.gov";

// A UDP instance to let us send and receive packets over UDP
WiFiUDP Udp;
unsigned int localPort = 2390;      // local port to listen for UDP packets
const int NTP_PACKET_SIZE = 48; // NTP time is in the first 48 bytes of message
byte packetBuffer[NTP_PACKET_SIZE]; //buffer to hold incoming & outgoing packets

const int timeZone = 2;     // Central European Time


void tick()
{
  //toggle state
  int state = digitalRead(SONOFF_LED);  // get the current state of GPIO1 pin
  digitalWrite(SONOFF_LED, !state);     // set pin to the opposite state
}

//gets called when WiFiManager enters configuration mode
void configModeCallback (WiFiManager *myWiFiManager) {
  Serial.println("Entered config mode");
  Serial.println(WiFi.softAPIP());
  //if you used auto generated SSID, print it
  Serial.println(myWiFiManager->getConfigPortalSSID());
  //entered config mode, make led toggle faster
  ticker.attach(0.2, tick);
}



void setState(int s) {

  int lv_s = s;

  // When there is no water -> turn relay off!
  if ( lv_s == relStateON && InputState == HIGH ) {
    lv_s = relStateOFF;
    relayState = lv_s;
  }


  Serial.println(relayState);

  digitalWrite(SONOFF_RELAY, lv_s);
  digitalWrite(SONOFF_LED, (lv_s + 1) % 2); // led is active low
  Blynk.virtualWrite(6, lv_s * 255);
}

void turnOn() {
  relayState = relStateON;
  setState(relayState);
}

void turnOff() {
  relayState = relStateOFF;
  setState(relayState);
}

void toggleState() {
  cmd = CMD_BUTTON_CHANGE;
}

void toggleInput() {
  cmd_inp = CMD_INPUT_CHANGE;
}


//flag for saving data
bool shouldSaveConfig = false;

//callback notifying us of the need to save config
void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}


void toggle() {
  Serial.println("toggle state");
  relayState = relayState == relStateOFF ? relStateON : relStateOFF;
  setState(relayState);
}

void restart() {
  ESP.reset();
  delay(1000);
}

void reset() {
  //reset settings to defaults
  /*
    WMSettings defaults;
    settings = defaults;
    EEPROM.begin(512);
    EEPROM.put(0, settings);
    EEPROM.end();
  */
  //reset wifi credentials
  WiFi.disconnect();
  delay(1000);
  ESP.reset();
  delay(1000);
}


//off - button
BLYNK_WRITE(0) {
  int a = param.asInt();
  if (a != 0) {
    turnOff();
  }
}

//on - button
BLYNK_WRITE(1) {
  int a = param.asInt();
  if (a != 0) {
    turnOn();
  }
}

//toggle - button
BLYNK_WRITE(2) {
  int a = param.asInt();
  if (a != 0) {
    toggle();
  }
}

//restart - button
BLYNK_WRITE(3) {
  int a = param.asInt();
  if (a != 0) {
    restart();
  }
}

//restart - button
BLYNK_WRITE(4) {
  int a = param.asInt();
  if (a != 0) {
    reset();
  }
}

//status - display
BLYNK_READ(5) {
  Blynk.virtualWrite(5, relayState);
}

void setup()
{
  Serial.begin(115200);

  //set led pin as output
  pinMode(SONOFF_LED, OUTPUT);
  // start ticker with 0.5 because we start in AP mode and try to connect
  ticker.attach(0.6, tick);

  const char *hostname = "ambient";

  WiFiManager wifiManager;
  //set callback that gets called when connecting to previous WiFi fails, and enters Access Point mode
  wifiManager.setAPCallback(configModeCallback);

  //timeout - this will quit WiFiManager if it's not configured in 3 minutes, causing a restart
  wifiManager.setConfigPortalTimeout(180);

  //custom params
  EEPROM.begin(512);
  EEPROM.get(0, settings);
  EEPROM.end();

  if (settings.salt != EEPROM_SALT) {
    Serial.println("Invalid settings in EEPROM, trying with defaults");
    WMSettings defaults;
    settings = defaults;
  }

  Serial.println(settings.blynkToken);
  Serial.println(settings.blynkServer);
  Serial.println(settings.blynkPort);

  WiFiManagerParameter custom_blynk_text("Blynk config. <br/> No token to disable.");
  wifiManager.addParameter(&custom_blynk_text);

  WiFiManagerParameter custom_blynk_token("blynk-token", "blynk token", settings.blynkToken, 33);
  wifiManager.addParameter(&custom_blynk_token);

  WiFiManagerParameter custom_blynk_server("blynk-server", "blynk server", settings.blynkServer, 33);
  wifiManager.addParameter(&custom_blynk_server);

  WiFiManagerParameter custom_blynk_port("blynk-port", "blynk port", settings.blynkPort, 6);
  wifiManager.addParameter(&custom_blynk_port);

  //set config save notify callback
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  if (!wifiManager.autoConnect()) {
    Serial.println("failed to connect and hit timeout");
    //reset and try again, or maybe put it to deep sleep
    ESP.reset();
    delay(1000);
  }

  //Serial.println(custom_blynk_token.getValue());
  //save the custom parameters to FS
  if (shouldSaveConfig) {
    Serial.println("Saving config");

    strcpy(settings.blynkToken, custom_blynk_token.getValue());
    strcpy(settings.blynkServer, custom_blynk_server.getValue());
    strcpy(settings.blynkPort, custom_blynk_port.getValue());

    Serial.println(settings.blynkToken);
    Serial.println(settings.blynkServer);
    Serial.println(settings.blynkPort);

    EEPROM.begin(512);
    EEPROM.put(0, settings);
    EEPROM.end();
  }

  //config blynk
  if (strlen(settings.blynkToken) == 0) {
    BLYNK_ENABLED = false;
  }
  if (BLYNK_ENABLED) {
    Blynk.config(settings.blynkToken, settings.blynkServer, atoi(settings.blynkPort));
  }

  //OTA
  ArduinoOTA.onStart([]() {
    Serial.println("Start OTA");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.setHostname(hostname);
  ArduinoOTA.begin();

  //if you get here you have connected to the WiFi
  Serial.println("connected...yeey :)");
  ticker.detach();

  //setup button
  pinMode(SONOFF_BUTTON, INPUT);
  attachInterrupt(SONOFF_BUTTON, toggleState, CHANGE);

  //setup relay
  pinMode(SONOFF_RELAY, OUTPUT);

  //setup input
  pinMode(SONOFF_INPUT, INPUT);
  InputState = digitalRead(SONOFF_INPUT);
  attachInterrupt(SONOFF_INPUT, toggleInput, CHANGE);

  Serial.println("Starting UDP");
  Udp.begin(localPort);
  Serial.print("Local port: ");
  Serial.println(Udp.localPort());

  Serial.println("waiting for sync");
  //get a random server from the pool
  WiFi.hostByName(ntpServerName, timeServerIP);

  setSyncProvider(getNtpTime);
  while (timeStatus() == timeNotSet) { // wait until the time is set by the sync provider
    setSyncInterval(1); // Try to sync each second
    delay(2000);
  }
  setSyncInterval(300); // Sync every 5 minutes

  turnOff();

  //turnOn();
  Alarm.alarmRepeat(6, 0, 0, turnOn);
  Alarm.alarmRepeat(20, 15, 0, turnOff);

  Serial.println("done setup");

}


void loop()
{

  //ota loop
  ArduinoOTA.handle();
  //blynk connect and run loop
  if (BLYNK_ENABLED) {
    Blynk.run();
  }

  //delay(200);
  //Serial.println(digitalRead(SONOFF_BUTTON));

  switch (cmd_inp) {
    case CMD_WAIT:
      break;
    case CMD_INPUT_CHANGE:
      int currentStateInp = digitalRead(SONOFF_INPUT);
      if (currentStateInp != InputState) {
        if (currentStateInp == HIGH) {
          turnOff();
        }
        InputState = currentStateInp;
        break;
      }
  }

  switch (cmd) {
    case CMD_WAIT:
      break;
    case CMD_BUTTON_CHANGE:
      int currentState = digitalRead(SONOFF_BUTTON);
      if (currentState != buttonState) {
        if (buttonState == LOW && currentState == HIGH) {
          long duration = millis() - startPress;
          if (duration < 50) {
            Serial.println("too short press - no action");
          } else if (duration < 2000) {
            Serial.println("short press - toggle relay");
            toggle();
          } else if (duration < 10000) {
            Serial.println("medium press - reset");
            restart();
          } else if (duration < 60000) {
            Serial.println("long press - reset settings");
            reset();
          }
        } else if (buttonState == HIGH && currentState == LOW) {
          startPress = millis();
        }
        buttonState = currentState;
      }
      break;
  }

  Alarm.delay( 0 );

}



/*-------- NTP code ----------*/
time_t getNtpTime()
{
  while (Udp.parsePacket() > 0) ; // discard any previously received packets
  Serial.println("Transmit NTP Request");
  //sendNTPpacket(timeServer);
  sendNTPpacket(timeServerIP);
  uint32_t beginWait = millis();
  while (millis() - beginWait < 1500) {
    int size = Udp.parsePacket();
    if (size >= NTP_PACKET_SIZE) {
      Serial.println("Receive NTP Response");
      Udp.read(packetBuffer, NTP_PACKET_SIZE);  // read packet into the buffer
      unsigned long secsSince1900;
      // convert four bytes starting at location 40 to a long integer
      secsSince1900 =  (unsigned long)packetBuffer[40] << 24;
      secsSince1900 |= (unsigned long)packetBuffer[41] << 16;
      secsSince1900 |= (unsigned long)packetBuffer[42] << 8;
      secsSince1900 |= (unsigned long)packetBuffer[43];
      return secsSince1900 - 2208988800UL + timeZone * SECS_PER_HOUR;
    }
  }
  Serial.println("No NTP Response :-(");
  return 0; // return 0 if unable to get the time
}

// send an NTP request to the time server at the given address
void sendNTPpacket(IPAddress & address)
{
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12]  = 49;
  packetBuffer[13]  = 0x4E;
  packetBuffer[14]  = 49;
  packetBuffer[15]  = 52;
  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  Udp.beginPacket(address, 123); //NTP requests are to port 123
  Udp.write(packetBuffer, NTP_PACKET_SIZE);
  Udp.endPacket();
}



