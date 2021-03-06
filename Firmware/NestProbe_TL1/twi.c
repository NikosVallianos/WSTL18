﻿/*
				2018-01-14 Revamping of the TWI module for ATmega328PB
TODO:
	1. Consider regular use of REPEATED START instead of STOP and START to ease bus traffic.
	2. Make sure master considers the ACK bit at the end of the SLA+R/SLA+W packet.
	3. Check the twiSetStartCondition: Implement TWINT setting correctly.
	4. Make use of _twiStatusIs etc.

*/

/********************************************************************************

  twi.c: Two-wire interface (aka I2C) module of WSTL18 firmware.
 
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
#include <util/twi.h>								// Useful TWI bit mask definitions
#include "twi.h"
#include "uart.h"									// For debugging.

// ############################################################################
// ############################# DEFINITIONS ##################################
// ############################################################################




#define TWI_ERROR_START	0b00000001	// 0x01	Could not set start condition on line
#define TWI_ERROR_SLACK	0b00000010	// 0x02	Did not receive ACK from slave.
#define TWI_ERROR_DATA	0b00000100	// 0x04	Did not receive ACK after sending a data byte.
#define TWI_ERROR_3		0b00001000	// 0x08
#define TWI_ERROR_4		0b00010000	// 0x10
#define TWI_ERROR_5		0b00100000	// 0x20
#define TWI_ERROR_BUS	0b01000000	// 0x40	Bus error during START or STOP
#define TWI_ERROR_UNKN	0b10000000	// 0x80	Unknown error
uint8_t twi_error_flags = 0;
uint16_t twi_error_count = 0;




// ############################################################################
// ############################# ERROR FLAGS ##################################
// ############################################################################

/*
 * twiErrorFlag: Set supplied error flag from defined flags at top of module.
 */
void _twiErrorFlag(uint8_t flag) {
	twi_error_flags |= flag;
	twi_error_count++;
}

void _twiErrorClearFlag(uint8_t flag) {
	twi_error_flags &= ~flag;
}

void _twiErrorClearAll(void) {
	twi_error_flags = 0;
	twi_error_count = 0;
}

uint8_t twiErrorGetFlags(void) {
	return twi_error_flags;
}

uint16_t twiErrorGetCount(void) {
	return twi_error_count;
}
// ############################################################################
// ####################### INTERNAL USE FUNCTIONS #############################
// ############################################################################


/*
 * _twiWaitForTWINT: Wait for TWI module to complete current action.
 */
static void _twiWaitForTWINT(void) {
	loop_until_bit_is_set(TWCR0, TWINT);
}


/*
 * _twiStatusIs: Confirm that the status check requested is true.
 * Returns 1 if the status is confirmed, or 0 if the check fails.
 *
 * See DS p. 280 and http://www.atmel.com/webdoc/AVRLibcReferenceManual/ch20s33s01.html

 * The following defined statuses come from <util/twi.h>:
 	TW_START		0x08	A start condition has been transmitted.
 	TW_REP_START	0x10	A repeated start condition has been transmitted.
 	Master Transmit mode
 	TW_MT_SLA_ACK	0x18	SLA+W has been transmitted, ACK has been received.
 	TW_MT_SLA_NACK	0x20	SLA+W has been transmitted, NOT ACK has been received.
 	TW_MT_DATA_ACK	0x28	Data byte has been transmitted, ACK has been received.
 	TW_MT_DATA_NACK	0x30	Data byte has been transmitted, NOT ACK has been received.
 	TW_MT_ARB_LOST	0x38	Arbitration lost in SLA+W or data bytes.
 	Master Receive mode
 	TW_MR_SLA_ACK	0x40	SLA+R has been transmitted, ACK has been received.
 	TW_MR_SLA_NACK	0x48	SLA+R has been transmitted, NOT ACK has been received.
 	TW_MR_DATA_ACK	0x50	Data byte has been received, ACK has been returned.
 	TW_MR_DATA_NACK	0x58	Data byte has been received, NOT ACK has been returned.
 	Other
 	TW_NO_INFO		0xF8	No state information available.
 	TW_BUS_ERROR	0x00	Bus error. Illegal start or stop condition.
 */
