#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <Adafruit_NeoPixel.h>
#include <WiFi.h>
#include <Callmebot_ESP32.h>


#define ST77XX_DARKGREY 0x7BEF
// Pines
#define I2C_SDA 21
#define I2C_SCL 22
#define ONE_WIRE_BUS 17 //aqui esta el pin dedicado al TEMP2 
#define dataLED 26
#define fan1PWM 27
#define fan2PWM 16
#define temp2   17   //este era 17 
#define rele    14
#define servo   13
#define humid   5
#define buzzer  4

//PANTALLA 
#define TFT_CS    15
#define TFT_RST    2
#define TFT_DC    12

//ENCODER
#define ENC_CLK   34  //RECABLEAR
#define ENC_DT    35 //recablear
#define ENC_SW    32

//SPI MAX
#define MAX_CLK   25  // Pin liberado del encoder
#define MAX_DIN   33  // Pin liberado del encoder
#define MAX_CS    19  // Se queda en el pin 19

#define MAX7219_REG_NOOP   0x00
#define MAX7219_REG_DIG0   0x01
#define MAX7219_REG_DIG1   0x02
#define MAX7219_REG_DIG2   0x03
#define MAX7219_REG_DIG3   0x04
#define MAX7219_REG_DIG4   0x05
#define MAX7219_REG_DIG5   0x06
#define MAX7219_REG_DIG6   0x07
#define MAX7219_REG_DIG7   0x08
#define MAX7219_REG_DECODE 0x09
#define MAX7219_REG_INTENS 0x0A
#define MAX7219_REG_SCAN   0x0B
#define MAX7219_REG_SHUTDN 0x0C
#define MAX7219_REG_TEST   0x0F

//INTERRUPCIONES
volatile int8_t pasosEncoder = 0 ; // pasos para procesar en la funcion
volatile uint8_t encPrev = 0;  // como esta en encoder una posicion antes
volatile int8_t pasosAcumulados = 0; // saber si se llego a 4 mini pasos para mandar 1 a pasos encoder

//crear tipo de variable unica (estado del sistema)
enum SystemState {
  MENU_SELECT,
  MODIFY_PROGRAM,
  RUNNING_AUTO,
  TEST_ACTUATORS,
  MODIFY_TIME,
  PROGRAM_DONE,
  MODIFY_COLOR,
  PantallaFalla
};
//ESTADO DEL SYSTEMA 
SystemState currentState = MENU_SELECT; 

enum SystemError {
    SYS_ERR_OK,
    SYS_SENSOR_ERR,
    SYS_ERR_HEAT,
    SYS_ERR_HUMEDAD,
    SYS_ERR_COMMUNICATION_BME,
    SYS_ERR_COMMUNICATION_ONEWIRE
};

//PROTOTIPOS
void actualizarMenu();
void leerEncoder();
void botonPresionado (); 
void programaGallina ();
void programaReptiles ();
void programaMamiferos ();
void programaManual ();
void programaFermentacion ();
void setTemperature (float temp);
void IRAM_ATTR isrEncoder(); 
void calcularPWMTemp (float temperaturaMeta);
void manejarCalor (float PWMgenerado);
void calcularPWMHumedad (int humedadMeta);
void manejarHumedad (int PWMHumedad);  
void reiniciarBME280(); 
void mostrarTemperatura(float temp); 
void enviarMAX7219(uint8_t registro, uint8_t valor);
void initMAX7219 ();  
void enviarHumedad(int humedad); 
void aplicarColorLEDs(); 
void manejarErrores (SystemError Error); 
SystemError verificarSistema (); 


//DEFINIR COLORES PARA LEDS: 
const uint32_t COLOR_ROJO     = Adafruit_NeoPixel::Color(255, 0, 0);
const uint32_t COLOR_VERDE    = Adafruit_NeoPixel::Color(0, 255, 0);
const uint32_t COLOR_AZUL     = Adafruit_NeoPixel::Color(0, 0, 255);
const uint32_t COLOR_BLANCO   = Adafruit_NeoPixel::Color(255, 255, 255);
const uint32_t COLOR_AMBAR    = Adafruit_NeoPixel::Color(255, 140, 20); // Luz cálida
const uint32_t COLOR_MORADO   = Adafruit_NeoPixel::Color(180, 0, 255);
const uint32_t COLOR_CYAN     = Adafruit_NeoPixel::Color(0, 255, 255);
const uint32_t COLOR_APAGADO  = Adafruit_NeoPixel::Color(0, 0, 0);

int colorLedsIndex = 0; // Índice de nombresColores[] (0 a 5)


//CONFIGURACIONES MENU
const int TOTAL_MENU_ITEMS = 5; 
const char* menuItems [TOTAL_MENU_ITEMS] = {
  "1. Huevos (Gallina)",
  "2. Reptiles",
  "3. Mamiferos",
  "4. Fermentacion",
  "5. Modo Manual"
}; 

//menu PARA CAMBIAR DE COLOR LED
const int TOTAL_COLORES = 6; 
const char* nombresColores [TOTAL_COLORES] = {
  "Ambar (Calido)",
  "Blanco Puro",
  "Azul Frio",
  "Verde",
  "Rojo",
  "Apagado"
}; 

const int OPCIONES_PROGRAMA = 6;
const char* opcionesProgramas [OPCIONES_PROGRAMA] = {
  "Temperatura: ",
  "Humedad: ",
  "Tiempo: ",
  "Motor: ",
  "Iluminacion",
  ">> CONFIRMAR <<"
};

const int OPCION_CORRIENDO = 2;
const char* opciones_Corriendo [OPCION_CORRIENDO]={
  "¿Cancelar?",
  "Modificar valores"
};

const int OPCIONES_TIEMPO = 4;
const char* opciones_Tiempo [OPCIONES_TIEMPO] = {
  "DIAS: ",
  "HORAS: ",
  "MINUTOS: ",
  ">> VOLVER <<"
};

// Tabla de conversión a formato de color para pantalla ST7735 (RGB565 de 16 bits)
const uint16_t coloresTFT[TOTAL_COLORES] = {
  0xFBE0,          // Ámbar / Luz Cálida
  ST7735_WHITE,    // Blanco
  ST7735_BLUE,     // Azul
  ST77XX_GREEN,    // Verde
  ST7735_RED,      // Rojo
  0x18C3           // Gris oscuro para "Apagado"
};

// Tabla para la librería NeoPixel
const uint32_t tablaColoresNeo[TOTAL_COLORES] = {
  COLOR_AMBAR,
  COLOR_BLANCO,
  COLOR_AZUL,
  COLOR_VERDE,
  COLOR_ROJO,
  COLOR_APAGADO
};

//INTERNET 
const char* redWifi = "floreyes_2.4AP2";
const char* contrasena = "Fl0r35R3y35"; 
String numeroTelefono = "+5219995870390"; 
String ApiKey = "4123361";

//MODIFICAR VALORES DE BRILLO 
const int TOTAL_NIVELES_BRILLO = 10; 
const int nivelesBrillo[TOTAL_NIVELES_BRILLO]={10, 20, 30, 40, 50, 60, 70, 80, 90, 100}; 
int brilloIndex = 3; 
int brilloLedActual = 40; 

