// ----------------------------------------------------------------------------
// firmware name: FoxHomeIoT-ESP8266-Sender
// description: send ESP-NOW message to master and from master resend 
//              message to mqtt
// © 2022 Jiří Kučera
// version: 0.9.0beta (not tested)
// ----------------------------------------------------------------------------

// ----------------------------------------------------------------------------
// Libraries
// ----------------------------------------------------------------------------

#include <ESP8266WiFi.h>
#include <espnow.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>

// ----------------------------------------------------------------------------
// Definition of global constants
// ----------------------------------------------------------------------------

#define BOARD_ID 2
#define MAX_CHANNEL 13  // 11 for North America // 13 in Europe
#define SLEEP_MODE // deep sleep

// ----------------------------------------------------------------------------
// Definition of global variables
// ----------------------------------------------------------------------------

uint8_t serverAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

//Structure example to send data
//Must match the receiver structure
typedef struct struct_message {
  uint8_t msgType;
  uint8_t id;
  float temp;
  float hum;
  float pres;
  int batt;
  unsigned int readingId;
} struct_message;

// new structure for pairing
typedef struct struct_pairing {     
    uint8_t msgType;
    uint8_t id;
    uint8_t macAddr[6];
    uint8_t channel;
} struct_pairing;

// structure for COMMAND and state
typedef struct struct_command {       
    uint8_t msgType;
    uint8_t id;
    int     command;
    int     state;
    //uint8_t device_id; mozna v budoucnu
} struct_command;

//Create 2 struct_message 
struct_message myData;  // data to send
struct_message inData;  // data received
struct_pairing pairingData; // pairing
struct_command inCommandData; // command received
struct_command myCommandStateData; // state device data to send

enum PairingStatus {PAIR_REQUEST, PAIR_REQUESTED, PAIR_PAIRED, };
PairingStatus pairingStatus = PAIR_REQUEST;

enum MessageType {PAIRING, DATA, COMMAND,};
MessageType messageType;

uint8_t channel = 1;

unsigned long currentMillis   = millis(); 
unsigned long previousMillis  = 0;    // will store last time DHT was updated 
const long interval = 10000; 
unsigned long start;
int readingId = 0;

Adafruit_BME280 bme;

// ----------------------------------------------------------------------------
// ESP-NOW
// ----------------------------------------------------------------------------

// Callback when data is sent
void OnDataSent(uint8_t *mac_addr, uint8_t sendStatus) {
  Serial.print("Last Packet Send Status: ");
  if (sendStatus == 0){
    Serial.println("Delivery success");
  }
  else{
    Serial.println("Delivery fail");
  }
}

void printMAC(const uint8_t * mac_addr){
  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02x:%02x:%02x:%02x:%02x:%02x",
           mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
  Serial.print(macStr);
}

// Callback when data is received
void OnDataRecv(uint8_t * mac, uint8_t *incomingData, uint8_t len) {
  Serial.print("Packet received from: ");
  printMAC(mac);
  Serial.println();
  Serial.print("data size = ");
  Serial.println(sizeof(incomingData));
  uint8_t type = incomingData[0];
  switch (type) {
  case DATA :  
    memcpy(&inData, incomingData, sizeof(inData));
    Serial.print("ID  = ");
    Serial.println(inData.id);
    Serial.print("Setpoint temp = ");
    Serial.println(inData.temp);
    Serial.print("SetPoint humidity = ");
    Serial.println(inData.hum);
    Serial.print("reading Id  = ");
    Serial.println(inData.readingId);
    
    if (inData.readingId % 2 == 1){
      digitalWrite(LED_BUILTIN, LOW);
    } else { 
      digitalWrite(LED_BUILTIN, HIGH);
    }
    break;

  case PAIRING:
    memcpy(&pairingData, incomingData, sizeof(pairingData));
    if (pairingData.id == 0) {                // the message comes from server
      Serial.print("Pairing done for ");
      printMAC(pairingData.macAddr);
      Serial.print(" on channel " );
      Serial.print(pairingData.channel);    // channel used by the server
      Serial.print(" in ");
      Serial.print(millis()-start);
      Serial.println("ms");
      //esp_now_del_peer(pairingData.macAddr);
      //esp_now_del_peer(mac);
      esp_now_add_peer(pairingData.macAddr, ESP_NOW_ROLE_COMBO, pairingData.channel, NULL, 0); // add the server to the peer list 
      pairingStatus = PAIR_PAIRED ;            // set the pairing status
    }
    break;

  case COMMAND:     // we received command data from server
    //memcpy(&inCommandData, incomingData, sizeof(inCommandData));
    break;
  }  
}

