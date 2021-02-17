# ATTiny IR transmitter
10 button remote control with each button supporting macro commands sending a sequence of codes to different devices

## Features
- Based on ATTiny85 (DigiStamp)
- Battery operated with low quiescent current for extended battery life
- Supports 10 macro commands each containing a sequence of up to 20 device codes
- variable delay between each code in a sequence
- Supports up to 32 different device codes
- Supports NEC, rc5, 5c6 type encoding
- Macros and device codes stored in EEPROM
- uart port (2400 baud) for programming device codes and macros and for testing
- timing can be tweaked to give close to 38.4KHz modulation and best baud rate

## Build and install
- Add TinyIrUart.h library for IR and Uart support
- Build under Arduino with Digistump support
- Upload over USB
- Quiescent current can be optimised
	- fuse start time to minimum
	- fuse clock select to internal 8MHz

## Operation
- Normally in sleep
- A normal button press wakes up, executes macro and immediately returns to sleep
- A long button press (> 5 seconds) leaves controller temporarily awake for about 90 seconds
- Serial port can be attached to achieve configuration while awake
- If powered on with button pressed then OSCVAl and Ticks are reset to defaults 

## Serial Commands
{'c','m','o','r','s','t','x','z'}
- Save a code cCode,Type,Addr, Val1,Val2,Val3
	- Code=code number (0-31)
	- Type = 0(NEC),1(rc5),2(rc6),3(rc6Extended)
	- Addr = IR Address
	- Val = 1,2 or 3 data values
- Save a macro sequence mMacro,Sequence...
	- Macro = macro number (0-9)
	- Sequence is code number(0-31) + 32*delayTick(0-7) (Tick is 96mSec)
- Adjust Timing oCmd,Val
	- Cmd 0 is read OSCVAL (Basic 8MHz clock), Ticks (to divide to 38.4KHz)
	- Cmd 1 is adjust OSCVAL (Val=0 decrement, Val=1 increment) 
	- Cmd 2 is adjust Ticks (Val=0 decrement, Val=1 increment) 
- Read Macros and code r
	- Dumps all codes and Macros to serial port
- Set sleep Mode sMode
	- Mode 0(start sleeping), 1(temporary awake), 2(permanent awake)
	- use this again to set back to sleeping after configuration finished
- Test transmit a code tType,Address,Val1,Val2,Val3
	- same parameter definitions as c Command
- Execute Macro xMacro
	- Send macro sequence as previously defined
- Send Code zCode
	- Send Code as previously defined





