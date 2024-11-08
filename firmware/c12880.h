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

#define PULSE_US 1 

/*******************************************************************************
  C12880_Class

#include "c12880.h"

#define SPEC_TRG         A0
#define SPEC_ST          A1
#define SPEC_CLK         10
#define SPEC_VIDEO       A3

uint16_t g_spec_data[C12880_NUM_CHANNELS];
C12880_Class spec(SPEC_TRG,SPEC_ST,SPEC_CLK,SPEC_VIDEO);

float g_integration_time = 0.01;
void setup(){
  spec.begin();
}

void loop(){
  spec.set_integration_time(g_integration_time);
  spec.read_into(g_spec_data);
  // to read the timing data
  for(int i=0; i<9; i++){
    Serial << spec.get_timing(i) << ",";
  }
  Serial << spec.get_timing(9) << "\n";

}

  
*******************************************************************************/
class C12880_Class{
public:
  C12880_Class(const uint8_t TRG_pin,
               const uint8_t ST_pin,
               const uint8_t CLK_pin,
               const uint8_t VIDEO_pin
              );
  //Configuration methods
  void begin();
  void set_integration_time(uint32_t usec);
  //Functionality methods
  void read_into(uint16_t *buffer);
  uint32_t get_timing(uint8_t index){
    if(index>4) return 0;
    return _timings[index];
  }
private:
  //helper methods
  inline void _pulse_clock(uint16_t cycles);
  inline void _pulse_clock_timed(uint32_t duration_micros);
  void _measure_min_integ_micros();
  //Attributes
  uint8_t _TRG_pin;
  uint8_t _ST_pin;
  uint8_t _CLK_pin;
  uint8_t _VIDEO_pin;
  uint8_t _VIDEO_chan;
  uint32_t _clock_delay_micros;
  uint32_t _integ_time;
  uint32_t _min_integ_micros;
  uint32_t _timings[5];
};



#endif /* _C12880_H_INCLUDED */
