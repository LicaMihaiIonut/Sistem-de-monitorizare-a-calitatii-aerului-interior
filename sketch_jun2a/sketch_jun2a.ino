#include <WiFi.h>
#include <WebServer.h>
#include <EEPROM.h>
#include "DHT.h"

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "Adafruit_CCS811.h"

#include <Firebase_ESP_Client.h>

/* WIFI */
#define WIFI_SSID "RedmiNote14Pro5G"
#define WIFI_PASSWORD "i782af2mwjufggq"

/* FIREBASE */
#define API_KEY "AIzaSyDBtlip8qmHek6KQ0HIMJVP248k6_7YgL8"
#define DATABASE_URL "https://monitorizare-calitate-ae-bfdcf-default-rtdb.europe-west1.firebasedatabase.app/"

#define USER_EMAIL "licamihai1@gmail.com"
#define USER_PASSWORD "genius72"

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

//buzzer
#define BUZZER_PIN  7

//float maxHumidity = 65;  // praguri
//int maxCO2 = 1500;
// variabile stare
bool alertTriggered = false;
bool beepActive = false;
int beepCount = 0;
unsigned long previousMillis = 0;
const int beepInterval = 300; // ms (durata ON/OFF)

/* SERVER */
WebServer server(80);

/* DHT */
#define DHTPIN 6
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

/* OLED */
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
#define OLED_ADDR 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
bool OledState = true;
String ledStatus = "OFF";       // pentru afișare pe OLED
String ventStatus = "OFF";      // pentru afișare pe OLED


/* CCS811 */
Adafruit_CCS811 ccs;

/* BUTTON */
const int buttonPin = 5;
bool systemState = false;
bool lastButton = HIGH;


/* VALORI */
float temperature = 0;
float humidity = 0;
int eco2 = 0;
int tvoc = 0;

// state machine
enum AirState {
  IDLE,
  ABCDE,
  OCCUPIED,
  HIGH_VOC_EVENT,
  HUMID_EVENT,
  VENTILATING
};

AirState airState = IDLE;


String airStateToString(AirState state) {
  switch(state) {
    case IDLE: return "IDLE";
    case OCCUPIED: return "OCCUPIED";
    case HIGH_VOC_EVENT: return "HIGH_VOC_EVENT";
    case HUMID_EVENT: return "HUMID_EVENT";
    case VENTILATING: return "VENTILATING";
    default: return "UNKNOWN";
  }
}

// rate
float prevCO2 = 0, prevTVOC = 0, prevHum = 0;
float dCO2 = 0, dTVOC = 0, dHUM = 0;

// model
float predictedCO2 = 0;
float CO2_out = 420;
float G_act = 0;
float K_vent = 0.02;

// predictii
int predCO2_10 = 0;
int predCO2_20 = 0;
int predCO2_30 = 0;

// calitate aer
String airQuality = "OK";

unsigned long lastRateCalc = 0;
unsigned long lastDisplay = 0;


/* ====================== */

/* VALORI DIN FIREBASE */
int Nivel_max_CO2 = 3000; //valori initiale care se vor actualiza din Firebase
int Nivel_max_TVOC = 0;
int Nivel_max_Umid = 90; //valori initiale care se vor actualiza din Firebase
String Pornit_Vent = "";
String Pornit_LED = ""; //Adaugat pe 27. 05. 2026

unsigned long lastRead = 0;
unsigned long lastFirebase = 0;
unsigned long lastFirebaseRead = 0;
int count_send_off = 0;


volatile bool buttonPressed = false;
unsigned long lastInterruptTime = 0;

// functie btn interupt
void IRAM_ATTR handleButtonInterrupt() {
  unsigned long currentTime = millis();

  // debounce software (200 ms)
  if (currentTime - lastInterruptTime > 200) {
    buttonPressed = true;
    lastInterruptTime = currentTime;
  }
}
//LED 
const int ledPin = 40;  // LED indicator CO2
bool ledState = false;  // pentru intermitent

// PWM ventilator
const int outVent = 39;
bool outVentState = false;
const int pwmChannel = 0;
const int pwmFreq = 5000;
const int pwmResolution = 8; // 0-255




/* FUNCTIE PREDICTIE */
int simulateCO2(float currentCO2, int minutes){
  float simCO2 = currentCO2;
  for(int i = 0; i < minutes; i++){
    simCO2 = simCO2 + G_act - K_vent * (simCO2 - CO2_out);
  }
  return simCO2;
}