uint8_t _twiStatusIs(uint8_t twi_status) {
	return ((TWSR0 & TW_STATUS_MASK) == twi_status);
}


/*
 * twiSetStartCondition: Set start condition on TWI line.
 * The TWINT flag must be cleared by software by writing a logic 1 to it.
 * Note that clearing this flag starts the operation of the TWI, so all access to the TWI address
 * register (TWARn), TWI status register (TWSRn), and TWI data register (TWDRn) must be complete
 * _before_ clearing this flag.
 * A START condition is signaled by pulling the SDA line from
 * high to low when the SCL line is high.
 */
static void _twiSetStartCondition(void) {
	TWCR0 = (1<<TWINT)|(1<<TWEN)|(1<<TWSTA);
	_twiWaitForTWINT();
}

/*
 * _twiSetStopCondition: Set a stop condition on the TWI line
 * A STOP condition is signaled by pulling the SDA line from
 * low to high when the SCL line is high.
 */
void _twiSetStopCondition(void) {
	TWCR0 = (1<<TWINT) | (1<<TWEN) | (1<<TWSTO);
}


/*
 * _twiTransmitNoAck: In Master mode, trigger data transmission without enabling
 * setting the 9th ACK bit. This tells the slave there was a problem or no more
 * data are needed.
 */
static void _twiTransmitNoAck(void) {
	TWCR0 = (1<<TWINT)|(1<<TWEN);
	_twiWaitForTWINT();
}

/*
 * twiTransmitAck: In Master mode, trigger data transmission and enable setting
 * the 9th ACK bit to signal the sender to continue transmission.
 */
static void _twiTransmitAck(void) {
	TWCR0 = (1<<TWINT) | (1<<TWEN) | (1<<TWEA);
	_twiWaitForTWINT();
}



/*
 * Send a data byte on the TWI line, don't set 9th ACK bit.
 */
void _twiSend(uint8_t data) {
	TWDR0 = data;
	_twiTransmitNoAck();
	// TODO: Shouldn't this check if ACK was received?
}

// Read in from slave, sending ACK when done (sets TWEA).
uint8_t _twiReadAck(void) {
	_twiTransmitAck();
	return(TWDR0);
}
// Read in from slave (or master) without sending ack
uint8_t _twiReadNoAck(void) {
	_twiTransmitNoAck();
	return(TWDR0);
}

/*
 *  Issues a start condition and sends SLA+W or SLA+R.
 *  If the slave device is busy, use ACK polling to wait until device is ready.
 */
void _twiStartWait(uint8_t address) {
    while(1) {
        // Send start condition
		_twiSetStartCondition();		// Set start on TWI bus and check result.
		if ( _twiStatusIs(TW_START)||_twiStatusIs(TW_REP_START)) {
			;	// Do nothing, for now. All is good.
		}/* else {
			_twiErrorFlag(TWI_ERROR_START);
			if (_twiStatusIs(TW_BUS_ERROR)) {
				_twiErrorFlag(TWI_ERROR_BUS);
			}
			continue;
		}*/
		// TWSTA must be cleared by	software when the START condition has been transmitted.
		_twiSend(address);			// Send device address. Expected to contain R/W bit.
		if( _twiStatusIs(TW_MT_SLA_ACK)||_twiStatusIs(TW_MR_SLA_ACK) ) {
			; // For now, do nothing. All is ok with transmission.
		} else {
			_twiSetStopCondition();							// Device busy, stop operation.
			if (_twiStatusIs(TW_MT_SLA_NACK)) {
				_twiErrorFlag(TWI_ERROR_SLACK);
			} else if (_twiStatusIs(TW_BUS_ERROR)) {
				_twiErrorFlag(TWI_ERROR_BUS);
			}
			continue;
		}
        // if( twst != TW_MT_SLA_ACK) return 1 // TODO: Actually do this!
        break;
    }
}


/*
 *  Issues a start condition and sends address with transfer direction (R/W).
 *  Return 0 = device accessible, 1 = failed to access device
 */
