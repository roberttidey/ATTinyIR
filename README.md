# ATTiny IR transmitter
10 button remote control with each button supporting macro commands sending a sequence of codes to different devices

An improved version can be found at https://github.com/roberttidey/ATTinyTx
This is more memory efficient and supports driving 433MHz Transmitters like Lightwave
Codes will need to be redefined due to change in EEPROM layout

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
	- Sequence is code(0-31)+32*delay(0-7) (0,100,200,400,800,1600,3200,6400mS)
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

## Capture IR codes
- Often the codes for a remote can be found on the web
- A utility rxirEx.py is also provided in the docs folder which can get codes for nec and rc6 based remotes
- This can be run on a Raspberry Pi with an IR receiver connected to a GPIO pin (24 default)
- IR polarity is defined near top of rxirEx.py (default is high is IR active, using an inverting buffer to amplify output of receiver)
- Prepare a text file like the example (philipstv-top) which contains a list of the buttons to capture
- Run the program, enter device name (e.g. philipstv, subset of buttons (e.g. top), protocol (e.g. rc6) and retry (e.g. n)
- It will prompt for each button to be pressed in turn
- Results are placed in device.ircodes (e.g. philipstv.ircodes)
- Each button has the hex and binary data received and the address and command values are at the end of the line
- These last two values are what is needed to program the ATTinyIR device



