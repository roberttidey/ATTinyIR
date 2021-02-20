// BitTx.cpp
//
//
// General purpose interrupt driven bit transmitter for ATTiny85
// Message buffer consists of 16 bit words.
//	Bit 7 is level to write to pin
//	Bits 6 - 0 is tick count to maintain this level before next change
//	Tick period is 26us
//  Minimum period should be > 2 ticks 52uS 
//  Maximum period is 127 * 26 = 3.2mSec use multiple periods fotlonger
// ir carrier modulation for 1 state (must use pin 2 or 14)
//
// Supports a serial RX, TX at 1200, 2400, 4800 baud
// Recommended to use 2400
// Author: Bob Tidey (robert@tideys.co.uk)

#include "TinyIrUart.h"
#include <Arduino.h>

#define IRMask 2	//PB1 OC0B
#define DFLT_TIM0TICKS 108 // 26uS (38.4KHz) 8MHz clock

//variables
uint8_t tim0Ticks;
uint8_t irData[80];
static int ir_msg_free = 1; //set 0 to activate ir message sending
static uint8_t irNextPeriod = 0;
static uint8_t irModulated = 1;
static uint8_t rxtxBaud; // Divider 4,8,16,32 (9600,4800,2400,1200)
static uint8_t* tx_bufptr; // the message buffer pointer during transmission
static uint8_t TXMask; //mask for TX pin used for transmit
static uint8_t TXMaskI; //inverted mask for TX pin used for transmit
static uint8_t RXMask; //mask for TX pin used for transmit
static uint8_t* TXBytes; //Pointer to transmit data
static uint8_t TXByte; //byte to transmit uart
static uint8_t TXByteCount; //counter for bytes to transmit
static int8_t TXState = 0; //transmit uart state
static uint8_t TXDiv; //Divider during tx
static uint8_t RXBuffer[RXBUFFER_MASK + 1]; //bytes being received
static uint8_t RXBufferHead = 0;
static uint8_t RXBufferTail = 0;
static uint8_t RXTicks; //Tick counter to sample bit
static uint8_t RXByte; //byte being received
static uint8_t RXState; //receive uart state

ISR(TIM0_OVF_vect) {
	if(ir_msg_free == 0) {
		irNextPeriod--;
		if(irNextPeriod == 0) {
			if (*tx_bufptr & 0x80){
				DDRB |= IRMask;
			} else {
				DDRB &= ~IRMask;
			}
			//Set next period
			irNextPeriod = *tx_bufptr & 0x7f;
			//terminate isr if a period is 0 or short (52uSec)
			if(irNextPeriod < 2) {
				//stop ir
				ir_msg_free = 1;
				//set input no pull up
				DDRB &= ~IRMask;
				PORTB &= ~IRMask;
			} else {
				tx_bufptr++;
			}
		}
	}
	if(TXByteCount) {
		if(TXDiv) {
			TXDiv--;
		} else {
			if(TXState == 0) {
				// Start bit
				PORTB &= TXMaskI; 
			} else if(TXState < (TXCOUNT - 1)) {
				//Data Bit
				if(TXByte & 1) {
					PORTB |= TXMask;
				} else {
					PORTB &= TXMaskI; 
				}
				TXByte >>= 1;
			} else {
				//Stop bit
				PORTB |= TXMask;
				if(TXState == TXCOUNT) {
					TXByteCount--;
					TXBytes++;
					TXByte = TXBytes[0];
					TXState = -1;
				}
			}
			TXState++;
			TXDiv = rxtxBaud - 1;
		}
	}
	if(RXState) {
		RXTicks++;
		if(RXTicks == rxtxBaud) {
			if(RXState == 10) {
				RXBuffer[RXBufferHead] = RXByte;
				RXBufferHead = (RXBufferHead + 1) & (RXBUFFER_MASK); 
				RXState = 0;
			} else {
				RXByte >>= 1;
				if((PINB & RXMask) != 0) {
					RXByte |= 0x80;
				}
				RXTicks = 0;
				RXState++;
			}
		}
	}
}

ISR(PCINT0_vect) {
	if(RXState == 0) {
		//start bit, set sample at half bit intervals
		RXTicks = (rxtxBaud >> 1) - 1;
		RXState++;
	}
}

/**
  Set things up to transmit messages
**/
void tinyIU_init(uint8_t ticks, uint8_t baud, uint8_t modulation, uint8_t TXpin,  uint8_t RXpin) {
	//38.4KHz, 26.0uS for a 8Mz clock
	uint8_t ocr = ticks ? ticks : DFLT_TIM0TICKS;
	TCCR0B = 0;			// Stop Counter
	ir_msg_free = 1;	// clear message send flag
	OCR0A = ocr;
	OCR0B = OCR0A >> 1;	//50% duty cycle
	irModulated = modulation;
	//set ir input no pull up
	DDRB &= ~IRMask;
	PORTB &= ~IRMask;
	if(irModulated) {
		TCCR0A = 0x31;	//OCR0B set/clear, PWM PHASE CORRECT
	} else {
		TCCR0A = 0x01;	//NORMAL, PWM PHASE CORRECT
	}
	TIMSK &= ~(1<<TOIE0); //Disable Timer0 Overflow Interrupt
	rxtxBaud = baud;
	TXMask = 1 << TXpin;
	TXMaskI = ~TXMask;
	if(TXpin >=0) {
		TXMask = 1 << TXpin;
		TXMaskI = ~TXMask;
		PORTB |= TXMask;
		DDRB |= TXMask;
	}
	RXMask = 1<< RXpin;
	if(RXpin >=0) {
		GIMSK |= (1<<PCIE);
		PCMSK |= RXMask;
		RXBufferHead = 0;
		RXBufferTail = 0;
		RXState = 0;
		//input no pull up
		DDRB &= ~RXMask;
		PORTB &= ~RXMask;
	}
	TCCR0B = 9; //Start counter TOP = OCR0A, 16MHz
	TIMSK |= (1<<TOIE0); //Enable Timer0 Overflow Interrupt
}

