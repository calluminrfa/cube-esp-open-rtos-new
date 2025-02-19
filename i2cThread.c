#include <esp/uart.h>
#include <espressif/esp_common.h>
#include <i2c/i2c.h>
#include <stdio.h>
// TEst
#include <FreeRTOS.h>
#include <burnerControl.h>
#include <esp8266.h>
#include <gui.h>
#include <hd44780/hd44780.h>
#include <i2cThread.h>
#include <inputs.h>
#include <pcf8574/pcf8574.h>
#include <queue.h>
#include <semphr.h>
#include <task.h>

extern SemaphoreHandle_t i2CMutex;
extern QueueHandle_t xGPIOForProcQueue;
extern SemaphoreHandle_t temperatureVarsMutex;
extern SemaphoreHandle_t printSemaphore;

void i2CThread(void *pvParameters) {
  printf("Starting I2C Thread\r\n");
  // Setup various I2C stuff HERE
  TickType_t lastWakeTime;
  const TickType_t xFreq = 10 / portTICK_PERIOD_MS;
  lastWakeTime = xTaskGetTickCount();
  // Setup MCP23016 as inputs for all GP0
  uint8_t devAddr = 0x20;
  uint8_t dataBuffer[4] = {0x06, 0xFF, 0, 0};

  int err = i2c_slave_write(I2C_BUS, devAddr, NULL, &dataBuffer, 2);
  if (err != 0) {
    printf("Could not set the inputs for MCP\r\n");
  }

  static const uint8_t taChar[8] = {0x1c, 0x08, 0x08, 0x08,
                                    0x02, 0x05, 0x07, 0x05};

  static const uint8_t rgChar[8] = {0x1c, 0x14, 0x18, 0x14,
                                    0x03, 0x04, 0x05, 0x03};

  static const uint8_t etChar[8] = {0x1c, 0x10, 0x18, 0x10,
                                    0x1F, 0x02, 0x02, 0x02};

  static const uint8_t onOff[8] = {0x0E, 0x0A, 0x0A, 0x00,
                                   0x1B, 0x12, 0x1B, 0x12};

  hd44780_t lcd = {
      .i2c_dev.bus = I2C_BUS,
      .i2c_dev.addr = HD44780,
      .font = HD44780_FONT_5X8,
      .lines = 2,
      .pins = {.rs = 0, .e = 2, .d4 = 4, .d5 = 5, .d6 = 6, .d7 = 7, .bl = 3},
      .backlight = true};
  // taskENTER_CRITICAL();
  hd44780_init(&lcd);
  hd44780_upload_character(&lcd, 0, taChar);
  hd44780_upload_character(&lcd, 1, rgChar);
  hd44780_upload_character(&lcd, 2, etChar);
  hd44780_upload_character(&lcd, 3, onOff);
  //  taskEXIT_CRITICAL();

  /// Write configuration to SC18IS602B
  // taskENTER_CRITICAL();
  // Setup Thermocouple
  dataBuffer[0] = 0xf0;
  dataBuffer[1] = 0x2;
  err = i2c_slave_write(I2C_BUS, SC18IS602B, NULL, &dataBuffer, 2);
  if (err != 0) {
    printf("Couldn't set SPI int\r\n");
  }
  // taskEXIT_CRITICAL();

  struct I2CVarsStruct *rxI2CVars;
  extern QueueHandle_t xI2CQueue;

  while (1) {
    static uint8_t tempIttCounter = 0;
    // Redraw LCD
    if (xQueueReceive(xI2CQueue, &(rxI2CVars), 0)) {
      //  taskENTER_CRITICAL();
      hd44780_gotoxy(&lcd, 0, rxI2CVars->printRow);
      hd44780_puts(&lcd, rxI2CVars->dataForI2CQueue);
      //  taskEXIT_CRITICAL();
    }
    vTaskDelayUntil(&lastWakeTime, xFreq);

    // Poll Buttons
    static uint8_t lastGPIOBuffer = 0;
    //  taskENTER_CRITICAL();
    dataBuffer[0] = 0;
    err = i2c_slave_write(I2C_BUS, devAddr, NULL, &dataBuffer, 1);
    if (err != 0) {
      printf("Couldn't address the slave to prep read\r\n");
    } else {
      err = i2c_slave_read(I2C_BUS, devAddr, NULL, &dataBuffer, 1);
      if (err != 0) {
        printf("couldn't read from slave\r\n");
      } else {
        if (dataBuffer[0] != lastGPIOBuffer &&
            (dataBuffer[0] != 0x00 && dataBuffer[0] != 0xFF)) {
          // Send value to thread for processing
          uint8_t txDataBuffer = dataBuffer[0];
          xQueueSend(xGPIOForProcQueue, &txDataBuffer, 10);
        }
        lastGPIOBuffer = dataBuffer[0];
      }
    }

    //    taskEXIT_CRITICAL();
    // Poll Thermocouple ever 1s

    if (tempIttCounter >= 50) {
      //    taskENTER_CRITICAL();
      dataBuffer[0] = 0x1;
      dataBuffer[1] = 0xFF;
      dataBuffer[2] = 0xFF;
      err = i2c_slave_write(I2C_BUS, SC18IS602B, NULL, &dataBuffer, 3);
      if (err != 0) {
        printf("Couldn't init SPI read\r\n");
      }
      //    taskEXIT_CRITICAL();

      vTaskDelayUntil(&lastWakeTime, xFreq);

      //    taskENTER_CRITICAL();

      uint8_t tempBuffer[8] = {1};
      err = i2c_slave_read(I2C_BUS, SC18IS602B, NULL, &tempBuffer, 2);
      //    taskEXIT_CRITICAL();

      if (err != 0) {
        printf("Couldn't read SPI buffer\r\n");
      } else {
        uint16_t tempInt = tempBuffer[0];
        tempInt = tempInt << 8;
        tempInt = tempInt | tempBuffer[1];
        tempInt = tempInt >> 3;
        float temp = tempInt * 0.25;
        tempInt = (int)temp;

        if (xSemaphoreTake(temperatureVarsMutex, 20) == pdTRUE) {
          temperatureVars.currentTemperature = tempInt;
          xSemaphoreGive(temperatureVarsMutex);
          xSemaphoreGive(printSemaphore);
        } else {
          printf("Couldn't store new temperature\r\n");
        }
      }
      tempIttCounter = 0;
    } else {
      tempIttCounter++;
    }
  }
}

void sendLCDOutI2C() {}

void sendThermoOutI2C() {}

uint8_t pollButtonsI2C() { return 0; }
