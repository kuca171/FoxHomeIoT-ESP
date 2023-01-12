// ----------------------------------------------------------------------------
// firmware name: FoxHomeIoT-ESP32-Sender
// description: send ESP-NOW message to master and from master resend 
//              message to mqtt
// © 2022 Jiří Kučera
// version: 0.9.0beta (not tested)
// ----------------------------------------------------------------------------

// ----------------------------------------------------------------------------
// Libraries
// ----------------------------------------------------------------------------

#include <esp_now.h>
#include <esp_wifi.h>
#include <WiFi.h>
#include <EEPROM.h>

// ----------------------------------------------------------------------------
// Definition of global constants
// ----------------------------------------------------------------------------

#define BOARD_ID 1
#define MAX_CHANNEL 13  // 11 for North America // 13 in Europe

// ----------------------------------------------------------------------------
// Definition of global variables
// ----------------------------------------------------------------------------

uint8_t serverAddress[] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

//Structure to send data
//Must match the receiver structure

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
struct_pairing pairingData; // pairing
struct_command inCommandData; // command received
struct_command myCommandStateData; // state device data to send

enum PairingStatus {NOT_PAIRED, PAIR_REQUEST, PAIR_REQUESTED, PAIR_PAIRED,};
PairingStatus pairingStatus = NOT_PAIRED;

enum MessageType {PAIRING, DATA, COMMAND,};
MessageType messageType;

#ifdef SAVE_CHANNEL
  int lastChannel;
#endif  
int channel = 1;

unsigned long currentMillis = millis();
unsigned long previousMillis = 0;   
unsigned long start;                // used to measure Pairing time

int led_pin     = 2;   
int button_pin  = 4; //for button control

// Variables will change:
int ledState = LOW;          // the current state of the output pin
int buttonState;             // the current reading from the input pin
int lastButtonState = LOW;   // the previous reading from the input pin

unsigned long lastDebounceTime = 0;  // the last time the output pin was toggled
unsigned long debounceDelay = 50;    // the debounce time; increase if the output flickers

// ----------------------------------------------------------------------------
// ESP-NOW
// ----------------------------------------------------------------------------

void addPeer(const uint8_t * mac_addr, uint8_t chan){
  esp_now_peer_info_t peer;
  ESP_ERROR_CHECK(esp_wifi_set_channel(chan ,WIFI_SECOND_CHAN_NONE));
  esp_now_del_peer(mac_addr);
  memset(&peer, 0, sizeof(esp_now_peer_info_t));
  peer.channel = chan;
  peer.encrypt = false;
  memcpy(peer.peer_addr, mac_addr, sizeof(uint8_t[6]));
  if (esp_now_add_peer(&peer) != ESP_OK){
    Serial.println("Failed to add peer");
    return;
  }
  memcpy(serverAddress, mac_addr, sizeof(uint8_t[6]));
}

void printMAC(const uint8_t * mac_addr){
  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02x:%02x:%02x:%02x:%02x:%02x",
           mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
  Serial.print(macStr);
}

void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.print("\r\nLast Packet Send Status:\t");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Delivery Success" : "Delivery Fail");
}

void OnDataRecv(const uint8_t * mac_addr, const uint8_t *incomingData, int len) { 
  Serial.print("Packet received from: ");
  printMAC(mac_addr);
  Serial.println();
  Serial.print("data size = ");
  Serial.println(sizeof(incomingData));
  uint8_t type = incomingData[0];
  switch (type) {
  case DATA :      // we received data from server
    break;

  case PAIRING:    // we received pairing data from server
    memcpy(&pairingData, incomingData, sizeof(pairingData));
    if (pairingData.id == 0) {              // the message comes from server
      printMAC(mac_addr);
      Serial.print("Pairing done for ");
      printMAC(pairingData.macAddr);
      Serial.print(" on channel " );
      Serial.print(pairingData.channel);    // channel used by the server
      Serial.print(" in ");
      Serial.print(millis()-start);
      Serial.println("ms");
      addPeer(pairingData.macAddr, pairingData.channel); // add the server  to the peer list 
      #ifdef SAVE_CHANNEL
        lastChannel = pairingData.channel;
        EEPROM.write(0, pairingData.channel);
        EEPROM.commit();
      #endif  
      pairingStatus = PAIR_PAIRED;             // set the pairing status
    }
    break;
  
  case COMMAND:     // we received command data from server
    memcpy(&inCommandData, incomingData, sizeof(inCommandData));
    if (inCommandData.id == BOARD_ID) {
      Serial.println("Command data receive" + inCommandData.command);
      SetCommandSendState(inCommandData.command);
    }
    break;
  }  
}

