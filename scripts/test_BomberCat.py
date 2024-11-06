"""
//-----------------------------------
//  Command test script for BomberCat Client/Host
//  by Andres Sabas, Electronic Cats (https://electroniccats.com/)
//  by Raul Vargas
//  Date: 24/11/2022
//
//  This script sends serial commands from a list to the BomberCat client.
//
//  Development environment specifics:
//  IDE: gedit
//  Hardware Platform:
//  Ubuntu Linux - Intel
//
//  Thanks Wallee for support this project open source https://en.wallee.com/
//
//  Electronic Cats invests time and resources providing this open source code,
//  please support Electronic Cats and open-source hardware by purchasing
//  products from Electronic Cats!
//
//  This code is beerware; if you see me (or any other Electronic Cats
//  member) at the local, and you've found our code helpful,
//  please buy us a round!
//  Distributed as-is; no warranty is given.
"""

import serial
import time
import sys

ser = serial.Serial()
ser.port = '/dev/ttyACM0'
ser.baudrate = 9600
ser.bytesize = serial.EIGHTBITS
ser.parity = serial.PARITY_NONE
ser.stopbits = serial.STOPBITS_ONE
ser.timeout = 1

cmds = ["mode_ms", "set_h-", "get_config", "mode_ms", \
	"set_h-", "mode_ms"] # put here the commands to be processed
	
print(sys.argv[1])	
	
N = 5 # commands number minus 1 in cmds

time.sleep(1)
ser.open()

e = 0 # index in cmds

while True:
	if ser.is_open:
		cmd = cmds[e] #get the command in turn
		if cmd[3:5] == '_h': # check for select host
			cmd = cmd + sys.argv[1]
			print(cmd)
		#if cmd[4:6] == '_h':
		#	cmd = cmd + sys.argv[1]
			
		cmd = cmd + '\r\n'	
			

	if ser.in_waiting == 0:
		for i in cmd:
			ser.write(i.encode())
			time.sleep(0.001)

	while(ser.in_waiting == 0):
		pass
		
	time.sleep(0.01)
		
	if ser.in_waiting > 0:
		msg = ser.read(ser.in_waiting)
		cad = msg.decode()
		
		if cad[:2] == 'ER':
			print('ERROR:', end = " ")
			print(cmds[e], end = " ")
			print('command could not be executed')
		
		elif cad[:2] == 'OK':
			print("OK")
					
		else:
			print (cad)	
		

	e = e + 1
	if e > N:
		e = 0
            
	ser.flush()
	time.sleep(5)

ser.close()