//CONTADOR PARA DESPLAZAR EL SCROLL 
int selectedItem = 0; 

// Guarda el último estado conocido del pin CLK
int lastClkState;

int   humidGoal; //humedad fijada por usuario
int   currentHumid; //humedad medida por sensores
int   lastHumid; 
float tempGoal; //temperatura fijada por el usuario
float lastTemp; //POR SI FALLAN TODOS LOS SENSORES PONER EL ULTIMO VALOR DE TEMP
float currentTemp; //temperatura actual (medida por sensores)
int sensores = 0; //para poner la temperatura de donde la esta sacando 
int   diasGoal;
int   horasGoal;
int   minutosGoal; 
uint32_t segRestantes = 0;
// variables para el SSR
float potenciaCalor = 0;
unsigned long inicioVentana = 0;
const unsigned long duracionMaxima = 3000; 
bool motorON = false; 
bool cambiarColorLED = false; 
const unsigned long coolDownRefresco = 50; 
unsigned long ultimoRefresco = 0; 
int temperaturaIgual = 0; 
int falloSensores = 0; 
int vecesRevisada = 0; 
int vecesEncendidasResistencia = 0; 
const char* mensajeFalla = "ERROR DESCONOCIDO";
bool movimientoEncoder = false; 
unsigned long ultimoEncendidoLed = 0; 
bool conexionWifi = false; 

//VARIABLES PARA SONAR ALARMA 
bool alarmaOn = false; //FLAG PARA HACER QUE SEA INTERNMITENTE LA ALARMA 
unsigned long ultimaAlarma = 0;
const unsigned long tiempoAlarma = 5000; 

//VARIABLES PARA ACTIVAR HUMIDIFICADOR
int potenciaHumedad = 0; 
unsigned long empiezaConteo = 0; 
const unsigned long tiempoMaximo = 5000;

bool  editMode = false; //para saber si vamos a modificar algun valor o nos desplazamos en el menu 

// Carrusel de vistas en RUNNING_AUTO
int autoScreenPage = 0;
unsigned long lastAutoScreenSwitch = 0;
const unsigned long AUTO_SCREEN_INTERVAL = 4000; // Alternar cada 4 segundos

// Control de tiempo de incubación
unsigned long startTimeIncubation = 0;
int diasTranscurridos = 0;

//CONTROL TIEMPO DE LECTURA SENSORES
unsigned long ultimaLectura = 0;
const unsigned long intervaloLectura = 3000; //3 segundos 



// Control de rebote (debounce) para el botón del switch
int lastBtnState = HIGH;
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 50; // 50 milisegundos de filtro


Adafruit_BME280 bme;
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature ds18b20(&oneWire);

