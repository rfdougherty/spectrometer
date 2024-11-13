/*
  c12880.cpp - Library for interacting with the Hamamatsu C12880 microspectrometer
  Created by Craig Wm. Versek, 2017-03-01
  
  Much of the initial implmentation was based on this project:
  https://github.com/groupgets/c12880ma/blob/master/arduino_c12880ma_example/arduino_c12880ma_example.ino

  modified by Bob Dougherty (https://github.com/rfdougherty) 2024.05.04

  The C12880 can do a 5MHz clock. The code below drives it at 500kHz (1us pulse). To hit 5MHz, 
  the clock pulse will need to be 10x shorter (100 nanoseconds). Using a timer we 
  should be able to run it at 5MHz.

  the fast analog read can do about 100ks/sec, which implies that a read takes 10us.

  TODO: investigate using dedicated gpio bundles, or a peripheral like rmt or i2s?
  https://esp32.com/viewtopic.php?t=27963

 */
#include <elapsedMillis.h>
#include "c12880.h"
#include <hal/gpio_ll.h>

// Continuous ADC reads
volatile bool g_isr_adc_done = false;
void ARDUINO_ISR_ATTR adcComplete() {
  g_isr_adc_done = true;
}

////////////////////////////////////////////////////////////////////////////////

C12880_Class::C12880_Class(const uint8_t TRG_pin,
                             const uint8_t ST_pin,
                             const uint8_t CLK_pin,
                             const uint8_t VIDEO_pin
                             ){
  _TRG_pin = TRG_pin;
  _ST_pin  = ST_pin;
  _CLK_pin = CLK_pin;
  _VIDEO_pin = VIDEO_pin;
  // Fast analog read needs the adc channel, not the pin. This code only works with ADC unit 1,
  // and on the s3 the pin,channel mapping is channel = pin - 1 (e.g., pin1 is chan2, ... pin10 is chan9)
  _VIDEO_chan = VIDEO_pin - 1;
  _clock_delay_micros = 1; // half of a clock period
  _min_integ_micros = 0;   // this is correction which is platform dependent and 
                           // should be measured in `begin`
  set_integration_time(1000);  // integration time default to 1ms
}

inline void C12880_Class::_pulse_clock(uint16_t cycles){
  for(uint16_t i = 0; i < cycles; i++){
    // Faster digital write ~ 50ns / write vs. 82ns using digitlWrite. 
    // https://www.reddit.com/r/esp32/comments/f529hf/results_comparing_the_speeds_of_different_gpio/
    // minimum integration time is reduced from ~235us to 179us 
    //digitalWrite(_CLK_pin, HIGH);
    GPIO.out_w1ts = ((uint32_t)1 << _CLK_pin);
    delayMicroseconds(PULSE_US);
    //digitalWrite(_CLK_pin, LOW);
    GPIO.out_w1tc = ((uint32_t)1 << _CLK_pin);
    delayMicroseconds(PULSE_US);
  }
}

inline void C12880_Class::_pulse_clock_timed(uint32_t duration_micros){
  elapsedMicros sinceStart_micros = 0;
  while (sinceStart_micros < duration_micros){
    GPIO.out_w1ts = ((uint32_t)1 << _CLK_pin);
    delayMicroseconds(PULSE_US);
    GPIO.out_w1tc = ((uint32_t)1 << _CLK_pin);
    delayMicroseconds(PULSE_US);
  }
}

void C12880_Class::begin() {
  // Initialize ADC for fast reading on the video pin.
  analogContinuousSetWidth(12); // S3 should support 13 bits, but runtime error says only option is 12
  analogContinuousSetAtten(ADC_11db); // for the S3, this is 0 mV ~ 3100 mV
  // array of pins, pin count, # of conversions per pin per cycle to average, sample frequency, callback function
  uint8_t adc_pins[] = {_VIDEO_pin};
  analogContinuous(adc_pins, 1, ADC_NUM_READS_TO_AVERAGE, ADC_CLOCK_FREQ, &adcComplete);
  //analogReadResolution(12);
  //analogSetAttenuation(ADC_11db);

  pinMode(_CLK_pin, OUTPUT);
  pinMode(_ST_pin, OUTPUT);
  
  digitalWrite(_CLK_pin, LOW);
  digitalWrite(_ST_pin, LOW);
  
  _measure_min_integ_micros();
}

void C12880_Class::_measure_min_integ_micros() {
  //48 clock cycles are required after ST goes low
  elapsedMicros sinceStart_micros = 0;
  _pulse_clock(48);
  _min_integ_micros = sinceStart_micros;
}

void C12880_Class::read_into(uint16_t *buffer) {
  // compute integration time with timing correction in _min_integ_micros
  uint32_t duration_micros = _integ_time - min(_integ_time, _min_integ_micros);

  // Start clock cycle and set start pulse to signal start
  uint32_t start_micros = micros();
  GPIO.out_w1ts = ((uint32_t)1 << _CLK_pin); // CLK HIGH
  delayMicroseconds(PULSE_US);
  GPIO.out_w1tc = ((uint32_t)1 << _CLK_pin); // CLK LOW
  GPIO.out_w1ts = ((uint32_t)1 << _ST_pin); // ST HIGH
  delayMicroseconds(PULSE_US);

  // pixel integration starts three clock pulses after ST goes high
  _pulse_clock(3);
  _timings[0] = micros() - start_micros;

  // Integrate pixels for a while
  _pulse_clock_timed(duration_micros);

  // Now bring ST LOW-- the beginning of the end of integration
  GPIO.out_w1tc = ((uint32_t)1 << _ST_pin); 
  _timings[1] = micros() - start_micros;

  // integration stops on the 48th pulse after ST went low
  _pulse_clock(48);
  _timings[2] = micros() - start_micros;

  // pixel output is ready after 40 more pulses (a total of 88 pulses after ST went low)
  _pulse_clock(40); // 48 + 40 = 88
  _timings[3] = micros() - start_micros;

  // Read from SPEC_VIDEO
  // continuous adc mode is over 2x faster than one-shot reads (5500us vs. 11000us)
  adc_continuous_data_t *result = NULL;
  analogContinuousStart();
  for(uint16_t i=0; i<C12880_NUM_CHANNELS; i++) {
    while (!g_isr_adc_done) {
      if (micros() - _timings[3] > ADC_CONVERSION_TIMEOUT_USEC) {
        buffer[i] = 9998;
        break;
      }
    }
    g_isr_adc_done = false;
    if (analogContinuousRead(&result, ADC_READ_TIMEOUT_MSEC)) {
      buffer[i] = result[0].avg_read_mvolts;
    } else {
      //Serial.println("Error reading ADC. Set Core Debug Level to error or lower for more info.");
      buffer[i] = 9999; // we can check this to see if we had any errors
      // TODO: log errors
    }
    _pulse_clock(1);
  }
  analogContinuousStop();
  // for(uint16_t i=0; i<C12880_NUM_CHANNELS; i++) {
  //   buffer[i] = analogReadMilliVolts(_VIDEO_pin);
  //   _pulse_clock(1);
  // }
  _timings[4] = micros() - start_micros;
}

