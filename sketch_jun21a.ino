#ifdef ESP32
#include <WiFi.h>
#include <EEPROM.h>
#else
#include <ESP8266WiFi.h>
#include <EEPROM.h>
#endif
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

const char* ssid = "DESKTOP-31N24P4 8540";
const char* password = "thilina123";

#define BOTtoken "7096100609:AAHGvi1IEM5gvorFzJ3j2UNHqkXvinKd8c4"
#define CHAT_ID "-4259684685"

#ifdef ESP8266
X509List cert(TELEGRAM_CERTIFICATE_ROOT);
#endif

WiFiClientSecure client;
UniversalTelegramBot bot(BOTtoken, client);

int botRequestDelay = 1000;
unsigned long lastTimeBotRan;

bool previousStateD1 = true;
bool previousStateD3 = true;
bool previousStateD5 = true;

const int pinD1 = 5;  // GPIO 5 (D1) - CEB Line
const int pinD3 = 4;  // GPIO 4 (D3) - Solar Line
const int pinD5 = 0;  // GPIO 0 (D5) - Generator Line

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");

const int STATUS_AFTER_RESTART_FLAG_ADDRESS = 0;
const int LAST_RESTART_HOUR_ADDRESS = 1;
const int LAST_RESTART_MINUTE_ADDRESS = 2;
const int LAST_PROCESSED_MESSAGE_ID_ADDRESS = 3;

int lastProcessedMessageId = -1; // Initialize with -1 to indicate no message processed yet

// Function Prototypes
void setupWiFi();
void setupNTP();
void sendMessage(const String& message);
void handleNewMessages(int numNewMessages);
String createStatusMessage(const String& from_name);
String createAlertMessage();
void checkStatusChange();
void handleFailedMessageSending();
void checkDailyRestart();

void setup() {
  Serial.begin(115200);
  EEPROM.begin(512);

#ifdef ESP8266
  configTime(0, 0, "pool.ntp.org");
  client.setTrustAnchors(&cert);
#endif

  pinMode(pinD1, INPUT_PULLUP);
  pinMode(pinD3, INPUT_PULLUP);
  pinMode(pinD5, INPUT_PULLUP);

  setupWiFi();
  setupNTP();

  // Check if the restart flag is set and send the daily status message
  if (EEPROM.read(STATUS_AFTER_RESTART_FLAG_ADDRESS) == 1) {
    String message = createStatusMessage("Daily Restart Status");
    sendMessage(message);
    EEPROM.write(STATUS_AFTER_RESTART_FLAG_ADDRESS, 0);
    EEPROM.commit();
  }

  // Load the last processed message ID from EEPROM
  lastProcessedMessageId = EEPROM.read(LAST_PROCESSED_MESSAGE_ID_ADDRESS);
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    handleFailedMessageSending();

    if (millis() > lastTimeBotRan + botRequestDelay) {
      int numNewMessages = bot.getUpdates(bot.last_message_received + 1);
      while (numNewMessages) {
        Serial.println("got response");
        handleNewMessages(numNewMessages);
        numNewMessages = bot.getUpdates(bot.last_message_received + 1);
      }
      lastTimeBotRan = millis();
    }

    checkStatusChange();
    checkDailyRestart();
  } else {
    setupWiFi();  // Reconnect to WiFi if disconnected(he he...important-newly added function)
  }

  delay(100);
}

