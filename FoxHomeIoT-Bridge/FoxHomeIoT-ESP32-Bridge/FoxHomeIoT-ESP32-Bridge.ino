// ----------------------------------------------------------------------------
// firmware name: FoxHomeIoT-ESP32-Bridge
// description: receiving ESP-NOW message from slaves and resend 
//              message to mqtt
// © 2022 Jiří Kučera
// version: 1.0.0
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
const char* ssid = "****";
const char* password = "****";

// mqtt setting
const char* mqtt_server = "10.0.0.112";
const int   mqtt_port   = 1883;
const char* MQTT_USER = "mymqtt";
const char* MQTT_PASSWORD = "****";

const char* DEVICE_NAME = "FoxHomeIoT-Bridge"; 
const char* TOPIC_MQTT_AVAILABILITY   = "bridge/availability";
const char* TOPIC_MQTT_RESEND_SENSORS = "bridge/slave/sensors";
const char* TOPIC_MQTT_COMMAND_SUB    = "bridge/slave/command";
const char* TOPIC_MQTT_RESEND_STATE   = "bridge/slave/state";

// ----------------------------------------------------------------------------
// Definition of global variables
// ----------------------------------------------------------------------------

esp_now_peer_info_t slave;
int chan; 

enum MessageType {PAIRING, DATA, COMMAND,};
MessageType messageType;

// Structure example to receive data
// Must match the sender structure
typedef struct struct_message {
  uint8_t msgType;
  uint8_t id;
  float temp;
  float hum;
  float pres;
  int batt;
  unsigned int readingId;
} struct_message;

// structure for pairing
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
} struct_command;

struct_message incomingReadings;  //  data sensor received
struct_message outgoingReadings; //  data sensor to send
struct_pairing pairingData;       // pairing
struct_command commandData;       // command to send (MQTT receive -> ESP-NOW send)
struct_command stateData;         // state receive (ESP-NOW receive -> MQTT send)

char smacStr[18];

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

void senderMacToStr(const uint8_t * mac_addr) {
  snprintf(smacStr, sizeof(smacStr), "%02x:%02x:%02x:%02x:%02x:%02x",
           mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
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
      senderMacToStr(mac_addr);                     
      memcpy(&incomingReadings, incomingData, sizeof(incomingReadings));
      // create a JSON document with received data and send it by event to the web page
      root["id"]          = incomingReadings.id;
      root["mac_addr"]    = smacStr;
      root["temperature"] = incomingReadings.temp;
      root["humidity"]    = incomingReadings.hum;
      root["pressure"]    = incomingReadings.pres;
      root["battery"]     = incomingReadings.batt;
      root["readingId"]   = String(incomingReadings.readingId);
      serializeJson(root, payload);
      Serial.print("mqtt send :");

      if (client.publish(TOPIC_MQTT_RESEND_SENSORS, payload.c_str())) {
        Serial.println("Published to " + String(TOPIC_MQTT_RESEND_SENSORS) + ":");
        // Write JSON document to serial port
        serializeJson(root, Serial);
        Serial.println("");
      } else {
        Serial.println("MQTT publish error");
      }

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
    
    // the message is a COMMAND or state
    case COMMAND:
      memcpy(&stateData, incomingData, sizeof(stateData)); 
      root["id"]      = stateData.id;
      root["state"]   = stateData.state;
      serializeJson(root, payload);
      Serial.print("mqtt send :");
       
      if (client.publish(TOPIC_MQTT_RESEND_STATE, payload.c_str())) {
        Serial.println("Published to " + String(TOPIC_MQTT_RESEND_STATE) + ":");
        // Write JSON document to serial port
        serializeJson(root, Serial);
        Serial.println("");
      } else {
        Serial.println("MQTT publish error");
      }

      Serial.println();
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
void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();

  // arrive message {id=0;command=1}
  StaticJsonDocument<256> doc;
  deserializeJson(doc, payload, length);
  commandData.id      = doc["id"];
  commandData.msgType = COMMAND;
  commandData.command = doc["command"];
  
  esp_now_send(NULL, (uint8_t *) &commandData, sizeof(commandData));
}  

void connectMqtt() {
  while (!client.connected()) {
    Serial.println("MQTT connecting...");

    if (client.connect(DEVICE_NAME, MQTT_USER, MQTT_PASSWORD, 
                       TOPIC_MQTT_AVAILABILITY, 1, true, "offline")) {
      Serial.println("MQTT connected");
      client.publish(TOPIC_MQTT_AVAILABILITY, "online", true);

      //subscribe command
      if (client.subscribe(TOPIC_MQTT_COMMAND_SUB)) {
        Serial.println("Subscribed to " + String(TOPIC_MQTT_COMMAND_SUB) + " successfully");
      } else {
        Serial.println("MQTT subscription error");
        //mqttError(client.state());
      }
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
  client.setCallback(callback);

  initESP_NOW();
}

// ----------------------------------------------------------------------------
// loop
// ----------------------------------------------------------------------------

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    connectWifi();
  }
  
  if (!client.connected()) {
    connectMqtt();
  }
  client.loop();

  
}