/**
  Send a message
**/
void tinyIU_sendIr(uint8_t* irD) {
	tx_bufptr = irD;
	if(irModulated == 0) {
		//set to output high for non modulated
		PORTB |= IRMask;
	}
	TCNT0 = OCR0B + 1;
	ir_msg_free = 0;
}

/**
  send a NEC coded message
    addr, ~addr, cmd, ~cmd
	irData buffer needs 72 bytes of space
**/
void tinyIU_sendNEC(uint8_t	 addr, uint8_t cmd) {
	uint8_t i;
	uint8_t j;
	uint8_t k;
	uint8_t b;
	irData[0] = 0xFF; // 3.3mS high
	irData[1] = 0xFF; // 3.3mS high
	irData[2] = 0xDC; // 2.4mS high
	irData[3] = 0x7F; // 3.3mS low
	irData[4] = 0x2E; // 1.2mS low
	k = 5;
	for(i=0; i < 4; i++) {
		switch(i) {
			case 0 : b = addr; break;
			case 1 : b = ~addr; break;
			case 2 : b = cmd; break;
			case 3 : b = ~cmd; break;
		}
		for(j=0; j < 8; j++) {
			irData[k++] = 0x96; //570uS high
			if(b & 1){
				irData[k++] = 0x40; //1.68mS low
			} else {
				irData[k++] = 0x16; //570uS low
			}
			b >>= 1;
		}
	}
	irData[k++] = 0x96; //560uS high
	irData[k++] = 0x16; //560uS low
	irData[k++] = 0x00; //end
	tinyIU_sendIr(irData);
}

/**
  make a rc5 or rc6 coded message
    rcType 11 = rc5 else rc6 bitCount (16 or 32)
	msg buffer needs 42(rc5), 78 bytes of space
**/
void tinyIU_sendRC(uint8_t rcType, uint8_t toggle, uint8_t addr, uint8_t cmd, uint8_t cmdEx1, uint8_t cmdEx2) {
	uint8_t d;
	uint8_t i;
	uint8_t j;
	uint8_t b;
	uint8_t k;
	uint8_t t = 0;
	uint8_t period;
	uint8_t p;

	if(rcType > 12) {
		//rc6 start pulse
		period = 0x11; // 450uS
		irData[0] = period * 6 | 0x80; // 2.65mS high
		irData[1] = period << 1; // 900uS low
		irData[2] = period | 0x80; // 450uS high
		irData[3] = period; // 450uS low
		// field bits
		for(k = 4; k < 10; k++) {
			irData[k] = period; // 450uS low
			k++;
			irData[k] = period | 0x80; // 450uS high
		}
		p = period << 1;
		k = 10;
		if(toggle & 0x40) {
			irData[10] = p | 0x80; // high
			irData[11] = p; // low
		} else {
			irData[10] = p; // low
			irData[11] = p | 0x80; // high
		}
		k = 12;
	} else {
		// rc5
		period = 0x22; //900uS
		irData[0] = period | 0x80; // high
		irData[1] = period; // low
		//extended command bit
		if(cmd & 0x40) {
			irData[2] = period; // low
			irData[3] = period | 0x80; // high
		} else {
			irData[2] = period | 0x80; // high
			irData[3] = period; // low
		}
		if(toggle) {
			irData[4] = period; // low
			irData[5] = period | 0x80; // high
		} else {
			irData[4] = period | 0x80; // high
			irData[5] = period; // low
		}
		k = 6;
	}
	d = 0;
	for(i=0; i < 4 && t < rcType; i++) {
		p = 8;
		switch(i) {
			case 0 : b = addr;
					if(rcType < 13) {
						p = 5;
						b <<= 3;
					}
					break;
			case 1 : b = cmd;
					if(rcType < 13) {
						p = 6;
						b <<= 2;
					}
					 break;
			case 2 : b = cmdEx1; break;
			case 3 : b = cmdEx2; break;
		}
		// invert bits for rc6
		if(rcType > 12) b = ~b;
		for(j=0; j < p; j++) {
			if(b & 0x80){
				irData[k++] = period; // low
				irData[k++] = period | 0x80; // high
			} else {
				irData[k++] = period | 0x80; // high
				irData[k++] = period; // low
			}
			b <<= 1;
			t++;
			if(t == rcType) break;
		}
	}
	irData[k++] = period; //low
	irData[k++] = 0x00; //end
	tinyIU_sendIr(irData);
}

//Basic send of Tx byte
void tinyIU_sendTx(uint8_t* TxData, uint8_t TxLen) {
	TXBytes = TxData;
	TXByte = TxData[0];
	TXDiv = rxtxBaud - 1;
	TXState = 0;
	TXByteCount = TxLen;
}

//Basic receive of Rx byte
uint8_t tinyIU_getRx() {
	uint8_t ch = RXBuffer[RXBufferTail];
	RXBufferTail = (RXBufferTail + 1) & RXBUFFER_MASK;
return ch;
}

/**
  Check for ir send free
**/
uint8_t tinyIU_irFree() {
  return ir_msg_free;
}

/**
  get tx count
**/
uint8_t tinyIU_txByteCount() {
  return TXByteCount;
}

uint8_t tinyIU_rxByteCount() {
	return (RXBufferHead - RXBufferTail) & (RXBUFFER_MASK);
}

/**
  clear out RX
**/
uint8_t tinyIU_rxReset() {
	RXState = 0;
	RXBufferHead = 0;
	RXBufferTail = 0;
}