/*
  probe.h - code pertaining to probing methods
  Part of Grbl

  Copyright (c) 2014-2015 Sungeun K. Jeon

  Grbl is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Grbl is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with Grbl.  If not, see <http://www.gnu.org/licenses/>.
*/
  

#ifndef probe_h
#define probe_h 

#include "grbl.h"

// Values that define the probing state machine.  
#define PROBE_OFF     0 // Probing disabled or not in use. (Must be zero.)
#define PROBE_ACTIVE  1 // Actively watching the input pin.

class Cprobe {
private:

public:

// Probe pin initialization routine.
void init();

// Called by probe_init() and the mc_probe() routines. Sets up the probe pin invert mask to 
// appropriately set the pin logic according to setting for normal-high/normal-low operation 
// and the probing cycle modes for toward-workpiece/away-from-workpiece. 
void configure_invert_mask(uint8_t is_probe_away);

// Returns probe pin state. Triggered = true. Called by gcode parser and probe state monitor.
uint8_t get_state();

// Monitors probe pin state and records the system position when detected. Called by the
// stepper ISR per ISR tick.
void state_monitor();

};

extern Cprobe Probe;



#endif
