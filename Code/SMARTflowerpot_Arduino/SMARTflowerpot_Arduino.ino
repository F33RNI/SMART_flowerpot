/*
*
* SMART Self-controlled Flower Pot + IoT
* Data exchange with ThingWorx® Academic
* __________________
*
*  © 2019, Neshumov Pavel Evgenyevich
*
*/

/* -------- Libraries  -------- */
#include <Wire.h>
#include <Adafruit_SHT31.h>
#include <Adafruit_NeoPixel.h>
#include <ArduinoJson.h>

/* -------- PINs  -------- */
#define WiFi Serial3	// ESP8266 Serial (for Arduino® Mega)
#define LightGND A8		// Light Sensor GND
#define LightVCC A9		// Light Sensor +5V
#define LightAO A10		// Light Sensor Signal
#define MoistureGND A2	// Moisture Sensor GND
#define MoistureVCC A3	// Moisture Sensor +5V
#define MoistureAO  A0	// Moisture Sensor Signal
#define LEDsPin 9		// WS2812 DIN pin
#define PumpPin 5		// Pump PWM pin

/* -------- Setup  -------- */
#define LEDsAmount 24		// Amount of LEDs in strip
#define PumpPower 128		// PWM 8bit (0-255)
#define SendingTimeout 5000 // Delay between data exchange with ThingWorx

/* -------- System vars  -------- */
Adafruit_SHT31 sht31 = Adafruit_SHT31();	//Temp + Humid sensor
Adafruit_NeoPixel leds = Adafruit_NeoPixel(LEDsAmount, LEDsPin, NEO_GRB + NEO_KHZ800);
struct RGBpixel
{
	uint8_t R = 0;
	uint8_t G = 0;
	uint8_t B = 0;
};
struct RGBpixel RGBcolor;
boolean LEDstage = false;
boolean canTransmitt = false;
boolean pumpEnabled = false;
unsigned int k, color = 0, moistureSetup = 100;
unsigned long ActionTimer = 0;
unsigned long WiFitimer = 0;
#define SECS_PER_MIN  (60UL)
#define SECS_PER_HOUR (3600UL)
#define SECS_PER_DAY  (SECS_PER_HOUR * 24L)

#define numberOfSeconds(_time_) (_time_ % SECS_PER_MIN)  
#define numberOfMinutes(_time_) ((_time_ / SECS_PER_MIN) % SECS_PER_MIN) 
#define numberOfHours(_time_) (( _time_% SECS_PER_DAY) / SECS_PER_HOUR)
#define elapsedDays(_time_) ( _time_ / SECS_PER_DAY) 

void setup() {
	pinsAndSensorsInit();
	sayHello();
}

void loop() {
	updateFlowerPot();
	TWaction();
}

/* -------- Initialization  -------- */
void pinsAndSensorsInit() {
	Serial.begin(9600);
	WiFi.begin(115200);
	sht31.begin(0x44);
	leds.begin();

	pinMode(LightAO, INPUT);
	pinMode(MoistureAO, INPUT);
	pinMode(LightVCC, OUTPUT);
	pinMode(LightGND, OUTPUT);
	pinMode(MoistureVCC, OUTPUT);
	pinMode(MoistureGND, OUTPUT);
	pinMode(PumpPin, OUTPUT);

	digitalWrite(LightVCC, 1);
	digitalWrite(MoistureVCC, 1);
}

/* -------- Data exchange with Thingworx Server  -------- */
void TWaction() {
	if (canTransmitt && millis() - WiFitimer >= SendingTimeout) {
		float temp = sht31.readTemperature();
		float humid = sht31.readHumidity();

		WiFi.print("Temp=");
		WiFi.print(temp, 0);
		WiFi.print("&Humid=");
		WiFi.print(humid, 0);
		WiFi.print("&Light=");
		WiFi.print(map(analogRead(LightAO), 0, 1024, 0, 100));
		WiFi.print("&Moisture=");
		WiFi.print(map(analogRead(MoistureAO), 0, 1024, 0, 100));
		WiFi.print("&NewLog=Time:");
		WiFi.print(elapsedDays(millis() / 1000));
		WiFi.print(".");
		WiFi.print(numberOfHours(millis() / 1000));
		WiFi.print(":");
		WiFi.print(numberOfMinutes(millis() / 1000));
		WiFi.print(":");
		WiFi.print(numberOfSeconds(millis() / 1000));
		WiFi.print("_PumpState:");
		WiFi.print(pumpEnabled ? "ON" : "OFF");

		Serial.println("Sended!");
		canTransmitt = false;
		WiFitimer = millis();
	}

	String message = "";
	if (WiFi.available()) {
		message = WiFi.readStringUntil('\n');

		if (message.startsWith(">"))
			canTransmitt = true;

		else if (message.startsWith("{")) {
			Serial.print("Recieved JSON: ");
			Serial.println(message);

			StaticJsonBuffer<200> jsonBuffer;
			JsonObject& root = jsonBuffer.parseObject(message);

			if (!root.success())
				Serial.println("JSON paring failed");
			else {
				color = (float)root["C"];
				moistureSetup = (float)root["M"];

				Serial.println("Parsed from JSON: ");
				Serial.print("Color: ");
				Serial.print(color);
				Serial.print(" Moisure: ");
				Serial.println(moistureSetup);
			}
		}
		else
			Serial.println(message);
	}
}

