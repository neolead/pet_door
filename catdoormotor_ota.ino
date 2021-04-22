/*
range / rssi
20cm - -59..-60 
30cm - -64..-65
40cm - -67..-68
50cm - -69
60cm - -70..-71
*/
#include <Servo.h>
#include <SlowMotionServo.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <BLEAddress.h>
#include <Ticker.h>
#include <Arduino.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
SMSSmooth myServo; 

bool gerkondebug = false; //debug gerkon
static boolean debug = false; //more verbose
String fwversion = "22042021.1";
String knownAddresses[] = {"d8:e3:43:1d:67:71", "d8:e3:43:1d:67:70"};  //mac of tag ibeacon
const int gerkonpin = 14; //pin for gerkon GND and d14
const int switch_disable_pin = 27; //pin for switch to always open door.. if switched - door always open
const int servo_pin = 13;
float servo_speed = 1.0;
float opendoor = 0.6 ; //servo in open door position
float closedoor = 0.99; //servo in closed door position
int sleeptime = 1;  //sleep before init
int yourInputInt, blockInt, unblockInt,updateInt;
int alr,  q, f = 0;
const char* defaultrssi = "-60";   // default level of rssi
const char* defaultblock = "1";    // default times to retry ble found
const char* defaultunblock = "2";  // default times to retry ble is gone
const char* ssid = "door";         // default wifi name
const char* password = "doordoor"; // default wifi password
const char* PARAM_INT = "inputInt";
const char* PARAM2_INT = "blockInt";
const char* PARAM3_INT = "unblockInt";
const char* UPDATE_INT = "updateInt";
bool updflag = false;
int cal = 0;
int bigrssi = 1; //total count high rssi
int countrssi = 0; //inc rssi
bool gerkonok = false;
bool sw1 = false;
bool sw2 = false;
bool ok = false;
const char* ota_hostname = "catdoor_update";
AsyncWebServer server(80);
int Verzoegerung = 3;                // Switch-off delay if the signal from BLE ibeacon is missing
bool relayswitched = false;
bool donottouch = false;
int VerzoegerungZaeler = 0;
Ticker Tic;
static BLEAddress *pServerAddress;
BLEScan* pBLEScan ;
int scand = 2; // scantime In seconds
int co = 0;
int co2 = 0;
int ncountretr = 2;
int currssi = 0;
 
unsigned long entry;
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html><head>
  <title>ESP Input Form</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <script>
    function submitMessage() {
      alert("Saved value to ESP SPIFFS");
      setTimeout(function(){ document.location.reload(false); }, 500);   
    }
  </script></head><body>
  <form action="/get" target="hidden-form">
    Enter distance to block .<br>(-50 very closer -80 very far)(default -60)<br> (current value %inputInt%): <input type="number " name="inputInt">
    <input type="submit" value="Submit" onclick="submitMessage()">
  </form><br>
  <form action="/get" target="hidden-form">
    Reboot to update firmware<br>Device will reboot after 3 seconds,<br> back to normal mode,reboot device again: <input type="checkbox" name="updateInt" value="1" id="1">
    <input type="submit" value="Submit" onclick="submitMessage()">
  </form><br>
  <iframe style="display:none" name="hidden-form"></iframe>
</body></html>)rawliteral";


void notFound(AsyncWebServerRequest* request) {
    request->send(404, "text/plain", "Not found");
}

String readFile(fs::FS& fs, const char* path) {
    if (debug) {
        Serial.printf("Reading file: %s\r\n", path);
    }
    File file = fs.open(path, "r");
    if (!file || file.isDirectory()) {
        if (debug) {
            Serial.println(F("- empty file or failed to open file"));
        }
        return String();
    }
    if (debug) {
        Serial.println(F("- read from file:"));
    }
    String fileContent;
    while (file.available()) {
        fileContent += String((char)file.read());
    }
    if (debug) {
        Serial.println(fileContent);
    }

    file.close();
    return fileContent;
}
void writeFile(fs::FS& fs, const char* path, const char* message) {
    if (debug) {
        Serial.printf("Writing file: %s\r\n", path);
    }
    delay(150);
    File file = fs.open(path, "w");
    if (!file) {
        if (debug) {
            Serial.println(F("- failed to open file for writing"));
        }
        return;
    }
    if (file.print(message)) {
        if (debug) {
            Serial.println(F("- file written"));
        }
    } else {
        if (debug) {
            Serial.println(F("- write failed"));
        }
    }
}

