#!/usr/bin/python
# rxirEx.py
# captures codes from ir remotes
# uses a control file to manage the collection of data
# based on rxir.py, modified to give better rc6 and address / value outputs as well
#
# Author : Bob Tidey
# Date   : 21/02/2021
import time
import RPi.GPIO as GPIO
import array

# -----------------------
# Main Script
# -----------------------
MAX_BUFFER = 40000 # twice number of samples per file
MAX_FILES = 2
CODE_EXT = ".ircodes"

# Use BCM GPIO references
# instead of physical pin numbers
GPIO.setmode(GPIO.BCM)
# Define GPIO to use on Pi
GPIO_RXDATA = 24

# reverse these to change sense of signal, Active LOW
RX_DATA_OFF = GPIO.LOW
RX_DATA_ON = GPIO.HIGH
#RX_DATA_OFF = GPIO.HIGH
#RX_DATA_ON = GPIO.LOW

DUR = 10
def get_raw( check=True):

	print 'get-raw'
	d = []
	start_time = time.time()
	pin = GPIO.input(GPIO_RXDATA)
	print 'pin: ', pin
	i = 0
	t = start_time
	while ( time.time() - start_time) < DUR :
		while ( GPIO.input(GPIO_RXDATA) == pin ) and ( ( time.time() - start_time) < DUR ): 	# 
			pass
		t_last = t
		t = time.time()
		pin = GPIO.input(GPIO_RXDATA)
		i = i + 1
		a = int(round(1000000 * (t-t_last)))
		if pin == RX_DATA_ON :
			a = -1 * a
		d.append(a)
	j =0
	i = i -1
	print 'max:', i
	rawfile = open(remotename + ".raw", "w")
	while ( i >= j ) :
		print d[j]
		rawfile.write(str(d[j]) + '\n')
		j = j+1
	rawfile.close()
	return 'Raw: ' + str(i), '-1', '-1', True
		
#routine to decode a NEC type ir code
def get_nec(header1=9000, header2=5000, maxlow=800, maxhigh=2000, endgap=3000, check=True):
	code = ''
	codehex=''
	startfound = False
	starttries = 0
	header1min = 0.6 * header1
	header1max = 1.5 * header1
	header2min = 0.6 * header2
	header2max = 1.5 * header2
	while not startfound and starttries < 10:
		while GPIO.input(GPIO_RXDATA) == RX_DATA_OFF:
			pass
		t0 = time.time()
		while GPIO.input(GPIO_RXDATA) == RX_DATA_ON:
			pass
		t1 = time.time()
		while GPIO.input(GPIO_RXDATA) == RX_DATA_OFF:
			pass
		t2 = time.time()
		start1 = (t1-t0) * 1000000
		start2 = (t2-t1) * 1000000
		if start1 > header1min and start1 < header1max and start2 > header2min and start2 < header2max:
			startfound = True
		else:
			starttries = starttries + 1
	if startfound:
		t2 = time.time()
		bitvalue = 8
		nibblevalue = 0
		addr = 0
		data = 0
		bitcount = 0
		while time.time() - t2 < 5:
			t0 = time.time()
			pin = GPIO.input(GPIO_RXDATA)
			while GPIO.input(GPIO_RXDATA) == pin and (t0 - t2) < 1:
				pass
			pulse = (time.time() - t0) * 1000000
			if pin == RX_DATA_OFF and pulse < maxhigh:
				if pulse < maxlow:
					code = code + '0'
				else:
					code = code + '1'
					nibblevalue = nibblevalue + bitvalue
					if bitcount < 8:
						addr = addr + (1 << bitcount)
					if bitcount > 15 and bitcount < 24:
						data = data + (1 << (bitcount - 16))
				bitvalue = bitvalue / 2
				if bitvalue < 1:
					codehex = codehex + format(nibblevalue,'01X')
					bitvalue = 8
					nibblevalue = 0
				bitcount = bitcount + 1
			elif pin == RX_DATA_ON and pulse > maxlow and pulse < maxhigh:
				code = code + '2'
				if check:
					return 'Bad data pulse', '-1', '-1', False
			elif pulse < endgap:
				pass
			else:
				if check and len(code) != 32:
					return 'Bad code length' + codehex + ',' + code, '-1', '-1', False
				return codehex + ',' + code, str(addr), str(data), True
		return 'Timeout', '-1', '-1', False
	else:
		return 'Bad start pulse ' + str(start1) + ' ' + str(start2), '-1', '-1', False

