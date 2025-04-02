#include <WiFi.h>
#include <AsyncFsWebServer.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Wire.h>

// Definiert die analogen Eingänge
int AINF_0 = A0;
int AINF_1 = A1;
int AINF_2 = A2;

// Variablen für die analogen Werte in mV
int AINF_0_mv = 0;
int AINF_1_mv = 0;
int AINF_2_mv = 0;

// ERFA Variablen
float k3 = 1;
float uoffs = 0;
float height_erfa = 0;

// Variablen für Druckwerte in mBar
float pressure_mbar = 0;
float pressure_offset = 0;
float pressure_by_height = 0;
float P_GAIN_K1 = 228.3105;
float P_OFFSET_P0 = 554.4635;
float P_R27 = 22000;
float P_R26 = 11000;

// Variablen für die Höhe
float height_OPV = 0;
float height_uC = 0;
float H_P0 = 101315;
float H_RHO = 1.29;
float H_G = 9.81;

// WiFi Settings
const char* ssid = "ERFA14";
const char* password = "erfaerfa14";

// Webserver Einstellungen
AsyncWebServer server(80);

void setup() {
  // Initialisierung der seriellen Schnittstelle mit 115'222 baud
  Serial.begin(115200);
  delay(10);

  // Initialisierung der analogen Eingänge
  pinMode(AINF_0, INPUT);
  pinMode(AINF_1, INPUT);
  pinMode(AINF_2, INPUT);

  // Verbindungsaufbau zu WLAN
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);

  Serial.println("scan start");

  // WiFi.scanNetworks will return the number of networks found
  int n = WiFi.scanNetworks();
  Serial.println("scan done");
  if (n == 0) {
    Serial.println("no networks found");
  } else {
    Serial.print(n);
    Serial.println(" networks found");
    for (int i = 0; i < n; ++i) {
      // Print SSID and RSSI for each network found
      Serial.print(i + 1);
      Serial.print(": ");
      Serial.print(WiFi.SSID(i));
      Serial.print(" (");
      Serial.print(WiFi.RSSI(i));
      Serial.print(")");
      Serial.println((WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? " " : "*");
      delay(10);
    }
  }

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

  // Initialisierung der Webserver - Antworten
  // Schickt die Website zum Client
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/html", SendHTML());
  });
  // Schickt eine GET request zu <ESP_IP>/get?paramk3=<inputMessage>
  server.on("/get", HTTP_GET, [] (AsyncWebServerRequest *request) {
    String inputMessage;
    String inputParam;
    String PARAM_INPUT_1 = "paramk3";
    String PARAM_INPUT_2 = "paramuoffs";

    // GET input1 value on <ESP_IP>/get?paramk3=<inputMessage>
    if (request->hasParam(PARAM_INPUT_1)) {
      inputMessage = request->getParam(PARAM_INPUT_1)->value();
      inputParam = PARAM_INPUT_1;

      set_k3(inputMessage.toFloat());
    }
    // GET input2 value on <ESP_IP>/get?paramuoffs=<inputMessage>
    else if (request->hasParam(PARAM_INPUT_2)) {
      inputMessage = request->getParam(PARAM_INPUT_2)->value();
      inputParam = PARAM_INPUT_2;

      set_uoffs(inputMessage.toFloat());
    }
    else {
      inputMessage = "Keine Nachricht geschickt";
      inputParam = "none";
    }
    Serial.println("HTTP GET Request:");
    Serial.println(inputMessage);
    request->send(200, "text/html", "HTTP GET Anfrage wurde an den ESP32 geschickt (" 
                                     + inputParam + ") mit dem Wert: " + inputMessage +
                                     "<br><a href=\"/\">Return to Home Page</a>");
  });    
  
  server.onNotFound(notFound);
  server.begin();

  Serial.println("HTTP server started");

  //MCP.begin();
}

/*
####################### Loop #######################
*/
int counter = 0;
void loop() {
  // Liest die analogen Werte vom Port
  ain_get_values();
  height_calc_erfa();

  // Ausgabe der aktuellen Wert
  //ain_print_values();
  
  // Warten
  delay(50);
}


/*
####################### IOT #######################
*/
void iot_print_values(){
  Serial.println("\nIOT Settings");
  Serial.printf("IP_Adresse:");
  Serial.println(WiFi.localIP());
}

/*
####################### Analoge Eingänge #######################
*/
void ain_get_values() {
  // Liest die analogen Eingänge in millivolts und mittelt sie mit Hilfe eines gleitenden Mittelwert-Filters
  AINF_0_mv = 0.7 * AINF_0_mv + 0.3 * analogReadMilliVolts(AINF_0);
  AINF_1_mv = 0.7 * AINF_1_mv + 0.3 * analogReadMilliVolts(AINF_1);
  AINF_2_mv = 0.7 * AINF_2_mv + 0.3 * analogReadMilliVolts(AINF_2);
}



