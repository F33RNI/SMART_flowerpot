/*
*
* ESP8266 + Thingworx academic
* Serial WiFi Thingworx Client
* __________________
*
*  © 2019, Neshumov Pavel Evgenyevich
*
*/

#include <ESP8266WiFi.h> // ESP Library

/* --- Wifi Setup --- */
#define SSID "Your_SSID"		// SSID of your WiFi
#define PASS "Your_Password"	// PSK  of your WiFi

/* --- Thingworx https server --- */
#define HOST "academic-educatorsextension.portal.ptc.io"							// Thingworx server IP
#define PORT 443																	// HTTPS port
#define FINGERPRINT "2b ed da e0 40 95 d0 f5 87 7f db af 06 9e 4c 11 b9 cd 62 cc"	// Thingworx HTTPS fingerprint 

/* --- Thingworx thing setup --- */
#define THING "NameOfThing"			// Name of your thing
#define APPKEY "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx"	// Your appKey
#define SERVICE "InputOutput"							// Thing's service for communication

//#define DEBUG								// Uncomment if you want to recieve debug messages by Serial

WiFiClientSecure client;					// WiFi https client

void setup() {
	Serial.begin(115200);					// Serial BoundRate

#ifdef DEBUG
	Serial.println();
	Serial.print("connecting to ");
	Serial.println(SSID);
#endif

	WiFi.begin(SSID, PASS);

	while (WiFi.status() != WL_CONNECTED) { // Waiting for connection
		delay(500);
#ifdef DEBUG
		Serial.print(".");
#endif
	}

#ifdef DEBUG
	Serial.println("WiFi connected");
	Serial.println("IP address: ");
	Serial.println(WiFi.localIP());;
#endif
}

void loop() {
	startStreaming();
#ifdef DEBUG
	Serial.println("Disconnected from Thingworx! Wainting for 4 secs...");
#endif
	delay(4000);
}

void startStreaming(void)					// Connection and streaming to Thingworx
{
	client.setFingerprint(FINGERPRINT);		// Sending HTTPS fingerprint

#ifdef DEBUG
	Serial.println(HOST);
#endif

	if (!client.connect(HOST, PORT)) {		// Connection failed
#ifdef DEBUG
		Serial.println("Connection failed!");
#endif
		return;
	}
	else									// Connected

#ifdef DEBUG
	Serial.println("Connected to ThingWorx!");
	Serial.println("Waiting for Serial data...");
#endif


	readyAndWait(0);						// Making request
	String data = Serial.readStringUntil('\n');

#ifdef DEBUG
	Serial.print("Received: ");
	Serial.println(data);
#endif

	sendRequest(createREQUEST(data));

	while (client.connected()) {
		char symbol = client.read();

		if (symbol == '{') {				// Accepting JSON
			String line = client.readStringUntil('}');
			Serial.println(String('{' + line + '}'));

#ifdef DEBUG
			Serial.println("Waiting for Serial data...");
#endif
			readyAndWait(1);
			String data = Serial.readStringUntil('\n');
			sendRequest(createREQUEST(data));
			
		}
	}
}

void readyAndWait(boolean connected) {		// Send Serial request for a new command
	Serial.println('>');
	while (!Serial.available()) {
		if (connected && !client.connected())
			break;
	}
}
String createREQUEST(String data) {			// Make URL for thingworx. (data example: "Light=10&Humid=50&Temp=25")
	String url = "/Thingworx/Things/";
	url += THING;
	url += "/Services/";
	url += SERVICE;
	url += "?appKey=";
	url += APPKEY;
	url += "&method=post&x-thingworx-session=true&";
	url += data;

	return url;
}
boolean sendRequest(String request) {		// Send request to Thingworx
	if (request.length() < 1)
		return 0;
	client.print(String("GET ") + request + " HTTP/1.1\r\n" +
		"Host: " + HOST + "\r\n" +
		"x-thingworx-session: false" + "\r\n" +
		"Accept: application/json" + "\r\n" +
		//"Connection: close\r\n" +
		"Content-Type: application/json\r\n\r\n");
	return 1;
}