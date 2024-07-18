#include <Arduino.h>
#include <DHT.h>
#include <DallasTemperature.h>
#include <ModbusRTUSlave.h>
#include <OneWire.h>
#include <SoftwareSerial.h>
#include <Wire.h>


// #define ESPNOW_PERIPH
// #define DEBUG
// #define HALL_ENABLE_WHILE_DEBUG // Disables hall to be used as RX pin for SoftwareSerial

#ifdef ESPNOW_PERIPH            // If this is a peripheral device
#define ESP_NOW_I2C_ADDR 0x03   // I2C address of the ESP-NOW master
#define MAX_NAME_LEN 20         // Maximum length of the device name
#define MAX_DEVICES 10          // Maximum number of devices that can be connected to the master
#endif

#define DE_PIN 9                // Pin for DE/RE of RS485
#define SLAVE_ID 0x01           // Modbus slave ID
#define MODBUS_SPEED 115200     // Modbus speed

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

#define DIGITAL_INPUT_1 3 // Digital input 3 coincides with TX pin of SoftwareSerial
#define DIGITAL_INPUT_2 4

#define HALL_SENSOR_PIN 2

#define DIGITAL_OUTPUT_1 5
#define DIGITAL_OUTPUT_2 10
#define DIGITAL_OUTPUT_3 11
#define DIGITAL_OUTPUT_4 12

ModbusRTUSlave modbus(Serial, DE_PIN);

#ifdef DEBUG
  #ifdef HALL_ENABLE_WHILE_DEBUG
    #define RX_PIN DIGITAL_OUTPUT_1

  #else
    #define RX_PIN HALL_SENSOR_PIN

  #endif
  
  #define TX_PIN DIGITAL_INPUT_1
  SoftwareSerial mySerial(RX_PIN, TX_PIN); // RX, TX

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
  // #ifdef ESPNOW_PERIPH
  // esp_now_hr_t esp_hr[MAX_DEVICES];
  // #endif
} __attribute__((packed)) fancoil_holdingRegisters_t;

typedef struct {
  bool digitalInputs[2];           
  // #ifdef ESPNOW_PERIPH
  // esp_now_status_t device_status[MAX_DEVICES];  
  // #endif

} __attribute__((packed)) fancoil_discreteInputs_t;

typedef struct{
  bool LED_STATE;
  bool digitalOutputs[4];
}__attribute__((packed)) fancoil_coils_t;

typedef union{
  fancoil_holdingRegisters_t members;
  uint16_t holdingRegisters[10];
} fancoil_holdingRegisters_union_t;

typedef union{
  fancoil_discreteInputs_t members;
  bool discreteInputs[2];
} fancoil_holding_discreteInputs_union_t;

typedef union{
  fancoil_coils_t members;
  bool coils[5];
} fancoil_holding_coils_union_t;

volatile int rpm = 0;
volatile int lastCount = 0;
volatile int count = 0;

void hallSensorInterrupt();

int main() {
  init();

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

  #ifdef DEBUG
  mySerial.begin(115200);
  // mySerial.println("Fan coil module started");
  #endif

  attachInterrupt(digitalPinToInterrupt(HALL_SENSOR_PIN), hallSensorInterrupt, FALLING);


  // Initialize sensors
  ds18b20.begin();
  dht1.begin();
  dht2.begin();

  fancoil_holdingRegisters_union_t fancoil_holdingRegisters;
  fancoil_holding_discreteInputs_union_t fancoil_discreteInputs;
  fancoil_holding_coils_union_t fancoil_coils;

  modbus.configureHoldingRegisters(fancoil_holdingRegisters.holdingRegisters, 10);
  modbus.configureDiscreteInputs(fancoil_discreteInputs.discreteInputs, 2);
  modbus.configureCoils(fancoil_coils.coils, 5);
  modbus.begin(SLAVE_ID, MODBUS_SPEED);

  // Set up timers
  long int timer1 = 0;
  long int timer2 = 0;

  // rpm 
  float rpm = 123.45;
  float prevRpm = 0.0;

  while (true) {
    modbus.poll();

    // Run every second
    if(millis() - timer1 > 100){
      timer1 = millis();
      // Read sensors //
      ds18b20.requestTemperatures();
      float temp[4];
      for (int i = 0; i < 4; i++) {
        temp[i] = ds18b20.getTempCByIndex(i);
        // temp[i] = i*15.5;
      }

      fancoil_holdingRegisters.members.tube_sensors.coldInputWater  = (uint16_t)(temp[0]*100.0);
      fancoil_holdingRegisters.members.tube_sensors.hotInputWater   = (uint16_t)(temp[1]*100.0);
      fancoil_holdingRegisters.members.tube_sensors.coldOutputWater = (uint16_t)(temp[2]*100.0);
      fancoil_holdingRegisters.members.tube_sensors.hotOutputWater  = (uint16_t)(temp[3]*100.0);

      // Check if the sensor is connected
      if (isnan(dht1.readTemperature())){
        fancoil_holdingRegisters.members.airvent_sensors.airVentInputTemp = 0x123F;
      }else{
        fancoil_holdingRegisters.members.airvent_sensors.airVentInputTemp = (uint16_t)(dht1.readTemperature()*100.0);
      }

      if (isnan(dht1.readHumidity())){
        fancoil_holdingRegisters.members.airvent_sensors.airVentOutputTemp = 0x234F;
      }else{
        fancoil_holdingRegisters.members.airvent_sensors.airVentOutputTemp = (uint16_t)(dht1.readHumidity()*100.0);
      }

      if (isnan(dht2.readTemperature())){
        fancoil_holdingRegisters.members.airvent_sensors.airVentInputHumidity = 0x345F;
      }else{
        fancoil_holdingRegisters.members.airvent_sensors.airVentInputHumidity = (uint16_t)(dht2.readTemperature()*100.0);
      }

      if (isnan(dht2.readHumidity())){
        fancoil_holdingRegisters.members.airvent_sensors.airVentOutputHumidity = 0x456F;
      }else{
        fancoil_holdingRegisters.members.airvent_sensors.airVentOutputHumidity = (uint16_t)(dht2.readHumidity()*100.0);
      }
      
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

      fancoil_holdingRegisters.members.fanSpeed = (uint16_t)(rpm*100.0);
      fancoil_holdingRegisters.members.nodes_alive = 888;
    }

    if (millis() - timer2 > 100) {
      timer2 = millis();

      rpm = ((count - lastCount) * 60.0 / .10)*0.2 + 0.8*prevRpm;
      lastCount = count;
      prevRpm = rpm;
    }

    delay(10);
  }
  
}

void hallSensorInterrupt() {
  count += 1;
}