//CONSTRUCTOR LEDS
Adafruit_NeoPixel tiraLed (64, dataLED , NEO_GRB + NEO_KHZ800); //CUANTOS LED, QUE PIN, PROTOCOOLO, FRCUENCIA


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
      int posY = 18 + (i * 18);

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
        pantalla.printf("%dd %02dh %02dm", diasGoal, horasGoal, minutosGoal);
      } else if (i == 3) {
        pantalla.print(motorON ? "ON " : "OFF");
      }
    }
  } else if (currentState == RUNNING_AUTO){
    //COMO SE VERA LA PANTALLA DE RUNNING AUTO
    pantalla.setTextSize(1); 
    pantalla.setTextColor(ST77XX_GREEN, ST7735_BLACK);
    pantalla.setCursor(6,4);
    pantalla.print("CORRIENDO AUTO");
    pantalla.setTextColor(motorON ? ST7735_YELLOW : ST77XX_DARKGREY, ST7735_BLACK);
    pantalla.setCursor(95,4);
    pantalla.print(motorON ? "Volteo: ON" : "Volteo: OFF");
    pantalla.drawFastHLine(0, 16, 160, ST77XX_DARKGREY);

    //LIMPIAR PANTALLA PARA IR CAMBIANDO 
    pantalla.fillRect (0, 18, 160, 75, ST7735_BLACK);

    //QUE HACE CADA GIRO
    if (autoScreenPage == 0){
      //VISTA TEMPERATURA
      pantalla.setTextSize(1);
      pantalla.setTextColor(ST7735_CYAN, ST7735_BLACK);
      pantalla.setCursor(8, 22);
      pantalla.print("TEMP CONTROL");

      //MOSTAR VALOR ACTUAL 
      pantalla.setTextSize(3);
      pantalla.setTextColor(ST7735_WHITE, ST7735_BLACK);
      pantalla.setCursor(10, 36);
      pantalla.printf("%.1f", currentTemp);
      pantalla.setTextSize(2);
      pantalla.print("C");

      //MOSTRAR VALOR FIJADO POR USUSARIO
      pantalla.setTextSize(1);
      pantalla.setTextColor(ST77XX_DARKGREY, ST7735_BLACK);
      pantalla.setCursor(10, 68);
      pantalla.print("VALOR FIJADO: ");
      pantalla.setTextColor(ST7735_YELLOW, ST7735_BLACK);
      pantalla.printf("%.1f C", tempGoal);
      pantalla.setCursor (10,80);
      pantalla.setTextSize(1);
      switch (sensores)
      {
      case 0:
        pantalla.setTextColor(ST77XX_GREEN, ST7735_BLACK);
        pantalla.print("SRC: PROMEDIO (2 SENS)");
        break;

      case 1:
        pantalla.setTextColor(ST77XX_GREEN, ST7735_BLACK);
        pantalla.print("SRC: SOLO BME280");
        break;

      case 2:
        pantalla.setTextColor(ST77XX_GREEN, ST7735_BLACK);
        pantalla.print("SRC: SOLO DS18B20");
        break;

      case 3:
        pantalla.setTextColor(ST77XX_GREEN, ST7735_BLACK);
        pantalla.print("SRC: FALLA (ULTIMO VAL)");
        break;
      
      default:
        break;
      }
    }
    else if (autoScreenPage == 1){
      //VISTA HUMEDAD
      pantalla.setTextSize(1);
      pantalla.setTextColor(ST7735_CYAN, ST7735_BLACK);
      pantalla.setCursor(8, 22);
      pantalla.print("HUMEDAD CONTROL");

      //MOSTAR VALOR ACTUAL 
      pantalla.setTextSize(3);
      pantalla.setTextColor(ST7735_WHITE, ST7735_BLACK);
      pantalla.setCursor(10, 36);
      pantalla.printf("%d %%", currentHumid);
      pantalla.setTextSize(2);
      pantalla.print("%");

      //MOSTRAR VALOR FIJADO POR USUSARIO
      pantalla.setTextSize(1);
      pantalla.setTextColor(ST77XX_DARKGREY, ST7735_BLACK);
      pantalla.setCursor(10, 68);
      pantalla.print("VALOR FIJADO: ");
      pantalla.setTextColor(ST7735_YELLOW, ST7735_BLACK);
      pantalla.printf("%d %%", humidGoal);
    }
    else if (autoScreenPage == 2){
      //TIEMPO RESTANTE
      pantalla.setTextSize(1);
      pantalla.setTextColor(ST7735_CYAN, ST7735_BLACK);
      pantalla.setCursor(8, 22);
      pantalla.print("TIEMPO RESTANTE");

      uint32_t totalSegundosGoal = ((uint32_t)diasGoal * 86400UL) + ((uint32_t)horasGoal * 3600UL) + ((uint32_t)minutosGoal * 60UL); 

      uint32_t segundosTranscurridos = (millis() - startTimeIncubation) / 1000UL;
      segRestantes = totalSegundosGoal - segundosTranscurridos;
      
      if (segRestantes < 0) segRestantes= 0; 

      // 2. Desglose en días, horas, minutos y segundos
      int rDias    = segRestantes / 86400UL;
      int rHoras   = (segRestantes % 86400UL) / 3600UL;
      int rMinutos = (segRestantes % 3600UL) / 60UL;
      int rSeg     = segRestantes % 60UL;

      // 3. Mostrar tiempo restante grande según la escala
      pantalla.setTextColor(ST7735_WHITE, ST7735_BLACK);


      if (rDias > 0){
        //SI QUEDAN DIAS NO HACE FALTA TANTA PRECISION DIAS+ HORAS
        pantalla.setTextSize(3);
        pantalla.setCursor(10, 36);
        pantalla.printf("%dd", rDias);
        pantalla.setTextSize(2);
        pantalla.printf(" %02dh", rHoras);
      } else {
        pantalla.setTextSize(2);
        pantalla.setCursor (16,40);
        pantalla.printf("%02d:%02d:%02d", rHoras, rMinutos, rSeg);
      }

      //AQUI LO QUE PUSO EL USUARIO
      pantalla.setCursor (10,68);
      pantalla.setTextSize(1);
      pantalla.setTextColor(ST77XX_DARKGREY, ST7735_BLACK);
      pantalla.printf ("META: %dd %02dh %02dm", diasGoal, horasGoal, minutosGoal);

    }

    pantalla.drawFastHLine(0, 94, 160, ST77XX_DARKGREY);
    for (int i = 0 ; i <OPCION_CORRIENDO ; i++){
        int posY = 98 + (i * 14);
        if (i == selectedItem ){
          pantalla.fillRect(4, posY, 152, 12, ST7735_BLUE);
          pantalla.setTextColor(ST7735_WHITE);
          pantalla.setTextSize(1);
          pantalla.setCursor(8, posY + 2);
          pantalla.print(">");
        } else {
          pantalla.fillRect(4, posY, 152, 12, ST7735_BLACK);
          pantalla.setTextColor(ST7735_BLACK);
          pantalla.setTextSize(1);
          pantalla.setCursor(8, posY + 2);
          pantalla.print(" ");
        }
        pantalla.print(opciones_Corriendo[i]);
    }


  }
  else if (currentState == MODIFY_TIME){
    //PINTAR LO QUE SIEMPRE SE TIENE QUE VER MIENTRAS ESTAMOS EN MODIFY TIME
    pantalla.setTextSize(1);
    pantalla.setTextColor(ST7735_CYAN, ST7735_BLACK);
    pantalla.setCursor(10, 8);
    pantalla.print("AJUSTE DE TIEMPO");
    pantalla.drawFastHLine(0, 20, 160, ST77XX_DARKGREY);

    for (int i = 0; i < OPCIONES_TIEMPO ; i++){
      int posY = 28 + (i * 20);
      if (i == selectedItem){
        uint16_t colorFondo = editMode ? ST7735_ORANGE : ST7735_BLUE;
        pantalla.fillRect(6, posY - 2, 148, 17, colorFondo);
        pantalla.setTextColor(ST7735_WHITE);
        pantalla.setTextSize(1);
        pantalla.setCursor(10, posY + 3);
        pantalla.print(editMode ? "* " : "> ");
      } else {
        pantalla.fillRect(6, posY - 2, 148, 17, ST77XX_BLACK);
        pantalla.setTextColor(ST77XX_DARKGREY);
        pantalla.setTextSize(1);
        pantalla.setCursor(10, posY + 3);
        pantalla.print("  ");
      }
      pantalla.print(opciones_Tiempo[i]); 
      //AQUI HACE FALTA UNA PARTE PERO NO ESTOY SEGURO DE QUE ES LO QUE HACE 
      if (i == 0){
        pantalla.printf("%d d", diasGoal);
      } else if (i == 1) {
        pantalla.printf("%d h", horasGoal);
      } else if (i == 2) {
        pantalla.printf("%d m", minutosGoal);
      }
    }
  } 
  else if (currentState == PROGRAM_DONE){
    pantalla.fillScreen(ST7735_BLACK);

    // 1. Cabecera
    pantalla.setTextSize(1);
    pantalla.setTextColor(ST77XX_GREEN, ST7735_BLACK);
    pantalla.setCursor(14, 8);
    pantalla.print("** CICLO FINALIZADO **");
    pantalla.drawFastHLine(0, 20, 160, ST77XX_DARKGREY);

    // 2. Mensaje central
    pantalla.setTextSize(2);
    pantalla.setTextColor(ST7735_YELLOW, ST7735_BLACK);
    pantalla.setCursor(46, 30);
    pantalla.print("LISTO!");

    // 3. Resumen de condiciones al terminar
    pantalla.setTextSize(1);
    pantalla.setTextColor(ST7735_CYAN, ST7735_BLACK);
    pantalla.setCursor(14, 56);
    pantalla.printf("TEMP FINAL : %.1f C", currentTemp);

    pantalla.setCursor(14, 72);
    pantalla.printf("HUMEDAD    : %d %%", currentHumid);

    // 4. Botón interactivo de salida
    pantalla.drawFastHLine(0, 94, 160, ST77XX_DARKGREY);
    pantalla.fillRect(8, 102, 144, 18, ST7735_BLUE);
    pantalla.setTextColor(ST7735_WHITE, ST7735_BLUE);
    pantalla.setTextSize(1);
    pantalla.setCursor(18, 107);
    pantalla.print("> MENU PRINCIPAL");
  }
  else if (currentState == MODIFY_COLOR){
    // 1. Cabecera fija
    pantalla.setTextSize(1);
    pantalla.setTextColor(ST7735_CYAN, ST7735_BLACK);
    pantalla.setCursor(26, 6);
    pantalla.print("ILUMINACION LED");
    pantalla.drawFastHLine(0, 14, 160, ST77XX_DARKGREY);

    // 2. Muestra de color (Recuadro gráfico)
    pantalla.drawRect(10, 18, 140, 18, ST7735_WHITE);
    pantalla.fillRect(12, 20, 136, 14, coloresTFT[colorLedsIndex]);

    // 3. Barra gráfica de brillo (Debajo del recuadro de color)
    int anchoBarra = map(brilloLedActual, 0, 100, 0, 136); // Mapeo de 0 a 100% al ancho en píxeles
    pantalla.drawRect(10, 39, 140, 8, ST77XX_DARKGREY);
    pantalla.fillRect(12, 41, 136, 4, ST7735_BLACK); // Limpiar fondo de barra
    pantalla.fillRect(12, 41, anchoBarra, 4, ST7735_YELLOW); // Relleno de nivel de brillo

    pantalla.drawFastHLine(0, 51, 160, ST77XX_DARKGREY);

    // 4. Opciones inferiores (0: Modificar Color, 1: Volver)
    for (int i = 0; i < 3; i++) {
      int posY = 56 + (i * 18); // 86 y 104

      if (i == selectedItem) {
        uint16_t colorFondo = editMode ? ST7735_ORANGE : ST7735_BLUE;
        pantalla.fillRect(4, posY - 2, 152, 15, colorFondo);
        pantalla.setTextColor(ST7735_WHITE);
        pantalla.setCursor(8, posY + 2);
        pantalla.print(editMode ? "* " : "> ");
      } else {
        pantalla.fillRect(4, posY - 2, 152, 15, ST7735_BLACK);
        pantalla.setTextColor(ST77XX_DARKGREY);
        pantalla.setCursor(8, posY + 2);
        pantalla.print("  ");
      }

      if (i == 0) {
        pantalla.print(editMode ? "Girando: Cambia Tono" : "Editar Color");
      } 
      else if (i == 1){
        pantalla.print("Brillo: "); 
        pantalla.print(brilloLedActual); 
        pantalla.print("%"); 
      } 
      else if ( i == 2 ){
        pantalla.print(">> GUARDAR Y VOLVER <<"); 
      }
    }
  } else if (currentState == PantallaFalla){
    // 1. Fondo de alerta rojo
    pantalla.fillScreen(ST7735_RED);

    // 2. Encabezado de Error Crítico
    pantalla.fillRect(0, 0, 160, 24, ST7735_BLACK);
    pantalla.setTextSize(1);
    pantalla.setTextColor(ST7735_RED, ST7735_BLACK);
    pantalla.setCursor(12, 8);
    pantalla.print("!! FALLA CRITICA !!");

    // 3. Icono / Texto de Advertencia
    pantalla.setTextSize(2);
    pantalla.setTextColor(ST7735_WHITE, ST7735_RED);
    pantalla.setCursor(20, 36);
    pantalla.print("SISTEMA STOP");

    // 4. Detalle de la Falla
    pantalla.setTextSize(1);
    pantalla.setCursor(8, 62);
    pantalla.print("CAUSA:");
    pantalla.setCursor(8, 74);
    pantalla.setTextColor(ST7735_YELLOW, ST7735_RED);
    pantalla.print(mensajeFalla);

    // 5. Botón interactivo para reiniciar
    pantalla.fillRect(10, 100, 140, 20, ST7735_BLACK);
    pantalla.setTextColor(ST7735_WHITE, ST7735_BLACK);
    pantalla.setCursor(22, 106);
    pantalla.print("> REINICIAR <");
  }
}