def get_rc5(midpulse=1200, endgap=3000, check=True):
	code = ''
	codehex=''
	state = 1
	while GPIO.input(GPIO_RXDATA) == RX_DATA_OFF:
		pass
	t0 = time.time()
	level = RX_DATA_ON
	incode = True
	addr = 0
	data = 0
	bitcount = 0
	bitvalue = 8
	nibblevalue = 0
	event = 0
	while incode:
		emit = ''
		while GPIO.input(GPIO_RXDATA) == level:
			pass
		event = event + 1
		t1 = time.time()
		td = (t1 - t0) * 1000000
		t0 = t1
		level = GPIO.input(GPIO_RXDATA)
		if td < endgap:
			if state == 0 and level == RX_DATA_ON and td < midpulse:
				emit = '1'
				state = 1
			elif state == 1 and level == RX_DATA_OFF and td < midpulse:
				state = 0
			elif state == 1 and level == RX_DATA_OFF:
				emit = '0'
				state = 2
			elif state == 2 and level == RX_DATA_ON and td < midpulse:
				state = 3
			elif state == 2 and level == RX_DATA_ON:
				emit = '1'
				state = 1
			elif state == 3 and level == RX_DATA_OFF and td < 1200:
				emit = '0'
				state = 2
			else:
				return code + 'bad level:event:state:time ' + str(event) + str(level) + ':' + str(state) + ':' + str(td), '-1', '-1', False
			if emit != '':
				code = code + emit
				if emit == '1':
					nibblevalue = nibblevalue + bitvalue
					if bitcount == 1:
						data = data + 64
					if bitcount > 2 and bitcount < 8:
						addr = addr + (128 >> (bitcount - 3))
					if bitcount > 7:
						data = data + (32 >> (bitcount - 8))
				bitvalue = bitvalue / 2
				if bitvalue < 1:
					codehex = codehex + format(nibblevalue,'01X')
					bitvalue = 8
					nibblevalue = 0
				bitcount = bitcount + 1
		else:
			incode = False
	if bitvalue != 8:
		codehex = codehex + format(nibblevalue,'01X')
	if check and len(code) != 13:
		return 'Bad code length', '-1', '-1', False
	return codehex + ',' + code, str(addr), str(data), True

def find_rc6header(pulse=450):
	starttries = 0
	p0min = round(3 * pulse)
	p0max = round(12 * pulse)
	p1min = round(pulse)
	p1max = round(4 * pulse)
	p2min = round(0.5 * pulse)
	p2max = round(2 * pulse)
	while starttries < 1:
		waitForQuiet(0.5)
		while GPIO.input(GPIO_RXDATA) == RX_DATA_OFF:
			pass
		t0 = time.time()
		while GPIO.input(GPIO_RXDATA) == RX_DATA_ON:
			pass
		t1 = time.time()
		while GPIO.input(GPIO_RXDATA) == RX_DATA_OFF:
			pass
		t2 = time.time()
		while GPIO.input(GPIO_RXDATA) == RX_DATA_ON:
			pass
		t3 = time.time()
		pulse0 = round((t1-t0) * 1000000)
		pulse1 = round((t2-t1) * 1000000)
		pulse2 = round((t3-t2) * 1000000)
		if (pulse0 > p0min) and (pulse0 < p0max) and (pulse1 > p1min) and (pulse1 < p1max) and (pulse2 > p2min) and (pulse2 < p2max):
			return ""
		else:
			starttries = starttries + 1
	return 'No header ' + str(pulse0) + ' ' + str(pulse1) + ' ' + str(pulse2)
	
def get_rc6(pulse=450, endgap=3000, check=True):
	midpulse = 1.3 * pulse
	header = find_rc6header(pulse)
	t0 = time.time()
	if header != "":
		return header, '-1', '-1', False
	code = ''
	codehex=''
	state = 0
	level = RX_DATA_OFF
	incode = True
	addr = 0
	data = 0
	bitcount = 0
	bitvalue = 8
	nibblevalue = 0
	event = 0
	while incode:
		emit = ''
		while GPIO.input(GPIO_RXDATA) == level:
			pass
		t1 = time.time()
		event = event + 1.0
		td = (t1 - t0) * 1000000
		t0 = t1
		level = GPIO.input(GPIO_RXDATA)
		if td < endgap:
			# deal with double width toggle
			if bitcount == 3 or bitcount == 4:
				if (state == 2 or state == 0) and td > 2.25 * pulse:
					td = pulse + pulse
				else:
					td = pulse
			if state == 0 and level == RX_DATA_ON and td < midpulse:
				#mid1 SS
				state = 1
			elif state == 0 and level == RX_DATA_ON:
				#mid1 LS
				emit = '0'
				state = 2
			elif state == 1 and level == RX_DATA_OFF and td < midpulse:
				#start1 SP
				emit = '1'
				state = 0
			elif state == 2 and level == RX_DATA_OFF and td < midpulse:
				#mid0 SP
				state = 3
			elif state == 2 and level == RX_DATA_OFF:
				#mid0 LP
				emit = '1'
				state = 0
			elif state == 3 and level == RX_DATA_ON and td < midpulse:
				emit = '0'
				state = 2
			else:
				return code + 'bad level:event:state:time ' + str(event) + str(level) + ':' + str(state) + ':' + str(td), False
			if emit != '':
				code = code + emit
				if emit == '1':
					nibblevalue = nibblevalue + bitvalue
					if bitcount > 3 and bitcount < 12:
						addr = addr + (128 >> (bitcount - 4))
					if bitcount > 11:
						data = data + (128 >> (bitcount - 12))
				bitvalue = bitvalue / 2
				if bitvalue < 1:
					codehex = codehex + format(nibblevalue,'01X')
					bitvalue = 8
					nibblevalue = 0
				bitcount = bitcount + 1
		else:
			incode = False
	if bitvalue != 8:
		codehex = codehex + format(nibblevalue,'01X')
	if check and len(code) < 13:
		return 'Bad code length', '-1', '-1', False
	return codehex + ',' + code, str(addr), str(data), True

