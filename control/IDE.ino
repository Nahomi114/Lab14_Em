#include <WiFi.h>
#include <WebSocketsClient.h>
#include <ESP32Servo.h>
#include <ArduinoJson.h>

// Configuración de WiFi y servidor
const char* ssid = "Pablito - 2.4 Ghz";
const char* password = "301245pablo";
const char* serverAddress = "192.168.1.3";
const int serverPort = 8080;

// Definición de pines
const int SERVO_PIN = 13;
const int POT_PIN = 35;
const int BUTTON_PIN = 16;
const int LED_PIN_START = 14;
const int LED_PIN_RESET = 12;

// Variables globales
Servo myServo;
WebSocketsClient webSocket;
int lastPotValue = 0;
int lastServoPos = 0;
const int INITIAL_POSITION = 0;
bool systemEnabled = false;
bool potControl = false;
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 50;
bool lastButtonState = HIGH;

void setup() {
  Serial.begin(115200);

  // Configuración del servo
  ESP32PWM::allocateTimer(0);
  myServo.setPeriodHertz(50);
  myServo.attach(SERVO_PIN, 500, 2400);
  myServo.write(INITIAL_POSITION);

  // Configuración de pines
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_PIN_START, OUTPUT);
  pinMode(LED_PIN_RESET, OUTPUT);
  
  // Estado inicial
  digitalWrite(LED_PIN_START, LOW);
  digitalWrite(LED_PIN_RESET, HIGH);  // Encendido al inicio ya que está en posición inicial

  // Conexión WiFi
  WiFi.begin(ssid, password);
  Serial.print("Conectando a WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConectado a WiFi");
  Serial.print("Dirección IP: ");
  Serial.println(WiFi.localIP());

  // Configuración WebSocket
  webSocket.begin(serverAddress, serverPort, "/ws");
  webSocket.onEvent(webSocketEvent);
  webSocket.setReconnectInterval(5000);
}

void loop() {
  webSocket.loop();

  // Manejar el botón de reinicio con debounce
  bool reading = digitalRead(BUTTON_PIN);
  if (reading != lastButtonState) {
    lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (reading == LOW) {  // Botón presionado
      resetServo();
    }
  }
  lastButtonState = reading;

  // Si el sistema está habilitado y el control por potenciómetro está activo
  if (systemEnabled && potControl) {
    handlePotentiometer();
  }

  // Actualizar LED de reinicio basado en la posición del servo
  digitalWrite(LED_PIN_RESET, (lastServoPos == INITIAL_POSITION));
}

void resetServo() {
  myServo.write(INITIAL_POSITION);
  lastServoPos = INITIAL_POSITION;
  digitalWrite(LED_PIN_RESET, HIGH);
  sendStatus();
  Serial.println("Servo reseteado a posición inicial");
}

void handlePotentiometer() {
  int currentPotValue = analogRead(POT_PIN);
  if (abs(currentPotValue - lastPotValue) > 20) {  // Umbral para reducir ruido
    lastPotValue = currentPotValue;
    int servoPos = map(currentPotValue, 0, 4095, 0, 180);
    if (abs(servoPos - lastServoPos) > 2) {  // Umbral para movimiento del servo
      myServo.write(servoPos);
      lastServoPos = servoPos;
      Serial.printf("Pot: %d -> Servo: %d°\n", currentPotValue, servoPos);
      sendStatus();
    }
  }
}

void sendStatus() {
  StaticJsonDocument<200> doc;
  doc["servoPos"] = lastServoPos;
  doc["potValue"] = lastPotValue;
  doc["systemEnabled"] = systemEnabled;
  doc["potControl"] = potControl;
  doc["isReset"] = (lastServoPos == INITIAL_POSITION);
  
  String jsonString;
  serializeJson(doc, jsonString);
  webSocket.sendTXT(jsonString);
  Serial.println("Estado enviado: " + jsonString);
}

void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED:
      Serial.println("Desconectado del WebSocket");
      break;
      
    case WStype_CONNECTED:
      Serial.println("Conectado al WebSocket");
      // Enviar mensaje de identificación
      webSocket.sendTXT("ESP32");
      // Luego enviar el estado inicial
      delay(1000); // Pequeña pausa para asegurar que el servidor procese la identificación
      sendStatus();
      break;
      
    case WStype_TEXT: {
      String message = String((char *)payload);
      Serial.println("Mensaje recibido: " + message);
      
      StaticJsonDocument<200> doc;
      DeserializationError error = deserializeJson(doc, message);
      
      if (!error) {
        if (doc.containsKey("enable")) {
          systemEnabled = doc["enable"];
          digitalWrite(LED_PIN_START, systemEnabled);
          Serial.printf("Sistema %s\n", systemEnabled ? "habilitado" : "deshabilitado");
        }
        if (doc.containsKey("potControl")) {
          potControl = doc["potControl"];
          Serial.printf("Control por potenciómetro %s\n", potControl ? "activado" : "desactivado");
        }
        if (doc.containsKey("servoPos") && systemEnabled && !potControl) {
          int newPos = doc["servoPos"];
          if (newPos >= 0 && newPos <= 180) {
            myServo.write(newPos);
            lastServoPos = newPos;
            Serial.printf("Servo movido a posición: %d°\n", newPos);
          }
        }
        sendStatus();
      } else {
        Serial.println("Error al parsear JSON");
      }
      break;
    }
    
    case WStype_ERROR:
      Serial.println("Error en WebSocket");
      break;
      
    case WStype_PING:
      Serial.println("Ping recibido");
      break;
      
    case WStype_PONG:
      Serial.println("Pong recibido");
      break;
  }
}