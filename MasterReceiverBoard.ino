#include <FirebaseArduino.h>
#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <espnow.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <vector>

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");

const char *ssid = "SHAW-1D80F0";
const char *password = "251173151996";

const String ROOT = "CalgaryStats/Boards/";
volatile bool haveBoardReading = false;
volatile int boardToUpdate = 0;

#define FIREBASE_HOST "temperaturesensornetwork.firebaseio.com"
#define FIREBASE_AUTH "0ZvLeOQcR4kctBdbYTR7BIFXpWibMMHYCVnjp05D"

String months[12] = {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};

//The definition of our Data.
struct Data
{
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
void OnDataRecv(uint8_t *mac_addr, uint8_t *incomingData, uint8_t len)
{
	char macStr[18];

	//Print the MAC Address of the sender board.
	Serial.print("Packet received from: ");
	snprintf(macStr, sizeof(macStr), "%02x:%02x:%02x:%02x:%02x:%02x",
			 mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
	Serial.println(macStr);
	memcpy(&myData, incomingData, sizeof(myData));
	Serial.printf("Board ID %u: %u bytes\n", myData.id, len);

	// Update the boards with the new incoming Data
	boards[myData.id - 1].id = myData.id;
	boards[myData.id - 1].temp = myData.temp;
	boards[myData.id - 1].humidity = myData.humidity;
	boards[myData.id - 1].location = myData.location;

	//Print the new Data
	Serial.println("Board at the " + boards[myData.id - 1].location + " recorded: \n");
	Serial.printf("temperature: %f \n", boards[myData.id - 1].temp);
	Serial.printf("humidity: %f \n", boards[myData.id - 1].humidity);
	Serial.println();

	haveBoardReading = true;

	//Our array is indexed 0-1, our board ID's are 1-2
	boardToUpdate = myData.id - 1;
}

//Connect to WIFI with ssid and password.
void connectToWifi()
{
	// Set device as a Wi-Fi Station
	WiFi.mode(WIFI_STA);

	Serial.println("Connecting to ");
	Serial.println(ssid);

	//connect to your local wi-fi network
	WiFi.begin(ssid, password);

	//check wi-fi is connected to wi-fi network
	while (WiFi.status() != WL_CONNECTED)
	{
		delay(500);
		Serial.print(".");
	}
	Serial.println("");
	Serial.println("WiFi connected..!");
	Serial.print("Got IP: ");
	Serial.println(WiFi.localIP());
}

void connectToFirebase()
{
	Firebase.begin(FIREBASE_HOST, FIREBASE_AUTH);
	if (Firebase.failed())
	{
		Serial.println(Firebase.error());
	}
	delay(10);
}

void connectToTimeClient()
{
	timeClient.begin();
	// GMT +1 = 3600
	// GMT +8 = 28800
	// GMT -1 = -3600
	// GMT -8 = -28800
	// GMT 0 = 0
	//Vancouver Time, 8 hours before GMT
	timeClient.setTimeOffset(-28800);
	timeClient.update();
}

String getCurrentDate()
{
	unsigned long epochTime = timeClient.getEpochTime();
	struct tm *tempTime = gmtime((time_t *)&epochTime);
	int day = tempTime->tm_mday;
	int month = tempTime->tm_mon + 1;
	int year = tempTime->tm_year + 1900;

	String currentDate = String(year) + "-" + String(month) + "-" + String(day);
	return currentDate;
}

//Writes our board Data to the Firebase Database. If the board does not exist in the
//Database at this moment in time, it will create a section for it.
void writeBoardData(Data boardDataReading)
{
	String currentDate = getCurrentDate();
	String formattedTime = timeClient.getFormattedTime();

	//Firebase.setFloat(ROOT + String(boardDataReading.id) + "/" + currentDate + "/temperature/" + formattedTime, boardDataReading.temp);
	//Firebase.setFloat(ROOT + String(boardDataReading.id) + "/" + currentDate + "/humidity/" + formattedTime, boardDataReading.humidity);

	StaticJsonBuffer<200> jsonBuffer;
	JsonObject& boardReadingJsonTree = jsonBuffer.createObject();
	boardReadingJsonTree["Id"] = boardDataReading.id;
	boardReadingJsonTree["Location"] = boardDataReading.location;
	boardReadingJsonTree["Temperature"] = boardDataReading.temp;
	boardReadingJsonTree["Humidity"] = boardDataReading.humidity;
	boardReadingJsonTree["TimeRecorded"] = formattedTime;
	Firebase.push(ROOT + String(boardDataReading.id) + "/" + currentDate + "/readings/" + formattedTime, boardReadingJsonTree);
	if (Firebase.failed())
	{
		Serial.println(Firebase.error());
	}
	else
	{
		Serial.printf("Uploaded Data for Board: %u \n", boardDataReading.id);
	}
}

void setup()
{
	// Initialize Serial Monitor
	Serial.begin(115200);

	WiFi.mode(WIFI_STA);
	WiFi.disconnect();

	// Init ESP-NOW
	if (esp_now_init() != 0)
	{
		Serial.println("Error initializing ESP-NOW");
		ESP.restart();
	}

	esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
	esp_now_register_recv_cb(OnDataRecv);
}

int counter;
void loop()
{
	if (millis() - counter > 20000)
	{
		Serial.println("Waiting for ESP-NOW messages...");
		counter = millis();
	}

	// If our master receiver received a reading from one of our sensor nodes, then connect to wifi and write that reading to firebase
	if (haveBoardReading)
	{
		haveBoardReading = false;
		connectToWifi();
		connectToTimeClient();
		connectToFirebase();
		writeBoardData(boards[boardToUpdate]);
		ESP.restart();
	}
}
