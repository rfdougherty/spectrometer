/*
  c12880.cpp - Library for interacting with the Hamamatsu C12880 microspectrometer
  Created by Craig Wm. Versek, 2017-03-01
  
  Much of the initial implmentation was based on this project:
  https://github.com/groupgets/c12880ma/blob/master/arduino_c12880ma_example/arduino_c12880ma_example.ino

  modified by Bob Dougherty (https://github.com/rfdougherty) 2024.05.04
 */
#include <elapsedMillis.h>
#include "c12880.h"
#include "analog.h"
 
#define CLOCK_FREQUENCY 50000

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
    digitalWrite(_CLK_pin, HIGH);
    delayMicroseconds(PULSE_US);
    digitalWrite(_CLK_pin, LOW);
    delayMicroseconds(PULSE_US);
  }
}

inline void C12880_Class::_pulse_clock_timed(uint32_t duration_micros){
  elapsedMicros sinceStart_micros = 0;
  while (sinceStart_micros < duration_micros){
    digitalWrite(_CLK_pin, HIGH);
    delayMicroseconds(PULSE_US);
    digitalWrite(_CLK_pin, LOW);
    delayMicroseconds(PULSE_US);
  }
}

void C12880_Class::begin() {
  //analogReadResolution(12);
  // Initialize ADC for reading really fast on the video pin.
  // NOTE that regular AnalogRead must NOT be used beyond this point
  // NOTE that pin 5 is on channel 4 and pin 6 is on channel 5
  // channel = pin-1 (adc channel 0 is on pin 1, ... channel 19 is on pin 20)
  fadcInit(1, _VIDEO_pin);

  //Set desired pins to OUTPUT
  pinMode(_CLK_pin, OUTPUT);
  pinMode(_ST_pin, OUTPUT);
  
  digitalWrite(_CLK_pin, LOW); // Start with CLK High
  digitalWrite(_ST_pin, LOW);  // Start with ST Low
  
  _measure_min_integ_micros();
}

void C12880_Class::_measure_min_integ_micros() {
  //48 clock cycles are required after ST goes low
  elapsedMicros sinceStart_micros = 0;
  _pulse_clock(48);
  _min_integ_micros = sinceStart_micros;
}

void C12880_Class::set_integration_time(uint32_t usec) {
  _integ_time = usec;
}


void C12880_Class::read_into(uint16_t *buffer) {
  // compute integration time with timing correction in _min_integ_micros
  uint32_t duration_micros = _integ_time - min(_integ_time, _min_integ_micros);
  // Start clock cycle and set start pulse to signal start
  uint32_t start_micros = micros();
  digitalWrite(_CLK_pin, HIGH);
  delayMicroseconds(PULSE_US);
  digitalWrite(_CLK_pin, LOW);
  digitalWrite(_ST_pin, HIGH);
  delayMicroseconds(PULSE_US);
  // pixel integration starts after three clock pulses
  _pulse_clock(3);
  _timings[0] = micros() - start_micros;
  // Integrate pixels for a while
  _pulse_clock_timed(duration_micros);
  // Set _ST_pin to low
  digitalWrite(_ST_pin, LOW);
  _timings[1] = micros() - start_micros;
  // Sample for a period of time; integration stops at pulse 48th pulse after ST went low
  _pulse_clock(48);
  _timings[2] = micros() - start_micros;
  // pixel output is ready after last pulse #88 after ST wen low
  _pulse_clock(40);
  _timings[3] = micros() - start_micros;
  //Read from SPEC_VIDEO
  for(uint16_t i=0; i<C12880_NUM_CHANNELS; i++){
    buffer[i] = analogReadMilliVoltsFast(_VIDEO_chan);
    _pulse_clock(1);
  }
  _timings[4] = micros() - start_micros;
}