String processor(const String& var) {
    if (var == "inputInt") {
        return readFile(SPIFFS, "/inputInt.txt");
    } else if (var == "blockInt") {
        return readFile(SPIFFS, "/block.txt");
    } else if (var == "unblockInt") {
        return readFile(SPIFFS, "/unblock.txt");
    } else if (var == "updateInt") {
        return readFile(SPIFFS, "/update.txt");
    }
    return String();
}

void beginOTA(const char* hostname){
    ArduinoOTA.setHostname(hostname);
    ArduinoOTA.onStart([]() {
        Serial.println("Start");
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
     ArduinoOTA.setPort(8266);
 ArduinoOTA.setHostname("updatefeed");
    ArduinoOTA.begin();
    Serial.print("OTA is up");
}

void inputx() {
    yourInputInt = readFile(SPIFFS, "/inputInt.txt").toInt();
    blockInt = readFile(SPIFFS, "/block.txt").toInt();
    unblockInt = readFile(SPIFFS, "/unblock.txt").toInt();
    updateInt = readFile(SPIFFS, "/update.txt").toInt();
    if (!updateInt ){
        writeFile(SPIFFS, "/update.txt", "0");
    }
    if (updateInt == 1){
        updflag = true;
        writeFile(SPIFFS, "/update.txt", "0");
//        writeFile(SPIFFS, "/updflag", "1");
}
    if (!yourInputInt || yourInputInt < -120 || yourInputInt > -30) {
        long yourInputInt = atol(defaultrssi);
        Serial.println(yourInputInt);
        writeFile(SPIFFS, "/inputInt.txt", defaultrssi);
    }
    if (!blockInt || blockInt < 1 || blockInt > 20) {
        long blockInt = atol(defaultblock);
        Serial.println(blockInt);
        writeFile(SPIFFS, "/block.txt", defaultblock);
    }
    if (!unblockInt || unblockInt < 1 || unblockInt > 20) {
        long unblockInt = atol(defaultunblock);
        Serial.println(unblockInt);
        writeFile(SPIFFS, "/unblock.txt", defaultunblock);
    }
    if (debug) {
        Serial.printf("*** Your block value: %d\n", blockInt);
        Serial.printf("*** Your inputInt: %d\n", yourInputInt);
        Serial.printf("*** Your unblock value: %d\n", unblockInt);
    }
}

 
class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice)  {

            BLEAddress pServerAddress = BLEAddress(advertisedDevice.getAddress());
            for (size_t i = 0; i < (sizeof(knownAddresses) / sizeof(knownAddresses[0])); i++) {
              bool deviceKnown = knownAddresses[i].equalsIgnoreCase(pServerAddress.toString().c_str());
              if (deviceKnown) {
                            Serial.println((String)pServerAddress.toString().c_str()+"  Current rssi: "+advertisedDevice.getRSSI());
                            if (advertisedDevice.getRSSI() >= yourInputInt){ 
                                Serial.println("ON");
                                Serial.println((String)"We found cat is near,rssi= "+advertisedDevice.getRSSI());
                                Serial.println ("Turning on relay, door is open");
                                Serial.println("opening");
                                if (myServo.isStopped()) {
                                    myServo.goTo(opendoor);
                                    relayswitched = true;
                                    }
                              VerzoegerungZaeler = 0;                                       // Reset switch-off delay
                              advertisedDevice.getScan()->stop();                           // Stop scanning
                              donottouch = true;
                              alr = 1;
                    }else {
                      donottouch = false;
                    }
            } 
                        
           }
          }
  };

void SekundenTic()  // Runs every second
{
  SlowMotionServo::update();
  if (relayswitched && !donottouch ) {
    VerzoegerungZaeler++;  // Seconds counter
  if (digitalRead(gerkonpin) == HIGH){
        Serial.println("Door is open,can't be blocked");
        gerkonok = false;
      }
   if (digitalRead(gerkonpin) == LOW){
            if (debug) {
              Serial.println("Door at the place,can be blocked");
            }
        gerkonok = true;
        }

    if (VerzoegerungZaeler >= (Verzoegerung * 100)) {
      Serial.println ("Delay time has been reached, checking gerkon");
      
      if (myServo.isStopped() && gerkonok) {
      Serial.println ("Delay time has been reached, switching off relay, door is closing,gerkon allows");
      myServo.goTo(closedoor);
      relayswitched = false;
      }
    }}
}
 
