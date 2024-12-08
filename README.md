# Spectrometer

An Arduino-based controller for the [Hamamatsu C12880MA](https://www.hamamatsu.com/us/en/product/optical-sensors/spectrometers/mini-spectrometer/C12880MA.html) mini spectrometer using the [ESP32-S3](https://www.espressif.com/en/products/socs/esp32-s3) microcontroller. The figure below shows measurements of daylight entering a window across a 2-hour period just a little after noon in Redwood City California on October 26th, 2024. A simple auto-integration time algorithm was used so the raw voltage readings were scaled by the integration time to show the luminance differences between readings as the sun moved across the sky. (See [examples/spectrum.ipynb](examples/spectrum.ipynb) for the code used to make this plot.)

![example spectrum](examples/spec.png)

## Overview

This project includes firmware for controlling the Hamamatsu C12880 mini spectrometer, offering web-based remote control and data acquisition. The system includes temperature and humidity monitoring via an AHT10 sensor. The device is very portable and easily powered for days using a small usb power bank, allowing longitudinal measurements in the field (e.g., to measure the daylight variation in a particular location).

### Features

- Web server interface for spectrometer control
- Over-the-Air (OTA) firmware updates
- WiFi configuration via WiFiManager
- Temperature and humidity data
- Wavelength calibration (be sure to update the coefficients in [getWavelength](firmware/firmware.ino) with the values from your calibration certificate!)
- NTP time synchronization for precise timestamping of measurements
- Status LED indicator

### Integration time

A spectrometer's integration time is the length of time it takes to collect photons and accrue charge on the sensor. It must be adjusted to maximize the signal-to-noise by using the full range of the sensor's analog output while avoiding saturation of any of the pixels. The current firmware does not include an automatic integration time algorithm, so the integration time must be manually set when you call the API to get a reading. The [examples/spectrum.ipynb](examples/spectrum.ipynb) notebook includes an example of how to use the API to automatically select an integration time by making multiple API calls with different integration times to find a value just below the sensor's saturation point.

## Hardware

- **Microcontroller**: ESP32-S3 (e.g., [LOLIN S3 mini](https://www.wemos.cc/en/latest/s3/s3_mini.html))
- **Spectrometer**: [Hamamatsu C12880MA](https://www.hamamatsu.com/us/en/product/optical-sensors/spectrometers/mini-spectrometer/C12880MA.html)
- **Temperature/Humidity Sensor**: AHT10/AHT20 (e.g., [this module](https://www.amazon.com/gp/product/B092495GZJ))
- **Connections**:
  - SPEC_TRG (output): GPIO 12 (requires voltage divider)
  - SPEC_ST (input): GPIO 13 (direct connection)
  - SPEC_CLK (input): GPIO 16 (direct connection)
  - SPEC_EOS (output): GPIO 11 (requires voltage divider)
  - SPEC_VIDEO (output): GPIO 10 (requires voltage divider; see note below)
  - I2C: SDA (GPIO 35), SCL (GPIO 36) (direct connection to the AHT10)

  You can buy a C12880 module and breakout board from [GroupGets](https://groupgets.com/products/hamamatsu-c12880ma-breakout-board). Their board offers level shifters and a buffer for the analog output of the spectrometer. However, the C12880 inputs can be driven directly from the microcontroller as they are rated for a 3V HIGH threshold. You do need to level-shift the digital outputs from the C12880 to avoid damaging the microcontroller. However, this can be easily achived with a simple 2-resistor [voltage divider](https://randomnerdtutorials.com/how-to-level-shift-5v-to-3-3v/). I used a 5K/10K pair. The S3 analog input is high impedance and does not need a buffer (but *does* need a divider; see notes below). Given this, I picked up a C12880 (with calibration certificate) from ebay for under $200 and built a simple breakout board with spare parts. (A schematic and pcd files for the breakout are coming soon!)

### Analog notes
The ESP32 had [notoriously bad](https://www.reddit.com/r/esp32/comments/1dgjxtm/honest_question_why_is_the_adc_so_bad_non_rant/) ADC performance, but the ESP32-S3 has a much improved ADC. I did some preliminary tests using an external [12-bit SPI ADC](https://www.microchip.com/en-us/product/mcp3204), an [op-amp buffer](https://www.ti.com/lit/ds/symlink/opa344.pdf?ts=1731079872575), and a simple voltage divider to read the C12880 analog output but the results were actually *more* noisy than using the S3's ADC with no buffer and just a 2-resistor divider. Take this external ADC test with a grain of salt, as this was done on a breadboard, so it's quite possible that a well-designed PCB could yield better results with an external ADC. But for my purposes, the simplest solution was good enough to achieve very stable spectrometer readings. Also note that unlike the divider for the digital pins, I paid careful attention to the exact resistance values to achive a full-range ADC input of [0-3.1V](https://docs.espressif.com/projects/esp-idf/en/v4.4.3/esp32s3/api-reference/peripherals/adc.html) and measured a bunch of individual resistors to pick a pair that got me closest to <= 3.1V (e.g., a 2K that measures a bit high and a 3.3K that measures a bit low). Adding a small (0.1nF) filter capacitor helped with stability.

## Firmware

Note that the C12880 wavelenths are individually calibrated and thus you should receive a calibration certificate with your spectrometer. You must update the coefficients in [getWavelength](firmware/firmware.ino) with the values from your calibration certificate to get accurate data.

The web API returns json payloads. To keep things simple and fast, I used snprintf to build the JSON payload strings. This works well for now, but will not be the easiest to maintain if the API is expanded. In that case, this code should be refactored to use a proper JSON library like [ArduinoJson](https://arduinojson.org/).

The firmware code uses the following Arduino libraries:
- [Streaming](https://github.com/janelia-arduino/Streaming)
- [AHTxx](https://github.com/m5stack/AHTxx)
- Custom C12880 library (included in this repo)

### C12880 clock pulses
The code uses simple bit-banging to drive the C12880 which imposes some limits on how fast it can run and thus the minimum integration time that can be achieved. I did explore other options and implemented one (RMT; see notes below) but found that given the somehat involved C12880 requirement that the clock pin, read start pin, and analog video out pin need to be coordinated the RMT would not be a good solution. So to make the bit-banging fast, I use [direct register settings](https://www.reddit.com/r/esp32/comments/f529hf/results_comparing_the_speeds_of_different_gpio/) to save the digitalWrite overhead. I also implemented a sub-microsecond delay function that counts CPU clock ticks. The latter was critical for getting fast reads (and thus short integration times) as delayMicros has a theoretical minimum delay of 1 microsecond, but in practice averages about 2-3 microseconds due to rounding and overhead.

### ADC Reads
I originally used [ESP32-S3-FastAnalogRead](https://github.com/stg/ESP32-S3-FastAnalogRead) to speed up single-shot analog reads, but that package is out of date and throws runtime errors when compiled with ESP-IDF 5 (aka Arduino in ESP-Arduino 3.x). Also, the new IDF has improved the analogRead functions and thus closed the gap with hand-optizied code. I did explore using continuous ADC reads and found that to be about 2x faster than single-shot reads, but I couldn't be confident that the readings were perfectly synchronized with each C12880 pixel and it may have been off by one or two pixels, so I reverted to using a simple sing-shot ADC read approach. This results in about a 10 millisecond readout time. However, unlike the clok pulses, this does not affect the minimum integration time and merely adds an extra 5ms delay between measurements, which seems worth the tradeoff for more reliable readings.

## Examples

See the [Jupyter](https://jupyter.org/) notebook in [examples](examples) for a simple example of how to read spectrum data from the spectrometer using python code.

## Configuration

The device uses WiFiManager for initial WiFi setup:
1. On first boot, the device creates an access point
2. Connect to the AP and configure your WiFi credentials
3. The device will automatically connect to your network on subsequent boots

## Usage

The device provides a web interface accessible via its IP address. Available endpoints:
- `/wavelength`: Get the calibrated wavelengths.
- `/spectrum/{integration_time}`: Acquire spectral data for a given integration time. (see python code for more details)
- `/pulserate/{pulse_rate}`: Set the spectrometer clock pulse rate, in Hz. Should be in the rante 100000 - 5000000.
- `/temperature`: Get the temperature and humidity.
- `/i2c`: Scan I2C bus for connected devices (useful for debugging I2C issues)

## Development

### TODO

- Improve performance by removing delayMicros in clock logic. The C12880 supports being clocked at up to 5MHz, but we are maxing out at less than 0.5MHz due to the use of delayMicros, which has a theoretical minimum delay of 1us, but in practice is about 1.5us. Options for removing this limit include:
  - ~~use a peripheral that can generate the pulses, like the RMT~~
  - [dedicated GPIO](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/peripherals/dedic_gpio.html)
  - implement a sub-microsecond delay 
  
  Of these, the first is likely to be the most performant and should be able to easily hit 5MHz with minimal CPU cycles. But the last option is the easilest to implement.

  UPDATE: I tried using RMT and found it didn't help speed things up much despite adding lots of complexity. 
  The main reason that it didn't help much is due to the fact that we need to bit-bang the clock pin for the ADC
  readout and switching the pin from RMT mode to digitalWrite mode was taking several hundrend microseconds.

- Add an API endpoint to set and store calibration coefficients
- Add an automatic integration time selection algorithm
- Add a web GUI
- Design a better enclosure

### Info
- Version: 0.3
- Author: Bob Dougherty
- Repository: https://github.com/rfdougherty

## License

MIT License
