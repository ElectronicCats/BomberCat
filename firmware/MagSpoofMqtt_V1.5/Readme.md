The changes to this code were minumum and were mainly in the Void callback.


The changes were to restrict the messages sent through the Mag topic and that can activate the MagSpoof mode. 

1. Now the code verify if the message has the format of the track1 and track2
   
2. It checks if it contains % at the beginning and ? at the end, as well as the position of ? and ;
   
4. If the message doesn't accomplish these requirements, then it descards it, and send a message in the Serial monitor notifying us that an invalid message has been received.

5. If the message accomplishes the requirements, then it prints the tracks in the Serial monitor and acitvates the MagSpoof mode.
