"""
//-----------------------------------
//  Cordinator script for BomberCat Client/Host
//  by Andres Sabas, Electronic Cats (https://electroniccats.com/)
//  by Raul Vargas
//  Date: 22/11/2022
//
//  This script receives requests from BomberCat clients that want 
//  to connect to a given BomberCat host, it also takes care of releasing 
//  the status of the host once the connection is terminated.
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

import paho.mqtt.client as mqtt #import the client1
import time
import sys

# Initializing a queue
queue = []
shosts = ['#'] * 84 # 42 hosts max

########################################

def on_message(client, userdata, message):
    command = str(message.payload.decode("utf-8"))
    print(str(message.payload.decode("utf-8")))
    
    #print(command[0])
    if command[0] == 'c' or command[0] == 'h' or command[0] == 'u':
        # Adding elements to the queue
        queue.append(command)
    
    # a request has been received, it is queued

########################################

broker_address = "test.mosquitto.org"
if(len(sys.argv) > 1):
    broker_address = sys.argv[1]

print("Creating new instance.")
client = mqtt.Client("CORDINATOR") #create new instance
client.on_message = on_message #attach function to callback
print("connecting to broker ")
print(broker_address)

client.connect(broker_address) #connect to broker

client.loop_start() #start the loop
print("Subscribing to topic","queue")
client.subscribe("queue")

s = "".join(shosts)
print(s)
client.publish("hosts",s)

while True:

    time.sleep(0.5) # wait

    # host requests are processed
    while True:
        if queue:
            result = queue.pop(0)
            print(result)
            
            if result[0] == 'u':
                s = "".join(shosts)    
                print(s)    
                client.publish("hosts",s)                
                continue    
                    
            if result[0] == 'c':
                # new client
                if result[4] == '#' and result[5] == '#':
                    print("New client added")
                    s = "".join(shosts)
                    print(s)            
                    client.publish("hosts",s)
                    continue

                # check if the host is busy
                # check if there is another client instance
                
                for i in range(0,len(shosts),2):
                    #print(shosts[i])
                    #print(shosts[i+1])
                    if shosts[i] == result[1] and shosts[i+1] == result[2]:
                        shosts[i] = '#'
                        shosts[i+1] = '#'
                               
                if ((ord(result[5]) - 48) + (ord(result[4]) - 48)*10) > 41:
                    continue  
                
                if shosts[2*((ord(result[4]) - 48)*10 + ord(result[5]) - 48) + 1] == '#':
                    print("Host vacant")
           
                    # status is updated and required messages are published
                    
                    shosts[2*((ord(result[4]) - 48)*10 + ord(result[5]) - 48) + 1] = result[2]
                    shosts[2*((ord(result[4]) - 48)*10 + ord(result[5]) - 48)] = result[1]
                    s = "".join(shosts)
                    print(s)
            
                    #shosts[ord(result[3]) - 48] = result[1]
                    client.publish("hosts",s)
                
                else:
                    print("Busy host")
                    s = 'h' + result[4] + result[5] + '-'
                    client.publish("status",s)
                    
                      
            elif result[0] == 'h': # release the host        
                # status is updated and required messages are published
            
                shosts[2*((ord(result[1]) - 48)*10 + ord(result[2]) - 48) + 1] = '#'
                shosts[2*((ord(result[1]) - 48)*10 + ord(result[2]) - 48)] = '#'
                s = "".join(shosts)
                print(s)
            
                client.publish("hosts",s)
            
        else:
            break  
     
client.loop_stop() #stop the loop

