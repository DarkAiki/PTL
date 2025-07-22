#include <LedControl.h> // Incluye la librería para el display MAX7219

// --- CONFIGURACIÓN DE HARDWARE ---
#define DIN_PIN 48
#define CLK_PIN 47
#define CS_PIN  46
const int NUM_DESTINOS = 4;
LedControl lc = LedControl(DIN_PIN, CLK_PIN, CS_PIN, NUM_DESTINOS);

// --- MAPEO DE PINES ---
int pinesLED[NUM_DESTINOS] = {22, 23, 24, 25};
int pinesBotonConfirmacion[NUM_DESTINOS] = {28, 29, 31, 32};
int pinesBotonIncremento[NUM_DESTINOS] = {34, 35, 36, 37};
int pinesBotonDecremento[NUM_DESTINOS] = {40, 41, 42, 43};

// --- ESTRUCTURA DE CONTROL ---
struct Destino {
  int ordenDeVenta;
  bool estadoBotonConfirmacion;
  unsigned long ultimoPulsoConfirmacion;
  bool estadoBotonMas;
  unsigned long ultimoPulsoMas;
  bool estadoBotonMenos;
  unsigned long ultimoPulsoMenos;
};

Destino destinos[NUM_DESTINOS];
String comandoSerial = "";
const long DEBOUNCE_DELAY = 50; // Aumentado ligeramente para mayor estabilidad

void setup() {
  Serial.begin(115200);
  comandoSerial.reserve(200);

  for (int i = 0; i < NUM_DESTINOS; i++) {
    lc.shutdown(i, false);
    lc.setIntensity(i, 2);
    lc.clearDisplay(i);
    pinMode(pinesLED[i], OUTPUT);
    digitalWrite(pinesLED[i], LOW);
    pinMode(pinesBotonConfirmacion[i], INPUT_PULLUP);
    pinMode(pinesBotonIncremento[i], INPUT_PULLUP);
    pinMode(pinesBotonDecremento[i], INPUT_PULLUP);
    destinos[i].ordenDeVenta = -1;
    destinos[i].estadoBotonConfirmacion = HIGH;
    destinos[i].estadoBotonMas = HIGH;
    destinos[i].estadoBotonMenos = HIGH;
  }
  
  delay(1000);
  Serial.println("Arduino: Iniciado.");
}

void loop() {
  if (Serial.available()) {
    char c = Serial.read();
    if (c == '\n') {
      procesarComando(comandoSerial);
      comandoSerial = "";
    } else {
      comandoSerial += c;
    }
  }
  revisarBotones();
}

void procesarComando(String cmd) {
  cmd.trim();
  if (cmd.length() == 0) return;

  // --- INICIO DE LA CORRECCIÓN ---
  if (cmd.startsWith("ENCENDER_")) {
    int primerGuion = cmd.indexOf('_');
    int segundoGuion = cmd.lastIndexOf('_');
    int orden = cmd.substring(primerGuion + 1, segundoGuion).toInt();
    int piezas = cmd.substring(segundoGuion + 1).toInt();
    
    // El número de la orden (1-4) determina directamente el display.
    // Convertimos la orden (1-based) a un índice de array (0-based).
    int indiceDestino = orden - 1;

    // Verificamos que el índice sea válido para nuestro hardware (0 a 3)
    if (indiceDestino >= 0 && indiceDestino < NUM_DESTINOS) {
      // Asignamos la O.V. al destino físico correspondiente
      destinos[indiceDestino].ordenDeVenta = orden; 
      mostrarNumero(piezas, indiceDestino);
      digitalWrite(pinesLED[indiceDestino], HIGH);
      Serial.println("CONFIRMACION_ENCENDIDO_" + String(orden));
    }
  } 
  // --- FIN DE LA CORRECCIÓN ---
  
  else if (cmd.startsWith("APAGAR_")) {
    int orden = cmd.substring(7).toInt();
    // La función buscarDestinoPorOrden sigue siendo correcta y necesaria aquí
    int indiceDestino = buscarDestinoPorOrden(orden);
    if (indiceDestino != -1) {
      apagarDestino(indiceDestino);
      Serial.println("CONFIRMACION_APAGADO_" + String(orden));
    }
  }
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
  else if (cmd == "APAGAR_TODO") {
    for (int i = 0; i < NUM_DESTINOS; i++) {
      apagarDestino(i);
    }
  }
}

void revisarBotones() {
  for (int i = 0; i < NUM_DESTINOS; i++) {
    if (destinos[i].ordenDeVenta != -1) {
      bool lecturaConf = digitalRead(pinesBotonConfirmacion[i]);
      if (lecturaConf != destinos[i].estadoBotonConfirmacion && millis() - destinos[i].ultimoPulsoConfirmacion > DEBOUNCE_DELAY) {
        if (lecturaConf == LOW) {
          Serial.println("boton_" + String(destinos[i].ordenDeVenta));
          destinos[i].ultimoPulsoConfirmacion = millis();
        }
        destinos[i].estadoBotonConfirmacion = lecturaConf;
      }
      
      bool lecturaMas = digitalRead(pinesBotonIncremento[i]);
      if (lecturaMas != destinos[i].estadoBotonMas && millis() - destinos[i].ultimoPulsoMas > DEBOUNCE_DELAY) {
        if (lecturaMas == LOW) {
          Serial.println("+" + String(destinos[i].ordenDeVenta));
          destinos[i].ultimoPulsoMas = millis();
        }
        destinos[i].estadoBotonMas = lecturaMas;
      }

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
  while (numero > 0 && digito < 8) {
    lc.setDigit(indiceModulo, digito, numero % 10, false);
    numero /= 10;
    digito++;
  }
  if (negativo) {
    lc.setChar(indiceModulo, digito, '-', false);
  }
}

// La función buscarDestinoLibre ya no es necesaria para ENCENDER,
// pero la dejamos por si se usa en otro lado o para futuras expansiones.
int buscarDestinoLibre() {
  for (int i = 0; i < NUM_DESTINOS; i++) {
    if (destinos[i].ordenDeVenta == -1) {
      return i;
    }
  }
  return -1;
}

int buscarDestinoPorOrden(int orden) {
  for (int i = 0; i < NUM_DESTINOS; i++) {
    if (destinos[i].ordenDeVenta == orden) {
      return i;
    }
  }
  return -1;
}

void apagarDestino(int indice) {
  if (indice < 0 || indice >= NUM_DESTINOS) return;
  destinos[indice].ordenDeVenta = -1;
  lc.clearDisplay(indice);
  digitalWrite(pinesLED[indice], LOW);
}