/* WEB PAGE */
String webPage(){

  String page="<!DOCTYPE html><html><head><meta charset='UTF-8'>";

  page+="<script>";
  page+="setInterval(()=>{fetch('/data').then(r=>r.json()).then(d=>{";
  page+="document.getElementById('sys').innerText=d.system;";
  page+="document.getElementById('temp').innerText=d.temp;";
  page+="document.getElementById('hum').innerText=d.hum;";
  page+="document.getElementById('co2').innerText=d.co2;";
  page+="document.getElementById('tvoc').innerText=d.tvoc;";
  page+="});},2000);";
  page+="</script>";

  page+="</head><body>";

  page+="<h1>Monitorizare aer</h1>";

  page+="Sistem: <b id='sys'>-</b><br>";
  page+="Temperatura: <b id='temp'>-</b> C<br>";
  page+="Umiditate: <b id='hum'>-</b> %<br>";
  page+="CO2: <b id='co2'>-</b> ppm<br>";
  page+="TVOC: <b id='tvoc'>-</b> ppb<br>";
  page+="</body></html>";

  return page;
}

void handleRoot(){
server.send(200,"text/html",webPage());
}

void handleData(){

  String json="{";

  json+="\"system\":\""+String(systemState?"PORNIT":"OPRIT")+"\",";
  json+="\"temp\":"+String(temperature)+",";
  json+="\"hum\":"+String(humidity)+",";
  json+="\"co2\":"+String(eco2)+",";
  json+="\"tvoc\":"+String(tvoc)+",";
  json+="\"pred10\":"+String(predCO2_10)+","; //Adaugat pe 02. 06. 2026
  json+="\"pred20\":"+String(predCO2_20)+","; //Adaugat pe 02. 06. 2026
  json+="\"pred30\":"+String(predCO2_30)+",";
  json+="\"aq\":\""+airQuality+"\"";

  json+="}";

  server.send(200,"application/json",json);
}

void updateDisplay() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);

  display.print("CO2: ");
  display.println(eco2);

  display.print("TVOC: ");
  display.println(tvoc);

  display.print("P30: ");
  display.println(predCO2_30);

  display.print("AQ: ");
  display.println(airQuality);

  display.setCursor(67, 0);
  display.print("T: ");
  display.println(temperature);

  display.setCursor(67, 8);
  display.print("Hum: ");
  display.println(humidity);

  display.setCursor(67, 16);
  display.print("LED: ");
  display.println(ledStatus);

  display.setCursor(67, 24);
  display.print("Vent: ");
  display.println(ventStatus);

  display.display();
}

//buzzer
void handleBuzzer(float humidity, int co2) {
  unsigned long currentMillis = millis();

  // verificare depășire prag
  bool conditionExceeded = (humidity > Nivel_max_Umid || co2 > Nivel_max_CO2);

  // declanșare DOAR o dată
  if (conditionExceeded && !alertTriggered) {
    beepActive = true;
    alertTriggered = true;
    beepCount = 0;
    previousMillis = currentMillis;
  }

  // reset când revine la normal
  if (!conditionExceeded) {
    alertTriggered = false;
  }

  // logica beep (3 bipuri)
  if (beepActive) {
    if (currentMillis - previousMillis >= beepInterval) {
      previousMillis = currentMillis;

      static bool buzzerState = false;
      buzzerState = !buzzerState;

      digitalWrite(BUZZER_PIN, buzzerState);

      if (!buzzerState) { // numărăm doar ciclurile complete
        beepCount++;
      }

      if (beepCount >= 3) {
        beepActive = false;
        digitalWrite(BUZZER_PIN, LOW);
      }
    }
  }
}

//PWM VENT
void controlFan(float value, float minVal, float maxVal) {
  int pwm;

  if (value <= minVal) {
    pwm = 0; // oprit
  } 
  else if (value >= maxVal) {
    pwm = 255; // maxim
  } 
  else {
    pwm = map(value, minVal, maxVal, 150, 255);
  }

  ledcWrite(outVent, pwm);
}

