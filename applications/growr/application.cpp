#include "application.h"
#include "lib/Adafruit_DHT_Library/firmware/Adafruit_DHT.h"
#include "lib/OneWireSpark/firmware/OneWire.h"
#include "lib/SparkCoreDallasTemperature/firmware/spark-dallas-temperature.h"
#include "lib/NtpClient/NtpClient.h"

SYSTEM_MODE(SEMI_AUTOMATIC);

// PIN assignments
#define DHTINTERNAL_PIN       D6
#define DHTEXTERNAL_PIN       D0
#define RESSERVOIR_TEMP_PIN   D2
#define RANGE_TUBE_TRIGGER    D3
#define RANGE_TUBE_ECHO       D4
#define RANGE_TANK_TRIGGER    D5
#define RANGE_TANK_ECHO       D1
#define RELAY_PUMP_PIN        A0
#define RELAY_MAIN_LIGHT1_PIN A1
#define RELAY_MAIN_LIGHT2_PIN A2
#define RELAY_GROW_LIGHT_PIN  A3
#define STATUS_LED_PIN        D7

extern byte server[];
extern char post_path[];

// Hour and minute values for lighting timer (UTC)
// {'hour_on', 'minute_on', 'run_for_hours', run_for_minutes}
int lights_timing[] = {6,0,16,0};

bool main_lights_on;
bool grow_lights_on;

// Pump timings - {'minutes on', 'minutes off'}
int pump_timing_light_on[] = {1,10};
int pump_timing_light_off[] = {1,20};

bool pump_on;
int pump_cycle_seconds = 0;

int minute = 0; // The current/last saved minute of the hour

#define DHTTYPE DHT22	// DHT 22 (AM2302)
// Connect pin 1 (on the left) of the sensor to +5V
// Connect pin 2 of the sensor to whatever your DHTPIN is
// Connect pin 4 (on the right) of the sensor to GROUND
// Connect a 10K resistor from pin 2 (data) to pin 1 (power) of the sensor
DHT dht_internal(DHTINTERNAL_PIN, DHTTYPE);
DHT dht_external(DHTEXTERNAL_PIN, DHTTYPE);

const unsigned int ntpPort = 123;
const char ntpServer[13] = "pool.ntp.org";

/*
* pulseIn Function for the Spark Core - Version 0.1.1 (Beta)
* Copyright (2014) Timothy Brown - See: LICENSE
*
* Due to the current timeout issues with Spark Cloud
* this will return after 10 seconds, even if the
* input pulse hasn't finished.
*
* Input: Trigger Pin, Trigger State
* Output: Pulse Length in Microseconds (10uS to 10S)
*
*/
unsigned long pulseIn(uint16_t pin, uint8_t state) {
  GPIO_TypeDef* portMask = (PIN_MAP[pin].gpio_peripheral); // Cache the target's peripheral mask to speed up the loops.
  uint16_t pinMask = (PIN_MAP[pin].gpio_pin); // Cache the target's GPIO pin mask to speed up the loops.
  unsigned long pulseCount = 0; // Initialize the pulseCount variable now to save time.
  unsigned long loopCount = 0; // Initialize the loopCount variable now to save time.
  unsigned long loopMax = 20000000; // Roughly just under 10 seconds timeout to maintain the Spark Cloud connection.
  // Wait for the pin to enter target state while keeping track of the timeout.
  while (GPIO_ReadInputDataBit(portMask, pinMask) != state) {
    if (loopCount++ == loopMax) {
      return 0;
    }
  }
  // Iterate the pulseCount variable each time through the loop to measure the pulse length; we also still keep track of the timeout.
  while (GPIO_ReadInputDataBit(portMask, pinMask) == state) {
    if (loopCount++ == loopMax) {
      return 0;
    }
    pulseCount++;
  }
  // Return the pulse time in microseconds by multiplying the pulseCount variable with the time it takes to run once through the loop.
  return pulseCount * 0.405; // Calculated the pulseCount++ loop to be about 0.405uS in length.
}

float getRange(int trigPin, int echoPin) {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);

  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);

  digitalWrite(trigPin, LOW);
  long duration = pulseIn(echoPin, HIGH);

  // Calculate the distance (in cm) based on the speed of sound.
  float distance = duration/58.2;

  // Delay 50ms before next reading.
  delay(50);

  return distance;
}

