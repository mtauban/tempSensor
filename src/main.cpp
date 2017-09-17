#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>
#include <FS.h>
#include <Hash.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>

#include <DallasTemperature.h>
#include <ArduinoJson.h>

#include <user_default.h>


#define ONE_WIRE_BUS D1
#define CLIENT_DATA_JSON_SIZE 500

#define UD_RELAY_PIN 16
#define UD_LED_PIN LED_BUILTIN
#define UD_SECURITY_MS 300000

#define UD_RELAY_OFF HIGH
#define UD_RELAY_ON  LOW

#define UD_STATE_OFF 0
#define UD_STATE_ON 1


// #define DEBUG_CORS (1)

unsigned int relaystate = 0;
unsigned long lasttime = 0;

char temperatureString[6];

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature DS18B20(&oneWire);

// SKETCH BEGIN
AsyncWebServer server(80);

AsyncEventSource events("/events");



float getTemperature() {
    float temp;
    do {
        DS18B20.requestTemperatures();
        temp = DS18B20.getTempCByIndex(0);
        delay(1);
    } while (temp == 85.0 || temp == (-127.0));
    return temp;
}



int turnRelayOn() {
    digitalWrite(UD_RELAY_PIN, UD_RELAY_ON);
    relaystate = UD_STATE_ON;
    return relaystate;
}
int turnRelayOff() {
    digitalWrite(UD_RELAY_PIN, UD_RELAY_OFF);
    relaystate = UD_STATE_OFF;
    return relaystate;
}

int getRelayState() {
    return relaystate;
}

String formatedJSONResponse() {
    StaticJsonBuffer<CLIENT_DATA_JSON_SIZE> jsonBuffer;
    JsonObject& root = jsonBuffer.createObject();

    dtostrf(getTemperature(), 2, 2, temperatureString);

    root["temp"] = temperatureString;
    unsigned long dlm = millis() - lasttime;
    root["dlm"] = dlm;
    root["status"] = relaystate;
    String ret;
    root.printTo(ret);
    return ret;
}


// Intended for debugging of frontend only
// returns a very permissive CORS response to AJAX request
#ifdef DEBUG_CORS
void handleCORS(AsyncWebServerRequest * request,  AsyncWebServerResponse * response) {
    int headers = request->headers();
    int i;
    for (i = 0; i < headers; i++) {
        AsyncWebHeader * h = request->getHeader(i);
        Serial.printf("HEADER[%s]: %s\n", h->name().c_str(),
        h->value().c_str());
        if (h->name() == "Access-Control-Request-Method") {
            Serial.printf("Allowing method [%s]", h->value().c_str());
            response->addHeader("Access-Control-Allow-Method", h->value());
        }
        if (h->name() == "Access-Control-Request-Headers") {
            Serial.printf("Allowing header [%s]", h->value().c_str());
            response->addHeader("Access-Control-Allow-Headers", h->value());
        }
    }
    response->addHeader("Access-Control-Allow-Origin", "*");
}
#endif