uint8_t _twiStart(uint8_t address) {
    // Send start condition
	_twiSetStartCondition();
    if(!(_twiStatusIs(TW_START) || _twiStatusIs(TW_REP_START)) ) {
		_twiErrorFlag(TWI_ERROR_START);
		if (_twiStatusIs(TW_BUS_ERROR)) {
			_twiErrorFlag(TWI_ERROR_BUS);
		}		return twiErrorGetFlags();
	}
    
    _twiSend(address);						// Send SLA+W or SLA+R
	if(!(_twiStatusIs(TW_MT_SLA_ACK) || _twiStatusIs(TW_MR_SLA_ACK)) ) {
		_twiErrorFlag(TWI_ERROR_SLACK);
		return twiErrorGetFlags();
	}
    return 0;
}



void _twiStartRegisterWrite(uint8_t slave_address, uint8_t register_address) {
	// Set start condition on TWI bus
	_twiSetStartCondition();
	if( !_twiStatusIs(TW_START)) {
		;// Do nothing here
	}
	
	// Send slave address with write bit
	_twiSend(TWI_SLA_WRITE(slave_address));
	if ( !_twiStatusIs(TW_MT_SLA_ACK) ) {
		;	// Do nothing yet
	}
	
	// Send slave register address we want to access (considered data).
	_twiSend(register_address);
	if ( !_twiStatusIs(TW_MT_DATA_ACK) ) {
		;	// Do nothing yet
	}
}

/*
 * Internal function: Start a register-read transaction on TWI
 */
void _twiStartRegisterRead(uint8_t slave_address, uint8_t register_address) {
	_twiStartRegisterWrite(slave_address, register_address);
	
	// Set start condition again, here considered a re-start.
	_twiSetStartCondition();
	if((TWSR0 & 0xF8) != TW_REP_START) {
		;	// Do nothing yet.
	}

	// Send slave address with read bit.
	_twiSend(TWI_SLA_READ(slave_address));
	if ( !_twiStatusIs(TW_MR_SLA_ACK) ) {
		;	// Do nothing yet.
	}
	
}


// ############################################################################
// #################### END OF INTERNAL USE FUNCTIONS #########################
// ############################################################################









// ############################################################################
// ############################# PUBLIC FUNCTIONS #############################
// ############################################################################

/*
 * twiEnable: Initialize the TWI peripheral for operation.
 *
 * Note that the internal pull-ups in the AVR pads can be enabled by
 * setting the PORT bits corresponding to the SCL and SDA pins, as explained in the I/O Port section. The
 * internal pull-ups can in some systems eliminate the need for external ones (DS p.303).
 */
void twiEnable(void) {
	PRR0  &= ~(1<<PRTWI0);					// Start clock to TWI peripheral.
	//TWSR0 &= ~((1<<TWPS1)|(1<<TWPS0));		// Prescaler, not needed if it's 0b00.
	TWSR0 &= ~(0b11);						// Clear prescaler bits
	TWSR0 |= (0<<TWPS1|0<<TWPS0);			// Set prescaler bits: 00 for 1, 01 for 4, 10 for 16, 11 for 64.
	TWBR0 = 32;								// 32 for 100k, 2 for 400k (with 8MHz, prescaler at 1).
	TWCR0 |= (1<<TWEN);						// Enable TWI
}

// Deactivate the TWI0 module and set SDA and SCL pins to low power consumption.
void twiDisable(void) {
	TWCR0 &= ~(1<<TWEN);					// Disable TWI, may not be necessary
	PRR0  |= (1<<PRTWI0);					// Stop TWI clock
	DDRC  &= ~((1<<DDRC4)|(1<<DDRC5));		// Set TWI pins as inputs
	PORTC |= ((1<<PORTC4)|(1<<PORTC5));		// Pull them up to reduce power consumption.
}

/*
 *	twiReadRegister8: Reads data from an 8-bit register of slave device.
 *
 *	Requires a working TWI interface.
 *
 *	Input:
 *		slave_address:		The 8-bit slave address of slave TWI device. Address is on 7 MSBs.
 *		register_address:	The 8-bit address to be read from slave.
 *
 *	Output:
 *		8-bit value of the register.
 */
