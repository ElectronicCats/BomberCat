"""
//-----------------------------------
//  Command test script for BomberCat Client/Host
//  by Andres Sabas, Electronic Cats (https://electroniccats.com/)
//  by Raul Vargas
//  by Salvador Mendoza (salmg.net)
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

ser = serial.Serial()
ser.port = '/dev/ttyACM0'
ser.baudrate = 9600
ser.bytesize = serial.EIGHTBITS
ser.parity = serial.PARITY_NONE
ser.stopbits = serial.STOPBITS_ONE
ser.timeout = 1

cmds = ["set_h 01", "set_h 01", "set_h 01", "help", "free_h 01", "set_h 11", \
	"mode_ms", "set_h 21", "set_h 21", "mode_ms", "free_h 21", "set_h 21", \
	"mode_nfc", "free_h 01", "set_01", "mode_ms", "set_h 21", "get_config", \
	"mode_ms", "mode_ms", "set_h 8", "mode_nfc", "free_h 9", "set_h 9", \
	"mode_ms", "set_h 44", "set_h a", "get_config", "free_h 21", "free_h 21", \
	"mode_nfc", "set_h 11", "set_h 1", "help", "free_h 1", "set_h 21", \
	"mode_ms", "set_h 10", "set_h 21", "mode_nfc", "free_h 10", "set_h 21",
	"setup_wifi", "set_h 23", "set_h 8", "mode_nfc", "help", "set_h 14", \
	"mode_nfc", "set_h 1", "set_h 1", "mode_ms", "free_h 21", "set_h 21", \
	"mode_ms", "set_h 11", "set_h 11", "mode_nfc", "free_h 42", "set_h 37"]
N = 59

time.sleep(1)
ser.open()

e = 0

while True:
	if ser.is_open:
		cmd = cmds[e] + '\r\n'

	if ser.in_waiting == 0:
		for i in cmd:
			ser.write(i.encode())
			time.sleep(0.01)
    
	if ser.in_waiting > 0:
		msg = ser.read(ser.in_waiting)
		print (msg.decode())

	e = e + 1
	if e > N:
		e = 0
            
	ser.flush()
	time.sleep(1)

ser.close()
