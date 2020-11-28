#include "DHT.h"
#include <ESP8266WiFi.h>
#include <espnow.h>

#define DHTTYPE DHT11 

//REPLACE WITH RECEIVER MAC Address
// Cassady MAC: 40:F5:20:26:B5:DE
uint8_t broadcastAddress[] = {0x40, 0xF5, 0x20, 0x26, 0xB5, 0xDE};

//Our output from the DHT11 sensor
// Cassady: D3
uint8_t DHTPin = D3; 
               
// Initialize DHT sensor.
DHT dht(DHTPin, DHTTYPE); 

// Set your Board ID
#define BOARD_ID 1


struct Data {
    int id;
    float temp;
    float humidity;
    String location;
};

//The data we will be sending to our Receiver
Data myData;

unsigned long lastTime = 0;
unsigned long timerDelay = 6000;

// Callback when data is sent
void OnDataSent(uint8_t *mac_addr, uint8_t sendStatus) {
  Serial.print("\r\nLast Packet Send Status: ");
  if (sendStatus == 0){
    Serial.println("Delivery success");
  }
  else{
    Serial.println("Delivery fail");
  }
}
 
void setup() {
  //Init Serial Monitor
  Serial.begin(115200);

  pinMode(DHTPin, INPUT);
  dht.begin();
 
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  //Initialize ESP NOW
  if (esp_now_init() != 0) {
    Serial.println("Error initializing ESP-NOW");
    return;
  } 

  //Set ESP-NOW role. The sender is a Controller.
  esp_now_set_self_role(ESP_NOW_ROLE_CONTROLLER);

  //Call back function when we send our myData object to the Receiver
  esp_now_register_send_cb(OnDataSent);
  
  //Register our "peer" which is the Receiver that we are sending to. The first parameter is the MAC Address of the Receiver
  esp_now_add_peer(broadcastAddress, ESP_NOW_ROLE_SLAVE, 1, NULL, 0);

}
 
void loop() {
  if ((millis() - lastTime) > timerDelay) {
    //Set values to send
    myData.id = BOARD_ID;
    myData.temp = dht.readTemperature();
    myData.humidity = dht.readHumidity();
    myData.location = "By Window";

    //Send our myData object
    esp_now_send(0, (uint8_t *) &myData, sizeof(myData));
    Serial.println("Sending data reading from " + myData.location + " recorded T: " + myData.temp + " H: " + myData.humidity);
    lastTime = millis();
  }
}  
