#include <Arduino.h>
#include <ModbusRTUMaster.h>
#include <SoftwareSerial.h>

// #define ESPNOW_PERIPH
// #define DEBUG

#ifdef ESPNOW_PERIPH
#define ESP_NOW_I2C_ADDR 0x01
#define MAX_NAME_LEN 20
#define MAX_DEVICES 10
#endif

#define DE_PIN 9 
#define NUM_OF_SLAVES 1
#define MODBUS_SPEED 38400

uint8_t fancoil_addresses[NUM_OF_SLAVES]; //  = {0x01, 0x02, 0x03, 0x04, 0x05};

ModbusRTUMaster modbus(Serial,DE_PIN);
SoftwareSerial mySerial(2, 3); // RX, TX

#ifdef ESPNOW_PERIPH

// 20 bytes
typedef struct {
  uint8_t mac[6];           // 6 bytes
  float temperature;        // 4 bytes
  float pressure;           // 4 bytes        
  float humidity;           // 4 bytes
  uint8_t hallState;        // 1 byte
  uint8_t alive;            // 1 byte
} __attribute__((packed)) esp_now_device_t;
#endif

// 4 * 4 bytes = 16 bytes
typedef struct{
  float coldInputWater;
  float hotInputWater;
  float coldOutputWater;
  float hotOutputWater;
}__attribute__((packed)) tube_sensors_t;

// 4 * 4 bytes = 16 bytes
typedef struct{
  float airVentInputTemp;
  float airVentOutputTemp;
  float airVentInputHumidity;
  float airVentOutputHumidity;
}__attribute__((packed)) airvent_sensors_t;

// 243 bytes
typedef struct {

    // Read only data
#ifdef ESPNOW_PERIPH
  esp_now_device_t devices[MAX_DEVICES];  // 10 * 20 bytes = 200 bytes
#endif
  tube_sensors_t tube_sensors;        // 16 bytes
  airvent_sensors_t airvent_sensors;  // 16 bytes
  float fanSpeed;                     // 4 bytes
  uint8_t digitalInputs[2];           // 2 bytes
  
  // Read/Write data
  uint8_t address;    
  uint8_t digitalOutputs[4];          // 4 bytes
  uint8_t LEDSTATE;                   // 1 byte
} __attribute__((packed)) fancoil_module_t;

typedef union{
  fancoil_module_t strct;
  uint16_t holdingRegisters[sizeof(fancoil_module_t)/2];
} fancoil_module_union_t;

// void readSlaveData(fancoil_module_t *fancoil);
// void writeSlaveData(fancoil_module_t *fancoil);
// void printFancoilData(fancoil_module_t *fancoil);
bool debug(bool modbusRequest);

int main(){
    init();
    
    mySerial.begin(9600);
    modbus.begin(MODBUS_SPEED);

    fancoil_module_union_t fancoil[NUM_OF_SLAVES];

    mySerial.println("Size of fancoil_module_t: ");
    mySerial.println(sizeof(fancoil_module_t));

    mySerial.println("Size of fancoil_union_module_t: ");
    mySerial.println(sizeof(fancoil_module_union_t));

    mySerial.println("Size off fancoil memebers:");
    mySerial.println(sizeof(fancoil[0].strct.tube_sensors));
    mySerial.println(sizeof(fancoil[0].strct.airvent_sensors));
    mySerial.println(sizeof(fancoil[0].strct.fanSpeed));
    mySerial.println(sizeof(fancoil[0].strct.digitalInputs));
    mySerial.println(sizeof(fancoil[0].strct.digitalOutputs));
    mySerial.println(sizeof(fancoil[0].strct.LEDSTATE));
    mySerial.println(sizeof(fancoil[0].strct.address));

    // Calculate offset to the Read/Write data 
    int offsetAddr = offsetof(fancoil_module_t, address);
    mySerial.println("Offset of address: ");
    mySerial.println(offsetAddr);


    while(true){
        // Read and write slave data 
        
        // Print fancoil data
        for(int i = 0; i < NUM_OF_SLAVES; i++){

            // Read inputs and log data
            readSlaveData(&fancoil[i].strct,fancoil[i].holdingRegisters,0,offsetAddr-1);
            // printFancoilData(&fancoil[i]);

            // Do something with data
            fancoil[0].strct.LEDSTATE = !fancoil[0].strct.LEDSTATE;

            // Write to outputs
            writeSlaveData(&fancoil[i].strct,fancoil[i].holdingRegisters,offsetAddr,sizeof(fancoil_module_t) - offsetAddr - 1);
        }

        delay(2000);
    }

    return 0;
}

