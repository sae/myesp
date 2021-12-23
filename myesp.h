/**
 * myESP - essentials for esp projects:
 * - Wifi connect: try client then AP
 * - SPIFFS filesystem
 * - config file
 * - web server
 * - OTA
 * - mDNS
 * - ...
 * 
 * include myesp.h in project
 * put myesp.begin() in setup
 * put myesp.handle() in loop
 * 
 * todo:
 * - extend config file(s) (may be json?)
 * - make report page (uptime, heap, gpio, etc)
 * 
 * * connect to defined SSID, PASS - for some chips with problem AP
 * 
 */
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <FS.h>

#define OTA  //include by default, if forget - you'll never ota again )
#ifdef OTA
#include <ArduinoOTA.h>
#endif
#ifdef NTP
#include <WiFiUdp.h>
#endif

#ifdef NTP
#define NTP_PACKET_SIZE  48 // NTP time stamp is in the first 48 bytes of the message
#define ntpPeriod 10000 //10 sec
#endif
//minimal config 
char* conf="<html><body>"
"<form method='post'>"
"SSID:<input name=\"ssid\"><br>"
"Password:<input name=\"pass\"><br>"
"Hostname:<input name=\"hostname\"><br>"
"<input type=\"submit\" value=\"Save&amp;Restart\">"
"</form>"
"</body></html>";
char* upl="<html><body>"
"<form method=\"post\" enctype=\"multipart/form-data\">"
"    <input type=\"file\" name=\"name\">"
"    <input class=\"button\" type=\"submit\">"
"</form>";
const char* build_info =" " __DATE__ " " __TIME__ " " __FILE__ ;

class MyESP   {
  public:
    ESP8266WebServer* web;

    //ESP8266Server telnetd(23);//telnet
    MyESP() { 
    
   };

    //start all
    void begin() {
      #ifdef DEBUG 
        Serial.begin(115200);
      #endif
      debug(F("\n\nstart..."));
      SPIFFS.begin();
      if (SPIFFS.exists("/myconf")) {   
        File file = SPIFFS.open("/myconf", "r");
        ssid=file.readStringUntil(';'); 
        pass=file.readStringUntil(';');
        hostname=file.readStringUntil(';');
        file.close();
      }
      #ifdef WIFI_SSID
      connect(WIFI_SSID, WIFI_PASS);
      #else
      //connect to given ssid, if timeout, start AP
      //if your router was down when esp started, just bring it up then restart esp
      if (!connect()) 
		startAP();
      #endif
      debug("mdns begin");
      MDNS.begin(hostname);
      MDNS.addService("http", "tcp", 80);
      #ifdef OTA
      debug("ota begin");
      ArduinoOTA.begin();
      ArduinoOTA.setHostname((char*)hostname.c_str());
      //ArduinoOTA.setPassword(otapw);
      #endif
      #ifdef NTP
      udp.begin(2390);//ntp
      #endif
      debug("web begin");
      web=new ESP8266WebServer(80);
      //WEB INIT
      web->onNotFound([this]() {                              // If the client requests any URI
        if (!sendFile(web->uri())) {                 // send it if it exists
          webSend(404, "text/plain", "404: Not Found"); // otherwise, respond with a 404 (Not Found) error
        } 
      //list directory
      web->on("/ls", HTTP_GET, [this]() {
        String s="";
        Dir dir = SPIFFS.openDir("/");
        while (dir.next()) {
            s+=dir.fileName()+"\n";
        }
        webSend(200, "text/plain", (char*)s.c_str());
      });
      //upload form
      web->on("/upload", HTTP_GET, [this]() {
        if (!sendFile("/upload.html")) {
         webSend(200, "text/html", upl);
         //webSend(404, "text/plain", "use curl -F \"file=@upload.html\" <this>/upload");
        }
      });   
      web->on("/format", HTTP_GET, [this]() {
        if (SPIFFS.format()) {
			webSend(200, "text/html", "format ok");
        } else {
			webSend(200, "text/html", "format error");
        }			
      });     
      web->on("/rm", HTTP_GET, [this]() {
        if (web->args() == 0) return webSend(500, "text/plain", "BAD ARGS");
		String path = web->arg(0);
        if (!path.startsWith("/")) {
            path = "/" + path;
        }
   		debugln("rm: " + path);
		if (!SPIFFS.exists(path)) return webSend(404, "text/plain", "FileNotFound");
		SPIFFS.remove(path);
		webSend(200, "text/plain", "OK");
      });     
      //upload processing
      web->on("/upload", HTTP_POST, [this]() {
        webSend(200, "text/plain", ""); //first, ask OK
        },[this]() {                    //then handle upload process
           debug("\nupload...");
           HTTPUpload& upload = web->upload(); //if upload==null, esp goes to reload
            if (&upload == NULL ){
                debug("upload=NULL\n");
                webSend(500, "text/plain", "NULL");
                return;
            }
          //debug(String(upload.status));
          if (upload.status == UPLOAD_FILE_START) {
            String filename = upload.filename;
            if (!filename.startsWith("/")) {
              filename = "/" + filename;
            }
            debug(filename);
            //SPIFFS.remove(filename); //not overwriting in some time
            fsUploadFile = SPIFFS.open(filename, "w");
          } else if (upload.status == UPLOAD_FILE_WRITE) {
            //DBG_OUTPUT_PORT.print("handleFileUpload Data: "); DBG_OUTPUT_PORT.println(upload.currentSize);
            if (fsUploadFile) {
              fsUploadFile.write(upload.buf, upload.currentSize);
            }
          } else if (upload.status == UPLOAD_FILE_END) {
            if (fsUploadFile) {
              fsUploadFile.close();
            }
            debug(" done\n"); 
          }
        //webSend(200, "text/plain", "");
      });
      web->on("/conf", HTTP_GET, [this]() {
        if (!sendFile("/config.html")) {
          webSend(200, "text/html", conf);
        }
      });
      web->on("/conf", HTTP_POST, [this]() {
          debug("handleConfig...\n");
          if (web->args() < 3) {
            web->send(500, "text/plain", "BAD ARGS");
            return;
          }
          fsUploadFile=SPIFFS.open("/myconf", "w");
          fsUploadFile.print(web->arg("ssid"));
          fsUploadFile.print(";");
          fsUploadFile.print(web->arg("pass"));
          fsUploadFile.print(";");
          fsUploadFile.print(web->arg("hostname"));
          fsUploadFile.print(";");
          fsUploadFile.close();
          webSend(200, "text/plain", "OK, restarting...");
          debug("restarting...\n");
          ESP.restart();
        });
      });
      //get heap status, analog input value and all GPIO statuses in one json call
      web->on("/sys", HTTP_GET, [this]() {
        String json = "{";
        json += "\"heap\":" + String(ESP.getFreeHeap());
        json += ", \"analog\":" + String(analogRead(A0));
        json += ", \"gpio\":" + String((uint32_t)(((GPI | GPO) & 0xFFFF) | ((GP16I & 0x01) << 16)));
        json += "}";
        webSend(200, "text/json", (char*)json.c_str());
        json = String();
      });
      web->on("/version", [this]() {
          webSend(200, "text/plain", (char*)build_info); 
      });
      web->begin();      
    }

