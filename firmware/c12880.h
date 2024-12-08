/*
  c12880.h - Library for interacting with the Hamamatsu C12880 microspectrometer
  Created by Craig Wm. Versek, 2017-03-01
  https://github.com/open-eio/arduino-microspec/tree/master/microspec

  modified by Bob Dougherty (https://github.com/rfdougherty) 2024.05.04
 */
#ifndef _C12880_H_INCLUDED
#define _C12880_H_INCLUDED

#include <Arduino.h>

#define C12880_NUM_CHANNELS 288

class C12880_Class{
public:
  C12880_Class(const uint8_t TRG_pin,
               const uint8_t ST_pin,
               const uint8_t CLK_pin,
               const uint8_t VIDEO_pin
              );
  void begin();
  void set_integration_time(uint32_t usec) {
    _integ_time = usec;
  }
  uint32_t get_timing(uint8_t index) {
    if(index>4) return 0;
    return _timings[index];
  }
  uint32_t get_min_iteg_us() {
    return _min_integ_micros;
  }
  uint32_t set_pulse_rate(uint32_t pulse_rate);
  void read_into(uint16_t *buffer);
private:
  //helper methods
  inline uint32_t _pulse_clock(uint32_t cycles);
  inline void _pulse_clock_timed(uint32_t duration_micros);
  //Attributes
  uint8_t _TRG_pin;
  uint8_t _ST_pin;
  uint8_t _CLK_pin;
  uint8_t _VIDEO_pin;
  uint32_t _clock_delay_micros;
  uint32_t _integ_time;
  uint32_t _min_integ_micros;
  uint32_t _cpu_freq;
  uint32_t _pulse_ticks;
  uint32_t _timings[5];
};



#endif /* _C12880_H_INCLUDED */
