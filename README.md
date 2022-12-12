# FoxHomeIoT-ESP

## TO-DO

 - [x] ESP-NOW - receiver (master) - příjemce
 - [x] ESP-NOW autopair - autopárování
 - [x] MQTT -> Hassio (ESP-NOW receive message -> MQTT resend -> Hassio)
 - [x] přidat stav baterie do struktury ESP-NOW
 - [x] opravit plnění hodnot ze senzorů 
 - [x] dořešit mqtt publish
 - [x] Oversampling - opakované čtení dat ze senzorů (x-krát a udělat průměr) - knihovna bme má default 16x
 - [x] sjednotit projekt zde na gitu
 - [x] otestovat přenos z ESP-NOW na mqtt
 - [x] deep sleep - vyřešit odeslání a sleep
 - [x] deep sleep ESP32
 - [ ] otestovat wemos d1 mini - přidán deep sleep
 - [ ] otestovat ESP32 - přidán deep sleep
 - [ ] command and state - příjem příkazu a odeslání stavu
 - [ ] zpracování v HASSIO
 - [ ] upravit README.md
 - [ ] dokumentace
 - [ ] vyřešit komunikaci zpět (Salimu upozorňovač) -> odesílat na všechny desky a vybírat dle BoardID (nový parametr command ve zpráve s hodnoty senzorů? nebo nový typ zprávy COMMAND?)
