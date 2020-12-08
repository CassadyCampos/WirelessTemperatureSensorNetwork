#include <FirebaseArduino.h>
#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <espnow.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <vector>

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");
const char* ssid = "SHAW-C2B250";
const char* password = "25116A081151"; 
String ROOT = "VancouverStats/Boards/";
volatile bool haveBoardReading = false;
volatile int boardToUpdate = 0;
#define FIREBASE_HOST "temperaturesensornetwork.firebaseio.com"
#define FIREBASE_AUTH "0ZvLeOQcR4kctBdbYTR7BIFXpWibMMHYCVnjp05D"

String months[12]={"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};

//The definition of our Data.
struct Data {
    int id;
    float temp;
    float humidity;
    String location;
};

//Holds the Data sent by one of the sender boards.
Data myData;

// Create a structure to hold the readings from each board
Data board1 = {1, 0.0, 0.0, "unknown"};
Data board2 = {2, 0.0, 0.0, "unknown"};


// Create an array with all the boards
Data boards[2] = {board1, board2};

//Reciving Data from our Receiver Boards. Print out the mac address of the sender board and its Data to our Serial Monitor.
void OnDataRecv(uint8_t * mac_addr, uint8_t *incomingData, uint8_t len) {
  char macStr[18];
  
  //Print the MAC Address of the sender board.
  Serial.print("Packet received from: ");
  snprintf(macStr, sizeof(macStr), "%02x:%02x:%02x:%02x:%02x:%02x",
           mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
  Serial.println(macStr);
  memcpy(&myData, incomingData, sizeof(myData));
  Serial.printf("Board ID %u: %u bytes\n", myData.id, len);
  
  // Update the boards with the new incoming Data
  boards[myData.id-1].id = myData.id;
  boards[myData.id-1].temp = myData.temp;
  boards[myData.id-1].humidity = myData.humidity;
  boards[myData.id - 1].location = myData.location;
  
  //Print the new Data
  Serial.println("Board at the " + boards[myData.id - 1].location + " recorded: \n");
  Serial.printf("temperature: %f \n", boards[myData.id-1].temp);
  Serial.printf("humidity: %f \n", boards[myData.id-1].humidity);
  Serial.println();

  haveBoardReading = true;

  //Our array is indexed 0-1, our board ID's are 1-2
  boardToUpdate = myData.id-1;
}

//Connect to WIFI with ssid and password.
void connectToWifi() {
  // Set device as a Wi-Fi Station
  WiFi.mode(WIFI_STA);
  
  Serial.println("Connecting to ");
  Serial.println(ssid);

  //connect to your local wi-fi network
  WiFi.begin(ssid, password);

  //check wi-fi is connected to wi-fi network
  while (WiFi.status() != WL_CONNECTED) {
  delay(500);
  Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected..!");
  Serial.print("Got IP: ");  Serial.println(WiFi.localIP());
}

void connectToFirebase() {
  Firebase.begin(FIREBASE_HOST, FIREBASE_AUTH);
  if (Firebase.failed()){
    Serial.println(Firebase.error());
  }
  delay(10);
}

void connectToTimeClient() {
  timeClient.begin();
  // Vancouver Time, GMT -8 = -28800 
  // Calgary Time, GMT -7 = -25200
  timeClient.setTimeOffset(-28800);
  timeClient.update();
}

String getCurrentDate() {
  unsigned long epochTime = timeClient.getEpochTime();
  struct tm *tempTime = gmtime ((time_t *)&epochTime); 
  int day = tempTime->tm_mday;
  int month = tempTime->tm_mon+1;
  int year = tempTime->tm_year+1900;

  String currentDate = String(year) + "-" + String(month) + "-" + String(day);
  return currentDate;
}

//Writes our board Data to the Firebase Database. If the board does not exist in the 
//Database at this moment in time, it will create a section for it.
void writeBoardData(Data boardDataReading) {
  String currentDate = getCurrentDate();
  String formattedTime = timeClient.getFormattedTime();
   

  StaticJsonBuffer<200> jsonBuffer;
  JsonObject& boardReadingJsonTree = jsonBuffer.createObject();
  boardReadingJsonTree["Id"] = boardDataReading.id;
  boardReadingJsonTree["Location"] = boardDataReading.location;
  boardReadingJsonTree["Temperature"] = boardDataReading.temp;
  boardReadingJsonTree["Humidity"] = boardDataReading.humidity;
  boardReadingJsonTree["TimeRecorded"] = formattedTime;

  boardReadingJsonTree["TimeZone"] = "PST"; // Calgary: MST, Vancouver: PST

  // POST to firebase
  Firebase.set(ROOT + String(boardDataReading.id) + "/" + "/readings/" + currentDate + "/" + timeClient.getHours(), boardReadingJsonTree);

  if (Firebase.failed()){
     Serial.println(Firebase.error());
  }
  else {
     Serial.printf("Uploaded Data for Board: %u \n", boardDataReading.id);
  }

  writeAverages(boardDataReading);
}

void writeAverages(Data boardDataReading) {
  String currentDate = getCurrentDate();

  // Fetch readings from current day
  FirebaseObject dayReadings  = Firebase.get(ROOT + String(boardDataReading.id) + "/readings/" + currentDate);

  // Need to be switched to JsonVariant to be processed
  JsonVariant dayReadingVariant = dayReadings.getJsonVariant();

  float dailyTemperatureAverage = 0.0;
  float dailyHumidityAverage = 0.0;
  int recordingsFound = 0;

  // For each hour of the day, grab the temperature and humidity reading
  for(int i = 0; i < 24; i++) {
    FirebaseObject temperatureRecord = Firebase.get(ROOT + String(boardDataReading.id) + "/readings/" + currentDate + "/" + i);
    JsonVariant testPrinting = temperatureRecord.getJsonVariant();

    int id = temperatureRecord.getInt("Id");
    // If we missed a reading, we will fetch a default JSON object with default values
    // Make sure we have a reading by checking against boardId
    if (id != boardDataReading.id) {
      continue;
    }

    float temperature = temperatureRecord.getFloat("Temperature");
    float humidity = temperatureRecord.getFloat("Humidity");

    dailyTemperatureAverage += temperature;
    dailyHumidityAverage += humidity;
    recordingsFound++;
  }

  dailyTemperatureAverage = dailyTemperatureAverage / recordingsFound;
  dailyHumidityAverage = dailyHumidityAverage / recordingsFound;

  // Create JSON structure for daily average
  StaticJsonBuffer<200> jsonBuffer;
  JsonObject& dailyAverageJsonTree = jsonBuffer.createObject();
  dailyAverageJsonTree["Board Location"] = boardDataReading.location;
  dailyAverageJsonTree["Temperature Average"] = dailyTemperatureAverage;
  dailyAverageJsonTree["Humidity Average"] = dailyHumidityAverage;

  // Post to firebase
  Firebase.set(ROOT + String(boardDataReading.id) + "/" + "/Statistics/" + currentDate, dailyAverageJsonTree);

  if (Firebase.success()) {

    Serial.println("-----------------Success-----------------\n");
    Serial.println("Printed daily average to firebase");
    Serial.println("-----------------Success-----------------\n");
  }
  else {
    Serial.println("Firebase get failed");
        Serial.println(Firebase.error());
        return;
  }
}

void setup() {
  // Initialize Serial Monitor
  Serial.begin(115200);

   WiFi.mode(WIFI_STA);
   WiFi.disconnect();
  
  // Init ESP-NOW
  if (esp_now_init() != 0) {
    Serial.println("Error initializing ESP-NOW");
    ESP.restart();
  }
  
  esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
  esp_now_register_recv_cb(OnDataRecv);
}

int counter;
void loop(){
  if (millis()- counter > 20000) {
    Serial.println("Waiting for ESP-NOW messages...");
    counter = millis();
  }
  
  if(haveBoardReading) {
     haveBoardReading = false;
     connectToWifi();
     connectToTimeClient();
     connectToFirebase();
     writeBoardData(boards[boardToUpdate]);
     ESP.restart();
  }
} 
      
