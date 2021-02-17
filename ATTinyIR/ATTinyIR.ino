/*
	R. J. Tidey 2021/01/14
	Test for IR driver for ATTiny
 */
#include <Arduino.h>
#include "TinyIrUart.h"
#include <avr/eeprom.h>
#include <avr/sleep.h>
#include <avr/power.h>
#include <avr/wdt.h>
#include <avr/interrupt.h>
#include <avr/boot.h>

#define PORT3_MASK 0x08

//used for debugging, comment out to disable
//#define FLAG_MASK 0x20
#define FLAG_MASK 0x20
#ifdef FLAG_ACTIVE
	#define FLAG_ON PORTB |= FLAG_MASK;
	#define FLAG_OFF PORTB &= ~FLAG_MASK;
	#define FLAG_PULSE PORTB |= FLAG_MASK;PORTB &= ~FLAG_MASK;
#else
	#define FLAG_ON
	#define FLAG_OFF
	#define FLAG_PULSE
#endif

#define UART_TX 0
#define UART_RX 4
#define BAUD_DIV 16 // 2400
#define MODULATION 1

//ADC direct access
#define ADC_CHAN 1 //PB2
#define ADCSRA_INIT 0x04  // DISABLED | 16 prescale 
#define ADCSRA_STARTSINGLE 0xc4 //16 prescale 
#define ADCSRA_CLEAR ADCSRA_INIT | (1 << ADIF) //16 prescale 
#define ADCSRB_INIT 0x00
#define ADMUX_INIT 0x20 | ADC_CHAN; // ADLAR Vcc Reference

//watchdog set
//#define WDTCR_ENABLE 1<<WDIE | 1<<WDE | 1<<WDP1 | 1<<WDP0 //128mS
#define WDTCR_ENABLE 1<<WDIE | 1<<WDE | 1<<WDP2 //256mS

// ticks for a 8MHz clock
#define DFLT_TICKS0 108

uint8_t txtOK[4] = {'O','K',13,10};
uint8_t txtBUT[12] = {'B','x',13,10};
uint8_t txtBAD[5] = {'B','A','D',13,10};

uint8_t rxGet[32];
uint8_t txCount;
uint8_t rxCount;

#define PARAMETERS_MAX 16
uint8_t rxParameters[PARAMETERS_MAX];
uint8_t rxParameterCount;

#define IRDEVICES_MAX 10
//ir code for a device
//DEVICE_TYPE device number * 16 + 0=NEC, 1=RC5(11), 2=RC6(16), 3=RC6EXT(32)
#define IRDEVICE_TYPE 0
#define IRDEVICE_ADDR 1
#define IRDEVICE_CMD1 2
#define IRDEVICE_CMD2 3
#define IRDEVICE_CMD3 4
#define IRDEVICE_FIELDS 5
uint8_t irDevice[IRDEVICE_FIELDS];
uint8_t irToggles[IRDEVICES_MAX];
uint8_t rcLengths[3] = {11,16,32};
uint8_t irReady = 0;
uint8_t doDelay = 0;
uint8_t ticks0 = DFLT_TICKS0;

// add,delete,read,save,transmit,execute
#define NUM_COMMANDS 8
uint8_t cmds[NUM_COMMANDS] = {'c','m','o','r','s','t','x','z'};

#define EEPROM_MACRO_BASE 256
#define EEPROM_MACRO_LEN 20
#define EEPROM_MACRO_MAX 10
#define EEPROM_CODE_BASE 0
#define EEPROM_CODE_LEN IRDEVICE_FIELDS
#define EEPROM_CODE_MAX 32
#define EEPROM_INITVAL 90
#define EEPROM_INITED 500
#define EEPROM_OSCCAL 501
#define EEPROM_TICKS0 502
uint8_t* macroPtr = 0;

// holds button pressed number, 0xff means up
uint8_t buttonDown = 0xff;
uint8_t buttonUp = 1;
//button ladder 47k,4k7,4k7,10k,10k,22k,22k,47k,100k,220k
#define NUMBER_BUTTONS 10
#define BUTTON_TOLERANCE 5
//button nominal adc 0,93,171,299,394,534,624,736,843,924,1023
// use nearest 8 bit equivalent
uint8_t buttonValue[NUMBER_BUTTONS] = {2,24,43,75,98,136,156,184,211,231};
uint8_t saveADCSRA;
//sleep control
#define SLEEP_ON 0 //sleep until button pressed
#define SLEEP_OFF_TEMP 1 //suspend sleep (will time out if no button activity)
#define SLEEP_OFF_PERM 2 //sleep off (until reset by serial)
#define SUSPEND_SLEEP 1600 // suspend sleep time button down in 3mS units
#define RESTART_SLEEP 30000 // restart sleep  from suspend time button up in 3mS units
uint8_t sleepMode = SLEEP_ON;
uint16_t wakeCounter = 0;

