/*
This is the code for the AirGradient DIY Air Quality Sensor with an ESP8266 Microcontroller.

It is a high quality sensor showing PM2.5, CO2, Temperature and Humidity on a small display and can send data over Wifi.

For build instructions please visit https://www.airgradient.com/diy/

Compatible with the following sensors:
Plantower PMS5003 (Fine Particle Sensor)
SenseAir S8 (CO2 Sensor)
SHT30/31 (Temperature/Humidity Sensor)

Please install ESP8266 board manager (tested with version 3.0.0)

The codes needs the following libraries installed:
"WifiManager by tzapu, tablatronix" tested with Version 2.0.3-alpha
"ESP8266 and ESP32 OLED driver for SSD1306 displays by ThingPulse, Fabrice Weinberg" tested with Version 4.1.0

Configuration:
Please set in the code below which sensor you are using and if you want to connect it to WiFi.

If you are a school or university contact us for a free trial on the AirGradient platform.
https://www.airgradient.com/schools/

MIT License
*/

#include <AirGradient.h>
#include <WiFiManager.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>

#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>

#include <Wire.h>
#include "SSD1306Wire.h"

AirGradient ag = AirGradient();

SSD1306Wire display(0x3c, SDA, SCL);

ESP8266WebServer server(80);


// set sensors that you do not use to false
boolean hasPM=true;
boolean hasCO2=true;
boolean hasSHT=true;

// set to true if you want to connect to wifi. The display will show values only when the sensor has wifi connection
boolean connectWIFI=true;
boolean sendDataToServer=false;

// change if you want to send the data to another server
String APIROOT = "http://hw.airgradient.com/";

int sleepMS = 250;
static int loopCount = 999999999;
int sendDataIntervalMin = 5; //min - interval to send temp & humidity http updates to server
int sensorBeingDisplayed = 0;


void handleRoot() {
	digitalWrite(LED_BUILTIN, LOW); 
	static char json[96];
	
	int PM2 = ag.getPM2_Raw();
	int CO2 = ag.getCO2_Raw();
	TMP_RH result = ag.periodicFetchData();	
	sprintf(json, "{\"wifi\":%d, \"temp\":%.1f, \"hum\":%d, \"pm2\":%d, \"co2\":%d}", WiFi.RSSI(), result.t, result.rh, PM2, CO2);
	server.send(200, "application/json", json);
	digitalWrite(LED_BUILTIN, HIGH);
}


void setup(){
	Serial.begin(9600);

	display.init();
	display.flipScreenVertically();
	showTextRectangle("Init", String(ESP.getChipId(),HEX),true);

	if (hasPM) ag.PMS_Init();
	if (hasCO2) ag.CO2_Init();
	if (hasSHT) ag.TMP_RH_Init(0x44);

	if (connectWIFI) connectToWifi();
	delay(2000);

	if (MDNS.begin("AirSensor")) {
		Serial.println("MDNS responder started AirSensor");
	}

	MDNS.addService("http", "tcp", 80);
	server.on("/", handleRoot);
	server.begin();
	Serial.println("HTTP server started");
}

void loop(){

	delay(sleepMS); //once per second

	// create payload
	String payload = "{\"wifi\":" + String(WiFi.RSSI()) + ",";

	if (hasPM) {
		int PM2 = ag.getPM2_Raw();
		payload=payload+"\"pm02\":" + String(PM2);
		if(sensorBeingDisplayed == 0) showTextRectangle("PM2",String(PM2),false);
	}

	if (hasCO2) {
		if (hasPM) payload=payload+",";
		int CO2 = ag.getCO2_Raw();
		payload=payload+"\"rco2\":" + String(CO2);
		if(sensorBeingDisplayed == 1) showTextRectangle("CO2",String(CO2),false);
	}

	if (hasSHT) {
		if (hasCO2 || hasPM) payload=payload+",";
		TMP_RH result = ag.periodicFetchData();
		payload=payload+"\"atmp\":" + String(result.t) +   ",\"rhum\":" + String(result.rh);
		if(sensorBeingDisplayed == 2) showTextRectangle(String(result.t),String(result.rh)+"%",false);
	}

	payload = payload + "}";

	server.handleClient();

	int maxLoopCount = sendDataIntervalMin * ( 1000 / sleepMS ) * ( 60 /* 1 min */ );
	if (loopCount >= maxLoopCount) { //every ~sendDataIntervalMin minutes, ping server with data
		if (connectWIFI && sendDataToServer){
			Serial.println(payload);
			String POSTURL = APIROOT + "sensors/airgradient:" + String(ESP.getChipId(),HEX) + "/measures";
			Serial.println(POSTURL);

			WiFiClient client;
			HTTPClient http;
			http.begin(client, POSTURL);
			http.addHeader("content-type", "application/json");
			int httpCode = http.POST(payload);
			String response = http.getString();
			Serial.println(httpCode);
			Serial.println(response);
			http.end();
		}
		loopCount = 1;
	}

	//loop through displayed items
	sensorBeingDisplayed++;
	if(sensorBeingDisplayed > 2) sensorBeingDisplayed = 0;

	loopCount++;
}


// DISPLAY
void showTextRectangle(String ln1, String ln2, boolean small) {
	display.clear();
	display.setTextAlignment(TEXT_ALIGN_LEFT);
	if (small) {
		display.setFont(ArialMT_Plain_16);
	} else {
		display.setFont(ArialMT_Plain_24);
	}
	display.drawString(32, 16, ln1);
	display.drawString(32, 36, ln2);
	display.display();
}

// Wifi Manager
void connectToWifi(){
	WiFiManager wifiManager;
	//WiFi.disconnect(); //to delete previous saved hotspot
	String HOTSPOT = "AIRGRADIENT-"+String(ESP.getChipId(),HEX);
	wifiManager.setTimeout(120);
	if(!wifiManager.autoConnect((const char*)HOTSPOT.c_str())) {
		Serial.println("failed to connect and hit timeout");
		delay(3000);
		ESP.restart();
		delay(5000);
	}

}
