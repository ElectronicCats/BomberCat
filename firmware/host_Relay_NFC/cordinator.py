import paho.mqtt.client as mqtt #import the client1
import time

# Initializing a queue
queue = []
status = [-1, -1, -1, -1, -1, -1, -1, -1, -1, -1]
shosts = ['#', '#', '#', '#', '#', '#', '#', '#', '#', '#']

############
def on_message(client, userdata, message):
    command = str(message.payload.decode("utf-8"))
    print(str(message.payload.decode("utf-8")))
    
    #print(command[0])
    if command[0] == 'c' or command[0] == 'h':
        # Adding elements to the queue
        queue.append(command)
    
    # se ha recibido una petición, se guarda en la cola
    	
    
    #if('G' in command):
    #	client.publish("hosts","########")#publish
    
    #print("message topic=",message.topic)
    #print("message qos =",message.qos)
    #print("message retain flag=",message.retain)
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

    #print("Saca la cola: ")
    # se procesan las peticiones
    while True:
        if queue:
            result = queue.pop(0)
            print(result)

            if result[0] == 'c':
                # se revisa si el host esta ocupado
                if shosts[ord(result[3]) - 48] == '#':
                    print("Host desocupado")
           
                    # se actualiza el estatus y se publican mensajes necesarios
 
            
                    shosts[ord(result[3]) - 48] = result[1]
                    s = "".join(shosts)
                    print(s)
            
                    #shosts[ord(result[3]) - 48] = result[1]
                    client.publish("hosts",s)
                
                else:
                    print("Host ocupado")
                      
            elif result[0] == 'h':
                print("HELL")
                
                # se actualiza el estatus y se publican mensajes necesarios
            
                shosts[ord(result[1]) - 48] = '#'
                s = "".join(shosts)
                print(s)
            
                #shosts[ord(result[3]) - 48] = result[1]
                client.publish("hosts",s)
            
        else:
            break
    #print("Termina")   
     
client.loop_stop() #stop the loop

