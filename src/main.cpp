#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>

#define ST77XX_DARKGREY 0x7BEF
// Pines
#define I2C_SDA 21
#define I2C_SCL 22
#define ONE_WIRE_BUS 4
#define dataLED 26
#define fan1PWM 27
#define fan2PWM 16
#define temp2   17
#define rele    14
#define servo   13
#define humid   19
//PANTALLA Y ENCODER
#define TFT_CS    15
#define TFT_RST    2
#define TFT_DC    12
#define ENC_CLK   25
#define ENC_DT    33
#define ENC_SW    32

//INTERRUPCIONES
volatile int8_t pasosEncoder = 0 ; // pasos para procesar en la funcion
volatile uint8_t encPrev = 0;  // como esta en encoder una posicion antes
volatile int8_t pasosAcumulados = 0; // saber si se llego a 4 mini pasos para mandar 1 a pasos encoder

//crear tipo de variable unica (estado del sistema)
enum SystemState {
  MENU_SELECT,
  MODIFY_PROGRAM,
  RUNNING_AUTO,
  TEST_ACTUATORS
};
//ESTADO DEL SYSTEMA 
SystemState currentState = MENU_SELECT; 

enum SystemError {
    ERR_OK,
    ERR_TEMP,
    ERR_HEAT,
    ERR_VENTILATION,
    ERR_COMMUNICATION
};

//PROTOTIPOS
void actualizarMenu();
void leerEncoder();
SystemError verification();
void botonPresionado (); 
void programaGallina ();
void programaReptiles ();
void programaMamiferos ();
void programaManual ();
void programaFermentacion ();
void setTemperature (float temp);
void IRAM_ATTR isrEncoder(); 

//CONFIGURACION MENU
const int TOTAL_MENU_ITEMS = 5; 
const char* menuItems [TOTAL_MENU_ITEMS] = {
  "1. Huevos (Gallina)",
  "2. Reptiles",
  "3. Mamiferos",
  "4. Fermentacion",
  "5. Modo Manual"
}; 

const int OPCIONES_PROGRAMA = 5;
const char* opcionesProgramas [OPCIONES_PROGRAMA] = {
  "Temperatura: ",
  "Humedad: ",
  "Tiempo: ",
  "Motor: ",
  ">> CONFIRMAR <<"
};

//CONTADOR PARA DESPLAZAR EL SCROLL 
int selectedItem = 0; 

// Guarda el último estado conocido del pin CLK
int lastClkState;

int   humidGoal; //humedad fijada por usuario
int   currentHumid; //humedad medida por sensores
float tempGoal; //temperatura fijada por el usuario
float currentTemp; //temperatura actual (medida por sensores)
int   diasGoal; 
bool motorON = false; 

bool  editMode = false; //para saber si vamos a modificar algun valor o nos desplazamos en el menu 



// Control de rebote (debounce) para el botón del switch
int lastBtnState = HIGH;
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 50; // 50 milisegundos de filtro


Adafruit_BME280 bme;
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature ds18b20(&oneWire);


//CONSTRUCTOR PANTALLA
Adafruit_ST7735 pantalla = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);

