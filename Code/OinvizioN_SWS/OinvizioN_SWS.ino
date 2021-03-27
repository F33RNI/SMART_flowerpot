/*
    Name:       OinvizioN_SWS.ino
    Created:	2020-01-18 03:49:25
    Author:     PLACEBO\Frey
*/

#include <ESP8266WiFi.h>
#include <Wire.h>
#include <Adafruit_ADS1015.h>
#include <time.h>

// Pins
#define BTN_PIN     0
#define LED_PIN     2
#define PUMP_0_PIN  15
#define PUMP_1_PIN  13

// Setup
char const* SSID = "<your WiFi name>";
char const* PASS = "<your WiFi PSK>";
char const* UBIDOTS_SERVER = "industrial.api.ubidots.com";
const int UBIDOTS_PORT = 80;
char const* UBIDOTS_TOKEN = "BBFF-<your ubidots token>";
char const* DEVICE_LABEL = "<your device label>";
char const* USER_AGENT = "ESP8266";
char const* VERSION = "1.0";

#define NTP_SERVERS		"128.138.140.44", "129.6.13.35"

#define OFFLINE_SUNDAY_TIME             0
#define OFFLINE_MONDAY_TIME             0
#define OFFLINE_TUESDAY_TIME            0
#define OFFLINE_WEDNESDAY_TIME          12
#define OFFLINE_THURSDAY_TIME           0
#define OFFLINE_FRIDAY_TIME             0
#define OFFLINE_SATURDAY_TIME           0

#define CLOCK_START_HOURS	    10
#define CLOCK_END_HOURS		    22
#define PUMP_PWM_LOW_V          600         //0-1023
#define PUMP_PWM_HIG_V          70          //0-1023
#define BTN_LONG_PRESS          1500        //ms
#define RECONNECT_TIME          60000L      //ms
#define UBIDOTS_SEND_INTERVAL   1800000L    //ms
#define UBIDOTS_GET_INTERVAL    120000L      //ms
#define NTP_SYNC_INTERVAL       60000L      //ms
#define VOLTAGE_THRESHOLD       6.0         //V

// Pump cycles
#define CYCLE_DURATION      7000    //ms
#define DELAY_BTW_CYCLES    2000    //ms
#define CYCLES_NUM          2

Adafruit_ADS1115 ads;
WiFiClient client_ubi;
time_t current_time;
struct tm* local_time;
uint64_t ntp_update_timer, pump_cycle_timer, start_time, led_state_timer, ubidots_send_timer, ubidots_get_timer;
int16_t raw_adc_data[4];
float voltage;
float moisture[3];
char moisture_str[3][10];
char current_state_str[10];
char manual_run_str[10];
uint8_t time_by_days[7];

boolean this_day_poured;
uint8_t weekday_poured;
uint8_t time_temp_value;

uint8_t pump_cycle;
uint16_t cycle_delay;
boolean cycle_state;
uint8_t current_state;
float last_pump_time;
float manual_run;
char payload[700];
boolean ubidots_error;

void setup()
{
    // Init variables
    time_by_days[0] = OFFLINE_SUNDAY_TIME;
    time_by_days[1] = OFFLINE_MONDAY_TIME;
    time_by_days[2] = OFFLINE_TUESDAY_TIME;
    time_by_days[3] = OFFLINE_WEDNESDAY_TIME;
    time_by_days[4] = OFFLINE_THURSDAY_TIME;
    time_by_days[5] = OFFLINE_FRIDAY_TIME;
    time_by_days[6] = OFFLINE_SATURDAY_TIME;

    // Pins setup
    pinMode(LED_PIN, OUTPUT);
    pinMode(PUMP_0_PIN, OUTPUT);
    pinMode(PUMP_1_PIN, OUTPUT);
    digitalWrite(LED_PIN, 0);
    digitalWrite(PUMP_0_PIN, 0);
    digitalWrite(PUMP_1_PIN, 0);
    delay(100);

    // Serial port setup
    Serial.begin(57600);
    delay(100);
    Serial.println();

    // ADS1115 setup
    ads.setGain(GAIN_ONE);
    ads.begin();

    // Check input voltage
    read_adc_data();
    if (voltage > 14 || voltage < 4)
        while (true) {
            digitalWrite(LED_PIN, 0);
            delay(50);
            digitalWrite(LED_PIN, 1);
            delay(50);
        }

    // Connect to wifi and configure time
    iot_connect();

    // Try to read threshold values
    if (WiFi.status() == WL_CONNECTED) {
        read_update_weekdays();
        ubidots_send();
    }
        
    Serial.println(F("Configuration finished."));
}