ISR(WDT_vect) {
	MCUSR = 0x00; // Clear WDRF in MCUSR/
	WDTCR |= (1<<WDCE) | (1<<WDE); // Write logical one to WDCE and WDE/
	WDTCR = 0x00; // Turn off WDT
}

uint8_t getAnalog() {
	//start conversion
	ADCSRA = ADCSRA_STARTSINGLE;
	//wait till complete
	while((ADCSRA & (1 << ADIF)) == 0) {
	}
	//reset complete flag
	ADCSRA = ADCSRA_CLEAR;
	return ADCH;
}

void sleepTillButton () {
	//put uart tx low to lower current
	PORTB &= ~(1<<UART_TX);
	while(getAnalog() > 252) {
		saveADCSRA = ADCSRA;
		ADCSRA = 0;
		power_all_disable();
		noInterrupts();
		sleep_bod_disable();
		WDTCR = WDTCR_ENABLE;
		set_sleep_mode(SLEEP_MODE_PWR_DOWN);
		sleep_enable();
		interrupts();
		FLAG_PULSE
		sleep_mode();
		//resume here after watchdog wakes up from sleep
		sleep_disable();
		power_all_enable();
		ADCSRA = saveADCSRA;
		FLAG_PULSE
	}
	//restore uart tx high
	PORTB |= 1<<UART_TX;
}

//get ,separated parameters from serial data line
void parseParameters() {
	uint8_t c,i,p;
	rxParameterCount = 0;
	i = 1;
	p =0;
	while(i < rxCount && rxParameterCount < PARAMETERS_MAX) {
		c = rxGet[i];
		if(c != ',' || i == (rxCount-1)) {
			p = p * 10 + c - 48;
		}
		if(c == ',' || i == (rxCount-1)) {
			rxParameters[rxParameterCount++] = p;
			p = 0;
		}
		i++;
	}
}

// check for received char if available and check for complete line
uint8_t checkRX() {
	uint8_t r = 0;
	while(tinyIU_rxByteCount() && r == 0) {
		rxGet[rxCount] = tinyIU_getRx();
		rxCount++;
		r = (rxGet[rxCount-1] == 0x0a);
	}	
	return r;
}

// send chars and wait for complete or wait mSec
void sendTx(uint8_t* tData, uint8_t tLen, uint8_t wait) {
	if(sleepMode) {
		tinyIU_sendTx(tData, tLen);	
		while(tinyIU_txByteCount() && wait < 100) {
			wait++;
			delay(1);
		}
	}
}

// send 1 decimal number with terminator 0=, or 1 =crlf
void sendDecimalField(uint8_t b, uint8_t terminator) {
	uint8_t buffer[5];
	buffer[0] = 48;
	while(b >= 100){
		buffer[0]++;
		b -= 100;
	}
	buffer[1] = 48;
	while(b >= 10){
		buffer[1]++;
		b -= 10;
	}
	buffer[2] = 48 + b;
	if(!terminator) {
		buffer[3] = ',';
		sendTx(buffer, 4, 50);
	} else {
		buffer[3] = 13;
		buffer[4] = 10;
		sendTx(buffer, 5, 50);
	}
}

// send a dump of an EEPROM region as csv decimals
void sendDecimalFields(uint8_t* base, uint8_t fields, uint8_t count) {
	uint8_t i,j;
	uint8_t b;
	uint8_t* p = base;
	for(i = 0; i < count; i++) {
		b = i; // send index number first	
		for(j = 0; j <= fields; j++) {
			if(j) {
				b = eeprom_read_byte(p);
				p++;
			}
			sendDecimalField(b, ((j < fields) ? 0 : 1));
		}
	}
}

// save an IR code to EEPROM
void addCode() {
	if(rxParameters[0] < EEPROM_CODE_MAX) {
		uint8_t* d = (uint8_t*)(EEPROM_CODE_BASE + rxParameters[0] * EEPROM_CODE_LEN);
		eeprom_write_block(&rxParameters[1], d, EEPROM_CODE_LEN);
		sendTx(txtOK, 4, 50);	
	} else {
		sendTx(txtBAD, 5, 50);			
	}
}

// save a macro to EEPROM
void addMacro() {
	if(rxParameters[0] < EEPROM_MACRO_MAX) {
		uint8_t* d = (uint8_t*)(EEPROM_MACRO_BASE + rxParameters[0] * EEPROM_MACRO_LEN);
		// macro terminator
		rxParameters[rxParameterCount] = 255;
		eeprom_write_block(&rxParameters[1], d, EEPROM_MACRO_LEN);
		sendTx(txtOK, 4, 50);	
	} else {
		sendTx(txtBAD, 5, 50);			
	}
}