void setup() {
  pinMode(ENC_CLK, INPUT);
  pinMode(ENC_DT, INPUT);
  pinMode(ENC_SW, INPUT_PULLUP); 
  pinMode(rele, OUTPUT);
  pinMode(humid, OUTPUT); 
  pinMode(buzzer, OUTPUT);
  pinMode(TFT_CS, OUTPUT); 
  digitalWrite(TFT_CS, HIGH); // Mantiene aislada la TFT de inmediato
  digitalWrite(rele,LOW); 

  // Leemos el estado inicial de reposo de CLK (casi siempre HIGH)
  lastClkState = digitalRead(ENC_CLK);

  // Leer estado inicial antes de activar interrupciones
  encPrev = (digitalRead(ENC_CLK) << 1) | digitalRead(ENC_DT);

  //activar interrupciones para ciertos pines 
  attachInterrupt(digitalPinToInterrupt(ENC_CLK), isrEncoder, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENC_DT),isrEncoder, CHANGE); 

  //INICIAR WIFI
  WiFi.mode(WIFI_STA);
  WiFi.begin(redWifi,contrasena);

  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== SISTEMA DE MONITOREO DE TEMPERATURA LISTO ===");

  //EMPEZAR PROTOCOLO ONE WIRE
  ds18b20.begin();
  ds18b20.setResolution(12);
  ds18b20.setWaitForConversion(false); // <--- CLAVE PARA NO CONGELAR EL LOOP
  ds18b20.requestTemperatures();       // Pedir primera conversión inicial

  //empezar prtocolo I2C
  Wire.begin(I2C_SDA, I2C_SCL);

  //EMPEZAR PROTOCOLO TIRA LED
  tiraLed.begin();
  tiraLed.setBrightness(map(brilloLedActual, 0, 100, 0, 255)); 
  aplicarColorLEDs();  


  pantalla.initR(INITR_BLACKTAB); 
  pantalla.setRotation(1); 
  pantalla.fillScreen(ST7735_BLACK); 
  actualizarMenu(); 

  //CONFIGURAR PWM
  ledcSetup(0,25000,8); //PWM ventilador para resistencia (canal 0, freq 25k, 8 bit resolution)
  ledcAttachPin(fan1PWM,0);//unir el canal de pwm arriba con el pin de pwm de ventilador
  ledcWrite(0,0); 
  ledcSetup(1,25000,8); //pwm ventilador para sacar el aire
  ledcAttachPin(fan2PWM,1);
  ledcWrite(1,0);
  ledcSetup(2,2700,8); //PWM para el sonido del buzzer 
  ledcAttachPin(buzzer, 2);
  ledcWrite(2,0); 

  
  initMAX7219(); 
  mostrarTemperatura(0.0); 

// INICIALIZACIÓN BME280
  if (!bme.begin(0x76, &Wire)) {
    Serial.println("[ERROR] No se detectó BME280.");
  } else {
    Serial.println("[OK] BME280 inicializado en 0x76.");

    // Configuración explícita de registros internos recomendada por Bosch
    bme.setSampling(Adafruit_BME280::MODE_FORCED,
                    Adafruit_BME280::SAMPLING_X2,
                    Adafruit_BME280::SAMPLING_X1,
                    Adafruit_BME280::SAMPLING_X1,
                    Adafruit_BME280::FILTER_X4,
                    Adafruit_BME280::STANDBY_MS_1000);
  }

  if(WiFi.status() != WL_CONNECTED){
    Serial.print("intentando conectar a wifi");
      for (int i = 0; i<3 ; i++){
        delay (500);
        Serial.print(".");
    }
    if (WiFi.status() != WL_CONNECTED){
    Serial.print("NO SE PUDO CONECTAR A WIFI INTENTANDO MAS TARDE");
    conexionWifi = false; 
    }
  } else {
    conexionWifi = true; 
    Serial.print("Wifi CONECTADO. Direccion IP:");
    Serial.print(WiFi.localIP()); 
  }


  Serial.printf("[OK] Sensores DS18B20 encontrados: %d\n", ds18b20.getDeviceCount());
  Serial.println("=================================================\n");
}

unsigned long lastSensorTime = 0;