void setup() 
{
  //pinMode(gerkonpin, INPUT);
  pinMode(gerkonpin, INPUT_PULLUP);
  pinMode(switch_disable_pin, INPUT_PULLUP);
  Serial.begin(115200);
    if (!SPIFFS.begin(true)) {
        Serial.println("An Error has occurred while mounting SPIFFS");
        return;
    }
    Serial.print(F("FW. Version:"));
    Serial.println(fwversion);
    delay (2000);
    inputx();
    WiFi.softAP(ssid, password);
    if (debug) {
        Serial.println(F("IP Address: "));
        Serial.println(WiFi.localIP());
    }
    server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) { request->send_P(200, "text/html", index_html, processor); });
    server.on("/get", HTTP_GET, [](AsyncWebServerRequest* request) {
        String inputMessage;

        if (request->hasParam(PARAM_INT)) {
            inputMessage = request->getParam(PARAM_INT)->value();
            int ch = inputMessage.toInt();
            long yourInputInt = atol(defaultrssi);
            if (ch < -120) {
                ch = yourInputInt;
            }
            if (ch > -30) {
                ch = yourInputInt;
            }
            String wr = String(ch, DEC);
            writeFile(SPIFFS, "/inputInt.txt", wr.c_str());
        }
        else if (request->hasParam(UPDATE_INT)) {
            inputMessage = request->getParam(UPDATE_INT)->value();
            int ch = inputMessage.toInt();
            String wr = String(ch, DEC);
            writeFile(SPIFFS, "/update.txt", wr.c_str());
            delay (100);
            ESP.restart();

        } 
        else if (request->hasParam(PARAM2_INT)) {
            inputMessage = request->getParam(PARAM2_INT)->value();
            int ch = inputMessage.toInt();
            long blockInt = atol(defaultblock);
            if (ch < 1) {
                ch = blockInt;
            }
            if (ch > 20) {
                ch = blockInt;
            }
            String wr = String(ch, DEC);
            writeFile(SPIFFS, "/block.txt", wr.c_str());
        } else if (request->hasParam(PARAM3_INT)) {
            inputMessage = request->getParam(PARAM3_INT)->value();
            int ch = inputMessage.toInt();
            long unblockInt = atol(defaultunblock);
            if (ch < 1) {
                ch = unblockInt;
            }
            if (ch > 20) {
                ch = unblockInt;
            }
            String wr = String(ch, DEC);
            writeFile(SPIFFS, "/unblock.txt", wr.c_str());
        } else {
            inputMessage = "No message sent";
        }
        if (debug) {
            Serial.println(inputMessage);
        }
        request->send(200, "text/text", inputMessage);
    });
    server.onNotFound(notFound);
    server.begin();
    delay(100);

if (!updflag){
    String ss = "We sleep ";
    ss += sleeptime / 1000;
    ss += " sec, wait for door init";
    Serial.println(ss);
    delay(sleeptime);
    //WiFi.mode(WIFI_OFF);
}

if (!updflag){
  Serial.println("");
  Serial.println("Starting gateway BLE Scanner");
  //myServo.setSpeed(1.0);
  myServo.setSpeed(servo_speed);
  myServo.setInitialPosition(closedoor);
  myServo.setMinMax(544, 2400);
  myServo.setPin(servo_pin);
  BLEDevice::init("");
  pBLEScan = BLEDevice::getScan(); //create new scan
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(true); //active scan uses more power, but get results faster
  Tic.attach( 0.01,SekundenTic);

  
}

if (updflag){ beginOTA(ota_hostname); }

}
 
void loop() {
    if (updflag){
      if (cal==0){
        Serial.println("Update flag is SET:");
        cal = 1;
      }
    ArduinoOTA.handle();
    }

    if (!updflag){

      if (digitalRead(switch_disable_pin) == HIGH){
          if (!sw1){
          Serial.println("We open door and ignore all");
          sw2 = false;
          sw1 = true;
          }
          if (myServo.isStopped()) {
              myServo.goTo(opendoor);
             }
      }
      if (digitalRead(switch_disable_pin) == LOW){
          if (!sw2){
          Serial.println("We will processing BLE");
          sw2 = true;
          sw1 = false;
          if (myServo.isStopped()) {
              myServo.goTo(closedoor);
             }
          }
          inputx();
          pBLEScan->start(scand);
          delay(2000);      // Scan every n(sec) for ibeacon
          }
    }
};