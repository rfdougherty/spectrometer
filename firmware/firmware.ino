/*
 * Controller for Hamamatsu C12880 mini spectrometer:
 * https://www.hamamatsu.com/us/en/product/optical-sensors/spectrometers/mini-spectrometer/C12880MA.html 
 * 
 * Ideal board is an esp32-S3 as it has an improved ADC. I used a LOLIN S3 mini (4MB flash, 2MB PSRAM).
 * board def: ~/Library/Arduino15/packages/esp32/hardware/esp32/2.0.11/variants/lolin_s3_mini/pins_arduino.h
 * Note: to get serial output, enable "USB CDC On Boot" in the tools menu.
 * 
 * Bob Dougherty (https://github.com/rfdougherty) 2024.05.04
 *
 */
#include <WiFi.h>
#include <WebServer.h>
#include <uri/UriBraces.h>
#include <ArduinoOTA.h>
#include <Streaming.h>
#include <time.h>
#include <AHTxx.h>
#include <WiFiManager.h> // https://github.com/tzapu/WiFiManager
#include "c12880.h"
//#include <Wire.h>
//#include <ESPmDNS.h>

#define VERSION_STRING "Spectrometer 0.3"
#define STAT_LED LED_BUILTIN // LED_BUILTIN is 21 on xiao s3, 13 on tft, and 47 on s3 mini
#define LED_ON HIGH // On most boards this is LOW

#define SPEC_TRG 12 // 5
#define SPEC_ST  13 // 6
#define SPEC_CLK 16 // 9
#define SPEC_EOS  11 // 11
// ADC2 is used by the wifi, so be sure to use an adc1 pin (1,2,3,4,5,6,7,8,9,10,14)
#define SPEC_VIDEO 10

#define SDA_PIN 35 // 42
#define SCL_PIN 36 // 41

float g_temperature;
float g_humidity;

#define SPEC_CHANNELS 288
#define MAX_SAMPLES 20
uint16_t g_spec_data[SPEC_CHANNELS];
double g_nm_lut[SPEC_CHANNELS];

#define TIME_SYNC_TIMEOUT_MS 500 // getLocalTime is called often, so reduce the default timeout from 5s to .5s
const char* g_ntpServer = "pool.ntp.org";
const char* g_timezone = "PST8PDT,M3.2.0,M11.1.0";
struct tm g_timeinfo;

char g_mac_str[18]; // 12 digits, 5 colons, and a '\0' at the end
String g_ipaddr;
long g_first_boot_epoch_secs;

// need a char buffer big enough to hold data payload, where each sample needs at most 6 chars
// {"ts":1712435572,"spec":[1000,2000,...]}
char g_buf[SPEC_CHANNELS * 6 + 100];

WebServer server(80);

//MCP3201 adc;  //  use HW SPI (cs pin is set in begin)
AHTxx aht10(AHTXX_ADDRESS_X38, AHT1x_SENSOR); //sensor address, sensor type
C12880_Class spec(SPEC_TRG, SPEC_ST, SPEC_CLK, SPEC_VIDEO);

void setup(){
  Serial.begin(115200); // Baud Rate set to 115200
  delay(100);
  Serial << F("Booting ") << VERSION_STRING << "\n";

  // Configure spectrometer 
  //pinMode(SPEC_CLK, OUTPUT);
  //pinMode(SPEC_ST, OUTPUT);
  //digitalWrite(SPEC_CLK, HIGH); // Set SPEC_CLK High
  //digitalWrite(SPEC_ST, LOW); // Set SPEC_ST Low
  //adcAttachPin(SPEC_VIDEO);
  //analogSetClockDiv(1);
  spec.begin();

  startWifi();
  Serial.println("Configuring time...");
  initTime(g_timezone);
  updateTime(5000); 
  setupRouting();
  Serial.println("HTTP server started");
  startOTA();
  Serial.println("OTA begun");

  if(aht10.begin() != true) {
    Serial << F("AHT10 sensor failed to start!") << "\n";
  } else {
    g_temperature = aht10.readTemperature(); // read 6-bytes via I2C, takes 80 milliseconds
    g_humidity = aht10.readHumidity(AHTXX_USE_READ_DATA);
    Serial << F("AHT10 sensor initialized; temp=") << g_temperature << F("C, hum=") << g_humidity << "%\n";
  }

  for (int i=0; i<SPEC_CHANNELS; i++){
    g_nm_lut[i] = getWavelength(i);
  }
  g_first_boot_epoch_secs = getEpochSecs();
  delay(1000);
}