void loop() {


  if (currentTemp != lastTemp){
    lastTemp = currentTemp; 
    temperaturaIgual = 0 ; //REINICIAR SI LA TEMPERATURA ESTA CAMBIANDO 
  }
    else { temperaturaIgual++; } //SI LA TEMPERATURA ES IGUAL SE SUMA PARA VERIFICAR UQE SIRVA EL CALEFACTO

  if (currentHumid != lastHumid){
    lastHumid = currentHumid; 
    
  }


  if (currentState == PROGRAM_DONE){

    digitalWrite(rele, LOW);
    digitalWrite(humid, LOW);
    ledcWrite(0, 0); //APAGAR VENTILADORES AL ACABAR
    ledcWrite(1, 0); 

    if (millis()- ultimaAlarma >= tiempoAlarma){
      ultimaAlarma = millis(); 
      alarmaOn= !alarmaOn; 
    }

    if (alarmaOn){
      Serial.println("[ALARMA] Sonando...");
        ledcWrite(2,250); 
    } else {
      ledcWrite(2,0); 
      Serial.println("[ALARMA] Silenciada...");
    }
  }

  leerEncoder();
  botonPresionado();

  //FUNCION PARA HACER ROTAR LAS IMAGENES SIN BLOQUEAR AL PROCESADOR
  if (currentState == RUNNING_AUTO){

    // 1. CALCULO GLOBAL CONTINUO DE TIEMPO RESTANTE
    uint32_t totalSegundosGoal = ((uint32_t)diasGoal * 86400UL) + 
                                 ((uint32_t)horasGoal * 3600UL) + 
                                 ((uint32_t)minutosGoal * 60UL); 

    uint32_t segundosTranscurridos = (millis() - startTimeIncubation) / 1000UL;
    
    if (segundosTranscurridos >= totalSegundosGoal) {
      segRestantes = 0;
    } else {
      segRestantes = totalSegundosGoal - segundosTranscurridos;
    }

    //VERIFICAR SI EL TIEMPO SELECCIONADO YA TERMINO 
  if (segRestantes <= 0 && currentState == RUNNING_AUTO){
    currentState = PROGRAM_DONE; 

    //CREAR MENSAJE A ENVIAR 
    char bufferMensaje [200]; 
    snprintf(bufferMensaje, sizeof(bufferMensaje),
      "Programa Finalizado Exitosamente\n"
      "Temperatura Final : %.1f C (Objetivo: %.1f C)\n"
      "Humedad Final     : %d %% (Objetivo: %d %%)\n"
      "Duracion          : %dd %02dh %02dm\n",
      currentTemp, tempGoal, currentHumid, humidGoal, 
      diasGoal, horasGoal, minutosGoal 
    );

    String mensajeFinalizacion = String(bufferMensaje); 
    Callmebot.whatsappMessage(numeroTelefono, ApiKey, mensajeFinalizacion); 

    alarmaOn = true; 
    ultimaAlarma = millis(); 
    ledcWrite(2,200); 
    pantalla.fillScreen(ST7735_BLACK);
    actualizarMenu(); // Dibuja la pantalla final UNA SOLA VEZ
  }

    manejarCalor(potenciaCalor);
    manejarHumedad(potenciaHumedad);
    if (millis() - lastAutoScreenSwitch >= AUTO_SCREEN_INTERVAL){
      //SI YA PASARON 4 SEGUNDOS
      lastAutoScreenSwitch = millis();
      autoScreenPage = (autoScreenPage +1) % 3;
      actualizarMenu();
      calcularPWMTemp(tempGoal);
      calcularPWMHumedad(humidGoal);
    } 
    if (millis() - ultimoEncendidoLed >= 60000 && movimientoEncoder == false){
      tiraLed.setBrightness(40);
      aplicarColorLEDs(); 
      ultimoEncendidoLed = millis(); 
    } else if (movimientoEncoder){
      tiraLed.setBrightness (map(brilloLedActual,0,100,0,255));  // AJUSTAR BRILLO EN MENU Y PODER PONER LA VARIABLE AQUI  
      aplicarColorLEDs(); 
    }
  }

  if (currentState == PantallaFalla){
    // Alarma sonora intermitente
    if (millis() - ultimaAlarma >= 1000) {
      ultimaAlarma = millis();
      alarmaOn = !alarmaOn;
      ledcWrite(2, alarmaOn ? 220 : 0);
    }
  }

  //LECTURA DE SENSORES
  if (millis()-ultimaLectura >= intervaloLectura){
    ultimaLectura = millis();

    //LECTURA TEMPERATURA ONE WIRE
    float tempOneWire = ds18b20.getTempCByIndex(0); //TRAER LA PRIMERA TEMPERATURA DEL PRIMER SENSOR EN EL BUS
    ds18b20.requestTemperatures(); //DECIR AL BUS QUE VAS A PREGUNTAR POR TEMPERATURA
    
    // 2. Disparar medición forzada en BME280 y leer
    bme.takeForcedMeasurement(); // Despierta el sensor, mide y vuelve a reposo
    float tempBME280 = bme.readTemperature();
    float humBME280  = bme.readHumidity();

    bool oneWireValido = (tempOneWire != DEVICE_DISCONNECTED_C && tempOneWire > -50.0);
    bool BMEFuncionando = (!isnan(tempBME280) && tempBME280 > -40.0 && humBME280 >= 0.0 && tempBME280 < 150.0 && humBME280 < 90);


    static int fallasConsecutivas = 0; 
    bool coherenciaBME = false; 

    if (oneWireValido && BMEFuncionando){ //AMBOS ESTAN MANDANDO SENAL
      float diferencia = abs (tempOneWire - tempBME280); 
        if (diferencia <= 2.5){
          coherenciaBME = true; 
          fallasConsecutivas = 0;
        } else {
          coherenciaBME = false; 
          fallasConsecutivas++; 
          Serial.printf("[ALERTA] Desvío térmico detectado: DS18B20=%.2f C vs BME=%.2f C (Dif: %.2f C)\n", 
                      tempOneWire, tempBME280, diferencia);
        }
    } else if (BMEFuncionando){
        coherenciaBME = true; //SI NO FUNCIONA EL ONEWIRE
    }
     
    if (fallasConsecutivas >=2){
      reiniciarBME280();
      fallasConsecutivas = 0; 
    }
    if (BMEFuncionando && coherenciaBME){
      currentHumid = (int)humBME280; 
    } else {
      currentHumid = lastHumid;
    }

    //CASO 1 AMBOS SENSORE FUNCIONAN
    if (oneWireValido && coherenciaBME){
      sensores = 0; // CASO 0 = TODO FUNCIONA  esta variable se va a usar cuando se vea la temp
      currentTemp = ((tempOneWire + tempBME280)/2);
      falloSensores = 0; 
    }
    //CASO 2 SENSOR HUMEDAD Y TEMP FUNCIONA
    else if (!oneWireValido && coherenciaBME){
      sensores = 1; // CASO 1 = FUNCIONA SOLO HUMEDAD Y TEMPERATURA
      currentTemp = tempBME280; 
      falloSensores = 0; 
    }
    //CASO 3 SENSOR ONEWIRE FUNCIONA SOLO
    else if (oneWireValido && !coherenciaBME){
      sensores = 2; //CASO 2 = FUNCIONA SOLO ONEWIRE
      currentTemp = tempOneWire;
      falloSensores = 0; 
    }
    //CASO 4 NINGUNO FUNCIONA 
    else {
      sensores = 3; // NADA FUNCIONA PARAR EL CALENTAMIENTO Y SACAR AIRE 
      currentTemp = lastTemp; 
      falloSensores++; 
    }

    mostrarTemperatura(currentTemp); 
    enviarHumedad(currentHumid); 
    SystemError error = verificarSistema();
    manejarErrores(error);  
  }

  if (!conexionWifi){
    WiFi.reconnect();
    delay (50); 
    if (WiFi.status() == WL_CONNECTED){
      conexionWifi = true; 
    } else { conexionWifi = false; }
  }

  movimientoEncoder = false; 
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
      // 3. NAVEGACIÓN EN programa AUTO
      else if (currentState == RUNNING_AUTO) {
        if (giroHorario) {
          if (selectedItem < OPCION_CORRIENDO - 1) selectedItem++;
        } else {
          if (selectedItem > 0) selectedItem--;
        }
      }
      // MODIFICAR TIEMPO 
      else if (currentState == MODIFY_TIME){
        if (!editMode){
            if (giroHorario){
              if (selectedItem < OPCIONES_TIEMPO - 1) selectedItem++;
            } else {
              if (selectedItem > 0) selectedItem--;
            }
          } else {
            switch (selectedItem)
            {
            case 0:
              if (giroHorario){
                if (diasGoal < 60) {
                  diasGoal++;
                }
              } else if (diasGoal > 0){
                  diasGoal--;
              }
              break;
            case 1:
              if (giroHorario){
                if (horasGoal < 23){
                  horasGoal++;
                }}else if (horasGoal>0) horasGoal--;
              break;

            case 2:
              if (giroHorario){
                if (minutosGoal < 59) minutosGoal++;
              } else if(minutosGoal>0) minutosGoal--;
              break;

            default:
            break;
          }
        }
      }
      else if (currentState == MODIFY_COLOR) {
        if (!editMode) {
          // Desplazarse entre: 0 (Color), 1 (Brillo) y 2 (Guardar y Volver)
          if (giroHorario && selectedItem < 2) selectedItem++;
          else if (!giroHorario && selectedItem > 0) selectedItem--;
        } else {
          // OPCIÓN 0: Cambiar el tono del color
          if (selectedItem == 0) {
            if (giroHorario) {
              colorLedsIndex = (colorLedsIndex + 1) % TOTAL_COLORES;
            } else {
              colorLedsIndex = (colorLedsIndex - 1 + TOTAL_COLORES) % TOTAL_COLORES;
            }
          } 
          // OPCIÓN 1: Cambiar el nivel de brillo (Preset)
          else if (selectedItem == 1) {
            if (giroHorario && brilloIndex < TOTAL_NIVELES_BRILLO - 1) {
              brilloIndex++;
            } else if (!giroHorario && brilloIndex > 0) {
              brilloIndex--;
            }
            brilloLedActual = nivelesBrillo[brilloIndex]; // Actualiza el % (ej. 20, 40, 60...)
            
            // Mapeo de Porcentaje (0-100) a valor de hardware NeoPixel (0-255)
            uint8_t brilloHardware = map(brilloLedActual, 0, 100, 0, 255);
            tiraLed.setBrightness(brilloHardware);
          }

          // Reflejar cambios de color y brillo en tiempo real
          aplicarColorLEDs();
        }
      }
      actualizarMenu();
  }
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
      movimientoEncoder = true; 
      ultimoEncendidoLed = millis(); 

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

        //opcion para iniciar el programa
        if (selectedItem == 5){
            currentState = RUNNING_AUTO; 
            editMode = false; 
            startTimeIncubation = millis(); 
            autoScreenPage = 0;
            lastAutoScreenSwitch = millis (); 
            pantalla.fillScreen(ST7735_BLACK);
            actualizarMenu (); 
            //completar de dibujar la pantalla de monitoreo en programa auto 
        }
        else if (selectedItem == 2){
            // poner la configurarion para hacer dias o horas 
            currentState = MODIFY_TIME;
            editMode = false;
            pantalla.fillScreen(ST7735_BLACK);
            actualizarMenu();
        
        } else if(selectedItem == 4){
            currentState = MODIFY_COLOR; 
            editMode = false; 
            pantalla.fillScreen(ST7735_BLACK);
            actualizarMenu(); 
        }
         else {
          editMode = !editMode; 
          actualizarMenu ();
        }
      } 
      else if (currentState == RUNNING_AUTO){
        if (selectedItem == 0){
          //cancelar
          currentState = MENU_SELECT;
          selectedItem = 0;
          ledcWrite (0, 0); //apagar ventiladores
          ledcWrite (1,0);
          digitalWrite(rele, LOW);//apagar rele 
          digitalWrite(humid,LOW); //APAGAR humidificador 
          pantalla.fillScreen(ST7735_BLACK);
          actualizarMenu();
        }
        else if (selectedItem == 1){
          //MODIFICAR
          currentState = MODIFY_PROGRAM;
          selectedItem = 0 ;
          editMode = false;
          pantalla.fillScreen(ST7735_BLACK);
          actualizarMenu();
        }
      }
      else if (currentState == MODIFY_TIME){
        //OPCION VOLVER
        if (selectedItem == 3){
          currentState = MODIFY_PROGRAM;
          selectedItem = 0; 
          editMode = false; 
          pantalla.fillScreen(ST7735_BLACK);
          actualizarMenu(); 
        } else {
          editMode=!editMode; 
          actualizarMenu ();
        }
      }
      else if (currentState == PROGRAM_DONE){
        alarmaOn = false;
        ledcWrite(2, 0); // Apagar buzzer por completo
        currentState = MENU_SELECT;
        selectedItem = 0;
        pantalla.fillScreen(ST7735_BLACK);
        actualizarMenu();
      }

      else if (currentState == MODIFY_COLOR) {
        if (selectedItem == 0 || selectedItem == 1) {
          editMode = !editMode; // Entra o sale de la edición del tono
        } else if (selectedItem == 2) {
          // Guardar y volver a MODIFY_PROGRAM
          editMode = false;
          selectedItem = 4; // Apunta de nuevo sobre la opción "Color Iluminacion"
          currentState = MODIFY_PROGRAM;
          pantalla.fillScreen(ST7735_BLACK);
        }
        actualizarMenu();
      }
      else if (currentState == PantallaFalla) {
        // Al presionar el botón, apagar alarma y volver al menú principal
        ledcWrite(2, 0); // Apagar buzzer
        alarmaOn = false;
        vecesRevisada = 0;
        vecesEncendidasResistencia = 0;
        falloSensores = 0;
        
        currentState = MENU_SELECT;
        selectedItem = 0;
        pantalla.fillScreen(ST7735_BLACK);
        actualizarMenu();
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
  aplicarColorLEDs(); 

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
    aplicarColorLEDs(); 

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
    aplicarColorLEDs();
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
    aplicarColorLEDs();
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
  
  aplicarColorLEDs(); 
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
    movimientoEncoder = true; 
    ultimoEncendidoLed = millis(); 
  }
}


 
//revisar los sensores de tempratura y revisar temperatura constante
void calcularPWMTemp (float temperaturaMeta){

  float diferencia =(temperaturaMeta - currentTemp);

  //si el resultado es positivo falta calor
  if (diferencia > 0){
    //poner el ventilador-resistencia al 50% para mover el aire de la resistencia
    ledcWrite(0,128);
    ledcWrite(1,0); //ventilador extractor apagado

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

} 

void manejarCalor (float PWMgenerado){
  unsigned long tiempoActual = millis();

  if (tiempoActual-inicioVentana >= duracionMaxima){
    inicioVentana = tiempoActual; 
  } 
  unsigned long tiempoEncendido = (unsigned long)((PWMgenerado/100) * duracionMaxima ); //calcula cuanto milisegundos se queda encedidos

  if ( PWMgenerado >0 && (tiempoActual - inicioVentana) < tiempoEncendido){
    digitalWrite(rele, HIGH); 
    vecesEncendidasResistencia++; 
  } else {
    digitalWrite(rele, LOW);
  }
}

void calcularPWMHumedad (int humedadMeta){
  potenciaHumedad = 0;

  //AGREGAR PRIORIDAD POR TEMPERATURA ANTES QUE HUMEDAD 
  if ( currentTemp + 1 < tempGoal){
    ledcWrite(1,0); //APAGAR VENTILADOR EXTRACTOR
  }

  int diferencia = (humedadMeta) - currentHumid; 

  if (diferencia > 0){ //NECESITA HUMEDAD
     if (diferencia >= 10 ){
      potenciaHumedad = 100;
    } 
    else if (diferencia < 10 && diferencia >= 7){
      potenciaHumedad = 70;
    }
    else if (diferencia < 7 && diferencia >= 4){
      potenciaHumedad = 40;
    }
    else if (diferencia < 4 && diferencia >= 2){
      potenciaHumedad = 20;
    }
    else  if (diferencia == 1){ //RANGO DE TOLERANCIA
      potenciaHumedad = 0;
    }

  } else { //SE NECESITA SACAR HUMEDAD 

    if (currentTemp >= tempGoal){
    diferencia = abs(diferencia); 
     if (diferencia >= 10 ){
      ledcWrite(1,128); //VENTILADOR EXTRACTOR AL 50%
    } 
    else if (diferencia < 10 && diferencia >= 7){
      ledcWrite(1, 90);
    }
    else if (diferencia < 7 && diferencia >= 4){
      ledcWrite(1,64);
    }
    else if (diferencia < 4 && diferencia >= 2){
      ledcWrite(1,39);
    }
    else  if (diferencia == 1){ //RANGO DE TOLERANCIA
      ledcWrite(1,0);
    }
  }
  }
}

void manejarHumedad (int PWMHumedad){
  unsigned long tiempoActual = millis();
  if (tiempoActual - empiezaConteo >= tiempoMaximo){
    empiezaConteo = tiempoActual; }

  unsigned long tiempoEncendido = (unsigned long)((PWMHumedad/100)* tiempoMaximo);

  if (PWMHumedad > 0 && (tiempoActual - empiezaConteo) < tiempoEncendido){
    digitalWrite(humid, HIGH);
  } else {
    digitalWrite(humid, LOW);
  }
}

void reiniciarBME280(){
  Serial.println("[RECUPERACIÓN] Reinicializando bus I2C y BME280 por desvío térmico...");

  // 1. Reset por software
  Wire.beginTransmission(0x76);
  Wire.write(0xE0);
  Wire.write(0xB6);
  Wire.endTransmission();
  delay(50); // Tiempo para que el chip recargue la NVM de fábrica

  // 2. Reabrir bus e inicializar librería
  Wire.begin(I2C_SDA, I2C_SCL);
  if (bme.begin(0x76, &Wire)) {
    bme.setSampling(Adafruit_BME280::MODE_FORCED,     // Modo Forzado: solo mide cuando se le pide
                    Adafruit_BME280::SAMPLING_X2,    // Temp 2x
                    Adafruit_BME280::SAMPLING_X1,    // Presión 1x
                    Adafruit_BME280::SAMPLING_X2,    // Humedad 2x
                    Adafruit_BME280::FILTER_X4);     // Filtro IIR
    Serial.println("[OK] BME280 reconfigurado y calibrado exitosamente.");
  } else {
    Serial.println("[ERROR] No se pudo recuperar el BME280 en el bus.");
  }

}

void reiniciarOneWire(){
Serial.println("[RECUPERACIÓN] Reinicializando bus OneWire (DS18B20)...");
  
  // Re-inicializar la librería sobre el pin definido
  ds18b20.begin();
  ds18b20.setResolution(12);
  ds18b20.setWaitForConversion(false);
  ds18b20.requestTemperatures();
  
  delay(20);
}
// ==========================================
// BIT-BANGING ROBUSTO CON REACTIVACIÓN DE HARDWARE SPI
// ==========================================
void bitBangByte(uint8_t data) {
  for (int i = 7; i >= 0; i--) {
    digitalWrite(MAX_CLK, LOW);
    digitalWrite(MAX_DIN, (data & (1 << i)) ? HIGH : LOW);
    delayMicroseconds(1);
    
    digitalWrite(MAX_CLK, HIGH);
    delayMicroseconds(1);
  }
  digitalWrite(MAX_CLK, LOW);
}

void enviarMAX7219(uint8_t registro, uint8_t valor) {
  // 2. Bajar CS del MAX7219 para iniciar trama
  digitalWrite(MAX_CS, LOW);
  delayMicroseconds(1);

  // 3. Enviar los dos bytes a mano
  bitBangByte(registro);
  bitBangByte(valor);

  // 4. Subir CS para fijar el dato (Latch)
  digitalWrite(MAX_CS, HIGH);
  delayMicroseconds(1);

  // 5. Dejar líneas de bus en reposo
  digitalWrite(MAX_CLK, LOW);
  digitalWrite(MAX_DIN, LOW);
}

void initMAX7219() {
  pinMode(MAX_CS, OUTPUT);
  pinMode(MAX_CLK, OUTPUT);
  pinMode(MAX_DIN, OUTPUT);
  
  digitalWrite(MAX_CS, HIGH);
  digitalWrite(MAX_CLK, LOW);
  digitalWrite(MAX_DIN, LOW);

  delay(50);
  enviarMAX7219(MAX7219_REG_TEST, 0x00);    // Modo prueba apagado
  enviarMAX7219(MAX7219_REG_SCAN, 0x07);    // 8 dígitos habilitados (0 a 3)
  enviarMAX7219(MAX7219_REG_DECODE, 0xFF);  // Code B en los 4 dígitos
  enviarMAX7219(MAX7219_REG_INTENS, 0x07);  // Brillo medio
  enviarMAX7219(MAX7219_REG_SHUTDN, 0x01);  // Encender
}

void mostrarTemperatura(float temp) {
  // Asegurar que la pantalla TFT no escuche
  digitalWrite(TFT_CS, HIGH);

  if (temp < -9.9 || temp > 99.9) {
    enviarMAX7219(MAX7219_REG_DIG0, 0x0A); // '-'
    enviarMAX7219(MAX7219_REG_DIG1, 0x0A); // '-'
    enviarMAX7219(MAX7219_REG_DIG2, 0x0A); // '-'
    enviarMAX7219(MAX7219_REG_DIG3, 0x0F); // Blanco
    return;
  }

  int valorEntero = (int)(temp * 10.0 + 0.5); 
  int decena  = (valorEntero / 100) % 10;
  int unidad  = (valorEntero / 10) % 10; 
  int decima  = valorEntero % 10; 

  enviarMAX7219(MAX7219_REG_DIG0, (decena > 0) ? decena : 0x0F);
  enviarMAX7219(MAX7219_REG_DIG1, unidad | 0x80); // Punto decimal
  enviarMAX7219(MAX7219_REG_DIG2, decima);
  enviarMAX7219(MAX7219_REG_DIG3, 0x0F);
}

void enviarHumedad(int humedad){
  // Asegurar que la pantalla TFT no escuche
  digitalWrite(TFT_CS, HIGH);

    if (humedad < 0 || humedad > 99) {
    enviarMAX7219(MAX7219_REG_DIG0, 0x0A); // '-'
    enviarMAX7219(MAX7219_REG_DIG1, 0x0A); // '-'
    enviarMAX7219(MAX7219_REG_DIG2, 0x0A); // '-'
    enviarMAX7219(MAX7219_REG_DIG3, 0x0F); // Blanco
    return;
  }
  
  //conversiona digitos a digito
  int decena = ((humedad / 10) % 10);
  int unidad = (humedad % 10); 

  enviarMAX7219(MAX7219_REG_DIG4, (decena > 0) ? decena : 0x0F);
  enviarMAX7219(MAX7219_REG_DIG5, unidad ); 
  enviarMAX7219(MAX7219_REG_DIG6, 0x0F); 
  enviarMAX7219(MAX7219_REG_DIG7, 0x0F);
}

// ==========================================
// FUNCIÓN PARA APLICAR COLOR A LOS 32 LEDS
// ==========================================
void aplicarColorLEDs() {
  for (int p = 0; p < 64; p++) {
    tiraLed.setPixelColor(p, tablaColoresNeo[colorLedsIndex]);
  }
  tiraLed.show(); 
}


SystemError verificarSistema (){

if (currentState ==RUNNING_AUTO && potenciaCalor >0 ){
  if (vecesEncendidasResistencia <= 10){
    if ((currentTemp - lastTemp) <= 0){ //temperatura NO SUBE CORRECTAMENTE
      vecesRevisada++; 
      if (vecesRevisada >= 5){
        return SYS_ERR_HEAT; 
      }
    } 
  }
  }

  if (falloSensores >= 5){
    return SYS_SENSOR_ERR; 
  }

  Wire.beginTransmission(0x76);
  if (Wire.endTransmission() != 0) {
    Serial.println("[ERROR SISTEMA] Fallo de comunicación I2C con BME280");
    return SYS_ERR_COMMUNICATION_BME;
  }

  if (ds18b20.getTempCByIndex(0) == DEVICE_DISCONNECTED_C){
    Serial.println("[ERROR SISTEMA] Fallo de comunicación I2C con ONEWIRE");
    return SYS_ERR_COMMUNICATION_ONEWIRE; 
  }

  return SYS_ERR_OK; 
}

void manejarErrores (SystemError Error){

  switch (Error)
  {
  case SYS_ERR_HEAT:{

    //CREAR MENSAJE A ENVIAR 
    char bufferMensaje [200]; 
    snprintf(bufferMensaje, sizeof(bufferMensaje),
      "FALLO CRITICO EN CALEFACCION\n"
      "REVISA SI LA PUERTA ESTA CERRADA\n"
      "Temperatura Actual : %.1f C (Objetivo: %.1f C)\n"
      "Humedad Actual     : %d %% (Objetivo: %d %%)\n",
      currentTemp, tempGoal, currentHumid, humidGoal 
    );

    String mensajeFinalizacion = String(bufferMensaje); 
    Callmebot.whatsappMessage(numeroTelefono, ApiKey, mensajeFinalizacion); 

    mensajeFalla = "FALLO RESISTENCIA";
    Serial.print("FALLO EN LA RESITENCIA. VERIFICA QUE LA PUERTA ESTE CERRADA");
    // Apagar actuadores por seguridad
    digitalWrite(rele, LOW);
    digitalWrite(humid, LOW);
    ledcWrite(0, 0);
    ledcWrite(1, 0);
    currentState = PantallaFalla; //HACER PANTALLA PARA FALLO TOTAL APAGAR TODO Y MOSTRAR FALLO
    pantalla.fillScreen(ST7735_BLACK);
    actualizarMenu();
    break;
  }
  case SYS_SENSOR_ERR: { 
    bool recuperado = false; 
    for (int i = 0; i < 5; i++) {

      reiniciarBME280();
      reiniciarOneWire();
      delay(100); 

      //PROBAR SI FUNCIONA EL BME
      Wire.beginTransmission(0x76); 
      bool BMEOK = (Wire.endTransmission() == 0); 

      //PROBAR SI FUNCIONA ONEWIRE 
      float tempDS = ds18b20.getTempCByIndex(0);
      bool oneWireOK = (tempDS != DEVICE_DISCONNECTED_C && tempDS > -50.0);

      if (BMEOK || oneWireOK) {
        Serial.print("SE RECUPERO LOS SENSORES"); 
        recuperado = true; 
        break;
      }
    }
    if (!recuperado) {
          //CREAR MENSAJE A ENVIAR 
    char bufferMensaje [200]; 
    snprintf(bufferMensaje, sizeof(bufferMensaje),
      "FALLO COMUNICACION CON SENSORES\n"
      "INTENTA REINICIAR LA INCUBADORA \n"
      "Temperatura Actual : %.1f C (Objetivo: %.1f C)\n"
      "Humedad Actual     : %d %% (Objetivo: %d %%)\n",
      currentTemp, tempGoal, currentHumid, humidGoal 
    );
    String mensajeFinalizacion = String(bufferMensaje); 
    Callmebot.whatsappMessage(numeroTelefono, ApiKey, mensajeFinalizacion); 
      mensajeFalla = "SENSORES OFFLINE";
      Serial.print("NO se pudo recuperar comunicacion con los sensores"); 
      digitalWrite(rele, LOW);
      digitalWrite(humid, LOW);
      ledcWrite(0, 0);
      ledcWrite(1, 0);
      currentState = PantallaFalla; 
      pantalla.fillScreen(ST7735_BLACK);
      actualizarMenu();
    }
    break;
  } 

  case SYS_ERR_COMMUNICATION_BME:{
    reiniciarBME280(); 
    delay(100); 
    break;
  }
  case SYS_ERR_COMMUNICATION_ONEWIRE:{
    reiniciarOneWire(); 
    delay(100); 
    break;}
  
  default:
    break;
  }
}