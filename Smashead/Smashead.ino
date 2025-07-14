#include <LedControl.h> // Incluye la librería para el display MAX7219

// --- CONFIGURACIÓN DE HARDWARE ---

// Configuración de pines para los displays MAX7219 en cadena
#define DIN_PIN 42 // Pin de datos (DIN) del primer display
#define CLK_PIN 43 // Pin de reloj (CLK) para TODOS los displays
#define CS_PIN  44 // Pin de selección (CS/LOAD) para TODOS los displays

// Número total de destinos físicos (y módulos MAX7219)
const int NUM_DESTINOS = 5;

// Crea una instancia del objeto LedControl
LedControl lc = LedControl(DIN_PIN, CLK_PIN, CS_PIN, NUM_DESTINOS);

// --- MAPEO DE PINES ---
int pinesLED[NUM_DESTINOS] = {22, 23, 24, 25, 26};
int pinesBotonConfirmacion[NUM_DESTINOS] = {27, 28, 29, 30, 31};
int pinesBotonIncremento[NUM_DESTINOS] = {32, 33, 34, 35, 36};
int pinesBotonDecremento[NUM_DESTINOS] = {37, 38, 39, 40, 41};

// --- ESTRUCTURA DE CONTROL ---
// Esta estructura nos permite asociar dinámicamente una Orden de Venta
// a un destino físico (un display, un LED, y sus botones).
struct Destino {
  int ordenDeVenta; // El número de la O.V. asignada. -1 si está libre.
  bool estadoBotonConfirmacion; // Para el antirrebote (debounce)
  unsigned long ultimoPulsoConfirmacion;
  bool estadoBotonMas;
  unsigned long ultimoPulsoMas;
  bool estadoBotonMenos;
  unsigned long ultimoPulsoMenos;
};

Destino destinos[NUM_DESTINOS]; // Un arreglo de nuestros destinos físicos
String comandoSerial = ""; // Buffer para la entrada serial
const long DEBOUNCE_DELAY = 50; // 50ms para el antirrebote de botones

void setup() {
  Serial.begin(9600);
  comandoSerial.reserve(200);

  // Inicializa cada módulo MAX7219 y la estructura de control
  for (int i = 0; i < NUM_DESTINOS; i++) {
    // Configuración del display
    lc.shutdown(i, false);      // Saca el display del modo de ahorro de energía
    lc.setIntensity(i, 2);      // Establece el brillo (0-15)
    lc.clearDisplay(i);         // Limpia el display

    // Configuración de pines
    pinMode(pinesLED[i], OUTPUT);
    digitalWrite(pinesLED[i], LOW);
    pinMode(pinesBotonConfirmacion[i], INPUT_PULLUP);
    pinMode(pinesBotonIncremento[i], INPUT_PULLUP);
    pinMode(pinesBotonDecremento[i], INPUT_PULLUP);

    // Inicializa el estado del destino
    destinos[i].ordenDeVenta = -1; // -1 significa que el destino está libre
    destinos[i].estadoBotonConfirmacion = HIGH;
    destinos[i].estadoBotonMas = HIGH;
    destinos[i].estadoBotonMenos = HIGH;
  }
  
  delay(1000); // Espera a que todo se estabilice
  Serial.println("Arduino: Iniciado."); // Envía mensaje de confirmación a la PC
}

void loop() {
  // 1. Revisa si hay comandos desde la PC
  if (Serial.available()) {
    char c = Serial.read();
    if (c == '\n') {
      procesarComando(comandoSerial);
      comandoSerial = ""; // Limpia el comando para el siguiente
    } else {
      comandoSerial += c; // Agrega el caracter al comando
    }
  }

  // 2. Revisa el estado de los botones de cada destino
  revisarBotones();
}

// Procesa el comando recibido desde la PC
void procesarComando(String cmd) {
  cmd.trim();
  if (cmd.length() == 0) return;

  // Formato esperado: ENCENDER_ORDENDEVTA_PIEZAS (ej. "ENCENDER_101_15")
  if (cmd.startsWith("ENCENDER_")) {
    int primerGuion = cmd.indexOf('_');
    int segundoGuion = cmd.lastIndexOf('_');
    int orden = cmd.substring(primerGuion + 1, segundoGuion).toInt();
    int piezas = cmd.substring(segundoGuion + 1).toInt();
    
    // Busca un destino físico libre para asignarle esta orden de venta
    int indiceDestino = buscarDestinoLibre();
    if (indiceDestino != -1) {
      destinos[indiceDestino].ordenDeVenta = orden;
      mostrarNumero(piezas, indiceDestino);
      digitalWrite(pinesLED[indiceDestino], HIGH);
      Serial.println("CONFIRMACION_ENCENDIDO_" + String(orden));
    }
  } 
  // Formato esperado: APAGAR_ORDENDEVTA (ej. "APAGAR_101")
  else if (cmd.startsWith("APAGAR_")) {
    int orden = cmd.substring(7).toInt();
    int indiceDestino = buscarDestinoPorOrden(orden);
    if (indiceDestino != -1) {
      apagarDestino(indiceDestino);
      Serial.println("CONFIRMACION_APAGADO_" + String(orden));
    }
  }
  // Formato esperado: ACTUALIZAR_ORDENDEVTA_PIEZAS (ej. "ACTUALIZAR_101_14")
  else if (cmd.startsWith("ACTUALIZAR_")) {
    int primerGuion = cmd.indexOf('_');
    int segundoGuion = cmd.lastIndexOf('_');
    int orden = cmd.substring(primerGuion + 1, segundoGuion).toInt();
    int piezas = cmd.substring(segundoGuion + 1).toInt();
    int indiceDestino = buscarDestinoPorOrden(orden);
    if (indiceDestino != -1) {
      mostrarNumero(piezas, indiceDestino);
    }
  }
  // Comando para apagar todos los displays y LEDs
  else if (cmd == "APAGAR_TODO") {
    for (int i = 0; i < NUM_DESTINOS; i++) {
        apagarDestino(i);
    }
  }
}

