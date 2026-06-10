#include <WiFi.h>

// Replace with your network credentials
const char* ssid     = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

void setup() {
  // Initialize Serial Monitor at 115200 baud rate
  Serial.begin(115200);
  delay(1000); 

  // Set Wi-Fi to Station mode and begin connection
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  
  Serial.print("Connecting to WiFi");
  
  // Wait until the ESP32 successfully connects to the network
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  // Print connection confirmation and the local IP address
  Serial.println("\nConnected successfully!");
  Serial.print("ESP32 IP Address: ");
  Serial.println(WiFi.localIP()); 
}

void loop() {
  // Your main project code goes here
}