// clock parameters command
void clkParams() {
	switch(rxParameters[0]) {
		case 0: // read OSCCAL and ticks
				sendDecimalField(OSCCAL, 0);
				sendDecimalField(ticks0, 1);
				break;
		case 1: // increment or decrement OSCCAL
				OSCCAL = OSCCAL + (rxParameters[1] ? -1 : 1);
				eeprom_write_byte((uint8_t*)EEPROM_OSCCAL, OSCCAL);
				sendTx(txtOK, 4, 50);	
				break;
		case 2: //increment or decrement ticks0
				ticks0 = ticks0 + (rxParameters[1] ? -1 : 1);
				eeprom_write_byte((uint8_t*)EEPROM_TICKS0, ticks0);
				tinyIU_init(ticks0, BAUD_DIV, MODULATION, UART_TX, UART_RX);
				sendTx(txtOK, 4, 50);	
				break;
	}
}

//send code and macro areas of EEPROM
void readAll() {
	sendDecimalFields((uint8_t*)EEPROM_CODE_BASE, EEPROM_CODE_LEN, EEPROM_CODE_MAX);
	sendDecimalFields((uint8_t*)EEPROM_MACRO_BASE, EEPROM_MACRO_LEN, EEPROM_MACRO_MAX);
}

//transmit an IR code from parameters received
void transmitCode() {
	irDevice[IRDEVICE_TYPE] = rxParameters[0];
	irDevice[IRDEVICE_ADDR] = rxParameters[1];
	irDevice[IRDEVICE_CMD1] = rxParameters[2];
	irDevice[IRDEVICE_CMD2] = rxParameters[3];
	irDevice[IRDEVICE_CMD3] = rxParameters[4];
	irReady = 1;
	sendTx(txtOK, 4, 50);	
}

//execute a macro sequence from EEPROM
void executeMacro(uint8_t macro) {
	if(macro < EEPROM_MACRO_MAX) {
		macroPtr = (uint8_t*)(EEPROM_MACRO_BASE + macro * EEPROM_MACRO_LEN);
	}
}

//transmit an IR code from EEPROM
void executeCode(uint8_t code) {
	if(code < EEPROM_CODE_MAX) {
		eeprom_read_block(irDevice,(uint8_t*)(EEPROM_CODE_BASE + code * EEPROM_CODE_LEN), EEPROM_CODE_LEN);
		irReady = 1;
		sendTx(txtOK, 4, 50);	
	}
}

// process an rx command
void handleCmd(uint8_t cmd) {
	parseParameters();
	switch(cmd) {
		case 0 : // 'c'
				addCode();
				break;
		case 1 : // 'm'
				addMacro();
				break;
		case 2 : // '0'
				clkParams();
				break;
		case 3 : // 'r'
				readAll();
				break;
		case 4 : // 's'
				sleepMode = rxParameters[0];
				break;
		case 5 : // 't'
				transmitCode();
				break;
		case 6 : // 'x'
				executeMacro(rxParameters[0]);
				break;
		case 7 : // 'z'
				executeCode(rxParameters[0]);
				break;
	}
}

// macro processing
void handleMacro() {
	uint8_t m = eeprom_read_byte(macroPtr);
	uint8_t* p;
	if(m == 0xff) {
		macroPtr = 0;
		sendTx(txtOK, 4, 50);	
	} else {
		doDelay = m & 0xe0;
		m &= 0x1f;
		if(m != 0x1f) {
			p = (uint8_t*)(EEPROM_CODE_BASE + (m & 0x0f) * EEPROM_CODE_LEN);
			eeprom_read_block(irDevice, p, EEPROM_CODE_LEN);
			irReady = 1;
		}
		macroPtr++;
	}
}

//ir code processing
void handleIR() {
	uint8_t toggle;
	uint8_t t,d;
	if(tinyIU_irFree()) {
		t = irDevice[IRDEVICE_TYPE];
		if(t == 0) {
			tinyIU_sendNEC(irDevice[IRDEVICE_ADDR], irDevice[IRDEVICE_CMD1]);
		} else {
			d = t >> 4;
			t &= 0x0f;
			toggle = irToggles[d];
			irToggles[d] ^= 1;
			tinyIU_sendRC(rcLengths[t-1],toggle, irDevice[IRDEVICE_ADDR],
				irDevice[IRDEVICE_CMD1], irDevice[IRDEVICE_CMD2], irDevice[IRDEVICE_CMD3]);
		}
		irReady = 0;
	}
}

