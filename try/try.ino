#include <SPI.h>
#include <Wire.h>
#include <MFRC522.h>
#include <LiquidCrystal_I2C.h>
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <WebSocketsServer.h>
#include <ESP8266WebServer.h>
#include <Hash.h>
#include <ArduinoJson.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

#define SS_PIN D8
#define RST_PIN D0
#define LED_PIN D3
#define BUZZER_PIN D4

const char* Wifi_ssid = "PLDTWIFI2G";
const char* Wifi_passwd = "Batistisroel!2";

// WiFi access point credentials that the ESP will create
const char* AP_ssid = "Feeder";
const char* AP_passwd = "password123";

MFRC522 mfrc522(SS_PIN, RST_PIN);
LiquidCrystal_I2C lcd(0x27, 16, 2);
float totalAmount = 0.0;
bool shampooAdded = false;
bool cantonAdded = false;
bool sardinesAdded = false;
bool noodlesAdded = false;
bool tumblerAdded = false;
bool ketchupAdded = false;
bool soapAdded = false;

String value;
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");

ESP8266WiFiMulti    WiFiMulti;
ESP8266WebServer    server(80);
WebSocketsServer    webSocket = WebSocketsServer(81);

/* Front end code (i.e. HTML, CSS, and JavaScript) */
char html_template[] PROGMEM = R"=====(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta http-equiv="X-UA-Compatible" content="IE=edge">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>E Cart using IoT</title>
    <style>
        table {
            border-collapse: collapse;
        }
        th {
            background-color: #3498db;
            color: white;
        }
        table, td {
            border: 4px solid black;
            font-size: x-large;
            text-align: center;
            border-style: groove;
            border-color: rgb(255, 0, 0);
        }
    </style>
        <script>
        var socket = new WebSocket("ws://" + location.host + ":81");

        socket.onopen = function (e) {
          console.log("Hello world!");
            console.log("[socket] socket.onopen ");
        };

        socket.onerror = function (e) {
            console.error("[socket] socket.onerror ", e);
        };

        socket.onmessage = function (e) {
            console.log("[socket] " + e.data);
            var jsonData;
            try {
                jsonData = JSON.parse(e.data);
                var cmd = jsonData.cmd;

                if (cmd === 1) {
                    updateItemTable(jsonData);
                }
            } catch (error) {
                console.error("Error parsing JSON:", error);
            }
        };

        window.onload = function () {
            var payNowButton = document.getElementById("payNowButton");
            payNowButton.addEventListener("click", function () {
                // Add logic to connect to the printer
            });
        };

  function updateItemTable(data) {
    console.log(data);
    var table = document.getElementById("itemTable");

    // Clear existing rows
    while (table.rows.length > 1) {
        table.deleteRow(1);
    }

    // Add new rows based on WebSocket data
    var items = data.items;
    console.log("Received items:", items);  // Add this line for debugging

    for (var i = 0; i < items.length; i++) {
        var item = items[i];
        console.log("Item:", item);  // Add this line for debugging

        var row = table.insertRow(-1);
        var itemNameCell = row.insertCell(0);
        var quantityCell = row.insertCell(1);
        var costCell = row.insertCell(2);

        itemNameCell.textContent= item.productName;
        quantityCell.innerHTML = item.quantity;
        costCell.textContent= item.price;
    }

    // Update Grand Total
    var grandTotalRow = table.insertRow(-1);
    var totalLabelCell = grandTotalRow.insertCell(0);
    var totalCountCell = grandTotalRow.insertCell(1);
    var totalCostCell = grandTotalRow.insertCell(2);

    totalLabelCell.textContent= "<th>Grand Total</th>";
    totalCountCell.textContent= "<th>" + data.totalItems + "</th>";
    totalCostCell.textContent= "<th>" + data.totalCost + "</th>";
}

    </script>
