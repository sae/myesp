# myesp
concept/work my framework for esp8266/esp32 web/mdns/ntp/ota/versioning etc

framework started to read SPIFFS config file

if not - start as a hotspot with a password 12345678

you can go to http://192.168.4.1/conf then fill ssid, password and hostname

upload files to spiffs via <name>/upload
  
(index.html is a nice to start)
  
use javascript to handle actions
  
  

usage:
- add "myesp.h" to the project
  
- declare MyESP esp 
  
- add esp.begin() to setup
  
- add esp.handle() to loop (better to not put a delay more than 10ms to the loop)
  
- add esp.web->on("/handle) {} to setup to handle your requests
  

feel free to look at code 
