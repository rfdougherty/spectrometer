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
#include <Streaming.h>
#include "c12880.h"
#include <hal/gpio_ll.h>

/*
 * we want sub-microsecond delays so we'll use the CPU clock counter CCOUNT.
 */
inline static void delayTicks(uint32_t ticks) {
  uint32_t start = xt_utils_get_cycle_count(); // CCOUNT;
  while (xt_utils_get_cycle_count() /*CCOUNT*/ - start < ticks) {
    yield;
  }
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
  _clock_delay_micros = 1; // half of a clock period
  _min_integ_micros = 0;   // this is correction which is platform dependent and should be measured in `begin`
  set_integration_time(1000);  // integration time default to 1ms
}

inline uint32_t C12880_Class::_pulse_clock(uint32_t cycles){
  for(uint32_t i = 0; i < cycles; i++){
    // Faster digital write ~ 50ns / write vs. 82ns using digitlWrite. 
    // https://www.reddit.com/r/esp32/comments/f529hf/results_comparing_the_speeds_of_different_gpio/
    //digitalWrite(_CLK_pin, HIGH);
    GPIO.out_w1ts = ((uint32_t)1 << _CLK_pin);
    //delayMicroseconds(PULSE_US);
    delayTicks(_pulse_ticks);
    //digitalWrite(_CLK_pin, LOW);
    GPIO.out_w1tc = ((uint32_t)1 << _CLK_pin);
    //delayMicroseconds(PULSE_US);
    delayTicks(_pulse_ticks);
  }
  return cycles;
}

inline void C12880_Class::_pulse_clock_timed(uint32_t duration_micros){
  elapsedMicros sinceStart_micros = 0;
  while (sinceStart_micros < duration_micros){
    GPIO.out_w1ts = ((uint32_t)1 << _CLK_pin);
    delayTicks(_pulse_ticks);
    GPIO.out_w1tc = ((uint32_t)1 << _CLK_pin);
    delayTicks(_pulse_ticks);
  }
}

  uint32_t C12880_Class::set_pulse_rate(uint32_t pulse_rate){
    // To achieve sub-microsecond delays we use the cpu clock. This sets the number of ticks we want for each pulse.
    // The pulse_rate is specified in Hz.
    _cpu_freq = getCpuFrequencyMhz();
    // pulse rate is half the scale factor. E.g., with a scale of 5, pulse rate is 2.5MHz
    _pulse_ticks = _cpu_freq / (pulse_rate / 500000);
    // measure minimum integration time; 48 clock cycles are required after ST goes low
    elapsedMicros sinceStart_micros = 0;
    _pulse_clock(48);
    _min_integ_micros = sinceStart_micros;
    return _pulse_ticks;
  }

void C12880_Class::begin() {
  // Initialize ADC for reading on the video pin.
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);

  pinMode(_CLK_pin, OUTPUT);
  pinMode(_ST_pin, OUTPUT);
  digitalWrite(_CLK_pin, LOW);
  digitalWrite(_ST_pin, LOW);
  set_pulse_rate(5000000);

  // The first analogRead has a bit of overhead, so do a preemptive read here.
  analogReadMilliVolts(_VIDEO_pin);
}

void C12880_Class::read_into(uint16_t *buffer) {
  // compute integration time with timing correction in _min_integ_micros
  uint32_t duration_micros = _integ_time - min(_integ_time, _min_integ_micros);

  // Start clock cycle and set start pulse to signal start
  uint32_t start_micros = micros();
  _pulse_clock(1);
  GPIO.out_w1ts = ((uint32_t)1 << _ST_pin); // ST HIGH
  delayTicks(_pulse_ticks);
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
  // continuous adc mode is over 2x faster than one-shot reads (5500us vs. 11000us) but the reads were unreliable.
  // This does not affect the minimum integration time, just the refractory period between measurements.
  for(uint16_t i=0; i<C12880_NUM_CHANNELS; i++) {
    buffer[i] = analogReadMilliVolts(_VIDEO_pin);
    _pulse_clock(1);
  }
  _timings[4] = micros() - start_micros;
}

