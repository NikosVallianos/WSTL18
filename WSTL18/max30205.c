﻿/********************************************************************************

  max30205.c: MAX30205 temperature sensor module of WSTL18 firmware.
 
  Copyright (C) 2017 Nikos Vallianos <nikos@wildlifesense.com>
  
  This file is part of WSTL18 firmware.
  
  WSTL18 firmware is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
  
  WSTL18 firmware is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
  
  You should have received a copy of the GNU General Public License
  in the file COPYING along with this program.  If not,
  see <http://www.gnu.org/licenses/>.

  ********************************************************************************/
#include <avr/io.h>
#include "twi.h"
#include "max30205.h"

// !!!!!! Max first conversion time after POR is 50ms (DS p.2 bottom).
/*
 *	max30205_configuration_register: Always contains the current configuration register. All
 *	functions in this module write to this register and then send it to the device. This eliminates
 *	the need to read the register from the device before making changes, except when reading the
 *	fault queue.
 */
uint8_t max30205_configuration_register;

/*
 * Initialize MAX30205 sensor.
 */
void max30205Init(void) {
	DDRD &= ~(1<<DDRD2); PORTD |= (1<<PORTD2);		// Set D2 as input and pull-up.
	// No config setup; default is good.
	twiEnable();
	max30205_configuration_register = 0;
	max30205Shutdown();
	twiDisable();
}

/*
To minimize power consumption by the MAX30205:
+ Set MAX30205 to shutdown mode.
+ Switch TWI off
+ Set PD2 as input with pull-up. Pull-up resistor should then have no current.
*/
void max30205Shutdown(void) {
	max30205_configuration_register |= (1<<MAX30205_CONF_SHUTDOWN);
	max30205SaveConfiguration();
	twiDisable();
}

void max30205Resume(void) {
	if(max30205_configuration_register & (1<<MAX30205_CONF_SHUTDOWN)) {
		max30205_configuration_register &= ~(1<<MAX30205_CONF_SHUTDOWN);
		max30205SaveConfiguration();
	}
}



/*
 *	max30205SaveConfiguration: Takes the configuration register variable from this module
 *	and stores it in the temperature sensor.
 */
void max30205SaveConfiguration(void) {
	twiWriteRegister8(MAX30205_ADDRESS, MAX30205_REG_CONF, max30205_configuration_register);
}

uint8_t max30205ReadConfiguration(void) {
	return twiReadRegister8(MAX30205_ADDRESS, MAX30205_REG_CONF);
}

void max30205LoadConfiguration(void) {
	max30205_configuration_register = max30205ReadConfiguration();
}

/*
 * Trigger a one-shot on the temperature sensor.
 */
void max30205StartOneShot(void) {
	if(max30205_configuration_register & (1<<MAX30205_CONF_SHUTDOWN)) {	// One-shot only works from shut-down mode. Ignored otherwise.
		max30205_configuration_register |= (1<<MAX30205_CONF_ONESHOT);	// Set one-shot bit in configuration variable
		max30205SaveConfiguration();
		max30205_configuration_register &= ~(1<<MAX30205_CONF_ONESHOT);	// Only unset ONESHOT in variable, sensor auto-resets after one-shot completes.
	} // else: some sort of programming fault?
	// While in shutdown, the I2C interface remains active and all registers remain accessible to the master.
	// The results of a conversion are available to read after 50ms.
	// Max conversion time is 50ms.
}

/*
 *	max30205ReadTemperature: Read the current temperature from the sensor. 
 *  If reading after a one-shot, wait for 50ms before reading.
 *
 *	Returns:
 *		16-bit temperature reading as given by the device.
 */
uint16_t max30205ReadTemperature(void) {
	return twiReadRegister16(MAX30205_ADDRESS, MAX30205_REG_TEMP);
}