PairingStatus autoPairing(){
  switch(pairingStatus) {
    case PAIR_REQUEST:
    Serial.print("Pairing request on channel "  );
    Serial.println(channel);

    // set WiFi channel   
    ESP_ERROR_CHECK(esp_wifi_set_channel(channel,  WIFI_SECOND_CHAN_NONE));
    if (esp_now_init() != ESP_OK) {
      Serial.println("Error initializing ESP-NOW");
    }

    // set callback routines
    esp_now_register_send_cb(OnDataSent);
    esp_now_register_recv_cb(OnDataRecv);
  
    // set pairing data to send to the server
    pairingData.msgType = PAIRING;
    pairingData.id = BOARD_ID;     
    pairingData.channel = channel;

    // add peer and send request
    addPeer(serverAddress, channel);
    esp_now_send(serverAddress, (uint8_t *) &pairingData, sizeof(pairingData));
    previousMillis = millis();
    pairingStatus = PAIR_REQUESTED;
    break;

    case PAIR_REQUESTED:
    // time out to allow receiving response from server
    currentMillis = millis();
    if(currentMillis - previousMillis > 250) {
      previousMillis = currentMillis;
      // time out expired,  try next channel
      channel ++;
      if (channel > MAX_CHANNEL){
         channel = 1;
      }   
      pairingStatus = PAIR_REQUEST;
    }
    break;

    case PAIR_PAIRED:
      // nothing to do here 
    break;
  }
  return pairingStatus;
}  

// ----------------------------------------------------------------------------
// setup
// ----------------------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  Serial.println();

  Serial.print("Client Board MAC Address:  ");
  Serial.println(WiFi.macAddress());
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  start = millis();

  #ifdef SAVE_CHANNEL 
    EEPROM.begin(10);
    lastChannel = EEPROM.read(0);
    Serial.println(lastChannel);
    if (lastChannel >= 1 && lastChannel <= MAX_CHANNEL) {
      channel = lastChannel; 
    }
    Serial.println(channel);
  #endif  

  pairingStatus = PAIR_REQUEST;

  //device pinmode
  Serial.println("Setup device");
  pinMode(led_pin, OUTPUT);
  digitalWrite(led_pin, LOW);
  pinMode(button_pin, INPUT);
}

// ----------------------------------------------------------------------------
// loop
// ----------------------------------------------------------------------------

void loop() {
  if (autoPairing() == PAIR_PAIRED) {
    int reading = digitalRead(button_pin);

    if (reading != lastButtonState) {
      lastDebounceTime = millis(); // reset the debouncing timer
    }

    if ((millis() - lastDebounceTime) > debounceDelay) {
      // whatever the reading is at, it's been there for longer than the debounce
      // delay, so take it as the actual current state:

      // if the button state has changed:
      if (reading != buttonState) {
        buttonState = reading;

        // only toggle the LED if the new button state is HIGH
        if (buttonState == HIGH) {
          ledState = !ledState;

          // send esp-now
          myCommandStateData.id      = BOARD_ID;
          myCommandStateData.msgType = COMMAND; 
          myCommandStateData.state   = ledState; 
          esp_err_t result  = esp_now_send(serverAddress, (uint8_t *) &myCommandStateData, sizeof(myCommandStateData));
        }
      }
    }

    // set the LED:
    digitalWrite(led_pin, ledState);

    // save the reading. Next time through the loop, it'll be the lastButtonState:
    lastButtonState = reading;
  }
}

// ----------------------------------------------------------------------------
// SetCommandSendState
// ----------------------------------------------------------------------------

void SetCommandSendState (int cmd) {

  if (cmd == 1) {
    Serial.println("device on");
    ledState = HIGH;
    digitalWrite(led_pin, ledState);
  } else {
    Serial.println("device off");
    ledState = LOW;
    digitalWrite(led_pin, ledState);  
  }

  myCommandStateData.id       = BOARD_ID;
  myCommandStateData.msgType  = COMMAND;
  myCommandStateData.state    = ledState;
    
  esp_err_t result  = esp_now_send(serverAddress, (uint8_t *) &myCommandStateData, sizeof(myCommandStateData));
}
