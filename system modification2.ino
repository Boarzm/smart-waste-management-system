#include <WiFi.h>
#include <ESP32Servo.h>

// --- Network Config ---
const char* ssid = "Your_MiFi_Name";
const char* password = "Your_Password";

// --- Pin Definitions ---
#define PIR_PIN 13
#define TOUCH_PIN 14
#define SERVO_PIN 12
#define TRIG_PIN 26
#define ECHO_PIN 25
#define LED_RED 33
#define LED_GREEN 32

// --- Logic Constants ---
const int BIN_HEIGHT = 50; 
const int CLOSE_ANGLE = 0;
const int OPEN_ANGLE = 100;
const int OBSTACLE_THRESHOLD = 20; // cm (Distance to keep lid open)

// --- Global Shared Variables ---
float globalFillLevel = 0;
String globalStatus = "Initializing...";
bool lidIsOpen = false;

Servo myServo;
WiFiServer server(80);

void setup() {
  Serial.begin(115200);
  pinMode(PIR_PIN, INPUT);
  pinMode(TOUCH_PIN, INPUT);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  
  myServo.attach(SERVO_PIN);
  myServo.write(CLOSE_ANGLE);

  // Static IP (Requirement: Proper IP addressing)
  IPAddress local_IP(192, 168, 1, 100);
  IPAddress gateway(192, 168, 1, 1);
  IPAddress subnet(255, 255, 255, 0);
  WiFi.config(local_IP, gateway, subnet);

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  server.begin();

  // Task on Core 1: Sensors, Servo, and Automation Logic
  xTaskCreatePinnedToCore(SensorTask, "SensorTask", 10000, NULL, 1, NULL, 1);
}

// Function to get distance from Ultrasonic Sensor
float getDistance() {
  digitalWrite(TRIG_PIN, LOW); delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH); delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  return pulseIn(ECHO_PIN, HIGH) * 0.034 / 2;
}

// --- CORE 1: AUTOMATION ENGINE ---
void SensorTask(void * pvParameters) {
  unsigned long touchStart = 0;
  
  for (;;) {
    float dist = getDistance();
    
    // Calculate Fill Level
    globalFillLevel = ((BIN_HEIGHT - dist) / (float)BIN_HEIGHT) * 100;
    if (globalFillLevel < 0) globalFillLevel = 0;
    if (globalFillLevel > 100) globalFillLevel = 100;

    // 1. LID CONTROL LOGIC (Event-based)
    if (digitalRead(PIR_PIN) == HIGH && dist < OBSTACLE_THRESHOLD) {
      if (!lidIsOpen) {
        myServo.write(OPEN_ANGLE); // Quick Open
        lidIsOpen = true;
      }
      
      // Keep open as long as someone is there
      while (getDistance() < OBSTACLE_THRESHOLD) {
        vTaskDelay(500 / portTICK_PERIOD_MS); 
      }
      
      delay(2000); // Wait 2 extra seconds after they leave
      
      // Slow Close for Safety
      for (int pos = OPEN_ANGLE; pos >= CLOSE_ANGLE; pos--) {
        myServo.write(pos);
        vTaskDelay(30 / portTICK_PERIOD_MS);
      }
      lidIsOpen = false;
    }

    // 2. CRITICAL TOUCH LOGIC
    if (digitalRead(TOUCH_PIN) == HIGH || (dist < 3 && dist > 0)) {
      if (touchStart == 0) touchStart = millis();
      if (millis() - touchStart > 30000) globalStatus = "CRITICAL - OVERFLOW";
    } else {
      touchStart = 0;
      globalStatus = (globalFillLevel > 80) ? "Full - Needs Collection" : "Normal";
    }

    // 3. LED INDICATORS
    if (globalFillLevel > 80 || touchStart != 0) {
      digitalWrite(LED_RED, HIGH); digitalWrite(LED_GREEN, LOW);
    } else {
      digitalWrite(LED_RED, LOW); digitalWrite(LED_GREEN, HIGH);
    }

    vTaskDelay(200 / portTICK_PERIOD_MS);
  }
}

// --- CORE 0: DASHBOARD WEB SERVER ---
void loop() {
  WiFiClient client = server.available();
  if (client) {
    String request = client.readStringUntil('\r');
    client.flush();
    
    // Send HTML + CSS Responsive Dashboard
    client.println("HTTP/1.1 200 OK\nContent-type:text/html\n");
    client.print("<html><head><title>IoT Waste Manager</title>");
    client.print("<meta name='viewport' content='width=device-width, initial-scale=1'>");
    client.print("<style>body{font-family:Arial; text-align:center; background:#f4f4f4;}");
    client.print(".container{background:white; margin:20px auto; padding:20px; width:80%; border-radius:15px; box-shadow:0 4px 8px rgba(0,0,0,0.1);}");
    client.print(".progress-bg{background:#ddd; border-radius:13px; height:30px; width:100%; margin:20px 0;}");
    client.print(".progress-fill{background:" + String(globalFillLevel > 80 ? "#e74c3c" : "#2ecc71") + "; height:30px; border-radius:13px; width:" + String(globalFillLevel) + "%; transition: width 0.5s;}");
    client.print(".status{font-weight:bold; color:" + String(globalFillLevel > 80 ? "red" : "#333") + ";}");
    client.print("</style><meta http-equiv='refresh' content='3'></head><body>");
    
    client.print("<div class='container'><h1>Smart Bin Dashboard</h1>");
    client.print("<p class='status'>System Status: " + globalStatus + "</p>");
    client.print("<div class='progress-bg'><div class='progress-fill'></div></div>");
    client.print("<h2>" + String((int)globalFillLevel) + "% Full</h2>");
    client.print("<p>Lid is currently: " + String(lidIsOpen ? "OPEN" : "CLOSED") + "</p>");
    client.print("</div></body></html>");
    
    client.stop();
  }
}