void setup() {
    pinMode(UD_RELAY_PIN, OUTPUT);
    digitalWrite(UD_RELAY_PIN, UD_RELAY_OFF);


    DS18B20.begin();

    lasttime = millis();

    Serial.begin(115200);
    Serial.setDebugOutput(true);
    WiFi.hostname(UD_HOSTNAME);
    WiFi.mode(WIFI_STA);
    // WiFi.softAP(hostName);
    WiFi.begin(UD_WIFI_SSID, UD_WIFI_PASS);
    if (WiFi.waitForConnectResult() != WL_CONNECTED) {
        Serial.printf("STA: Failed!\n");
        WiFi.disconnect(false);
        delay(1000);
        WiFi.begin(UD_WIFI_SSID, UD_WIFI_PASS);
    }

    //Send OTA events to the browser
    ArduinoOTA.onStart([]() {
        events.send("Update Start", "ota");
    });
    ArduinoOTA.onEnd([]() {
        events.send("Update End", "ota");
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        char p[32];
        sprintf(p, "Progress: %u%%\n", (progress/(total/100)));
        events.send(p, "ota");
    });
    ArduinoOTA.onError(
        [](ota_error_t error) {
            if(error == OTA_AUTH_ERROR) events.send("Auth Failed", "ota");
            else if(error == OTA_BEGIN_ERROR) events.send("Begin Failed", "ota");
            else if(error == OTA_CONNECT_ERROR) events.send("Connect Failed", "ota");
            else if(error == OTA_RECEIVE_ERROR) events.send("Recieve Failed", "ota");
            else if(error == OTA_END_ERROR) events.send("End Failed", "ota");
        }
    );
    ArduinoOTA.setHostname(UD_HOSTNAME);
    ArduinoOTA.begin();

    MDNS.addService("http", "tcp", 80);

    events.onConnect(
        [](AsyncEventSourceClient *client) {
            client->send("hello!",NULL,millis(),1000);
        }
    );
    server.addHandler(&events);


    server.on("/heap", HTTP_GET,[](AsyncWebServerRequest *request) {
        request->send(200, "text/plain", String(ESP.getFreeHeap()));
    });

    server.on("/state/on", HTTP_GET, [](AsyncWebServerRequest * request) {
        lasttime = millis();
        turnRelayOn();
        request->send(200, "application/json", formatedJSONResponse());
    });
    server.on("/state/off", HTTP_GET, [](AsyncWebServerRequest *request) {
        lasttime = millis();
        turnRelayOff();
        request->send(200, "application/json", formatedJSONResponse());
    });

    server.on("/state", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "application/json", formatedJSONResponse());
    });

    server.on("/state", HTTP_POST,
        [](AsyncWebServerRequest * request) {
            int params = request->params();
            for (int i = 0; i < params; i++) {
                AsyncWebParameter * p = request->getParam(i);
                if (p->isFile()) {       // p->isPost() is also true
                    Serial.printf("FILE[%s]: %s, size: %u\n", p->name().c_str(),
                    p->value().c_str(), p->size());
                } else if (p->isPost()) {
                    Serial.printf("POST[%s]: %s\n", p->name().c_str(),
                    p->value().c_str());
                } else {
                    Serial.printf("GET[%s]: %s\n", p->name().c_str(),
                    p->value().c_str());
                }
            }
            int headers = request->headers();
            int i;
            for (i = 0; i < headers; i++) {
                AsyncWebHeader * h = request->getHeader(i);
                Serial.printf("HEADER[%s]: %s\n", h->name().c_str(), h->value().c_str());
            }

            Serial.printf("Starting Response\n");

            AsyncWebServerResponse * response =
            request->beginResponse(200, "application/json", formatedJSONResponse());
            #ifdef DEBUG_CORS
            handleCORS(request, response);
            #endif
            request->send(response);

            Serial.printf("Sent Response\n");
        },
        [](AsyncWebServerRequest * request, String filename, size_t index, uint8_t * data, size_t len, bool final) {
        },
        [](AsyncWebServerRequest * request, uint8_t * data, size_t len, size_t index, size_t total) {
        Serial.printf("Processing Body\n");
        if (!index) {
            Serial.printf("BodyStart: %u B\n", total);
        }
        for (size_t i = 0; i < len; i++) {
            Serial.write(data[i]);
        }
        if (index + len == total) {
            Serial.printf("BodyEnd: %u B\n", total);
        }
        DynamicJsonBuffer jsonBuffer;
        JsonObject & root = jsonBuffer.parseObject(data);

        // Test if parsing succeeds.
        if (!root.success()) {
            Serial.println("parseObject() failed");
            return;
        }
        if (root.containsKey("status")) {
            int status = root["status"];
            lasttime = millis();
            if (status==1) turnRelayOn();
            else turnRelayOff();
        }
    });

    server.onNotFound(
    [](AsyncWebServerRequest *request) {
        request->send(404);
    });

    server.begin();
}




void loop() {
    // server related events are handled directly by events handler ...

    // Over the Air firmware
    ArduinoOTA.handle();

    // Security check
    unsigned long dlm = millis();
    if ((relaystate == 1) && (dlm - lasttime > UD_SECURITY_MS)) {
        digitalWrite(UD_RELAY_PIN, HIGH);
        relaystate = 0;
        lasttime = dlm;
    }

}