def get_rc6sp(pulse=450, endgap=3000, check=True):
	midpulse = 1.3 * pulse
	header = find_rc6header(pulse)
	if header != "":
		return header, '-1', '-1', False
	code = ''
	codehex=''
	state = 1
	while GPIO.input(GPIO_RXDATA) == RX_DATA_OFF:
		pass
	t0 = time.time()
	level = RX_DATA_ON
	incode = True
	bitcount = 0
	bitvalue = 8
	nibblevalue = 0
	event = 0
	while incode:
		emit = ''
		while GPIO.input(GPIO_RXDATA) == level:
			pass
		event = event + 1
		t1 = time.time()
		td = (t1 - t0) * 1000000
		t0 = t1
		if bitcount == 2:
			td = td * 0.5
		level = GPIO.input(GPIO_RXDATA)
		if td < endgap:
			if state == 0 and level == RX_DATA_ON and td < midpulse:
				emit = '1'
				state = 1
			elif state == 1 and level == RX_DATA_OFF and td < midpulse:
				state = 0
			elif state == 1 and level == RX_DATA_OFF:
				emit = '0'
				state = 2
			elif state == 2 and level == RX_DATA_ON and td < midpulse:
				state = 3
			elif state == 2 and level == RX_DATA_ON:
				emit = '1'
				state = 1
			elif state == 3 and level == RX_DATA_OFF and td < 1200:
				emit = '0'
				state = 2
			else:
				return code + 'bad level:event:state:time ' + str(event) + str(level) + ':' + str(state) + ':' + str(td), -1, -1, False
			if emit != '':
				bitcount = bitcount + 1
				code = code + emit
				if emit == '1':
					nibblevalue = nibblevalue + bitvalue
				bitvalue = bitvalue / 2
				if bitvalue < 1:
					codehex = codehex + format(nibblevalue,'01X')
					bitvalue = 8
					nibblevalue = 0
		else:
			incode = False
	if bitvalue != 8:
		codehex = codehex + format(nibblevalue,'01X')
	if check and len(code) != 13:
		return 'Bad code length', '-1', '-1', False
	return codehex + ',' + code, '-1', '-1', True

def get_ir(codetype, chk):
	if codetype == 'nec':
		return get_nec(check=chk)
	elif codetype == 'nec1':
		return get_nec(header1=4500, check=chk)
	elif codetype == 'rc5':
		return get_rc5(check=chk)
	elif codetype == 'rc6':
		return get_rc6(check=chk)
	elif codetype == 'rc6sp':
		return get_rc6sp(check=chk)
	elif codetype == 'raw':
		return get_raw(check=chk)
	else:
		return 'unknown', False

def waitForQuiet(period=1):
	quiet = False
	hightime = time.time()
	while not quiet:
		pin = GPIO.input(GPIO_RXDATA)
		if pin == RX_DATA_OFF:
			if time.time() - hightime > period:
				quiet = True
		else:
			hightime = time.time()
		
# Main routine
# Set pin for input
GPIO.setup(GPIO_RXDATA,GPIO.IN)  #
remotename = raw_input('Name of remote control :')
buttons = raw_input('Name of subset buttons :')
codetype = raw_input('Codetype (nec,nec1,rc5,rc6,rc6sp,raw) :')
check = raw_input('Retry on bad code (y/n) :')

with open(remotename + '-' + buttons) as f:
    clines = f.read().splitlines() 
codefile = open(remotename + CODE_EXT, "a")
codefile.write('IR codes for ' + remotename + '-' + buttons + '\n')
print "codefile open"
try:
	for cline in clines:
		retries = 4
		print "cline: ",cline
		while retries > 0:
			print "Remote button ",cline," Waiting for quiet", waitForQuiet()
			print "Press Now ",cline
			result = get_ir(codetype, check == 'y')
			codefile.write(cline + ',' + result[0]  + ',' + result[1]  + ',' + result[2]+ '\n')
			if check != 'y' or result[3]:
				retries = 0
			else:
				print "Error. Try again", result[0],result[3]
				retries = retries - 1
except KeyboardInterrupt:
	pass
	
codefile.close()

GPIO.cleanup()