void loop()
{
    read_adc_data();
    time_updater();
    button_checker();
    pump_controller();
    update_state();
    ubidots_data();
}

void update_state(void) {
    if (WiFi.status() != WL_CONNECTED)
        current_state = 5;
    else if (local_time->tm_year == 70)
        current_state = 4;
    else if (ubidots_error)
        current_state = 3;
    else {
        if (WiFi.status() == WL_CONNECTED && !pump_cycle)
            current_state = 1;
        if (WiFi.status() == WL_CONNECTED && pump_cycle)
            current_state = 2;
    }

    if (millis() - led_state_timer > 4000) {
        blink_led(current_state);
        led_state_timer = millis();
    }
}

void blink_led(uint8_t cycles) {
    for (uint8_t i = 0; i < cycles; i++)
    {
        digitalWrite(LED_PIN, 0);
        delay(250);
        digitalWrite(LED_PIN, 1);
        delay(250);
    }
}

void button_checker(void) {
    if (!digitalRead(0)) {
        start_time = millis();
        while (!digitalRead(0));
        if (millis() - start_time > BTN_LONG_PRESS) {
            // Button long press
            Serial.println(F("[BUTTON] Long button press"));

        }
        else {
            // Button short press
            Serial.println(F("[BUTTON] Short button press"));
            manual_pump();
        }
    }
}

void read_adc_data(void) {
    raw_adc_data[0] = ads.readADC_SingleEnded(0);
    raw_adc_data[1] = ads.readADC_SingleEnded(1);
    raw_adc_data[2] = ads.readADC_SingleEnded(2);
    raw_adc_data[3] = ads.readADC_SingleEnded(3);

    for (uint8_t i = 0; i < 3; i++)
    {
        moisture[i] = mapfloat(raw_adc_data[i], 9000, 23000, 100, 0);
        if (moisture[i] > 100.0)
            moisture[i] = 100.0;
        else if (moisture[i] < 0.0)
            moisture[i] = 0.0;
        dtostrf(moisture[i], 4, 2, moisture_str[i]);
    }

    voltage = mapfloat(raw_adc_data[3], 0, 32768, 0, 42);
    if (voltage > 14)
        voltage = 14;
    else if (voltage < 4)
        voltage = 4;
}

void ubidots_data(void) {
    if (WiFi.status() == WL_CONNECTED && current_state < 4 && !pump_cycle) {
        if (millis() - ubidots_send_timer >= UBIDOTS_SEND_INTERVAL) {
            // Send data
            ubidots_send();
            ubidots_send_timer = millis();
        }
        if (millis() - ubidots_get_timer >= UBIDOTS_GET_INTERVAL && millis() - ubidots_send_timer > 10000) {
            // Read data
            Serial.println(F("[UBIDOTS] Getting data..."));
            // Get Weekdays times values
            Serial.println(F("[UBIDOTS] Reading day-time values..."));
            read_update_weekdays();

            manual_run = ubidots_read("manual_run");
            if (manual_run >= 0) {
                if (manual_run > 0) {
                    manual_pump();
                    ubidots_send();
                    manual_run = 0;
                }
            }
            else {
                Serial.println(F("[UBIDOTS] Error getting manual_run data!"));
                ubidots_error = 1;
            }
            ubidots_get_timer = millis();
        }
    }
}