/* -------- Enable LEDs, Checking Light and moisture  -------- */
void updateFlowerPot() {
	if (millis() - ActionTimer >= 20) {
		int light = analogRead(LightAO);
		if (light > 900)
			light = 900;
		setLight(color, map(analogRead(LightAO), 0, 900, 100, 0));

		if (map(analogRead(MoistureAO), 0, 1024, 0, 100) > moistureSetup) {
			pumpEnabled = true;
			analogWrite(PumpPin, PumpPower);
		}
		else {
			pumpEnabled = false;
			analogWrite(PumpPin, 0);
		}

		ActionTimer = millis();
	}
}

/* -------- LEDs starting message  -------- */
void sayHello() {
	for (uint8_t i = 0; i < 24; i++)
	{
		HSVtoRGB(map(i, 0, 23, 0, 355), 1, 1);
		leds.setPixelColor(i, RGBcolor.R, RGBcolor.G, RGBcolor.B);
		leds.show();
		delay(100);
	}
	delay(1000);
	leds.clear();
}

/* -------- LEDs change function  -------- */
void setLight(int color, uint8_t power) {
	HSVtoRGB(color, 1, 1);							//Calculate RGB color

	RGBcolor.R = map(power, 0, 100, 0, RGBcolor.R);	//Mapping colors by power
	RGBcolor.G = map(power, 0, 100, 0, RGBcolor.G);
	RGBcolor.B = map(power, 0, 100, 0, RGBcolor.B);

	uint8_t InvR = map(k, 255, 0, 0, RGBcolor.R);
	uint8_t InvG = map(k, 255, 0, 0, RGBcolor.G);
	uint8_t InvB = map(k, 255, 0, 0, RGBcolor.B);

	RGBcolor.R = map(k, 0, 255, 0, RGBcolor.R);
	RGBcolor.G = map(k, 0, 255, 0, RGBcolor.G);
	RGBcolor.B = map(k, 0, 255, 0, RGBcolor.B);


	for (uint8_t i = 0; i < LEDsAmount; i += 2) {		//Even LEDs
		if (LEDstage)
			leds.setPixelColor(i, RGBcolor.R, RGBcolor.G, RGBcolor.B);
		else
			leds.setPixelColor(i, InvR, InvG, InvB);

	}
	for (uint8_t i = 1; i < LEDsAmount + 1; i += 2) {		//Odd LEDs
		if (!LEDstage)
			leds.setPixelColor(i, RGBcolor.R, RGBcolor.G, RGBcolor.B);
		else
			leds.setPixelColor(i, InvR, InvG, InvB);
	}
	if (k == 255) {
		k = 0;
		LEDstage = !LEDstage;
	}
	else
		k++;
	leds.show();
}

/* -------- HSV to RGB converter  -------- */
void HSVtoRGB(int h, float s, float v) {
	float r = 0;
	float g = 0;
	float b = 0;

	double hf = h / 60.0;

	int i = (int)floor(h / 60.0);
	float f = h / 60.0 - i;
	float pv = v * (1 - s);
	float qv = v * (1 - s*f);
	float tv = v * (1 - s * (1 - f));

	switch (i)
	{
	case 0:
		r = v;
		g = tv;
		b = pv;
		break;
	case 1:
		r = qv;
		g = v;
		b = pv;
		break;
	case 2:
		r = pv;
		g = v;
		b = tv;
		break;
	case 3:
		r = pv;
		g = qv;
		b = v;
		break;
	case 4:
		r = tv;
		g = pv;
		b = v;
		break;
	case 5:
		r = v;
		g = pv;
		b = qv;
		break;
	}
	RGBcolor.R = constrain((int)255 * r, 0, 255);
	RGBcolor.G = constrain((int)255 * g, 0, 255);
	RGBcolor.B = constrain((int)255 * b, 0, 255);
}
