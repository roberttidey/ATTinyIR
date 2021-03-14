// TinyIrUart.h
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
// Supports a serial RX, TX at 1200, 2400, 4800, 9600 baud
// Recommended to use 2400
// Author: Bob Tidey (robert@tideys.co.uk)
#include <Arduino.h>

//Number of bits for UART RX to count to get ready
#define RXCOUNT 10
//Number of bits for UART TX to count to transmit
#define TXCOUNT 10

//RX buffer size. Should be 2^n - 1
#define RXBUFFER_MASK 7

void tinyIU_init(uint8_t ticks, uint8_t baud, uint8_t modulation, uint8_t TXpin,  uint8_t RXpin);

//Basic send of IR message, irD buffer needs to be constructed first
void tinyIU_sendIr(uint8_t* irD);

//Send NEC message
void tinyIU_sendNEC(uint8_t  addr, uint8_t cmd);

//Make rc message rcType = 0 (rc5) else rc6 and rcType has length of cmd bits
void tinyIU_sendRC(uint8_t rcType, uint8_t toggle, uint8_t addr, uint8_t cmd, uint8_t cmdEx1, uint8_t cmdEx2);

//Basic send Tx bytes
void tinyIU_sendTx(uint8_t* TxData, uint8_t TxLen);

//Basic receive of Rx byte
uint8_t tinyIU_getRx();

//Checks whether ir is free to accept a new message
uint8_t tinyIU_irFree();

//returns 0 when free
uint8_t tinyIU_txByteCount();

//returns number of Bytes available
uint8_t tinyIU_rxByteCount();

// clear out RX
void tinyIU_rxReset();