void loop(){
  static uint32_t last_update = 0;
  ArduinoOTA.handle();
  server.handleClient();
  //updateTime(); // only need to do this when we use the time
  heartbeat('b');
  yield();
}

// The following is not needed for the esp32-s3, as it has a much better adc
// double adcFix(uint16_t reading){
//   // Reference voltage is 3v3 so maximum reading is 3v3 = 4095 in range 0 to 4095
//   if(reading < 1 || reading > 4095) return 0;
//   // return -0.000000000009824 * pow(reading,3) + 0.000000016557283 * pow(reading,2) 
//   //+ 0.000854596860691 * reading + 0.065440348345433;
//   return (-0.000000000000016 * pow(reading,4) 
//          +0.000000000118171 * pow(reading,3)
//          -0.000000301211691 * pow(reading,2)
//          +0.001109019271794 * reading 
//          +0.034143524634089);
// }


double getWavelength(uint16_t pixnum) {
  // Convert pixel number to wavelength using the 5th order polynomial calibration function
  const double a0=3.096674805e+2;
  const double b1=2.735884054;
  const double b2=-1.945647682e-3;
  const double b3=-5.627850896e-7;
  const double b4=-1.784036917e-8;
  const double b5=4.22796801e-11;
  double nm = a0 + b1*pixnum + b2*pow(pixnum,2) + b3*pow(pixnum,3) + b4*pow(pixnum,4) + b5*pow(pixnum,5);
  return nm;
}

void heartbeat(char color) {
  // color must be 'r', 'g' or 'b'
  static uint32_t prev_millis;
  static uint32_t prev_blink_millis;
  static uint8_t led_on;
  const uint32_t flash_interval = 3000;
  const uint8_t on_ms = 3;
  const uint8_t bright = 10;
  uint32_t cur_millis = millis();

  if(led_on){
    if(cur_millis - prev_blink_millis > on_ms){
      neopixelWrite(RGB_BUILTIN, 0, 0, 0);
      led_on = false;
      prev_blink_millis = cur_millis;
    }
  }else{
    if(cur_millis - prev_millis > flash_interval){
      switch(color){
        case 'g':
          neopixelWrite(RGB_BUILTIN, 0, bright, 0);
          break;
        case 'b':
          neopixelWrite(RGB_BUILTIN, 0, 0, bright);
          break;
        default:
          neopixelWrite(RGB_BUILTIN, bright, 0, 0);
      }
      
      led_on = true;
      prev_millis = cur_millis;
      prev_blink_millis = cur_millis;
    }
  }
}

void startOTA() {
  ArduinoOTA
    .onStart([]() {
      String type;
      if (ArduinoOTA.getCommand() == U_FLASH)
        type = "sketch";
      else // U_SPIFFS
        type = "filesystem";

      // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
      Serial.println("Start updating " + type);
    })
    .onEnd([]() {
      Serial.println("\nEnd");
    })
    .onProgress([](unsigned int progress, unsigned int total) {
      Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    })
    .onError([](ota_error_t error) {
      Serial.printf("Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
      else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
      else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
      else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
      else if (error == OTA_END_ERROR) Serial.println("End Failed");
    });
  ArduinoOTA.begin();
}

void i2c_scan(char *output, size_t output_size) {
  byte error, address;
  int nDevices = 0;
  size_t written = 0;

  written += snprintf(output + written, output_size - written, "{\"devices\":[");

  for(address = 1; address < 127; address++ ) {
    Wire.beginTransmission(address);
    error = Wire.endTransmission();
    
    if (error == 0) {
      written += snprintf(output + written, output_size - written, 
                          "{\"num\":%d,\"address\":\"0x%02X\"}", nDevices, address);
      nDevices++;
    }
    else if (error == 4) {
      written += snprintf(output + written, output_size - written, 
                          "{\"num\":%d,\"address\":\"0x%02X\",\"error\":\"unknown\"}", nDevices, address);
    }
    
    if (written >= output_size - 1) {
      break; // Stop if we're about to overflow the buffer
    }
  }
  written += snprintf(output + written, output_size - written, "]}");
}