void read_update_weekdays(void) {
    Serial.println(F("[UBIDOTS] Reading sunday time after 2 sec..."));
    delay(2000);
    float temp_day_time = ubidots_read("sunday");
    if (temp_day_time >= 0)
        time_by_days[0] = temp_day_time;
    else
        Serial.println(F("[UBIDOTS] Reading error!"));

    Serial.println(F("[UBIDOTS] Reading monday time after 2 sec..."));
    delay(2000);
    temp_day_time = ubidots_read("monday");
    if (temp_day_time >= 0)
        time_by_days[1] = temp_day_time;
    else
        Serial.println(F("[UBIDOTS] Reading error!"));

    Serial.println(F("[UBIDOTS] Reading tuesday time after 2 sec..."));
    delay(2000);
    temp_day_time = ubidots_read("tuesday");
    if (temp_day_time >= 0)
        time_by_days[2] = temp_day_time;
    else
        Serial.println(F("[UBIDOTS] Reading error!"));

    Serial.println(F("[UBIDOTS] Reading wednesday time after 2 sec..."));
    delay(2000);
    temp_day_time = ubidots_read("wednesday");
    if (temp_day_time >= 0)
        time_by_days[3] = temp_day_time;
    else
        Serial.println(F("[UBIDOTS] Reading error!"));

    Serial.println(F("[UBIDOTS] Reading thursday time after 2 sec..."));
    delay(2000);
    temp_day_time = ubidots_read("thursday");
    if (temp_day_time >= 0)
        time_by_days[4] = temp_day_time;
    else
        Serial.println(F("[UBIDOTS] Reading error!"));

    Serial.println(F("[UBIDOTS] Reading friday time after 2 sec..."));
    delay(2000);
    temp_day_time = ubidots_read("friday");
    if (temp_day_time >= 0)
        time_by_days[5] = temp_day_time;
    else
        Serial.println(F("[UBIDOTS] Reading error!"));

    Serial.println(F("[UBIDOTS] Reading saturday time after 2 sec..."));
    delay(2000);
    temp_day_time = ubidots_read("saturday");
    if (temp_day_time >= 0)
        time_by_days[6] = temp_day_time;
    else
        Serial.println(F("[UBIDOTS] Reading error!"));

    Serial.println(F("[DATA] Current values:"));
    for (uint8_t i = 0; i < 7; i++)
    {
        Serial.println(time_by_days[i]);
    }
    Serial.println();
}

float ubidots_read(char* variable) {
    Serial.print(F("[UBIDOTS] Connecting to ubidots to get "));
    Serial.print(variable);
    Serial.println(F("..."));
    client_ubi.connect(UBIDOTS_SERVER, UBIDOTS_PORT);
    if (client_ubi.connected()) {
        Serial.println(F("[UBIDOTS] Connected"));
        // Send GET request
        client_ubi.print(F("GET /api/v1.6/devices/"));
        client_ubi.print(DEVICE_LABEL);
        client_ubi.print(F("/"));
        client_ubi.print(variable);
        client_ubi.print(F("/lv"));
        client_ubi.print(F(" HTTP/1.1\r\n"));
        client_ubi.print(F("Host: "));
        client_ubi.print(UBIDOTS_SERVER);
        client_ubi.print(F("\r\n"));
        client_ubi.print(F("User-Agent: "));
        client_ubi.print(USER_AGENT);
        client_ubi.print(F("/"));
        client_ubi.print(VERSION);
        client_ubi.print(F("\r\nX-Auth-Token: "));
        client_ubi.print(UBIDOTS_TOKEN);
        client_ubi.print(F("\r\nConnection: close\r\n"));
        client_ubi.print(F("Content-Type: application/json\r\n\r\n"));
        

        Serial.println(F("[UBIDOTS] GET sended."));
        char value_str[5];
        float value = -1;
        start_time = millis();
        while (client_ubi.connected()) {
            if (client_ubi.read() == '\r'
                && client_ubi.read() == '\n'
                && client_ubi.read() == '\r'
                && client_ubi.read() == '\n') {
                client_ubi.read();
                client_ubi.read();
                client_ubi.read();
                for (uint8_t i = 0; i < 5; i++)
                    value_str[i] = client_ubi.read();
                value = atof(value_str);
                
                break;
            }
            if (millis() - start_time >= 4000)
                break;
        }
        if (value >= 0) {
            Serial.print(F("[UBIDOTS] Server response: "));
            Serial.print(value);
            Serial.println(F("."));
            ubidots_error = 0;
        }
        else {
            Serial.print(F("[UBIDOTS] Error getting data!"));
            ubidots_error = 1;
        }

        client_ubi.stop();
        return value;
    }
    else {
        Serial.println(F("[UBIDOTS] Connection failed!"));
        ubidots_error = 1;
        return -1;
    }
}

