// Librerias
#include <WiFi.h>
#include <PubSubClient.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "DHT.h"

// Credenciales de WiFi
const char* ssid = "MOVISTAR-WIFI6-CC80";
const char* password = "CpUrcB5biQuVMWDTuH5c";

// Configuración del broker MQTT
const char* mqtt_broker = "broker.emqx.io";
const char* mqtt_topic = "LALO/esp32";
const char* mqtt_username = "ESP32House";
const char* mqtt_password = "1234";
const int mqtt_port = 1883;

// Configuración del sensor DHT22
#define DHTPIN 4
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

WiFiClient espClient;
PubSubClient mqtt_client(espClient);

// Configuración de la API de TimeZoneDB
const char* apiKey = "VVTOF50L27UI";
const char* timeZone = "America/Lima";
String apiUrl = "http://api.timezonedb.com/v2.1/get-time-zone?key=" + String(apiKey) + "&format=json&by=zone&zone=" + String(timeZone);

void setup() {
    Serial.begin(115200);
    dht.begin();
    connectToWiFi();
    mqtt_client.setServer(mqtt_broker, mqtt_port);
    mqtt_client.setCallback(mqttCallback);
    connectToMQTT();
}

void loop() {
    if (!mqtt_client.connected()) {
        Serial.println("MQTT desconectado, intentando reconectar...");
        connectToMQTT();
    }
    mqtt_client.loop();

    static unsigned long lastMsg = 0;
    if (millis() - lastMsg > 300000) {
        lastMsg = millis();
        String data = getFormattedData();
        Serial.print("Publicando datos: ");
        Serial.println(data);
        mqtt_client.publish(mqtt_topic, data.c_str());
    }
}

// Conectar a la red WiFi con temporizador de reinicio
void connectToWiFi() {
    WiFi.begin(ssid, password);
    Serial.print("Conectando a WiFi");

    unsigned long startAttemptTime = millis();
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");

        // Si pasan más de 15 segundos sin conexión, reiniciar el ESP32
        if (millis() - startAttemptTime > 15000) {
            Serial.println("\nNo se pudo conectar a WiFi, reiniciando...");
            ESP.restart();
        }
    }
    Serial.println("\nConectado a WiFi");
}

// Conectar al broker MQTT
void connectToMQTT() {
    while (!mqtt_client.connected()) {
        String client_id = "esp32-client-" + String(WiFi.macAddress());
        Serial.printf("Conectando al broker MQTT como %s...\n", client_id.c_str());
        
        if (mqtt_client.connect(client_id.c_str(), mqtt_username, mqtt_password)) {
            Serial.println("Conectado al broker MQTT");
            mqtt_client.subscribe(mqtt_topic);
        } else {
            Serial.print("Fallo, rc=");
            Serial.print(mqtt_client.state());
            Serial.println(" intentando de nuevo en 5 segundos");
            delay(5000);
        }
    }
}

// Callback para manejar mensajes MQTT
void mqttCallback(char* topic, byte* payload, unsigned int length) {
    Serial.print("Mensaje recibido en el tópico: ");
    Serial.println(topic);
    
    String message;
    for (unsigned int i = 0; i < length; i++) {
        message += (char)payload[i];  // Convertir el mensaje recibido a String
    }

    Serial.print("Mensaje: ");
    Serial.println(message);
    Serial.println("-----------------------");

    // Si recibe "medir", envía la lectura del sensor
    if (message == "medir") {
        String data = getFormattedData();
        Serial.print("Respondiendo con datos: ");
        Serial.println(data);
        mqtt_client.publish(mqtt_topic, data.c_str()); // Publica la respuesta
    }
}

// Obtener datos formateados con temperatura, humedad, fecha y hora
String getFormattedData() {
    float temperature = dht.readTemperature();
    float humidity = dht.readHumidity();
    if (isnan(temperature) || isnan(humidity)) {
        Serial.println("Fallo al leer del sensor DHT!");
        return "{\"temperatura\": NaN, \"humedad\": NaN, \"fecha\": \"\", \"hora\": \"\"}";
    }

    String formattedTime = obtenerHoraDesdeAPI();
    if (formattedTime == "") {
        Serial.println("Fallo al obtener la hora desde la API");
        return "{\"temperatura\": NaN, \"humedad\": NaN, \"fecha\": \"\", \"hora\": \"\"}";
    }

    StaticJsonDocument<200> doc;
    doc["temperatura"] = temperature;
    doc["humedad"] = humidity;
    doc["fecha"] = formattedTime.substring(0, 10); // Extraer parte de la fecha
    doc["hora"] = formattedTime.substring(11);     // Extraer parte de la hora

    String payload;
    serializeJson(doc, payload);
    return payload;
}

// Obtener la fecha y hora actual desde la API de TimeZoneDB
String obtenerHoraDesdeAPI() {
    if (WiFi.status() == WL_CONNECTED) {
        HTTPClient http;
        http.begin(apiUrl);
        int httpResponseCode = http.GET();

        String formattedTime = "";
        if (httpResponseCode == 200) {
            String payload = http.getString();
            StaticJsonDocument<512> doc;
            DeserializationError error = deserializeJson(doc, payload);

            if (!error) {
                formattedTime = String(doc["formatted"]);
            } else {
                Serial.println("Error al analizar JSON");
            }
        } else {
            Serial.print("Error en la solicitud HTTP, código: ");
            Serial.println(httpResponseCode);
        }

        http.end();  // Liberar memoria siempre
        return formattedTime;
    } else {
        Serial.println("No conectado a WiFi");
        return "";
    }
}