    boolean connect() {
      return connect(ssid,pass); //readed from config
    }
    //bring up wifi
    boolean connect(String ssid1, String pass1) {
      int count=0;
      WiFi.persistent(false);//this need for reconnect after reset
      WiFi.disconnect(true);//https://github.com/esp8266/Arduino/issues/2795#issuecomment-462484599
      WiFi.mode(WIFI_STA);
      debug("\nconnecting to "+ssid1);
      debug("\n");
      WiFi.hostname(hostname);  
      WiFi.begin(ssid1, pass1);
      //no reconnect: if esp lost connection, it have to start own AP
      //so you can connect and take some measures
      WiFi.setAutoReconnect(true);
      WiFi.persistent(true);
     
      pinMode(LED_BUILTIN, OUTPUT);
      while (WiFi.status() != WL_CONNECTED) {
        digitalWrite(LED_BUILTIN, 0);
        delay(100);
        debug(".");
        digitalWrite(LED_BUILTIN, 1);
        delay(300);
        if (count++ > 30) {
          debug("\nconnect() timeout, status="+String(WiFi.status()));
          return false;
        }
      }
    
      debug("\nWiFi connected");
      debug("\nIP address: "+WiFi.localIP().toString()+"\n"); 
      //pinMode(LED_BUILTIN, INPUT);
      digitalWrite(LED_BUILTIN, 1); //off led
     return true;
    }

    void startAP() {
      debug("\nAP starting...\n");
      WiFi.disconnect();
      WiFi.mode(WIFI_AP_STA);
      WiFi.softAP("ESP_"+String(ESP.getChipId()), "12345678");             // Start the access point
      debug("AP started: ESP_"+String(ESP.getChipId()));
      debug(("\nIP address:"+WiFi.softAPIP().toString())+"\n"); 
      digitalWrite(LED_BUILTIN, 0); //on led
   }
    