PairingStatus autoPairing(){
  switch(pairingStatus) {
  case PAIR_REQUEST:
    Serial.print("Pairing request on channel "  );
    Serial.println(channel);
  
    // clean esp now
    esp_now_deinit();
    WiFi.mode(WIFI_STA);
    // set WiFi channel   
    wifi_promiscuous_enable(1);
    wifi_set_channel(channel);
    wifi_promiscuous_enable(0);
    //WiFi.printDiag(Serial);
    WiFi.disconnect();

    // Init ESP-NOW
    if (esp_now_init() != 0) {
      Serial.println("Error initializing ESP-NOW");
    }
    esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
    // set callback routines
    esp_now_register_send_cb(OnDataSent);
    esp_now_register_recv_cb(OnDataRecv);
    
    // set pairing data to send to the server
    pairingData.id = BOARD_ID;     
    pairingData.channel = channel;
    previousMillis = millis();
    // add peer and send request
    Serial.println(esp_now_send(serverAddress, (uint8_t *) &pairingData, sizeof(pairingData)));
    pairingStatus = PAIR_REQUESTED;
    break;

  case PAIR_REQUESTED:
    // time out to allow receiving response from server
    currentMillis = millis();
    if(currentMillis - previousMillis > 100) {
      previousMillis = currentMillis;
      // time out expired,  try next channel
      channel ++;
      if (channel > MAX_CHANNEL) { 
        channel = 0;
      }
      pairingStatus = PAIR_REQUEST; 
    }
    break;

  case PAIR_PAIRED:
    //Serial.println("Paired!");
    break;
  }
  return pairingStatus;
} 

// ----------------------------------------------------------------------------
// setup
// ----------------------------------------------------------------------------

void setup() {
  // Init Serial Monitor
  Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);

  //initialize BME
  Serial.println(F("BME280 test"));
 
  bool status;
 
  status = bme.begin(0x76);  
  if (!status) {
    Serial.println("Could not find a valid BME280 sensor, check wiring!");
    while (1);
  }

  // Set device as a Wi-Fi Station
  WiFi.mode(WIFI_STA);
  Serial.println(WiFi.macAddress());
  WiFi.disconnect();
  start = millis();

  // Init ESP-NOW
  if (esp_now_init() != 0) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  // Set ESP-NOW Role
  esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
    
  // Register for a callback function that will be called when data is received
  esp_now_register_recv_cb(OnDataRecv);
  esp_now_register_send_cb(OnDataSent);

  pairingData.id = BOARD_ID;
}
 
// ----------------------------------------------------------------------------
// loop
// ----------------------------------------------------------------------------

void loop() { 
  if (autoPairing() == PAIR_PAIRED) { 
    #ifdef SLEEP_MODE 
      ReadAndSendDataSensors();
      Serial.println("I'm awake, but I'm going into deep sleep mode for 30 seconds");
      Serial.flush();
      ESP.deepSleep(30e6); //30s //ESP.deepSleep(ESP.deepSleepMax());
    #else
      unsigned long currentMillis = millis();
      if (currentMillis - previousMillis >= interval) {
        previousMillis = currentMillis;
        ReadAndSendDataSensors();
      }  
    #endif
  }
}

// ----------------------------------------------------------------------------
// ReadAndSendDataSensors
// ----------------------------------------------------------------------------

void ReadAndSendDataSensors () {
  //Set values to send
  myData.msgType = DATA;
  myData.id = BOARD_ID;
  myData.temp       = bme.readTemperature();;
  myData.hum        = bme.readHumidity();
  myData.pres       = bme.readPressure() / 100.0F;
  myData.batt       = 0;
  myData.readingId = readingId ++;

  // Send message via ESP-NOW to all peers 
  esp_now_send(pairingData.macAddr, (uint8_t *) &myData, sizeof(myData));
}