//serial receive processing
void handleRX() {
	//remove lf and terminate String
	uint8_t i;
	uint8_t c;
	
	rxGet[rxCount-1] = 0;
	rxCount--;
	//remove cr if present
	if(rxCount && rxGet[rxCount-1] == 0x0d) {
		rxGet[rxCount-1] = 0;
		rxCount--;
	}
	if(rxCount) {
		c = rxGet[0];
		for(i = 0; i < NUM_COMMANDS; i++) {
			if(c == cmds[i]) {
				break;
			}
		}
		if(i  < NUM_COMMANDS) {
			handleCmd(i);
		} else {
			sendTx(txtBAD, 5, 50);			
		}
	}
	rxCount = 0;
}

// check buttons for actions
void handleButtons() {
	uint8_t b;
	int16_t d;
	uint8_t adc = getAnalog();

	if(adc > 250) {
		buttonDown = 0xff;
	} else if(buttonDown != 0xfe) {
		for(b = 0; b < NUMBER_BUTTONS; b++) {
			d = adc - buttonValue[b];
			if(d < BUTTON_TOLERANCE && d > -BUTTON_TOLERANCE) {
				if(b == (buttonDown & 0xf)) {
					// 2 successive readings indicate same button
					txtBUT[1] = b + 48;
					sendTx(txtBUT, 4, 50);
					executeMacro(b);
					//flag button processed
					buttonDown = 0xfe;
				} else {
					// 
					buttonDown = b;
				}
				break;
			} 
		}
	}
}

void setClock(uint8_t setDefaults) {
	if(setDefaults || eeprom_read_byte((uint8_t*)EEPROM_INITED) != EEPROM_INITVAL) {
		eeprom_write_byte((uint8_t*)EEPROM_OSCCAL, OSCCAL);
		eeprom_write_byte((uint8_t*)EEPROM_TICKS0, DFLT_TICKS0);
		eeprom_write_byte((uint8_t*)EEPROM_INITED, EEPROM_INITVAL);
	}
	OSCCAL = eeprom_read_byte((uint8_t*)EEPROM_OSCCAL);
	ticks0 = eeprom_read_byte((uint8_t*)EEPROM_TICKS0);
}

void setup() {
	uint8_t i;
	// get clock set up from low fuse bits
	uint8_t clkSel = boot_lock_fuse_bits_get(GET_LOW_FUSE_BITS) & 0x0f;
	cli(); // Disable interrupts
	if(clkSel == 1) {
		//pll operation 16MHz so scale down to 8MHz
		CLKPR = (1<<CLKPCE); // Prescaler enable
		CLKPR = 1; // Clock division factor 2 8MHz
	}
	TCCR1 = (TCCR1 & 0xF0) | 6; // timer1 prescale 32 for 4uS ticks
	sei(); // Enable interrupts	int i;
	ADCSRA = ADCSRA_INIT;
	ADCSRB = ADCSRB = ADCSRB_INIT;
	ADMUX = ADMUX_INIT;
	//reset to defaults if button 0 down during power up
	setClock(getAnalog() < 3);
	for(i = 0; i < IRDEVICES_MAX; i++){
		irToggles[i] = 0;
	}
	tinyIU_init(ticks0, BAUD_DIV, MODULATION, UART_TX, UART_RX);
	txCount = 0;
	rxCount = 0;
	//set port 3 output low
	PORTB &= ~PORT3_MASK;
	DDRB |= PORT3_MASK;
	//set FLAG output low
	PORTB &= ~FLAG_MASK;
	DDRB |= FLAG_MASK;
}

void loop() {
	if(checkRX() && rxCount) {
		handleRX();
	}
	if(doDelay == 0) {
		if(irReady == 1) {
			handleIR();
		} else if(macroPtr) {
			handleMacro();
		} else {
			handleButtons();
		}
	} else if(tinyIU_irFree()) {
		doDelay--;
	}
	if(buttonDown != 0xff || sleepMode) {
		delay(3);
		switch(sleepMode) {
			case SLEEP_ON : wakeCounter++;
							if(wakeCounter > SUSPEND_SLEEP) {
								sleepMode = SLEEP_OFF_TEMP;
								wakeCounter = 0;
							}
							break;
			case SLEEP_OFF_TEMP :
							if(buttonDown == 0xff) {
								wakeCounter++;
								if(wakeCounter > RESTART_SLEEP) {
									sleepMode = SLEEP_ON;
								}
							} else {
								wakeCounter = 0;
							}
							break;
			case SLEEP_OFF_PERM : break;
		}
	} else {
		wakeCounter = 0;
		sleepTillButton();
	}
	FLAG_PULSE
}