/* SETUP */
void setup(){

  Serial.begin(115200);
  attachInterrupt(digitalPinToInterrupt(buttonPin), handleButtonInterrupt, FALLING);
  Wire.begin(8,9);

  pinMode(buttonPin, INPUT_PULLUP);
  pinMode(outVent, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);


  pinMode(ledPin, OUTPUT); //LED NOTIFICARE CO2

  ledcAttach(outVent, pwmFreq, pwmResolution);

  dht.begin();

  display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  display.clearDisplay();
  display.display();

  ccs.begin();

  WiFi.begin(WIFI_SSID,WIFI_PASSWORD);

  while(WiFi.status()!=WL_CONNECTED){
  delay(500);

  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);

  display.print("CONNECTING TO WIFI...");
  display.display();

  Serial.print(".");
  }

  Serial.println();
  Serial.println("WiFi conectat");
  Serial.println(WiFi.localIP());

  display.clearDisplay();
  display.display();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print("CONNECTED TO:");
  display.println();
  display.println(WiFi.localIP());
  display.display();


  display.print("CO2: ");
  display.println(eco2);
  delay(5000);

  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;

  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  server.on("/",handleRoot);
  server.on("/data",handleData);

  server.begin();
}

/* LOOP */

void loop(){

  server.handleClient();
  handleBuzzer(humidity, eco2);
  
  /* BUTTON */
  if(buttonPressed){
    buttonPressed = false;

    systemState = !systemState;
    OledState = !OledState;

    if(!systemState){
      display.clearDisplay();
      display.display();
      count_send_off=0;

      ledcWrite(outVent, 0);
      digitalWrite(ledPin, LOW);
      outVentState = false;
	  ledState = false;//Adaugat pe 27. 05. 2026
     if (count_send_off == 0){
        Firebase.RTDB.setFloat(&fbdo,"/Sistem/temperatura",temperature);
        Firebase.RTDB.setFloat(&fbdo,"/Sistem/umiditate",humidity);
        Firebase.RTDB.setFloat(&fbdo,"/Sistem/eco2",eco2);
        Firebase.RTDB.setFloat(&fbdo,"/Sistem/tvoc",tvoc);
        Firebase.RTDB.setBool(&fbdo,"/Sistem/ventilator",outVentState);
		Firebase.RTDB.setBool(&fbdo,"/Sistem/led",ledState);//Adaugat pe 27. 05. 2026
        Firebase.RTDB.setBool(&fbdo,"/Sistem/sistem",systemState);
        Firebase.RTDB.setBool(&fbdo,"/Sistem/oled",OledState);
        count_send_off++;
      }
      
    }
  }

  /* SISTEM PORNIT */
  if(systemState && millis()-lastRead>2000){

    lastRead = millis();
    float t = dht.readTemperature();
    float h = dht.readHumidity();
    if(!isnan(t)&&!isnan(h)){
      temperature=t;
      humidity=h;
    }


    if(ccs.available()){
      if(!ccs.readData()){
        eco2 = ccs.geteCO2();
        tvoc = ccs.getTVOC();
      }
    }
    /* RATE */
    if(millis() - lastRateCalc > 1000){
      lastRateCalc = millis();

      dCO2 = eco2 - prevCO2;
      dTVOC = tvoc - prevTVOC;
      dHUM = humidity - prevHum;

      prevCO2 = eco2;
      prevTVOC = tvoc;
      prevHum = humidity;
    }

    /* STATE */
    if(dCO2 > 10) airState = OCCUPIED;
    else if(dTVOC > 20) airState = HIGH_VOC_EVENT;
    else if(dHUM > 3) airState = HUMID_EVENT;
    else if(outVentState) airState = VENTILATING;
    else airState = IDLE;

    /* MODEL */
    switch(airState){
      case OCCUPIED: G_act = 25; break;
      case HIGH_VOC_EVENT: G_act = 5; break;
      default: G_act = 0; break;
    }

    K_vent = outVentState ? 0.05 : 0.01;
    predictedCO2 = eco2 + G_act - K_vent * (eco2 - CO2_out);

    /* PREDICTII */
    predCO2_10 = simulateCO2(eco2, 10);
    predCO2_20 = simulateCO2(eco2, 20);
    predCO2_30 = simulateCO2(eco2, 30);

    /* CALITATE AER */
    if(eco2 > 1200) airQuality = "PROST";
    else if(eco2 > 800 ) airQuality = "MEDIU";
    else airQuality = "BUN";
          


    /* VENT */
    if(systemState && Nivel_max_CO2 > 0){       
      if(eco2 >= Nivel_max_CO2  || humidity > Nivel_max_Umid){

        
         //ledcWrite(outVent, 255);   // am pus eu

        //const int ledPin = 40; 
        digitalWrite(ledPin, HIGH);// led Avertizare D40
        controlFan(eco2,Nivel_max_CO2,3000);     
        digitalWrite(outVent, HIGH);  //pornim vent D39
        outVentState = true; 
        ledState = true;
        ledStatus = "ON";       // pentru afișare pe OLED
        ventStatus = "ON"; 

      } 
      else {

       
        //ledcWrite(outVent, 0);   //   am pus eu

        outVentState = false;
        ledState = false;
        digitalWrite(outVent, LOW);
        digitalWrite(ledPin, LOW);
        ledStatus = "OFF";       // pentru afișare pe OLED
        ventStatus = "OFF"; 
      }
     
    }
      /* OLED */ 
    updateDisplay();

  
    /* FIREBASE */
    //trimitere date
    if((millis() - lastFirebase) > 10000){

      lastFirebase = millis();

      Firebase.RTDB.setFloat(&fbdo,"/Sistem/temperatura",temperature);
      Firebase.RTDB.setFloat(&fbdo,"/Sistem/umiditate",humidity);
      Firebase.RTDB.setFloat(&fbdo,"/Sistem/eco2",eco2);
      Firebase.RTDB.setFloat(&fbdo,"/Sistem/tvoc",tvoc);
      Firebase.RTDB.setBool(&fbdo,"/Sistem/ventilator",outVentState);
	  Firebase.RTDB.setBool(&fbdo,"/Sistem/led",ledState);//adaugat pe 27. 05. 2026
      Firebase.RTDB.setBool(&fbdo,"/Sistem/sistem",systemState);
      Firebase.RTDB.setBool(&fbdo,"/Sistem/oled",OledState);

      Firebase.RTDB.setFloat(&fbdo,"/Sistem/pred_co2",predictedCO2);
      Firebase.RTDB.setFloat(&fbdo,"/Sistem/pred_co2_10",predCO2_10);
      Firebase.RTDB.setFloat(&fbdo,"/Sistem/pred_co2_20",predCO2_20);
      Firebase.RTDB.setFloat(&fbdo,"/Sistem/pred_co2_30",predCO2_30);
      Firebase.RTDB.setString(&fbdo,"/Sistem/calitate_aer",airQuality);

      Firebase.RTDB.setString(&fbdo,"/Sistem/stare_aer",airStateToString(airState));  ////- am pus eu

    }

    /* CITIRE DIN FIREBASE */
    if(millis() - lastFirebaseRead > 20000 && systemState){
      lastFirebaseRead = millis();

      if(Firebase.RTDB.getInt(&fbdo,"/Setare_Control/Nivel_max_CO2")){
      Nivel_max_CO2 = fbdo.intData();
      }

      if(Firebase.RTDB.getInt(&fbdo,"/Setare_Control/Nivel_max_TVOC")){
      Nivel_max_TVOC = fbdo.intData();
      }

      if(Firebase.RTDB.getInt(&fbdo,"/Setare_Control/Nivel_max_Umid")){
      Nivel_max_Umid = fbdo.intData();
      }

      if(Firebase.RTDB.getString(&fbdo,"/Setare_Control/Pornit_Vent")){
      // Pornit_Vent = fbdo.stringData();
      }
	  
	  if(Firebase.RTDB.getString(&fbdo,"/Setare_Control/Pornit_LED")){
      // Pornit_LED = fbdo.stringData();
      }//Adaugat pe 27. 05. 2026

      /* AFISARE SERIAL */
      /*
      Serial.println("---- Date citite din firebase ----");
        Serial.print("Nivel_max_CO2: ");
        Serial.println(Nivel_max_CO2);

        Serial.print("Nivel_max_TVOC: ");
        Serial.println(Nivel_max_TVOC);

        Serial.print("Nivel_max_Umid: ");
        Serial.println(Nivel_max_Umid);

        Serial.print("Pornit_Vent: ");
        Serial.println(Pornit_Vent);
		
		//Adaugat pe 27. 05. 2026-- De aici
		Serial.print("Pornit_LED: ");
        Serial.println(Pornit_LED);
		//Pana aici

        Serial.println("-------------------------");

      */
      
    }
   controlFan(humidity, Nivel_max_Umid, 100);        

   

  }
}