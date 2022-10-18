import paho.mqtt.client as mqtt #import the client1
import time

# Initializing a queue
queue = []
shosts = ['#', '#', '#', '#', '#', '#', '#', '#', '#', '#']

########################################

def on_message(client, userdata, message):
    command = str(message.payload.decode("utf-8"))
    print(str(message.payload.decode("utf-8")))
    
    #print(command[0])
    if command[0] == 'c' or command[0] == 'h':
        # Adding elements to the queue
        queue.append(command)
    
    # a request has been received, it is queued

########################################

broker_address="192.168.1.9"

print("Creating new instance.")
client = mqtt.Client("CORDINATOR") #create new instance
client.on_message = on_message #attach function to callback
print("connecting to broker")
client.connect(broker_address) #connect to broker

client.loop_start() #start the loop
print("Subscribing to topic","queue")
client.subscribe("queue")

while True:

    time.sleep(1) # wait

    # host requests are processed
    while True:
        if queue:
            result = queue.pop(0)
            print(result)

            if result[0] == 'c':
                # check if the host is busy
                if shosts[ord(result[3]) - 48] == '#':
                    print("Host vacant")
           
                    # status is updated and required messages are published
 
            
                    shosts[ord(result[3]) - 48] = result[1]
                    s = "".join(shosts)
                    print(s)
            
                    #shosts[ord(result[3]) - 48] = result[1]
                    client.publish("hosts",s)
                
                else:
                    print("Busy host")
                      
            elif result[0] == 'h':        
                # status is updated and required messages are published
            
                shosts[ord(result[1]) - 48] = '#'
                s = "".join(shosts)
                print(s)
            
                #shosts[ord(result[3]) - 48] = result[1]
                client.publish("hosts",s)
            
        else:
            break
    #print("Finish")   
     
client.loop_stop() #stop the loop