</head>
<body>
    <center>
        <h1>Smart Shopping Cart using IoT</h1><br><br>
        <table id="itemTable" style="width: 1200px; height: 450px;">
            <tr>
                <th>ITEMS</th>
                <th>QUANTITY</th>
                <th>COST</th>
            </tr>
            <!-- Rows will be dynamically added by WebSocket -->
        </table><br>
        <input type="button" id="payNowButton" name="Pay Now" value="Pay Now" style="width: 200px; height: 50px">
    </center>

</body>
</html>
)=====";
void displayTotalAmount();
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length); // It handle all web socket responses
void handleMain(); 
void handleNotFound(); 
void displayProductInfo(const String& productName, const String& price, const String& total);
void processRFIDData(const String& cardUID);
void sendRFIDData(const String& product, const String& action, float amount);

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println();
  Serial.println("############ Serial Started ############");
  
  // Initialize ESP8266 in AP mode
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_ssid, AP_passwd);
  Serial.print("AP started: ");
  Serial.print(AP_ssid);
  Serial.print(" ~ ");
  Serial.println(AP_passwd);

  // Connect to a WiFi as client
  int attempt = 0;
  WiFiMulti.addAP(Wifi_ssid, Wifi_passwd);
  Serial.print("Connecting to: ");
  Serial.print(Wifi_ssid);
  while (WiFiMulti.run() != WL_CONNECTED) {
    delay(20);
    Serial.print(".");
    attempt++;
    if (attempt > 5 ) break;
  }
  Serial.println();
  if (WiFiMulti.run() == WL_CONNECTED) {
    Serial.print("Connected to: ");
    Serial.println(Wifi_ssid);
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
  } else Serial.println("Error: Unable to connect to the WiFi");

  // begin the web socket
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  Serial.println("Web socket started");

  server.on("/", handleMain);
  server.onNotFound(handleNotFound);
  server.begin();

  SPI.begin();
  mfrc522.PCD_Init();
  lcd.init();
  lcd.backlight();
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

    if (!mfrc522.PCD_PerformSelfTest()) {
        Serial.println("RFID connection failed");
        lcd.setCursor(0, 0);
        lcd.print("Closed");
    } else {
        Serial.println("RFID connected and working");
        lcd.setCursor(0, 0);
        lcd.print("Happy Shopping");
    }
    delay(2000);
    lcd.clear();
    displayTotalAmount();

}

void loop() {
    displayTotalAmount();

    if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
        String cardUID = "";

        for (byte i = 0; i < mfrc522.uid.size; i++) {
            cardUID += (mfrc522.uid.uidByte[i] < 0x10 ? "0" : "") + String(mfrc522.uid.uidByte[i], HEX);
        }

        // Process the RFID data and send it to WebSocket clients
        processRFIDData(cardUID);

        // Beep and blink to indicate successful RFID scan
        digitalWrite(BUZZER_PIN, HIGH);
        digitalWrite(LED_PIN, HIGH);
        delay(500);
        digitalWrite(BUZZER_PIN, LOW);
        digitalWrite(LED_PIN, LOW);

        // Halt the RFID card
        mfrc522.PICC_HaltA();
        mfrc522.PCD_StopCrypto1();
        delay(3000);
        lcd.clear();
        displayTotalAmount();
    }

        webSocket.loop();
        server.handleClient();
}

void displayProductInfo(const String& productName, const String& price, const String& total) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(productName + ": " + price);
    lcd.setCursor(0, 1);
    lcd.print("Total: " + total);
}

void displayTotalAmount() {
    lcd.setCursor(0, 1);
    lcd.print("Total: " + String(totalAmount));
}

