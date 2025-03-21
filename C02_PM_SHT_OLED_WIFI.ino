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

#define HOST_NAME 				"AirGradient"

#include <AirGradient.h> //v 2.4.15 installed
#include <WiFiManager.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include "WifiPass.h"  //define wifi SSID & pass
#include <SerialWebLog.h>
#include <ArduinoOTA.h>

#include <Wire.h>
#include "SSD1306Wire.h"

AirGradient ag = AirGradient();

SSD1306Wire display(0x3c, SDA, SCL);

SerialWebLog mylog;

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
	sprintf(json, "{\"id\":\"%s\", \"wifi\":%d, \"temp\":%.2f, \"hum\":%d, \"pm2\":%d, \"co2\":%d}", String(ESP.getChipId(),HEX), WiFi.RSSI(), data.temperature, data.humidity, data.pm2, data.co2);
	mylog.getServer()->send(200, "application/json", json);
	digitalWrite(LED_BUILTIN, HIGH);
}

void handleClimate() {
	digitalWrite(LED_BUILTIN, LOW); 
	static char json[64];
	sprintf(json, "{\"temperature\":%.2f, \"humidity\":%d}", data.temperature, data.humidity);
	mylog.getServer()->send(200, "application/json", json);
	digitalWrite(LED_BUILTIN, HIGH);
}

void handleCo2() {
	static char txt[5];
	sprintf(txt, "%d\n", data.co2);
	mylog.getServer()->send(200, "text/plain", txt);
}

void handlePm2() {
	static char txt[5];
	sprintf(txt, "%d\n", data.pm2);
	mylog.getServer()->send(200, "text/plain", txt);
}

void handleMetrics() {
	mylog.getServer()->send(200, "text/plain", GenerateMetrics() );
}

String GenerateMetrics() {
  String message = "";
  String idString = "{id=\"" + String(ESP.getChipId(),HEX) + "\",mac=\"" + WiFi.macAddress().c_str() + "\"}";

    message += "pm02";
    message += idString;
    message += String(data.pm2);
    message += "\n";

    message += "rco2";
    message += idString;
    message += String(data.co2);
    message += "\n";

    message += "atmp";
    message += idString;
    message += String(data.temperature);
    message += "\n";

    message += "rhum";
    message += idString;
    message += String(data.humidity);
    message += "\n";
    
	message += "wifi";
	message += idString;
	message += String((int)WiFi.RSSI());
	message += "\n";

	return message;
}


void setup(){
	mylog.setup(HOST_NAME, ssid, password);

	String ID = String(ESP.getChipId(), HEX);
	mylog.printf("HostName: %s\n", HOST_NAME);

	display.init();
	showTextRectangle("Init", String(ESP.getChipId(),HEX),true);

	ag.PMS_Init();
	ag.CO2_Init();
	ag.TMP_RH_Init(0x44);

	String ssid = WiFi.SSID();
	IPAddress ip = WiFi.localIP();
	String ipstr = String(ip[0]) + "." + String(ip[1]) + "." + String(ip[2]) + "." + String(ip[3]);
	showTextRectangle(ssid + "\nIP Addr", ipstr , true);
	delay(2000);

	mylog.getServer()->on("/json", handleRoot);
	mylog.addHtmlExtraMenuOption("JsonOutput", "/json");
	mylog.getServer()->on("/metrics", handleMetrics);
	mylog.addHtmlExtraMenuOption("Metrics", "/metrics");
	mylog.getServer()->on("/climate", handleClimate);
	mylog.addHtmlExtraMenuOption("Climate", "/climate");
	mylog.getServer()->on("/co2", handleCo2);
	mylog.addHtmlExtraMenuOption("co2", "/co2");
	mylog.getServer()->on("/pm2", handlePm2);
	mylog.addHtmlExtraMenuOption("pm2", "/pm2");

	ESP.wdtDisable();
	ESP.wdtEnable(WDTO_8S);
	mylog.printf("Watchdog Enabled!\n");

	ArduinoOTA.setHostname(HOST_NAME);
	ArduinoOTA.setRebootOnSuccess(true);
	ArduinoOTA.begin();

	delay(1000);
	updateSensorData();
}


void loop(){

	mylog.update();
	delay(sleepMS); 
	sensorUpdateTimer += sleepMS;
	sensorDisplayTimer += sleepMS;

	if(sensorUpdateTimer > sensorUpdateInterval){
		sensorUpdateTimer  = 0;
		updateSensorData();
	}

	ArduinoOTA.handle();
	ESP.wdtFeed(); //feed watchdog frequently
	
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

	//mylog.printf("updateSensorData()\n");
	
	int pm2 = ag.getPM2_Raw();
	if(pm2 > 0){
		data.pm2 = pm2;
	}else{
		mylog.printf("bad pm2 reading! skipping!\n");
	}
	
	int co2 = ag.getCO2_Raw();
	if(co2 > 0){
		data.co2 = co2;
	}else{
		mylog.printf("bad c02 reading! skipping!\n");
	}
	
	TMP_RH result = ag.periodicFetchData();
	data.temperature = result.t - 3.1; /// sensor wonkyness cheap calibration !
	data.humidity = result.rh + 10;  ///sensor wonlyness calibration
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
