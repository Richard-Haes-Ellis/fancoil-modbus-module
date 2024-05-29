#include <Arduino.h>
#include <DHT.h>
#include <DallasTemperature.h>
#include <ModbusRTUSlave.h>
#include <OneWire.h>
#include <SoftwareSerial.h>
#include <Wire.h>
// #include <Adafruit_Sensor.h>
// #include <Adafruit_BME280.h>

// #define ESPNOW_PERIPH
// #define DEBUG

#ifdef ESPNOW_PERIPH            // If this is a peripheral device
#define ESP_NOW_I2C_ADDR 0x01   // I2C address of the ESP-NOW master
#define MAX_NAME_LEN 20         // Maximum length of the device name
#define MAX_DEVICES 10          // Maximum number of devices that can be connected to the master
#endif

#define DE_PIN 9                // Pin for DE/RE of RS485
#define SLAVE_ID 0x01           // Modbus slave ID
#define MODBUS_SPEED 38400      // Modbus speed

// Pin definitions as per squematic
#define DS18B20_PIN 8

#define DHT22_PIN_1 7
#define DHT22_PIN_2 6

#define ANALOG_IO_1 A0
#define ANALOG_IO_2 A1
#define ANALOG_IO_3 A2
#define ANALOG_IO_4 A3

#define ANALOG_IO_6 A6
#define ANALOG_IO_7 A7

// #define BME280_ADDR_1 0x76
// #define BME280_ADDR_2 0x77

#ifndef DEBUG
#define DIGITAL_INPUT_1 3 // Digital input 3 coincides with TX pin of SoftwareSerial
#endif
#define DIGITAL_INPUT_2 4

#define HALL_SENSOR_PIN 2

#define DIGITAL_OUTPUT_1 5
#define DIGITAL_OUTPUT_2 10
#define DIGITAL_OUTPUT_3 11
#define DIGITAL_OUTPUT_4 12

ModbusRTUSlave modbus(Serial, DE_PIN);

#ifdef DEBUG
SoftwareSerial mySerial(2, 3); // RX, TX
#endif

OneWire oneWire(DS18B20_PIN);
DallasTemperature ds18b20(&oneWire);

DHT dht1(DHT22_PIN_1, DHT22);
DHT dht2(DHT22_PIN_2, DHT22);

#ifdef ESPNOW_PERIPH

typedef struct{
  uint16_t temp;
  uint16_t hum;
  uint16_t pres;
}__attribute__((packed)) esp_now_bme280_data_t;

typedef struct{
  uint8_t mac[6];
  esp_now_bme280_data_t bme_data;
}__attribute__((packed)) esp_now_hr_t;

typedef struct{
  bool hallState;
  bool alive;
}__attribute__((packed)) esp_now_status_t;

#endif

// 4 * 4 bytes = 16 bytes
typedef struct{
  uint16_t coldInputWater;
  uint16_t hotInputWater;
  uint16_t coldOutputWater;
  uint16_t hotOutputWater;
}__attribute__((packed)) tube_sensors_t;

// 4 * 4 bytes = 16 bytes
typedef struct{
  uint16_t airVentInputTemp;
  uint16_t airVentOutputTemp;
  uint16_t airVentInputHumidity;
  uint16_t airVentOutputHumidity;
}__attribute__((packed)) airvent_sensors_t;

typedef struct {
  // Read only data
  tube_sensors_t tube_sensors;        
  airvent_sensors_t airvent_sensors;  
  uint16_t fanSpeed;                                   
  uint16_t nodes_alive;           
  #ifdef ESPNOW_PERIPH
  esp_now_hr_t esp_hr[MAX_DEVICES];
  #endif
} __attribute__((packed)) fancoil_holdingRegisters_t;

typedef struct {
  bool digitalInputs[2];           
  #ifdef ESPNOW_PERIPH
  esp_now_status_t device_status[MAX_DEVICES];  
  #endif

} __attribute__((packed)) fancoil_discreteInputs_t;

typedef struct{
  bool LED_STATE;
  bool digitalOutputs[4];
}__attribute__((packed)) fancoil_coils_t;

typedef union{
  fancoil_holdingRegisters_t members;
  uint16_t holdingRegisters[sizeof(fancoil_holdingRegisters_t)/2];
} fancoil_holdingRegisters_union_t;

typedef union{
  fancoil_discreteInputs_t members;
  bool discreteInputs[sizeof(fancoil_discreteInputs_t)/2];
} fancoil_holding_discreteInputs_union_t;

