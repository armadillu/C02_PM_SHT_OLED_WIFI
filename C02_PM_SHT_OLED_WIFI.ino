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


// set to true if you want to connect to wifi. The display will show values only when the sensor has wifi connection
boolean connectWIFI=true;
boolean sendDataToServer=false;

// change if you want to send the data to another server
String APIROOT = "http://hw.airgradient.com/";

int sleepMS = 500; //ms

int sensorUpdateInterval = 20000; //ms
int sensorUpdateTimer= 0; //ms

int sensorBeingDisplayed = 0; //index, [0..4]
int sensorDisplayUpdateInterval = 2000; //ms
int sensorDisplayTimer = sensorDisplayUpdateInterval * 2;

//sensor data
struct SensorData{
	float temperature;
	int humidity;
	int pm2;
	int co2;
};

SensorData data;


void handleRoot() {
	digitalWrite(LED_BUILTIN, LOW); 
	static char json[96];	
	sprintf(json, "{\"id\":\"%s\", \"wifi\":%d, \"temp\":%.1f, \"hum\":%d, \"pm2\":%d, \"co2\":%d}", String(ESP.getChipId(),HEX), WiFi.RSSI(), data.temperature, data.humidity, data.pm2, data.co2);
	server.send(200, "application/json", json);
	digitalWrite(LED_BUILTIN, HIGH);
}


void handleMetrics() {
	server.send(200, "text/plain", GenerateMetrics() );
}


void HandleNotFound() {
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint i = 0; i < server.args(); i++) {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/html", message);
}


String GenerateMetrics() {
  String message = "";
  String idString = "{id=\"" + String(ESP.getChipId(),HEX) + "\",mac=\"" + WiFi.macAddress().c_str() + "\"}";

    message += "# HELP pm02 Particulate Matter PM2.5 value\n";
    message += "# TYPE pm02 gauge\n";
    message += "pm02";
    message += idString;
    message += String(data.pm2);
    message += "\n";

    message += "# HELP rco2 CO2 value, in ppm\n";
    message += "# TYPE rco2 gauge\n";
    message += "rco2";
    message += idString;
    message += String(data.co2);
    message += "\n";

    message += "# HELP atmp Temperature, in degrees Celsius\n";
    message += "# TYPE atmp gauge\n";
    message += "atmp";
    message += idString;
    message += String(data.temperature);
    message += "\n";

    message += "# HELP rhum Relative humidtily, in percent\n";
    message += "# TYPE rhum gauge\n";
    message += "rhum";
    message += idString;
    message += String(data.humidity);
    message += "\n";
    
	return message;
}


void setup(){
	Serial.begin(9600);

	display.init();
	//display.flipScreenVertically();
	showTextRectangle("Init", String(ESP.getChipId(),HEX),true);

	ag.PMS_Init();
	ag.CO2_Init();
	ag.TMP_RH_Init(0x44);

	if (connectWIFI) connectToWifi();

	String ssid = WiFi.SSID();
	IPAddress ip = WiFi.localIP();
	String ipstr = String(ip[0]) + "." + String(ip[1]) + "." + String(ip[2]) + "." + String(ip[3]);
	showTextRectangle(ssid + "\nIP Addr", ipstr , true);
	delay(2000);

	if (MDNS.begin("AirSensor")) {
		Serial.println("MDNS responder started AirSensor");
	}

	server.on("/", handleRoot);
	server.on("/metrics", handleMetrics);
	server.onNotFound(HandleNotFound);
	server.begin();
	
	Serial.println("HTTP server started");
	MDNS.addService("http", "tcp", 80);
	Serial.println("setup() ok!");

	delay(1000);
	updateSensorData();
}


void loop(){

	delay(sleepMS); 
	sensorUpdateTimer += sleepMS;
	sensorDisplayTimer += sleepMS;

	MDNS.update();
	server.handleClient();

	if(sensorUpdateTimer > sensorUpdateInterval){
		sensorUpdateTimer  = 0;
		updateSensorData();
	}

	
	if(sensorDisplayTimer > sensorDisplayUpdateInterval){
		sensorDisplayTimer = 0;
		
		//loop through displayed items
		sensorBeingDisplayed++;
		if(sensorBeingDisplayed > 3) sensorBeingDisplayed = 0;

		//display current sensor data index
		if(sensorBeingDisplayed == 0) showTextRectangle("PM2",String(data.pm2),false);
		if(sensorBeingDisplayed == 1) showTextRectangle("CO2",String(data.co2),false);
		if(sensorBeingDisplayed == 2) showTextRectangle("TMP", String(data.temperature,1)+"c",false);
		if(sensorBeingDisplayed == 3) showTextRectangle("HUMI",String(data.humidity)+"%",false);
	}
}

void updateSensorData(){

	Serial.println("updateSensorData()");
	
	int pm2 = ag.getPM2_Raw();
	if(pm2 > 0){
		data.pm2 = pm2;
	}else{
		Serial.println("bad pm2 reading! skipping!");
	}
	
	int co2 = ag.getCO2_Raw();
	if(co2 > 0){
		data.co2 = co2;
	}else{
		Serial.println("bad c02 reading! skipping!");
	}
	
	TMP_RH result = ag.periodicFetchData();
	data.temperature = result.t;
	data.humidity = result.rh;
}


// DISPLAY
void showTextRectangle(String ln1, String ln2, boolean small) {
	display.clear();
	display.setTextAlignment(TEXT_ALIGN_LEFT);
	int voffset = 0;
	if (small) {
		display.setFont(ArialMT_Plain_10);
		voffset = 25;
	} else {
		display.setFont(ArialMT_Plain_24);
		voffset = 20;
	}
	display.drawString(31, 0, ln1);
	display.drawString(31, voffset, ln2);
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
