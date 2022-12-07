// ----------------------------------------------------------------------------
// firmware name: FoxHomeIoT-ESP32-Bridge
// description: receiving ESP-NOW message from slaves and resend 
//              message to mqtt
// © 2022 Jiří Kučera
// version: 0.1.0beta
// ----------------------------------------------------------------------------

// ----------------------------------------------------------------------------
// Libraries
// ----------------------------------------------------------------------------
#include <esp_now.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>

// ----------------------------------------------------------------------------
// Definition of global constants
// ----------------------------------------------------------------------------

// wifi setting
const char* ssid = "kuca";
const char* password = "lisak397";

// mqtt setting
const char* mqtt_server = "10.0.0.112";
const int   mqtt_port   = 1883;
const char* MQTT_USER = "mymqtt";
const char* MQTT_PASSWORD = "foxhome1";

const char* DEVICE_NAME = "ESP-NOW-Bridge"; 
const char* TOPIC_MQTT_AVAILABILITY   = "bridge/availability";

// ----------------------------------------------------------------------------
// Definition of global variables
// ----------------------------------------------------------------------------

esp_now_peer_info_t slave;
int chan; 

enum MessageType {PAIRING, DATA,};
MessageType messageType;

int counter = 0;

// Structure example to receive data
// Must match the sender structure
typedef struct struct_message {
  uint8_t msgType;
  uint8_t id;
  float temp;
  float hum;
  float pres;
  unsigned int readingId;
} struct_message;

// structure for pairing
typedef struct struct_pairing {       
    uint8_t msgType;
    uint8_t id;
    uint8_t macAddr[6];
    uint8_t channel;
} struct_pairing;

struct_message incomingReadings;
struct_message outgoingSetpoints;
struct_pairing pairingData;

// MQTT wifi
WiFiClient espClient;
PubSubClient client(espClient);

// ----------------------------------------------------------------------------
// ESP-NOW
// ----------------------------------------------------------------------------

void printMAC(const uint8_t * mac_addr){
  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02x:%02x:%02x:%02x:%02x:%02x",
           mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
  Serial.print(macStr);
}

// add pairing
bool addPeer(const uint8_t *peer_addr) {      
  memset(&slave, 0, sizeof(slave));
  const esp_now_peer_info_t *peer = &slave;
  memcpy(slave.peer_addr, peer_addr, 6);
  
  slave.channel = chan; // pick a channel
  slave.encrypt = 0; // no encryption
  // check if the peer exists
  bool exists = esp_now_is_peer_exist(slave.peer_addr);
  if (exists) {
    // Slave already paired.
    Serial.println("Already Paired");
    return true;
  }
  else {
    esp_err_t addStatus = esp_now_add_peer(peer);
    if (addStatus == ESP_OK) {
      // Pair success
      Serial.println("Pair success");
      return true;
    }
    else 
    {
      Serial.println("Pair failed");
      return false;
    }
  }
} 

// callback when data is sent
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.print("Last Packet Send Status: ");
  Serial.print(status == ESP_NOW_SEND_SUCCESS ? "Delivery Success to " : "Delivery Fail to ");
  printMAC(mac_addr);
  Serial.println();
}