void ain_print_values() {
  // Gibt die analogen Werte über die serielle Schnittstelle aus
  Serial.println("\nAnaloge Eingänge");
  Serial.printf("AINF_0[mV]:%d,", AINF_0_mv);
  Serial.printf("AINF_1[mV]:%d,", AINF_1_mv);
  Serial.printf("AINF_2[mV]:%d,", AINF_2_mv);
}

/*
####################### Digital Analog #######################
*/
void aout_setVoltage_mv(int voltage_mv) {
  // Gibt die analogen Werte über die serielle Schnittstelle aus
  int dac_value = voltage_mv/(3300.0/4096.0);
  //MCP.setValue(dac_value);
}

/*
####################### Druck #######################
*/
void pressure_calc_pressure() {
  // Berechnen den Druck von der Spannung des Drucksensors
  float US_5V = float(AINF_0_mv) / P_R27 * (P_R26 + P_R27);
  pressure_mbar = (float(US_5V) / 1000) * P_GAIN_K1 + P_OFFSET_P0 + pressure_offset;
}

void pressure_calc_pressure_offset(float actual_height){
  pressure_by_height = (H_P0*exp(-(H_RHO*H_G*actual_height)/H_P0))/100;
  Serial.printf("Druck durch Hoehe: %f", pressure_by_height);
  pressure_offset = pressure_by_height-(pressure_mbar-pressure_offset);
}

void set_k3(float paramk3){
  k3 = paramk3;
}

void set_uoffs(float paramuoffs){
  uoffs = paramuoffs/1000;
}

void pressure_print_values() {
  // Gibt die berechneten Druckwerte aus
  Serial.println("\nDruckwerte");
  Serial.printf("Druck[mBar]:%f,", pressure_mbar);
  Serial.printf("Offset[mBar]:%f\n", pressure_offset);
}

/*
####################### Höhe #######################
*/
void height_by_pressure() {
  height_uC = -(H_P0 * log(pressure_mbar * 100 / H_P0)) / (H_RHO * H_G);
}

void height_by_voltage() {
  height_OPV = AINF_1_mv;
}

void height_calc_erfa() {
  // Berechnen den Druck von der Spannung des Drucksensors mit k3 und Uoffs
  height_erfa = (float(AINF_0_mv)/1000 * k3 + uoffs)*1000;

  Serial.printf("\nAINF_0_mv:%d\n", AINF_0_mv);
  Serial.printf("k3:%f\n", k3);
  Serial.printf("uoffs:%f\n", uoffs);
  Serial.printf("Height:%f\n", height_erfa);
}

void height_print_values() {
  // Gibt die berechneten Höhen aus
  Serial.println("\nHöhe");
  Serial.printf("Hoehe_uC[m]:%f,", height_uC);
  Serial.printf("Hoehe_OPV[m]:%f\n", height_OPV);
}

/*
####################### Webserver #######################
*/
void notFound(AsyncWebServerRequest *request){
  request->send(404, "text/plain", "Not found");
}

// Response 

String SendHTML()
{
  String ptr = "<!DOCTYPE html> <html>\n";
  ptr +="<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, user-scalable=no\">\n";
  ptr +="<title>ERFA Tagung GIBZ ZUG</title>\n";
  ptr +="<style>html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: center;}\n";
  ptr +="body{margin-top: 50px;} h1 {color: #444444;margin: 50px auto 30px;}\n";
  ptr +="p {font-size: 24px;color: #444444;margin-bottom: 10px;}\n";
  ptr +="</style>\n";
  ptr +="</head>\n";
  ptr +="<body>\n";
  ptr +="<div id=\"webpage\">\n";
  ptr +="<h1>Hoehenmessgeraet ERFA Tagung GIBZ</h1>\n";
  
  ptr +="<p>AINF_0_mv: ";
  ptr +=(int)AINF_0_mv;
  ptr +=" mV</p>";

  ptr +="<p>AINF_1_mv: ";
  ptr +=(int)AINF_1_mv;
  ptr +=" mV</p>";

  ptr +="<p>AINF_2_mv: ";
  ptr +=(int)AINF_2_mv;
  ptr +=" mV</p>";

  ptr +="<p>Hoehe ERFA[m]: ";
  ptr +=(int)height_erfa;
  ptr +=" m</p>";

  ptr +="<form action=\"/get\">\n";
  ptr +=  "Parameter k3: <input type=\"number\" name=\"paramk3\" step=\"any\">\n";
  ptr +=  "<input type=\"submit\" value=\"k3 setzen\">\n";
  ptr +="</form><br>\n";
  
  ptr +="<form action=\"/get\">\n";
  ptr +=  "Parameter Uoffs [mV]: <input type=\"number\" name=\"paramuoffs\" step=\"any\">\n";
  ptr +=  "<input type=\"submit\" value=\"Uoffs setzen\">\n";
  ptr +="</form><br>\n";

  ptr +="</div>\n";
  ptr +="</body>\n";
  ptr +="</html>\n";
  return ptr;
}
