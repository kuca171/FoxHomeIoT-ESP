# FoxHomeIoT-ESP

## Myšlenka

 - [x] ESP-NOW - receiver (master) - příjemce
 - [x] ESP-NOW autopair - autopárování
 - [x] MQTT -> Hassio (ESP-NOW receive message -> MQTT resend -> Hassio)
 - [x] přidat stav baterie do struktury ESP-NOW
 - [x] opravit plnění hodnot ze senzorů 
 - [x] dořešit mqtt publish
 - [x] Oversampling - opakované čtení dat ze senzorů (x-krát a udělat průměr) - knihovna bme má default 16x
 - [x] sjednotit projekt zde na gitu
 - [x] otestovat přenos z ESP-NOW na mqtt
 - [ ] zpracování v HASSIO
 - [ ] upravit README.md
 - [ ] dokumentace
 - [ ] vyřešit komunikaci zpět (Salimu upozorňovač)
 
## Supported ESP device

 - ESP32
 - ESP8266

## Supported sensors

 - BME280
 - you can modify the source code and add your own sensors
  
## Resources
 - https://randomnerdtutorials.com/esp-now-auto-pairing-esp32-esp8266/
 - https://github.com/Servayejc
 - https://www.espressif.com/