// callback when data receive
void OnDataRecv(const uint8_t * mac_addr, const uint8_t *incomingData, int len) { 
  Serial.print(len);
  Serial.print(" bytes of data received from : ");
  printMAC(mac_addr);
  Serial.println();
  StaticJsonDocument<1000> root;
  String payload;
  // first message byte is the type of message 
  uint8_t type = incomingData[0];       
  switch (type) {
    // the message is data type
    case DATA :                           
      memcpy(&incomingReadings, incomingData, sizeof(incomingReadings));
      // create a JSON document with received data and send it by event to the web page
      root["id"]          = incomingReadings.id;
      root["temperature"] = incomingReadings.temp;
      root["humidity"]    = incomingReadings.hum;
      root["pressure"]    = incomingReadings.pres;
      root["readingId"]   = String(incomingReadings.readingId);
      serializeJson(root, payload);
      Serial.print("mqtt send :");
      serializeJson(root, Serial);
      //events.send(payload.c_str(), "new_readings", millis());
      Serial.println();
      break;

    // the message is a pairing request 
    case PAIRING:                            
      memcpy(&pairingData, incomingData, sizeof(pairingData));
      Serial.println(pairingData.msgType);
      Serial.println(pairingData.id);
      Serial.print("Pairing request from: ");
      printMAC(mac_addr);
      Serial.println();
      Serial.println(pairingData.channel);
      if (pairingData.id > 0) {     // do not replay to server itself
        if (pairingData.msgType == PAIRING) { 
          pairingData.id = 0;       // 0 is server
          // Server is in AP_STA mode: peers need to send data to server soft AP MAC address 
          WiFi.softAPmacAddress(pairingData.macAddr);   
          pairingData.channel = chan;
          Serial.println("send response");
          esp_err_t result = esp_now_send(mac_addr, (uint8_t *) &pairingData, sizeof(pairingData));
          addPeer(mac_addr);
      }  
    }  
    break; 
  }
}

void initESP_NOW(){
    // Init ESP-NOW
    Serial.println("Init ESP-NOW");
    if (esp_now_init() != ESP_OK) {
      Serial.println("Error initializing ESP-NOW");
      return;
    }
    esp_now_register_send_cb(OnDataSent);
    esp_now_register_recv_cb(OnDataRecv);
} 

// ----------------------------------------------------------------------------
// mqtt
// ----------------------------------------------------------------------------

void connectMqtt() {
  while (!client.connected()) {
    Serial.println("MQTT connecting...");

    if (client.connect(DEVICE_NAME, MQTT_USER, MQTT_PASSWORD, 
                       TOPIC_MQTT_AVAILABILITY, 1, true, "offline")) {
      Serial.println("MQTT connected");
      client.publish(TOPIC_MQTT_AVAILABILITY, "online", true);

      /*if (client.subscribe(TOPIC_SUB_FARM_DEVICE)) {
        Serial.println("Subscribed to " + String(TOPIC_SUB_FARM_DEVICE) + " successfully");
      } else {
        Serial.println("MQTT subscription error");
        mqttError(client.state());
      }*/
    }
  }
}

// ----------------------------------------------------------------------------
// wifi
// ----------------------------------------------------------------------------

void connectWifi() {
  // Set device as a Wi-Fi Station
  Serial.println("wifi connecting...");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Setting as a Wi-Fi Station..");
  }
  Serial.println("Connected to WiFi");

  // Print status
  printWifiStatus();
}

void printWifiStatus() {
  Serial.print("Server SOFT AP MAC Address:  ");
  Serial.println(WiFi.softAPmacAddress());

  // Print the SSID of the network you're attached to
  Serial.print("SSID: ");
  Serial.println(WiFi.SSID());

  // Print your WiFi IP address and channel
  chan = WiFi.channel();
  Serial.print("Station IP Address: ");
  Serial.println(WiFi.localIP());
  Serial.print("Wi-Fi Channel: ");
  Serial.println(WiFi.channel());

  // Print the received signal strength
  long rssi = WiFi.RSSI();
  Serial.print("Signal strength (RSSI):");
  Serial.print(rssi);
  Serial.println(" dBm");
}

// ----------------------------------------------------------------------------
// setup
// ----------------------------------------------------------------------------

void setup() {
  // Initialize Serial Monitor
  Serial.begin(115200);

  Serial.println();
  Serial.print("Server MAC Address:  ");
  Serial.println(WiFi.macAddress());

  // Set the device as a Station and Soft Access Point simultaneously
  WiFi.mode(WIFI_AP_STA);

  connectWifi();  
  client.setServer(mqtt_server, mqtt_port);

  initESP_NOW();
}

// ----------------------------------------------------------------------------
// loop
// ----------------------------------------------------------------------------

void loop() {
  // put your main code here, to run repeatedly:
  if (WiFi.status() != WL_CONNECTED) {
    connectWifi();
  }
  if (!client.connected()) {
    connectMqtt();
  }
  client.loop();
}
