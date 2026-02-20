#include <WiFi.h>
#include <ESP32Servo.h>

// --- Configuration ---
const char* ssid = "Your_MiFi_Name";
const char* password = "Your_Password";

// Pins
#define PIR_PIN 13
#define TOUCH_PIN 14
#define SERVO_PIN 12
#define TRIG_PIN 26
#define ECHO_PIN 25
#define LED_RED 33
#define LED_GREEN 32

// Constants
const int BIN_HEIGHT = 50; // cm
const int CLOSE_ANGLE = 0;
const int OPEN_ANGLE = 100;

// Global Variables (Shared between cores)
float fillLevel = 0;
String binStatus = "Empty";
bool isCritical = false;
unsigned long touchStartTime = 0;

Servo myServo;
WiFiServer server(80);

// --- Task Handles ---
TaskHandle_t SensorTask;

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

  // 1. Static IP Configuration
  IPAddress local_IP(192, 168, 1, 100);
  IPAddress gateway(192, 168, 1, 1);
  IPAddress subnet(255, 255, 255, 0);
  WiFi.config(local_IP, gateway, subnet);

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) delay(500);
  server.begin();

  // 2. Create the Sensor Task on Core 1
  xTaskCreatePinnedToCore(SensorLoop, "SensorTask", 10000, NULL, 1, &SensorTask, 1);
}

// --- CORE 1: SENSOR & SERVO LOGIC ---
void SensorLoop(void * pvParameters) {
  for (;;) {
    // A. Measure Distance
    digitalWrite(TRIG_PIN, LOW); delayMicroseconds(2);
    digitalWrite(TRIG_PIN, HIGH); delayMicroseconds(10);
    digitalWrite(TRIG_PIN, LOW);
    long duration = pulseIn(ECHO_PIN, HIGH);
    float distance = duration * 0.034 / 2;

    // B. Calculate Fill Level
    fillLevel = ((BIN_HEIGHT - distance) / (float)BIN_HEIGHT) * 100;
    if (fillLevel < 0) fillLevel = 0;

    // C. PIR + Distance Lid Logic
    if (digitalRead(PIR_PIN) == HIGH && distance < 20) {
        // Quick Open
        myServo.write(OPEN_ANGLE);
        delay(5000); // Keep open for 5 seconds
        
        // Slow Close (Accident Prevention)
        for (int pos = OPEN_ANGLE; pos >= CLOSE_ANGLE; pos--) {
          myServo.write(pos);
          delay(30); // Higher delay = slower close
        }
    }

    // D. Touch Sensor 30s Logic
    if (digitalRead(TOUCH_PIN) == HIGH || distance < 3) {
      if (touchStartTime == 0) touchStartTime = millis();
      if (millis() - touchStartTime > 30000) isCritical = true;
    } else {
      touchStartTime = 0;
      isCritical = false;
    }

    // E. Status Logic
    if (isCritical) {
      binStatus = "CRITICAL - FULL";
      digitalWrite(LED_RED, HIGH);
      digitalWrite(LED_GREEN, LOW);
    } else if (fillLevel > 80) {
      binStatus = "Full (Alert)";
      digitalWrite(LED_RED, HIGH);
      digitalWrite(LED_GREEN, LOW);
    } else {
      binStatus = "Normal";
      digitalWrite(LED_RED, LOW);
      digitalWrite(LED_GREEN, HIGH);
    }

    vTaskDelay(100 / portTICK_PERIOD_MS); // Small rest for CPU
  }
}

// --- CORE 0: WEB SERVER (runs in default loop) ---
void loop() {
  WiFiClient client = server.available();
  if (client) {
    String currentLine = "";
    while (client.connected()) {
      if (client.available()) {
        char c = client.read();
        if (c == '\n') {
          if (currentLine.length() == 0) {
            // Send HTML Dashboard
            client.println("HTTP/1.1 200 OK");
            client.println("Content-type:text/html");
            client.println();
            client.print("<html><head><meta http-equiv='refresh' content='2'>"); // Auto refresh every 2s
            client.print("<style>body{font-family:sans-serif; text-align:center; padding-top:50px;}");
            client.print(".status-card{display:inline-block; padding:20px; border-radius:10px; border:2px solid #ccc;}");
            client.print("</style></head><body>");
            client.print("<h1>Smart Waste Dashboard</h1>");
            client.print("<div class='status-card'>");
            client.print("<h2>Fill Level: " + String(fillLevel) + "%</h2>");
            client.print("<h3 style='color:" + String(isCritical || fillLevel > 80 ? "red" : "green") + "'>Status: " + binStatus + "</h3>");
            client.print("</div></body></html>");
            break;
          } else { currentLine = ""; }
        } else if (c != '\r') { currentLine += c; }
      }
    }
    client.stop();
  }
}
