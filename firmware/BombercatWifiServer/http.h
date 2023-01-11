WiFiServer server(80);

static FILE *f = nullptr;

const char *fname = "/root/home.html";

char buf[64] { 0 };

void readContents() {
    f = fopen(fname, "r");
    if (f != nullptr) {
      while (std::fgets(buf, sizeof buf, f) != nullptr)
        Serial.print(buf);
      fclose(f);
      Serial.println("File found");
    }
    else {
      Serial.println("File not found");
    }  
}

void httpSketch (){
  // listen for incoming clients
  WiFiClient client = server.available();
  if (client) {
    Serial.println("new client");
    // an HTTP request ends with a blank line
    boolean currentLineIsBlank = true;
    while (client.connected()) {
      if (client.available()) {
        char c = client.read();
        Serial.write(c);
        // if you've gotten to the end of the line (received a newline
        // character) and the line is blank, the HTTP request has ended,
        // so you can send a reply
        if (c == '\n' && currentLineIsBlank) {
          // send a standard HTTP response heade


          
          client.println("HTTP/1.1 200 OK");
          client.println("Content-Type: text/html");
          
          

          
          f = fopen(fname, "r");
          if (f != nullptr) {
          while (std::fgets(buf, sizeof buf, f) != nullptr)
            client.write(buf);
          }
          fclose(f);
          //client.write(buf);
          //client.println("</html>");
          break;
        }
        if (c == '\n') {
          // you're starting a new line
          currentLineIsBlank = true;
        } else if (c != '\r') {
          // you've gotten a character on the current line
          currentLineIsBlank = false;
        }
      }
    }
    // give the web browser time to receive the data
    delay(1);

    // close the connection:
    client.stop();
    Serial.println("client disconnected");
  }
}