void lights() {
  int time_on;
  int time_off;
  struct tm start_info;

  start_info.tm_year = Time.year() - 1900;
  start_info.tm_mon = Time.month() - 1;
  start_info.tm_mday = Time.day();
  start_info.tm_hour = lights_timing[0];
  start_info.tm_min = lights_timing[1];
  start_info.tm_sec = 0;
  start_info.tm_isdst = 0;

  time_on = mktime(&start_info);
  if(time_on != -1)
  {
    time_off = time_on + (lights_timing[2] * 3600) + (lights_timing[3] * 60);

    // Grow lights on/off handling
    // Start 1 hour after main lights
    // Stops 1 hour before main lights
    if((time_on + 3600) <= Time.now() && (time_off - 3600) >= Time.now())
    {
      digitalWrite(RELAY_GROW_LIGHT_PIN, HIGH);
      grow_lights_on = true;
    }
    else
    {
      digitalWrite(RELAY_GROW_LIGHT_PIN, LOW);
      grow_lights_on = false;
    }

    // Main lights on/off handling
    if(time_on <= Time.now() && time_off >= Time.now())
    {
      main_lights_on = true;
      digitalWrite(RELAY_MAIN_LIGHT1_PIN, HIGH);
      digitalWrite(RELAY_MAIN_LIGHT2_PIN, HIGH);
    }
    else
    {
      main_lights_on = false;
      digitalWrite(RELAY_MAIN_LIGHT1_PIN, LOW);
      digitalWrite(RELAY_MAIN_LIGHT2_PIN, LOW);
    }
  }
}

void pump() {
  // Pump on/off handling
  int *pump_timing;
  if (main_lights_on) {
    pump_timing = pump_timing_light_on;
  } else {
    pump_timing = pump_timing_light_off;
  }

  if (pump_on && ((pump_timing[0] * 60) <= pump_cycle_seconds)) {
    // Turn pump off
    pump_on = false;
    digitalWrite(RELAY_PUMP_PIN, LOW);
    pump_cycle_seconds = 0;
  } else if (!pump_on && ((pump_timing[1] * 60) <= pump_cycle_seconds)) {
    // Turn pump on
    pump_on = true;
    digitalWrite(RELAY_PUMP_PIN, HIGH);
    pump_cycle_seconds = 0;
  }

  pump_cycle_seconds = pump_cycle_seconds + 60;
}