    //process clients
    void handle() {
      web->handleClient();
      #ifdef OTA
      ArduinoOTA.handle();
      #endif
      MDNS.update();
      #ifdef NTP
      if (millis()-ntpTime>ntpPeriod) {
        readNTP();//todo: we sent a packet ntpPeriod time ago, so we need to add it?
        sendNTPpacket(timeServerIP); // send an NTP packet to a time server
        ntpTime=millis();
        }
      #endif
      yield();
    }
    
    String getContentType(String filename) {
      if (web->hasArg("download")) return "application/octet-stream";
      if (filename.endsWith(".htm")) return "text/html; charset=utf-8";
      if (filename.endsWith(".html")) return "text/html; charset=utf-8";
      if (filename.endsWith(".css")) return "text/css";
      if (filename.endsWith(".js")) return "application/javascript";
      if (filename.endsWith(".png")) return "image/png";
      if (filename.endsWith(".gif")) return "image/gif";
      if (filename.endsWith(".jpg")) return "image/jpeg";
      if (filename.endsWith(".ico")) return "image/x-icon";
      if (filename.endsWith(".xml")) return "text/xml";
      //if (filename.endsWith(".pdf")) return "application/x-pdf";
      //if (filename.endsWith(".zip")) return "application/x-zip";
      //if (filename.endsWith(".gz")) return "application/x-gzip";
      return "text/plain; charset=utf-8";
    }

    void debug(String msg) {
      #ifdef DEBUG
      Serial.print(msg);
      #endif
      //WiFiClient client = telnet.available();
      //if (client) {
      //  client.print(msg);
      //}
    }
	void debugln(String msg) {
		debug(msg+"\n");
	}   
    void webSend(char* buf) { 
		webSend(200,"text/plain",buf);
		}
    void webSend(int stat, char* type, char* buf) { 
      web->sendHeader("Access-Control-Allow-Origin", "*");//must to debug a page on local machine
      web->send(stat, type, buf); 
    }
    String hostname="myesp";
    String ssid;
  private:
    String pass;
    File fsUploadFile; //for upload
    bool sendFile(String path) { // send the right file to the client (if it exists)
      debug("web.sendFile: " + path);
      if (path.endsWith("/")) path += "index.html";         // If a folder is requested, send the index file
      String contentType = getContentType(path);            // Get the MIME type
      if (SPIFFS.exists(path)) {                            // If the file exists
        File file = SPIFFS.open(path, "r");                 // Open it
        size_t sent = web->streamFile(file, contentType); // And send it to the client
        file.close();                                       // Then close the file again
        return true;
      }
      debug(" - not found\n");
      return false;                                         // If the file doesn't exist, return false
    }
    #ifdef NTP
    WiFiUDP udp;
    long ntpTime;//ntp timer
    IPAddress timeServerIP; // time.nist.gov NTP server address
    const char* ntpServerName = "pool.ntp.org";
    byte packetBuffer[ NTP_PACKET_SIZE]; //buffer to hold incoming and outgoing packets
    // send an NTP request to the time server at the given address
    void sendNTPpacket(IPAddress& address) {
      WiFi.hostByName(ntpServerName, timeServerIP);
      debug("sending NTP packet...");
      // set all bytes in the buffer to 0
      memset(packetBuffer, 0, NTP_PACKET_SIZE);
      // Initialize values needed to form NTP request
      // (see URL above for details on the packets)
      packetBuffer[0] = 0b11100011;   // LI, Version, Mode
      packetBuffer[1] = 0;     // Stratum, or type of clock
      packetBuffer[2] = 6;     // Polling Interval
      packetBuffer[3] = 0xEC;  // Peer Clock Precision
      // 8 bytes of zero for Root Delay & Root Dispersion
      packetBuffer[12]  = 49;
      packetBuffer[13]  = 0x4E;
      packetBuffer[14]  = 49;
      packetBuffer[15]  = 52;

      // all NTP fields have been given values, now
      // you can send a packet requesting a timestamp:
      udp.beginPacket(address, 123); //NTP requests are to port 123
      udp.write(packetBuffer, NTP_PACKET_SIZE);
      udp.endPacket();
    }
    boolean readNTP() {
      int cb = udp.parsePacket();
      if (!cb) {
        debug("readNTP: no packet yet");
      } else {
        debug("readNTP: packet received, length=");
        debug(String(cb));
        // We've received a packet, read the data from it
        udp.read(packetBuffer, NTP_PACKET_SIZE); // read the packet into the buffer
        unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
        unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);
        unsigned long secsSince1900 = highWord << 16 | lowWord;
        // Unix time starts on Jan 1 1970. In seconds, that's 2208988800:
        const unsigned long seventyYears = 2208988800UL;
        // subtract seventy years:
        unsigned long epoch = secsSince1900 - seventyYears;       }
        }
    #endif
};