void actualizarMenu (){
  if (currentState == MENU_SELECT){
    for (int i=0; i< TOTAL_MENU_ITEMS ; i++){
      int posY= 28 + (i * 18); //CALCULAR POSICION DONDE SE VE BIEN EL RENGLON

    //si recorre el arreglo y cae en la variable que esta apuntando el ususario
      if (i == selectedItem){
      //resaltar opcion seleccionada
      pantalla.fillRect(6, posY-2, 148, 16, ST7735_BLUE);
      pantalla.setTextColor(ST7735_WHITE);
      pantalla.setTextSize(1);
      pantalla.setCursor(10, posY+2);
      pantalla.print("> ");
      pantalla.print(menuItems[i]); 
    } 
    //Si no es la opcion selecionada por el ususario
      else { 
      pantalla.fillRect(6, posY-2, 148, 16, ST77XX_BLACK);
      pantalla.setTextColor(ST77XX_DARKGREY);
      pantalla.setTextSize(1);
      pantalla.setCursor(10, posY +2);
      pantalla.print("  ");
      pantalla.print(menuItems[i]);
    }

  }
} else if (currentState == MODIFY_PROGRAM) {
    for (int i = 0; i < OPCIONES_PROGRAMA; i++) {
      int posY = 26 + (i * 17);

      if (i == selectedItem) {
        // Seleccionar color según el modo
        uint16_t colorFondo = editMode ? ST7735_ORANGE : ST7735_BLUE;

        pantalla.fillRect(6, posY - 2, 148, 15, colorFondo);
        pantalla.setTextColor(ST7735_WHITE);
        pantalla.setTextSize(1);
        pantalla.setCursor(10, posY + 2);
        pantalla.print(editMode ? "* " : "> ");
      } else {
        pantalla.fillRect(6, posY - 2, 148, 15, ST77XX_BLACK);
        pantalla.setTextColor(ST77XX_DARKGREY);
        pantalla.setTextSize(1);
        pantalla.setCursor(10, posY + 2);
        pantalla.print("  ");
      }

      // Impresión de texto (una sola vez)
      pantalla.print(opcionesProgramas[i]);

      if (i == 0) {
        pantalla.print(tempGoal, 1);
        pantalla.print(" C");
      } else if (i == 1) {
        pantalla.print(humidGoal);
        pantalla.print(" %");
      } else if (i == 2) {
        pantalla.print(diasGoal);
        pantalla.print(" dias");
      } else if (i == 3) {
        pantalla.print(motorON ? "ON " : "OFF");
      }
    }
  } 
}

void setup() {
  pinMode(ENC_CLK, INPUT_PULLUP);
  pinMode(ENC_DT, INPUT_PULLUP);
  pinMode(ENC_SW, INPUT_PULLUP); 

  // Leemos el estado inicial de reposo de CLK (casi siempre HIGH)
  lastClkState = digitalRead(ENC_CLK);

  // Leer estado inicial antes de activar interrupciones
  encPrev = (digitalRead(ENC_CLK) << 1) | digitalRead(ENC_DT);

  //activar interrupciones para ciertos pines 
  attachInterrupt(digitalPinToInterrupt(ENC_CLK), isrEncoder, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENC_DT),isrEncoder, CHANGE); 

  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== SISTEMA DE MONITOREO DE TEMPERATURA LISTO ===");

  //empezar prtocolo I2C
  Wire.begin(I2C_SDA, I2C_SCL);

  pantalla.initR(INITR_BLACKTAB); 
  pantalla.setRotation(1); 
  pantalla.fillScreen(ST7735_BLACK); 
  actualizarMenu(); 

  //CONFIGURAR PWM
  ledcSetup(0,2500,8); //PWM ventilador para resistencia (canal 0, freq 25k, 8 bit resolution)
  ledcAttachPin(fan1PWM,0);//unir el canal de pwm arriba con el pin de pwm de ventilador
  ledcWrite(0,0); 
  ledcSetup(1,25000,8); //pwm ventilador para sacar el aire
  ledcAttachPin(fan2PWM,1);
  ledcWrite(1,0);

  if (!bme.begin(0x76, &Wire)) {
    Serial.println("[ERROR] BME280 no detectado.");
  } else {
    Serial.println("[OK] BME280 inicializado en 0x76.");
  }

  ds18b20.begin();
  Serial.printf("[OK] Sensores DS18B20 encontrados: %d\n", ds18b20.getDeviceCount());
  Serial.println("=================================================\n");
}

unsigned long lastSensorTime = 0;