typedef union{
  fancoil_coils_t members;
  bool coils[sizeof(fancoil_coils_t)/2];
} fancoil_holding_coils_union_t;

volatile unsigned long lastHallTime = 0;
volatile unsigned long rpm = 0;

void hallSensorInterrupt();

int main() {
  init();

#ifdef DEBUG
  mySerial.begin(9600);
#endif
  // Initialize pins
  pinMode(LED_BUILTIN, OUTPUT);
#ifndef DEBUG
  pinMode(DIGITAL_INPUT_1, INPUT_PULLUP);
#endif
  pinMode(DIGITAL_INPUT_2, INPUT_PULLUP);
  pinMode(DIGITAL_OUTPUT_1, OUTPUT);
  pinMode(DIGITAL_OUTPUT_2, OUTPUT);
  pinMode(DIGITAL_OUTPUT_3, OUTPUT);
  pinMode(DIGITAL_OUTPUT_4, OUTPUT);
  pinMode(HALL_SENSOR_PIN, INPUT);

  attachInterrupt(digitalPinToInterrupt(HALL_SENSOR_PIN), hallSensorInterrupt,
                  FALLING);


  // Initialize sensors
  ds18b20.begin();
  dht1.begin();
  dht2.begin();

  fancoil_holdingRegisters_union_t fancoil_holdingRegisters;
  fancoil_holding_discreteInputs_union_t fancoil_discreteInputs;
  fancoil_holding_coils_union_t fancoil_coils;

  modbus.configureHoldingRegisters(fancoil_holdingRegisters.holdingRegisters, sizeof(fancoil_holdingRegisters_t));
  modbus.configureDiscreteInputs(fancoil_discreteInputs.discreteInputs, sizeof(fancoil_discreteInputs_t));
  modbus.configureCoils(fancoil_coils.coils, sizeof(fancoil_coils_t));

  modbus.begin(SLAVE_ID, MODBUS_SPEED);

  while (true) {
    modbus.poll();

    // Read sensors //
    ds18b20.requestTemperatures();
    float temp[4];
    for (int i = 0; i < 4; i++) {
      temp[i] = ds18b20.getTempCByIndex(i);
    }

    fancoil_holdingRegisters.members.tube_sensors.coldOutputWater = (uint16_t)(temp[0]*100.0);
    fancoil_holdingRegisters.members.tube_sensors.coldInputWater  = (uint16_t)(temp[1]*100.0);
    fancoil_holdingRegisters.members.tube_sensors.hotInputWater   = (uint16_t)(temp[2]*100.0);
    fancoil_holdingRegisters.members.tube_sensors.hotOutputWater  = (uint16_t)(temp[3]*100.0);

    // Read for holding registers
    fancoil_holdingRegisters.members.airvent_sensors.airVentInputTemp       = (uint16_t)(100.0*dht1.readTemperature());
    fancoil_holdingRegisters.members.airvent_sensors.airVentInputHumidity   = (uint16_t)(100.0*dht1.readHumidity());
    fancoil_holdingRegisters.members.airvent_sensors.airVentOutputTemp      = (uint16_t)(100.0*dht2.readTemperature());
    fancoil_holdingRegisters.members.airvent_sensors.airVentOutputHumidity  = (uint16_t)(100.0*dht2.readHumidity());
    
    // Read inputs
#ifndef DEBUG
    fancoil_discreteInputs.members.digitalInputs[0] = (digitalRead(DIGITAL_INPUT_1) == LOW ? 0 : 1);
#endif
    fancoil_discreteInputs.members.digitalInputs[1] = (digitalRead(DIGITAL_INPUT_1) == LOW ? 0 : 1);

    // Update Outputs
    digitalWrite(LED_BUILTIN,       fancoil_coils.members.LED_STATE);
    digitalWrite(DIGITAL_OUTPUT_1,  fancoil_coils.members.digitalOutputs[0]);
    digitalWrite(DIGITAL_OUTPUT_2,  fancoil_coils.members.digitalOutputs[1]);
    digitalWrite(DIGITAL_OUTPUT_3,  fancoil_coils.members.digitalOutputs[2]);
    digitalWrite(DIGITAL_OUTPUT_4,  fancoil_coils.members.digitalOutputs[3]);

    // Update fan speed
    fancoil_holdingRegisters.members.fanSpeed = (uint16_t)(rpm*100.0);
    fancoil_holdingRegisters.members.nodes_alive = 1;

    delay(100);
  }
}

void hallSensorInterrupt() {
  unsigned long currentTime = millis();
  rpm = 60000 / (currentTime - lastHallTime); // calculate RPM
  lastHallTime = currentTime;
}