void logSensorData(bool transmit = true) {
  char payload[400];
  char logvalue[30];
  
  float h; // humidity
  float t; // temperature

  strcpy(payload, "{");

  snprintf(logvalue, sizeof logvalue, "%i-%.2i-%.2i %.2i:%.2i:%.2i UTC", Time.year(), Time.month(), Time.day(), Time.hour(), Time.minute(), Time.second());
  
  strcat(payload, "\"timestamp\": \"");
  strcat(payload, logvalue);

  h = dht_internal.getHumidity();
  t = dht_internal.getTempCelcius();

  strcat(payload, "\", \"temperature_internal\": \"");
  sprintf(logvalue, "%.2f", t);
  strcat(payload, logvalue);
  strcat(payload, "\", \"humidity_internal\": \"");
  sprintf(logvalue, "%.2f", h);
  strcat(payload, logvalue);

  // Log external temperature and humidity
  h = dht_external.getHumidity();
  t = dht_external.getTempCelcius();

  strcat(payload, "\", \"temperature_external\": \"");
  sprintf(logvalue, "%.2f", t);
  strcat(payload, logvalue);
  strcat(payload, "\", \"humidity_external\": \"");
  sprintf(logvalue, "%.2f", h);
  strcat(payload, logvalue);

  // Resservoir temperature
  OneWire oneWire(RESSERVOIR_TEMP_PIN);
  // Pass our oneWire reference to Dallas Temperature. 
  DallasTemperature sensors(&oneWire);
  sensors.begin();
  // call sensors.requestTemperatures() to issue a global temperature 
  // request to all devices on the bus
  sensors.requestTemperatures();
  t = sensors.getTempCByIndex(0);
  strcat(payload, "\", \"temperature_resservoir\": \"");
  sprintf(logvalue, "%.2f", t);
  strcat(payload, logvalue);

  // Log Tube water level
  float r; // range
  char range[10];
  r = getRange(RANGE_TUBE_TRIGGER, RANGE_TUBE_ECHO);
  sprintf(range, "%.2f", r);
  strcat(payload, "\", \"water_table_tube\": \"");
  strcat(payload, range);
  
  // Log resservoir water table
  r = getRange(RANGE_TUBE_TRIGGER, RANGE_TANK_ECHO  );
  sprintf(range, "%.2f", r);
  strcat(payload, "\", \"water_table_resservoir\": \"");
  strcat(payload, range);

  // Log light on/off status
  char val[1];
  sprintf(val, "%i", main_lights_on);
  strcat(payload, "\", \"main_lights_on\": \"");
  strcat(payload, val);

  sprintf(val, "%i", grow_lights_on);
  strcat(payload, "\", \"grow_lights_on\": \"");
  strcat(payload, val);

  sprintf(val, "%i", pump_on);
  strcat(payload, "\", \"pump_on\": \"");
  strcat(payload, val);

  /*unsigned long time_since_reset = millis();
  int uptime_sec = time_since_reset / 100;
  char uptime[100];
  sprintf(uptime, "%i", uptime_sec);
  strcat(payload, "\", \"uptime_sec\": \"");
  strcat(payload, uptime);*/

  strcat(payload, "\"}");

  TCPClient client;
  if (transmit) {
    if(client.connect(server, 80)) {
      char header[32];
      sprintf(header, "POST %s HTTP/1.0", post_path);
      client.println(header);
      //client.println("Host: www.google.com");
      //client.println("Content-Type: text/json; charset=utf-8");
      client.println("User-Agent: Growr/1.0");
      client.print("Content-Length: ");
      client.println(strlen(payload));
      client.println();
      client.println(payload);

      client.println("Connection: close");
      client.println();
      client.stop();
    } else {
      Serial.println("Connection failed.");
    }
  }
  return;
}

int round_hour(int hour) {
  if (hour >= 24) {
    return (hour - 24);
  } else if (hour < 0) {
    return (hour + 24);
  }
  return hour;
}

NtpTime NtpTime;

void setup() {
  // Set Relay pins
  pinMode(RELAY_PUMP_PIN, OUTPUT);
  digitalWrite(RELAY_PUMP_PIN, LOW);
  pinMode(RELAY_MAIN_LIGHT1_PIN, OUTPUT);
  digitalWrite(RELAY_MAIN_LIGHT1_PIN, LOW);
  pinMode(RELAY_MAIN_LIGHT2_PIN, OUTPUT);
  digitalWrite(RELAY_MAIN_LIGHT2_PIN, LOW);
  pinMode(RELAY_GROW_LIGHT_PIN, OUTPUT);
  digitalWrite(RELAY_GROW_LIGHT_PIN, LOW);

  pinMode(STATUS_LED_PIN, OUTPUT);
  digitalWrite(STATUS_LED_PIN, LOW);

  // Set Range pins
  pinMode(RANGE_TUBE_TRIGGER, OUTPUT);
  pinMode(RANGE_TUBE_ECHO, INPUT);
  //pinMode(RANGE_TANK_TRIGGER, OUTPUT);
  //pinMode(RANGE_TANK_ECHO, INPUT);

  dht_internal.begin();
  dht_external.begin();
  
  Serial.begin(9600);
  WiFi.connect();
  bool timeIsSynced = false;
  while (!timeIsSynced) {
    timeIsSynced = NtpTime.setTime(ntpServer, ntpPort);
    if(!timeIsSynced) {
      delay(1000);
    }
  }
  
  minute = Time.minute();
  logSensorData(false);
  delay(500);
}

void loop() {
  // Status LED 
  digitalWrite(STATUS_LED_PIN, LOW);
  delay(500);
  digitalWrite(STATUS_LED_PIN, HIGH);
  delay(500);
    
  if (minute != Time.minute()) {
    minute = Time.minute();

    lights();
    pump();
    logSensorData();
  }
  
  // Sync time once a day
  if((Time.minute() == 22) && (Time.hour() == 22)) {
    NtpTime.setTime(ntpServer, ntpPort);
    // Make sure we roll of the current minute before proceeding
    while(Time.minute() == 22) {
      delay(100);
    }
  }
}