void loop() {
  
  leerEncoder();
  botonPresionado();
  
}
void leerEncoder() {
  if (pasosEncoder == 0){
    return; 
  } else {

    int pasos = pasosEncoder;
    pasosEncoder = 0; //reiniciar los pasos del encoder para el proximo bucle 

    bool giroHorario (pasos>0); //es verdad cuando es giroHorario 

      // 1. NAVEGACIÓN EN MENÚ PRINCIPAL
      if (currentState == MENU_SELECT) {
        if (giroHorario) {
          if (selectedItem < TOTAL_MENU_ITEMS - 1) selectedItem++;
        } else {
          if (selectedItem > 0) selectedItem--;
        }
      }
      // 2. AJUSTE DE PARÁMETROS
      else if (currentState == MODIFY_PROGRAM) {
        if (!editMode) {
          // Desplazamiento por los renglones
          if (giroHorario) {
            if (selectedItem < OPCIONES_PROGRAMA - 1) selectedItem++;
          } else {
            if (selectedItem > 0) selectedItem--;
          }
        } else {
          // Modificación de la variable elegida
          switch (selectedItem) {
            case 0: // Temp Goal
              if (giroHorario) {
                if (tempGoal < 42.0) tempGoal += 0.1;
              } else {
                if (tempGoal > 20.0) tempGoal -= 0.1;
              }
              break;

            case 1: // Humid Goal
              if (giroHorario) {
                if (humidGoal < 95) humidGoal += 1;
              } else {
                if (humidGoal > 15) humidGoal -= 1;
              }
              break;

            case 2: // Días
              if (giroHorario) {
                if (diasGoal < 60) diasGoal += 1;
              } else {
                if (diasGoal > 1) diasGoal -= 1;
              }
              break;

            case 3: // Motor volteador
              motorON = giroHorario;
              break;

            default:
              break;
          }
        }
      }

      actualizarMenu();
  }
}

//FUNCION PARA VERIFICAR SI TODO ANDA BIEN 
SystemError verification () {
  
  return ERR_OK; 
}

void botonPresionado (){
  // Lectura del switch (botón)
  int reading = digitalRead(ENC_SW);

  // Si el pin cambió respecto a la última lectura, reseteamos el temporizador
  if (reading != lastBtnState) {
    lastDebounceTime = millis();
  }

  // Si la lectura se mantuvo estable más de 50 ms, es un clic real
  if ((millis() - lastDebounceTime) > debounceDelay) {
    static int confirmedBtnState = HIGH;

    if (reading != confirmedBtnState) {
      confirmedBtnState = reading;

      // El botón se presiona cuando cae a LOW
      if (confirmedBtnState == LOW) {

        if ( currentState == MENU_SELECT){//SI SE ENCUENTRA EN LA OPCION DEL MENU
          if ( selectedItem == 0 ) { //SELECCION HUEVOS GALLINAS
            programaGallina(); 
          }
          if (selectedItem == 1){
            programaReptiles(); 
          }
          if (selectedItem == 2 ){
            programaMamiferos(); 
          }
          if (selectedItem == 3){
            programaFermentacion(); 
          }
          if (selectedItem == 4){
            programaManual(); 
          } else {
              Serial.print("No existe este programa\n"); 
          
        }
      }
      if (currentState == MODIFY_PROGRAM){
        if (selectedItem == 4){
            currentState = RUNNING_AUTO; 
            editMode = false; 
            pantalla.fillScreen(ST7735_BLACK);
            //completar de dibujar la pantalla de monitoreo en programa auto 
        }
        else if (selectedItem == 2){
            // poner la configurarion para hacer dias o horas 
        } else {
          editMode = !editMode; 
          actualizarMenu ();
        }
      } 
    }
    }
  }
  lastBtnState = reading;
}

void programaMamiferos (){
  tempGoal = 34.0;
  humidGoal = 50;
  diasGoal = 15;
  motorON = false;
  selectedItem = 0;
  editMode = false;
  currentState = MODIFY_PROGRAM;

  pantalla.fillScreen(ST7735_BLACK);
  actualizarMenu();
}

void programaGallina (){
  tempGoal  = 37.7;
  humidGoal = 55;
  diasGoal  = 21;
  motorON   = true; 
  editMode  = false;
  currentState = MODIFY_PROGRAM;
  selectedItem = 0;
  pantalla.fillScreen(ST7735_BLACK);
  actualizarMenu();

}

