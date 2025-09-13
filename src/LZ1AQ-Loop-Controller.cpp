#include <Arduino.h>

#include <DNSServer.h>

#include <ArduinoJson.h>
#include <WiFiManager.h>
#include <ESPAsyncWebServer.h>
#include <ESPAsyncHTTPUpdateServer.h>
#include <SPIFFS.h>
#include <ESPmDNS.h>

unsigned int previousMillis = 0;
unsigned const int interval = 30*1000;
bool power = false;

/*
	Setup
*/
const char *wifi_hostname = "loopcontroller";

AsyncWebServer server(80);
ESPAsyncHTTPUpdateServer updateServer;

char activeMode[10];
int windowHeight = 90;

/*
	Auxiliary Relay #4 Options
*/

bool auxEnable = true; //Enable 4th Relay in Web UI, set true or false
bool auxNC = true; //Sets visual indicator for Normally Closed
const char *auxLabel = "Power Supply"; //Label for Auxiliary Relay


/*
	GPIO Pins to use
	https://randomnerdtutorials.com/esp8266-pinout-reference-gpios/
*/

#define Relay1 27 // D5 GPIO34 Relay 1
#define Relay2 32 // D6 GPIO35 Relay 2
#define Relay3 22 // D7 GPIO32 Relay 3
#define Relay4 21  // D1 GPIO27 Relay 4 (AUX)

/*
	Set Loop modes

	Relays are Active Low

	1 0 0 Loop A 	(LOW, HIGH, HIGH)
	0 1 0 Loop B 	(HIGH, LOW, HIGH)
	1 1 0 A + B 	(LOW, LOW, HIGH)
	0 0 1 Vertical 	(HIGH, HIGH, LOW)

	Relay1 is connected normally closed to save power, no need to energize a relay in the default state.
	Since Relay1 is connected to Normally Closed we need to flip Relay1 (LOW becomes HIGH).
	Relays are activated with active low.
*/

void loopA(){ // 1 0 0 Active Low
	digitalWrite(Relay1, LOW); // NC Flipped
	digitalWrite(Relay2, HIGH);
	digitalWrite(Relay3, HIGH);
	strcpy(activeMode, "A");
	//Serial.println("Loop A");
}

void loopB(){ // 0 1 0 Active Low
	digitalWrite(Relay1, HIGH); // NC Flipped
	digitalWrite(Relay2, LOW);
	digitalWrite(Relay3, HIGH);
	strcpy(activeMode, "B");
	//Serial.println("Loop B");
}

void crossed(){ // 1 1 0 Active Low
	digitalWrite(Relay1, LOW); // NC Flipped
	digitalWrite(Relay2, LOW);
	digitalWrite(Relay3, HIGH);
	strcpy(activeMode, "Crossed");
	//Serial.println("Crossed Parallel");
}

void vertical(){ // 0 0 1 Active Low
	digitalWrite(Relay1, HIGH); // NC Flipped
	digitalWrite(Relay2, HIGH);
	digitalWrite(Relay3, LOW);
	strcpy(activeMode, "Vertical");
}

void off() {
	digitalWrite(Relay4,LOW);
	power = false;
}

void on() {
	digitalWrite(Relay4,HIGH);
	power = true;
}

void addBool(JsonDocument *doc, String name, bool value) {
  if (value) {
    (*doc)[name].set("true");
  } else {
    (*doc)[name].set("false");
  }
}

void addString(JsonDocument *doc, String name, String value) {
    (*doc)[name].set(value);
}

void getStatus(AsyncWebServerRequest *request) {
  StaticJsonDocument<1024> doc;
  addBool(&doc,"D0",digitalRead(Relay1)); // NC Flipped
  addBool(&doc,"D1",digitalRead(Relay2));
  addBool(&doc,"D2",digitalRead(Relay3));
  addBool(&doc,"D3",digitalRead(Relay4));
  addBool(&doc,"power",power);
  addString(&doc,"mode",activeMode);
  char buffer[1024];
  serializeJson(doc, buffer);
  
  request->send(200, "application/json", buffer);
}

void setupApi() {
	updateServer.setup(&server);
	DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
  	DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET, POST, PUT");
  	DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Content-Type");
  	server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request){
    	getStatus(request);
  	});
	server.on("/api/crossed", HTTP_GET, [](AsyncWebServerRequest *request){
    	crossed();
		getStatus(request);
  	});
	server.on("/api/loopA", HTTP_GET, [](AsyncWebServerRequest *request){
    	loopA();
		getStatus(request);
  	});
	server.on("/api/loopB", HTTP_GET, [](AsyncWebServerRequest *request){
    	loopB();
		getStatus(request);
  	});
	server.on("/api/vertical", HTTP_GET, [](AsyncWebServerRequest *request){
    	vertical();
		getStatus(request);
  	});
	server.on("/api/off", HTTP_GET, [](AsyncWebServerRequest *request){
    	off();
		getStatus(request);
  	});
	server.on("/api/on", HTTP_GET, [](AsyncWebServerRequest *request){
    	on();
		getStatus(request);
  	});
	server.serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");
  	server.serveStatic("/s/", SPIFFS, "/");
  	updateServer.setup(&server);
  	updateServer.setup(&server,OTA_USER,OTA_PASSWORD);
  	server.begin();
}

void setup() {
	WiFi.hostname(wifi_hostname);
	WiFi.mode(WIFI_STA); // explicitly set mode, esp defaults to STA+AP

	// put your setup code here, to run once:
	Serial.begin(115200);
	pinMode(Relay1, OUTPUT);
	pinMode(Relay2, OUTPUT);
	pinMode(Relay3, OUTPUT);
	pinMode(Relay4, OUTPUT);

	off();
	vertical();

	//WiFiManager, Local intialization. Once its business is done, there is no need to keep it around
	WiFiManager wifiManager;	

	//reset settings - wipe credentials for testing
	// wifiManager.resetSettings();	

	// Automatically connect using saved credentials,
	// if connection fails, it starts an access point with the specified name ("LoopController"),
	// if empty will auto generate SSID, if password is blank it will be anonymous AP (wifiManager.autoConnect())
	// then goes into a blocking loop awaiting configuration and will return success result	
	bool res;
	res = wifiManager.autoConnect();	
	if(!res) {
		Serial.println("Failed to connect");
		// ESP.restart();
	} 
	else {  
		Serial.println("Connected :)");
	}

	if(!SPIFFS.begin(true)){
    	Serial.println("An Error has occurred while mounting SPIFFS");
    	return;
  	}
	
	if (!MDNS.begin(wifi_hostname)) {
		Serial.println("Error setting up MDNS responder!");
		while (1) {
			delay(1000);
		}
	} else {
		Serial.println("mDNS responder started");	
	}
	
	server.begin();
	//Serial.printf("Web server started, open %s in a web browser\n", WiFi.localIP().toString().c_str());
	if (!MDNS.begin("loopcontroller")) {   // Set the hostname to "esp32swr.local"
    	Serial.println("Error setting up MDNS responder!");
    	delay(1000);
  	} else {
    	Serial.println("mDNS responder started");
  	}
	MDNS.addService("http", "tcp", 80);
	setupApi();
}

void loop() {
	unsigned long currentMillis = millis();
  	// if WiFi is down, try reconnecting
  	if ((WiFi.status() != WL_CONNECTED) && (currentMillis - previousMillis >= interval)) {
    	Serial.print(millis());
    	Serial.println("Reconnecting to WiFi...");
    	WiFi.disconnect();
    	WiFi.reconnect();
    	previousMillis = currentMillis;
  	}
	delay(100);
}
