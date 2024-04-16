// Please select the corresponding model
#define SIM800L_AXP192_VERSION_20200327

// Define the serial console for debug prints, if needed
#define DUMP_AT_COMMANDS
#define TINY_GSM_DEBUG          SerialMon
#include <string.h>
#include "utilities.h"

// Set serial for debug console (to the Serial Monitor, default speed 115200)
#define SerialMon Serial
// Set serial for AT commands (to the module)
#define SerialAT  Serial1

// Configure TinyGSM library
#define TINY_GSM_MODEM_SIM800          // Modem is SIM800
#define TINY_GSM_RX_BUFFER      1024   // Set RX buffer to 1Kb

#include <TinyGsmClient.h>
#include <WiFi.h>
#include <WebSocketClient.h>

const char* ssid = "";
const char* password = "";
char path[] = "/";
char host[] = "";

WebSocketClient webSocketClient;
WiFiClient client;

#define PIN_RELAY_GREEN 18 // Pin del relé para la luz verde
#define PIN_RELAY_RED 19   // Pin del relé para la luz roja


bool puertaAbierta = false;

TinyGsm modem(SerialAT); // Declaración de la variable modem

String res = "";
bool new_data = false;
char* response = "";
char* lower = "";
char* valueOn = "relay on";
char* valueOff = "relay off";
#define SMS_TARGET "+1234567890" // Número de teléfono al que se enviará el SMS

void setup() {
  SerialMon.begin(115200);
  pinMode(PIN_RELAY_GREEN, OUTPUT); // Configura el pin 18 como salida para el relé verde
  pinMode(PIN_RELAY_RED, OUTPUT);   // Configura el pin 19 como salida para el relé rojo
  delay(10);

  // Start power management
  if (!setupPMU()) {
    SerialMon.println("Setting power error");
  }

  // Some start operations
  setupModem();

  // Set GSM module baud rate and UART pins
  SerialAT.begin(115200, SERIAL_8N1, MODEM_RX, MODEM_TX);
  delay(6000);

  SerialMon.println("Initializing modem...");
  modem.restart();

  String modemInfo = modem.getModemInfo();
  SerialMon.print("Modem Info: ");
  SerialMon.println(modemInfo);
  delay(1000);

  SerialAT.println("AT"); //Once the handshake test is successful, it will back to OK
  updateSerial();

  SerialAT.println("AT+CMGF=1"); // Configuring TEXT mode
  updateSerial();
  SerialAT.println("AT+CNMI=1,2,0,0,0"); // Decides how newly arrived SMS messages should be handled
  updateSerial();

  // Send initialization message to the target number
  sendSMS("System started successfully.");

  // WiFi setup
  Serial.begin(115200);
  delay(10);
  Serial.println();
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);
  
  WiFi.begin(ssid, password);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");  
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

  delay(5000);

  connectToWebSocket(); // Intentar conectar al servidor WebSocket
}

void loop() {
  updateSerial();

  String data;

  if (client.connected()) {
    webSocketClient.getData(data);
    if (data.length() > 0) {
      Serial.print("Received data: ");
      Serial.println(data);

      // Cambiar el color según el comando recibido
      if (data == "openDoor") {
        digitalWrite(PIN_RELAY_GREEN, HIGH); // Enciende el color verde
        digitalWrite(PIN_RELAY_RED, LOW);     // Apaga el color rojo
      } else if (data == "closeDoor") {
        digitalWrite(PIN_RELAY_GREEN, LOW);   // Apaga el color verde
        digitalWrite(PIN_RELAY_RED, HIGH);    // Enciende el color rojo
      }
    }
  } else {
    Serial.println("Client disconnected.");
    connectToWebSocket(); // Intentar reconectar al servidor WebSocket
  }
}

void updateSerial() {
  static int ringCount = 0;
  static bool isDeviceOn = false;

  delay(500);
  while (Serial.available()) {
    SerialAT.write(Serial.read()); // Forward what Serial received to Software Serial Port
  }
  while (SerialAT.available()) {
    char add = SerialAT.read();
    res = res + add;
    delay(1);
    new_data = true;
  }

  if (new_data) {
    response = &res[0];
    const int length = strlen(response); // get the length of the text
    lower = (char*)malloc(length + 1); // allocate 'length' bytes + 1 (for null terminator) and cast to char*
    lower[length] = 0; // set the last byte to a null terminator
    for (int i = 0; i < length; i++) {
      lower[i] = tolower(response[i]);
    }
    Serial.println("Received SMS: " + String(lower)); // Print the received SMS
    Serial.println("\n");

    // Check if the message received is from the target number and contains "relay on" or "relay off"
    if (strstr(lower, "ring")) {
      ringCount++;
      if (ringCount >= 3 && !isDeviceOn) {
        // Enciende el dispositivo si no está encendido
        digitalWrite(PIN_RELAY_GREEN, HIGH); // Enciende el relé en el pin 18
        sendSMS("Device turned ON.");
        isDeviceOn = true;
      } else if (ringCount >= 3 && isDeviceOn) {
        // Apaga el dispositivo si ya está encendido
        digitalWrite(PIN_RELAY_GREEN, LOW); // Apaga el relé en el pin 18
        sendSMS("Device turned OFF.");
        isDeviceOn = false;
      }
    } else {
      ringCount = 0; // Reinicia el contador si no es "ring"
    }

    if (strstr(lower, valueOn)) {
      // Relay command received: "relay on"
      Serial.println("Relay command received: ON");
      digitalWrite(PIN_RELAY_GREEN, HIGH); // Enciende el relé en el pin 18
      sendSMS("Received command: Relay ON."); // Send SMS to target number
    }
    else if (strstr(lower, valueOff)) {
      // Relay command received: "relay off"
      Serial.println("Relay command received: OFF");
      digitalWrite(PIN_RELAY_GREEN, LOW); // Apaga el relé en el pin 18
      sendSMS("Received command: Relay OFF."); // Send SMS to target number
    }

    response = "";
    res = "";
    lower = "";
    new_data = false;
  }
}

void sendSMS(const String& message) {
  res = modem.sendSMS(SMS_TARGET, message);
  SerialMon.println("SMS: " + String(res ? "OK" : "Fail"));
}

void connectToWebSocket() {
  // Intentar reconexión al servidor WebSocket
  while (!client.connect("", 5000)) {
    Serial.println("Connection failed. Retrying...");
    delay(5000); // Esperar 5 segundos antes de intentar nuevamente
  }

  // Handshake with the server
  webSocketClient.path = path;
  webSocketClient.host = host;
  if (webSocketClient.handshake(client)) {
    Serial.println("Handshake successful");
  } else {
    Serial.println("Handshake failed.");
    // No es necesario entrar en un bucle infinito aquí, simplemente volverá a intentar conectarse en el siguiente ciclo loop()
  }
}