void readSlaveData(fancoil_module_t *fancoil,uint16_t *data,int startAddr, int endAddr){

    debug(modbus.readHoldingRegisters(fancoil->address, startAddr, endAddr, data));
}

void writeSlaveData(fancoil_module_t *fancoil,uint16_t *data,int startAddr, int endAddr){
    debug(modbus.writeMultipleHoldingRegisters(fancoil->address, startAddr,&data[offsetof(fancoil_module_t, address) ]),endAddr); // Help im a little bit lost here :( 
}

void printFancoilData(fancoil_module_t *fancoil){
    mySerial.print("Cold Input Water: ");
    mySerial.println(fancoil->tube_sensors.coldInputWater);

    mySerial.print("Hot Input Water: ");
    mySerial.println(fancoil->tube_sensors.hotInputWater);

    mySerial.print("Cold Output Water: ");
    mySerial.println(fancoil->tube_sensors.coldOutputWater);

    mySerial.print("Hot Output Water: ");
    mySerial.println(fancoil->tube_sensors.hotOutputWater);

    mySerial.print("Air Vent Input Temp: ");
    mySerial.println(fancoil->airvent_sensors.airVentInputTemp);

    mySerial.print("Air Vent Output Temp: ");
    mySerial.println(fancoil->airvent_sensors.airVentOutputTemp);

    mySerial.print("Air Vent Input Humidity: ");
    mySerial.println(fancoil->airvent_sensors.airVentInputHumidity);

    mySerial.print("Air Vent Output Humidity: ");
    mySerial.println(fancoil->airvent_sensors.airVentOutputHumidity);

    mySerial.print("Fan Speed: ");
    mySerial.println(fancoil->fanSpeed);

    mySerial.print("Digital Inputs: ");
    for(int i = 0; i < 2; i++){
        mySerial.print(fancoil->digitalInputs[i]);
        mySerial.print(" ");
    }
    mySerial.println();

    mySerial.print("Digital Outputs: ");
    for(int i = 0; i < 4; i++){
        mySerial.print(fancoil->digitalOutputs[i]);
        mySerial.print(" ");
    }
    mySerial.println();

    mySerial.print("LED State: ");
    mySerial.println(fancoil->LEDSTATE);

    mySerial.print("Address: ");
    mySerial.println(fancoil->address);
}

bool debug(bool modbusRequest) {
  if (modbusRequest == true) {
    mySerial.println("Success");
  } else {
    mySerial.print("Failure");
    if (modbus.getTimeoutFlag() == true) {
      mySerial.print(": Timeout");
      modbus.clearTimeoutFlag();
    } else if (modbus.getExceptionResponse() != 0) {
      mySerial.print(": Exception Response ");
      mySerial.print(modbus.getExceptionResponse());
      switch (modbus.getExceptionResponse()) {
      case 1:
        mySerial.print(" (Illegal Function)");
        break;
      case 2:
        mySerial.print(" (Illegal Data Address)");
        break;
      case 3:
        mySerial.print(" (Illegal Data Value)");
        break;
      case 4:
        mySerial.print(" (Server Device Failure)");
        break;
      default:
        mySerial.print(" (Uncommon Exception Response)");
        break;
      }
      modbus.clearExceptionResponse();
    }
    mySerial.println();
  }
  mySerial.flush();
  return modbusRequest;
}