void setupWiFi() {
  Serial.print("Connecting to WiFi...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

#ifdef ESP32
  client.setCACert(TELEGRAM_CERTIFICATE_ROOT);
#endif

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.print("Connected to WiFi network with IP Address: ");
  Serial.println(WiFi.localIP());

  int32_t rssi = WiFi.RSSI();
  Serial.print("Signal strength (RSSI): ");
  Serial.print(rssi);
  Serial.println(" dBm");
}

void setupNTP() {
  timeClient.begin();
  timeClient.setTimeOffset(19800); 
  timeClient.forceUpdate();
}

void sendMessage(const String& message) {
  bot.sendMessage(CHAT_ID, message, "");
}

void handleNewMessages(int numNewMessages) {
  Serial.println("handleNewMessages");
  Serial.println(String(numNewMessages));

  for (int i = 0; i < numNewMessages; i++) {
    String chat_id = String(bot.messages[i].chat_id);
    String text = bot.messages[i].text;
    int message_id = bot.messages[i].message_id;
    Serial.println(text);
    String from_name = bot.messages[i].from_name;

    // Check the message ID
    if (chat_id == CHAT_ID && message_id > lastProcessedMessageId) {
      if (text == "/status") {
        String message = createStatusMessage(from_name);
        sendMessage(message);

        // Update the last processed message ID
        lastProcessedMessageId = message_id;
        EEPROM.write(LAST_PROCESSED_MESSAGE_ID_ADDRESS, lastProcessedMessageId);
        EEPROM.commit();
      }
    } else if (chat_id != CHAT_ID) {
      bot.sendMessage(chat_id, "Unauthorized user", "");
    }
  }
}

String createStatusMessage(const String& from_name) {
  bool currentStateD1 = digitalRead(pinD1);
  bool currentStateD3 = digitalRead(pinD3);
  bool currentStateD5 = digitalRead(pinD5);

  Serial.print("D1 state: "); Serial.println(currentStateD1);
  Serial.print("D3 state: "); Serial.println(currentStateD3);
  Serial.print("D5 state: "); Serial.println(currentStateD5);

  int hours = timeClient.getHours();
  int minutes = timeClient.getMinutes();
  int seconds = timeClient.getSeconds();

  String message = "Welcome, " + from_name + ".\n";
  message += "Requested Update on the Inverter Room Status.\n\n";
  message += "Status Request : " + String(hours) + ":" + String(minutes) + ":" + String(seconds) + "\n";

  message += (currentStateD1 == HIGH) ? "\nCEB Down" : "\nCEB Connected";
  message += (currentStateD3 == HIGH) ? "\nSolar  Down" : "\nSolar Connected";
  message += (currentStateD5 == HIGH) ? "\nGenerator Down" : "\nGenerator Connected";

  return message;
}

String createAlertMessage() {
  bool currentStateD1 = digitalRead(pinD1);
  bool currentStateD3 = digitalRead(pinD3);
  bool currentStateD5 = digitalRead(pinD5);

  String message = "ALERT: Inverter Room Status Change Detected!\n";

  message += (currentStateD1 == HIGH) ? "\nCEB Down" : "\nCEB Connected";
  message += (currentStateD3 == HIGH) ? "\nSolar Down" : "\nSolar Connected";
  message += (currentStateD5 == HIGH) ? "\nGenerator Down" : "\nGenerator Connected";

  return message;
}

void checkStatusChange() {
  bool currentStateD1 = digitalRead(pinD1);
  bool currentStateD3 = digitalRead(pinD3);
  bool currentStateD5 = digitalRead(pinD5);

  Serial.print("Checking status change: D1 = ");
  Serial.print(currentStateD1);
  Serial.print(", D3 = ");
  Serial.print(currentStateD3);
  Serial.print(", D5 = ");
  Serial.println(currentStateD5);

  if (currentStateD1 != previousStateD1 || currentStateD3 != previousStateD3 || currentStateD5 != previousStateD5) {
    delay(2000);  // debounce
    currentStateD1 = digitalRead(pinD1);
    currentStateD3 = digitalRead(pinD3);
    currentStateD5 = digitalRead(pinD5);

    if (currentStateD1 != previousStateD1 || currentStateD3 != previousStateD3 || currentStateD5 != previousStateD5) {
      String message = createAlertMessage();
      sendMessage(message);
      previousStateD1 = currentStateD1;
      previousStateD3 = currentStateD3;
      previousStateD5 = currentStateD5;
    }
  }
}

void handleFailedMessageSending() {
  static unsigned long lastFailedAttemptTime = 0;
  const unsigned long retryInterval = 30000;  // Retry interval(ms)

  if (WiFi.status() != WL_CONNECTED && (millis() - lastFailedAttemptTime >= retryInterval)) {
    String errorMessage = "Message not sent due to a connection issue\n";
    errorMessage += "Time: " + timeClient.getFormattedTime();
    Serial.println(errorMessage);
    lastFailedAttemptTime = millis();
  }
}

void checkDailyRestart() {
  int hours = timeClient.getHours();
  int minutes = timeClient.getMinutes();

  // Check if it's 8:00 AM and it's a new day (only check once per day)- Tada... its a new day
  if (hours == 00 && minutes == 0 && 
      !(EEPROM.read(LAST_RESTART_HOUR_ADDRESS) == 00 && EEPROM.read(LAST_RESTART_MINUTE_ADDRESS) == 0)) {
    EEPROM.write(STATUS_AFTER_RESTART_FLAG_ADDRESS, 1);
    EEPROM.write(LAST_RESTART_HOUR_ADDRESS, 00);
    EEPROM.write(LAST_RESTART_MINUTE_ADDRESS,0);
    EEPROM.commit();

    // Restart the board after a short delay
    delay(1000);
    ESP.restart();
  }
}