// Revisa si alguno de los botones ha sido presionado
void revisarBotones() {
  for (int i = 0; i < NUM_DESTINOS; i++) {
    // Solo revisa botones de destinos que están en uso
    if (destinos[i].ordenDeVenta != -1) {
      
      // --- Botón de Confirmación ---
      bool lecturaConf = digitalRead(pinesBotonConfirmacion[i]);
      if (lecturaConf != destinos[i].estadoBotonConfirmacion && millis() - destinos[i].ultimoPulsoConfirmacion > DEBOUNCE_DELAY) {
        if (lecturaConf == LOW) { // Se presionó el botón (flanco de bajada para PULLUP)
          Serial.println("boton_" + String(destinos[i].ordenDeVenta));
          destinos[i].ultimoPulsoConfirmacion = millis();
        }
        destinos[i].estadoBotonConfirmacion = lecturaConf;
      }
      
      // --- Botón de Suma (+) ---
      bool lecturaMas = digitalRead(pinesBotonIncremento[i]);
       if (lecturaMas != destinos[i].estadoBotonMas && millis() - destinos[i].ultimoPulsoMas > DEBOUNCE_DELAY) {
        if (lecturaMas == LOW) {
          Serial.println("+" + String(destinos[i].ordenDeVenta));
          destinos[i].ultimoPulsoMas = millis();
        }
        destinos[i].estadoBotonMas = lecturaMas;
      }

      // --- Botón de Resta (-) ---
      bool lecturaMenos = digitalRead(pinesBotonDecremento[i]);
       if (lecturaMenos != destinos[i].estadoBotonMenos && millis() - destinos[i].ultimoPulsoMenos > DEBOUNCE_DELAY) {
        if (lecturaMenos == LOW) {
          Serial.println("-" + String(destinos[i].ordenDeVenta));
          destinos[i].ultimoPulsoMenos = millis();
        }
        destinos[i].estadoBotonMenos = lecturaMenos;
      }
    }
  }
}

// --- Funciones de Ayuda ---

// Muestra un número en un display específico, alineado a la derecha.
void mostrarNumero(long numero, int indiceModulo) {
  if (indiceModulo < 0 || indiceModulo >= NUM_DESTINOS) return;

  lc.clearDisplay(indiceModulo);
  
  if (numero == 0) {
    lc.setDigit(indiceModulo, 0, 0, false);
    return;
  }
  
  bool negativo = (numero < 0);
  if (negativo) numero = -numero;
  
  int digito = 0;
  // Muestra los dígitos de derecha a izquierda (hasta 8)
  while (numero > 0 && digito < 8) {
    lc.setDigit(indiceModulo, digito, numero % 10, false);
    numero /= 10;
    digito++;
  }
  
  if (negativo) {
    lc.setChar(indiceModulo, digito, '-', false);
  }
}

// Busca un destino que no esté siendo usado
int buscarDestinoLibre() {
  for (int i = 0; i < NUM_DESTINOS; i++) {
    if (destinos[i].ordenDeVenta == -1) {
      return i; // Retorna el índice del primer destino físico libre
    }
  }
  return -1; // No hay destinos libres
}

// Busca un destino por su número de orden de venta
int buscarDestinoPorOrden(int orden) {
  for (int i = 0; i < NUM_DESTINOS; i++) {
    if (destinos[i].ordenDeVenta == orden) {
      return i; // Retorna el índice del destino físico encontrado
    }
  }
  return -1; // No se encontró la orden
}

// Apaga y resetea un destino físico específico
void apagarDestino(int indice) {
  if (indice < 0 || indice >= NUM_DESTINOS) return;
  
  destinos[indice].ordenDeVenta = -1; // Marca el destino como libre
  mostrarNumero(0, indice);
  digitalWrite(pinesLED[indice], LOW);
}