void programaReptiles (){
  tempGoal  = 29.5;
  humidGoal = 80;
  diasGoal  = 60;
  motorON   = false; 
  editMode  = false;
  currentState = MODIFY_PROGRAM;
  selectedItem = 0;
  pantalla.fillScreen(ST7735_BLACK);
  actualizarMenu();
}

void programaFermentacion (){
  tempGoal  = 28;
  humidGoal = 75;
  diasGoal  = 1;
  motorON   = false; 
  editMode  = false;
  currentState = MODIFY_PROGRAM;
  selectedItem = 0;
  pantalla.fillScreen(ST7735_BLACK);
  actualizarMenu();
}

//dejar configurar todo al ususario
void programaManual (){
  tempGoal = 37.5;
  humidGoal = 50;
  diasGoal = 1;
  motorON = false;
  selectedItem = 0;
  editMode = false;
  currentState = MODIFY_PROGRAM;

  pantalla.fillScreen(ST7735_BLACK);
  actualizarMenu();
}

void IRAM_ATTR isrEncoder() {

  uint8_t encCur = (digitalRead(ENC_CLK) << 1) | digitalRead(ENC_DT);

  if (encCur != encPrev) {
    int8_t direccion = 0;

    if ((encPrev == 0b00 && encCur == 0b01) ||
        (encPrev == 0b01 && encCur == 0b11) ||
        (encPrev == 0b11 && encCur == 0b10) ||
        (encPrev == 0b10 && encCur == 0b00)) {
      direccion = 1;
    } else if ((encPrev == 0b00 && encCur == 0b10) ||
               (encPrev == 0b10 && encCur == 0b11) ||
               (encPrev == 0b11 && encCur == 0b01) ||
               (encPrev == 0b01 && encCur == 0b00)) {
      direccion = -1;
    }

    pasosAcumulados += direccion;
    encPrev = encCur;

    if (pasosAcumulados >= 4) {
      pasosEncoder++; // Registra un paso a la derecha pendiente de procesar
      pasosAcumulados = 0;
    } else if (pasosAcumulados <= -4) {
      pasosEncoder--; // Registra un paso a la izquierda pendiente de procesar
      pasosAcumulados = 0;
    }
  }
}


 /*
//revisar los sensores de tempratura y revisar temperatura constante
void checkTemperature (float temp) { //valor de entrada es la temp meta 
    float toleranceTemp = temp + 1 ;

    if (currentTemp >= toleranceTemp-2) // TEMPERATURA POR DEBAJO 
       calcularPWMTemp(temp);

} 


void calcularPWMTemp (int temperaturaMeta){

  float diferencia =(temperaturaMeta - currentTemp);

  //si el resultado es positivo falta calor
  if (diferencia > 0){
    //poner el ventilador-resistencia al 50% para mover el aire de la resistencia
    ledcWrite(0,128);

    if (diferencia >= 10 ){ //encender al 100% la resistencia
      potenciaCalor = 100.00;
    }
    else if (diferencia >= 5){
      potenciaCalor = 75.00;
    }
    else if (diferencia >= 3){
      potenciaCalor = 50;
    }
    else if (diferencia >= 1){
      potenciaCalor = 25.00;
    } else 
      {potenciaCalor = 10.00; }
  }

  //si el resultado es negativo falta ventilacion (sacar el calor)
  if (diferencia < 0){
    diferencia = abs(diferencia); 
    potenciaCalor = 0.00;
    ledcWrite(0,190); //meter aire a la incubadora ventilador 1 
    if (diferencia >= 10 ){
      ledcWrite(1,255); //sacar todo el aire caliente de la incubadora (100%)
    }
    else if (diferencia >= 5){
      ledcWrite(1,192); //activar ventilador 2 a 75 % 
    }
    else if (diferencia >= 3){
      ledcWrite(1,127); //ventilador al 50
    }
    else if (diferencia >= 1){
      ledcWrite(1,64); //ventilador al 25 
    } else 
      {ledcWrite(1,0);}
  }

} */