void ubidots_send(void) {
    //Forming payload
    manual_run = 0;
    dtostrf(current_state, 4, 2, current_state_str);
    dtostrf(manual_run, 4, 2, manual_run_str);

    Serial.print(F("[UBIDOTS] Payload: "));
    sprintf(payload, "{\"");
    sprintf(payload, "%s%s\":%s", payload, "moisture_0", moisture_str[0]);
    sprintf(payload, "%s,\"%s\":%s", payload, "moisture_1", moisture_str[1]);
    sprintf(payload, "%s,\"%s\":%s", payload, "moisture_2", moisture_str[2]);
    sprintf(payload, "%s,\"%s\":%s", payload, "current_state", current_state_str);
    sprintf(payload, "%s,\"%s\":%s", payload, "manual_run", manual_run_str);
    sprintf(payload, "%s}", payload);
    Serial.println(payload);
    
    Serial.println(F("[UBIDOTS] Connecting to ubidots..."));
    client_ubi.connect(UBIDOTS_SERVER, UBIDOTS_PORT);
    if (client_ubi.connected()) {
        Serial.println(F("[UBIDOTS] Connected"));
        // Send POST request
        client_ubi.print(F("POST /api/v1.6/devices/"));
        client_ubi.print(DEVICE_LABEL);
        client_ubi.print(F("?force=true HTTP/1.1\r\n"));
        client_ubi.print(F("Host: things.ubidots.com\r\n"));
        client_ubi.print(F("User-Agent: "));
        client_ubi.print(USER_AGENT);
        client_ubi.print(F("/"));
        client_ubi.print(VERSION);
        client_ubi.print(F("\r\nX-Auth-Token: "));
        client_ubi.print(UBIDOTS_TOKEN);
        client_ubi.print(F("\r\nConnection: close\r\n"));
        client_ubi.print(F("Content-Type: application/json\r\n"));
        client_ubi.print(F("Content-Length: "));
        client_ubi.print(strlen(payload));
        client_ubi.print(F("\r\n\r\n"));
        client_ubi.print(payload);
        client_ubi.print(F("\r\n"));

        Serial.println(F("[UBIDOTS] POST sended."));
        Serial.print(F("[UBIDOTS] Server response: "));
        start_time = millis();
        while (client_ubi.available()) {
            Serial.print(client_ubi.read());
            if (millis() - start_time >= 4000) {
                Serial.print(F("Error reading responce"));
                ubidots_error = 1;
                break;
            }
        }
        Serial.println(F(" ."));
        ubidots_error = 0;
        client_ubi.stop();
    }
    else {
        Serial.println(F("[UBIDOTS] Connection failed!"));
        ubidots_error = 1;
    }
}


void time_updater(void) {
    if (millis() - ntp_update_timer >= NTP_SYNC_INTERVAL && WiFi.status() == WL_CONNECTED) {
        current_time = time(nullptr);
        local_time = localtime(&current_time);
        Serial.print(F("[TIME] Current time: "));
        Serial.println(ctime(&current_time));
        ntp_update_timer = millis();
    }
}

void iot_connect(void) {
    // WiFi setup
    Serial.println(F("[WIFI] Connecting to wifi..."));
    WiFi.mode(WIFI_STA);
    delay(100);
    WiFi.begin(SSID, PASS);
    start_time = millis();
    while (WiFi.status() != WL_CONNECTED) {
        digitalWrite(LED_PIN, 0);
        delay(100);
        digitalWrite(LED_PIN, 1);
        delay(100);
        if (millis() - start_time > 6000) {
            Serial.println(F("[WIFI] Wifi connection failed!"));
            break;
        }
    }
    WiFi.setAutoReconnect(true);

    // Time setup
    delay(1000);
    if (WiFi.status() == WL_CONNECTED) {
        Serial.print(F("[WIFI] Local IP: "));
        Serial.println(WiFi.localIP().toString());
        Serial.println(F("[TIME] Configurating time over NTP..."));
        configTime(3 * 3600, 0, NTP_SERVERS);
        start_time = millis();
        while (!time(nullptr)) {
            digitalWrite(LED_PIN, 0);
            delay(100);
            digitalWrite(LED_PIN, 1);
            delay(100);
            if (millis() - start_time > 6000) {
                Serial.println(F("[TIME] Time configurating failed!"));
                break;
            }
        }
        delay(100);
        current_time = time(nullptr);
        local_time = localtime(&current_time);
    }

    //Client setup
    client_ubi.setTimeout(5000);
    Serial.println(F("[WIFI] WL config done."));
}

float mapfloat(float x, float in_min, float in_max, float out_min, float out_max)
{
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}