uint8_t twiDRead8(uint8_t slave_address, uint8_t register_address) {
	uint8_t register_data;
	_twiStartRegisterRead(slave_address, register_address);

	register_data = _twiReadNoAck();
	if ( !_twiStatusIs(TW_MR_DATA_NACK) ) {
		return 0xFA;
	}
	
	// Set stop condition
	_twiSetStopCondition();
	return register_data;
}

uint16_t twiDRead16(uint8_t slave_address, uint8_t register_address) {
	uint8_t data_MSB;
	uint8_t data_LSB;
	uint16_t data_16b;
	_twiStartRegisterRead(slave_address, register_address);
	data_MSB = _twiReadAck();
	if ( !_twiStatusIs(TW_MR_DATA_ACK)) {
		return 0xFD;
	}
	data_LSB = _twiReadNoAck();
	if ( !_twiStatusIs(TW_MR_DATA_NACK)) {
		return 0xFE;
	}
	
	uartSendByte(data_MSB);
	uartSendByte(data_LSB);
	
	_twiSetStopCondition();
	data_16b = data_MSB;
	data_16b <<= 8;
	data_16b |= data_LSB;
	return data_16b;
}

void twiDWrite8(uint8_t slave_address, uint8_t register_address, uint8_t write_value) {
	_twiStartRegisterWrite(slave_address, register_address);
	_twiSend(write_value);
	if( !_twiStatusIs(TW_MT_DATA_ACK) ) {
		;	// Do something
	}
	_twiSetStopCondition();
}
/*
 *	twiReadRegister16: Reads data from a 16-bit register of slave device.
 *
 *	Requires a working TWI interface.
 *	
 *	Input:
 *		register_address: An 8-bit register address. Best to select a defined address macro from the header file.
 *	Output:
 *		16-bit value of the register. 
 */
uint16_t twiReadRegister16(uint8_t slave_address, uint8_t register_address ) {
	uint16_t dataMSB;
	uint8_t dataLSB;
	_twiStart(TWI_SLA_WRITE(slave_address));
	_twiSend(register_address);
	_twiStart(TWI_SLA_READ(slave_address));
	dataMSB = _twiReadAck();
	dataLSB = _twiReadNoAck();
	_twiSetStopCondition();
	dataMSB <<= 8;
	return dataMSB|dataLSB;
}

/*
 *	twiWriteRegister8: Writes data on an 8-bit register of the device. Works with 8-bit address.
 *
 *	Requires a working TWI interface.
 */
void twiWriteRegister8( uint8_t slave_address, uint8_t register_address, uint8_t register_value ) {
	_twiStart(TWI_SLA_WRITE(slave_address));
	_twiSend(register_address);
	_twiSend(register_value);
	_twiSetStopCondition();
}

/*
 *	twiWriteRegister: Writes data on given 16-bit register of the device. Works with 8-bit address.
 *
 *	Requires a working TWI interface.
 */
void twiWriteRegister16( uint8_t slave_address, uint8_t register_address, uint16_t register_value ) {
	uint8_t dataMSB = (uint8_t) (register_value>>8);
	uint8_t dataLSB = (uint8_t) (register_value);
	_twiStart(TWI_SLA_WRITE(slave_address));
	_twiSend(register_address);
	_twiSend(dataMSB);
	_twiSend(dataLSB);
	_twiSetStopCondition();
}

/*
 *	twiStream: Sends a stream of bytes to a slave device over TWI bus.
 * 
 *	Input:
 *		address: The 8-bit address of the slave device.
 *		*data:	A null-terminated array of 8-bit values (string)
 *
 *	This function is mainly used to send data to a character LCD display connected via TWI.
 */
void twiStream(uint8_t address, uint8_t *data) { // Expects a '\0' terminated string
	_twiStart(TWI_SLA_WRITE(address));
	for(uint8_t i=0; data[i]; i++) {
		_twiSend(data[i]);
	}
	_twiSetStopCondition();
}



// ############################################################################
// ################################# END ######################################
// ############################################################################