// it handles all the web socket
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
    switch (type) {
        case WStype_DISCONNECTED: {
            Serial.printf("[%u] Disconnected!\n", num);
            break;
        }
        case WStype_CONNECTED: {
            IPAddress ip = webSocket.remoteIP(num);
            Serial.printf("[%u] Connected from %d.%d.%d.%d url: %s\n", num, ip[0], ip[1], ip[2], ip[3], payload);
            // Send initial message to the client
            webSocket.sendTXT(num, "0");
            // Check if there are any scanned RFID items and send them
            if (shampooAdded || cantonAdded || sardinesAdded || noodlesAdded || tumblerAdded || ketchupAdded || soapAdded) {
                sendRFIDData("Shampoo", shampooAdded ? "Removed" : "Added", 10.00);
                sendRFIDData("Canton", cantonAdded ? "Removed" : "Added", 20.00);
                // Add similar calls for other products...
            }
            break;
        }
        case WStype_TEXT: {
            StaticJsonDocument<200> doc;
            deserializeJson(doc, payload, length);
            uint8_t cmd = doc["cmd"];
            Serial.print("cmd: ");
            Serial.print(cmd);

            switch (cmd) {
                case 1: {
                    // Handle the update command
                   Serial.printf("[%u] Received text: %s\n", num, payload);

                    // Extract data from the payload
                    int totalItems = doc["totalItems"];
                    float totalCost = doc["totalCost"];
                    JsonArray itemsArray = doc["items"];

                    // Prepare data for updating the item table
                    DynamicJsonDocument responseDoc(512);
                    responseDoc["cmd"] = 1;  // Command for updating the item table
                    responseDoc["totalItems"] = totalItems;
                    responseDoc["totalCost"] = totalCost;
                    responseDoc["items"] = itemsArray;

                    // Convert the JSON document to a string
                    String jsonResponse;
                    serializeJson(responseDoc, jsonResponse);

                    // Send the updated data to all connected clients
                    webSocket.broadcastTXT(jsonResponse);
                    break;
                }
                case 2: {
                    // Handle the RFID event command
                    Serial.println("Received RFID event command");

                    // Extract data from the payload
                    String product = doc["product"];
                    String action = doc["action"];
                    float amount = doc["amount"];

                    // Call sendRFIDData with the extracted data
                    sendRFIDData(product, action, amount);
                    break;
                }
            }
            break;
        }
    }
}

void handleMain() {
  server.send_P(200, "text/html", html_template ); 
}
void handleNotFound() {
  server.send(404,   "text/html", "<html><body><p>404 Error</p></body></html>" );
}

void processRFIDData(const String& cardUID) {
  Serial.print("Card UID: ");
    Serial.println(cardUID);

    if (cardUID.equals("61611626")) {
        if (shampooAdded) {
            totalAmount -= 10.00;
            displayProductInfo("Shampoo Removed", "10.00", String(totalAmount));
            sendRFIDData("Shampoo", "Removed", 10.00);
            shampooAdded = false;
        } else {
            totalAmount += 10.00;
            displayProductInfo("Shampoo", "10.00", String(totalAmount));
            sendRFIDData("Shampoo", "Added", 10.00);
            shampooAdded = true;
        }
    } else if (cardUID.equals("c1a89221")) {
        if (cantonAdded) {
            totalAmount -= 20.00;
            displayProductInfo("Canton Removed", "20.00", String(totalAmount));
            sendRFIDData("Canton", "Removed", 20.00);
            cantonAdded = false;
        } else {
            totalAmount += 20.00;
            displayProductInfo("Canton", "20.00", String(totalAmount));
            sendRFIDData("Canton", "Added", 20.00);
            cantonAdded = true;
        }
    }
    // Add similar logic for other products...
}

void sendRFIDData(const String& product, const String& action, float amount) {

    Serial.println("Sending RFID Data:");
    Serial.print("Product: ");
    Serial.println(product);
    Serial.print("Action: ");
    Serial.println(action);
    Serial.print("Amount: ");
    Serial.println(amount);
    // Prepare JSON data for RFID event
    DynamicJsonDocument doc(256);
    doc["cmd"] = 2.0;  // Command for RFID event
    doc["product"] = product;
    doc["action"] = action;
    doc["amount"] = amount;

    // Convert the JSON document to a string
    String jsonData;
    serializeJson(doc, jsonData);

    // Send the RFID data to all connected WebSocket clients
    webSocket.broadcastTXT(jsonData);
}

