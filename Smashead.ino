#include <LedControl.h> // Incluye la librería para el display MAX7219

// Configuración de pines para el display MAX7219
// Puedes usar cualquier pin digital disponible. Aquí usamos los que liberamos.
#define DIN_PIN 42 // Pin DIN del display conectado a D42 del Arduino
#define CLK_PIN 43 // Pin CLK del display conectado a D43 del Arduino
#define CS_PIN 44  // Pin CS (LOAD) del display conectado a D44 del Arduino

// Crea una instancia del objeto LedControl
// Los parámetros son: (PIN_DIN, PIN_CLK, PIN_CS, número_de_modulos_MAX7219)
// Asumimos 1 módulo de 7 segmentos/matriz de LEDs
LedControl lc = LedControl(DIN_PIN, CLK_PIN, CS_PIN, 1);

String input = ""; // Buffer para la entrada serial
const int NUM_DESTINOS = 5; // LIMITADO A 5 DESTINOS

// Pines para LEDs de destino
int pinesLED[NUM_DESTINOS] = {22, 23, 24, 25, 26};

// Pines para botones de Confirmación (uno por destino)
int pinesBotonConfirmacion[NUM_DESTINOS] = {27, 28, 29, 30, 31};
bool botonesConfirmacionEstadoAnterior[NUM_DESTINOS] = {false};

// Pines para botones de Incremento (+)
int pinesBotonIncremento[NUM_DESTINOS] = {32, 33, 34, 35, 36};
bool botonesIncrementoEstadoAnterior[NUM_DESTINOS] = {false};

// Pines para botones de Decremento (-)
int pinesBotonDecremento[NUM_DESTINOS] = {37, 38, 39, 40, 41};
bool botonesDecrementoEstadoAnterior[NUM_DESTINOS] = {false};

// Variable para almacenar la orden de venta que se muestra actualmente
// El display MAX7219 es para números/segmentos, no puede mostrar texto complejo fácilmente.
// Esta variable nos ayudará a saber a qué OV corresponden las piezas mostradas.
int currentDisplayedOV = 0;

void setup() {
  Serial.begin(9600);
  lc.setIntensity(0, 1);
  while (!Serial) {
    ; // Espera a que el puerto serial se conecte.
  }

  // Inicializa el display MAX7219
  // Establece el número de dígitos a escanear (0 a 7 para 1-8 dígitos)
  lc.shutdown(0, false); // Saca el display del modo de ahorro de energía (0 = primer módulo)
  lc.setIntensity(0, 8); // Establece el brillo (0 = primer módulo, 0-15 brillo)
  lc.clearDisplay(0); // Limpia el display (0 = primer módulo)

  // Muestra "0" en todos los dígitos al inicio
  for (int i = 0; i < 8; i++) { // Asumiendo un display de 8 dígitos
    lc.setDigit(0, i, 0, false); // (módulo, dígito, valor, puntoDecimal)
  }

  Serial.println("Arduino: Iniciado.");

  // Configuración de pines para LEDs y botones
  for (int i = 0; i < NUM_DESTINOS; i++) {
    pinMode(pinesLED[i], OUTPUT);
    digitalWrite(pinesLED[i], LOW);

    pinMode(pinesBotonConfirmacion[i], INPUT_PULLUP);
    pinMode(pinesBotonIncremento[i], INPUT_PULLUP);
    pinMode(pinesBotonDecremento[i], INPUT_PULLUP);
  }
}

void loop() {
  // --- Procesamiento de entrada serial del PC ---
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n') {
      procesarComando(input);
      input = "";
    } else if (c != '\r') {
      input += c;
    }
  }

  // --- Leer botones de Confirmación ("boton_X") ---
  for (int i = 0; i < NUM_DESTINOS; i++) {
    bool estadoActual = digitalRead(pinesBotonConfirmacion[i]) == LOW;
    if (estadoActual && !botonesConfirmacionEstadoAnterior[i]) {
      delay(50); // Debounce
      if (digitalRead(pinesBotonConfirmacion[i]) == LOW) {
        Serial.println("boton_" + String(i + 1));
        while (digitalRead(pinesBotonConfirmacion[i]) == LOW) {
          delay(10);
        }
      }
    }
    botonesConfirmacionEstadoAnterior[i] = estadoActual;
  }

  // --- Leer botones de Incremento y Decremento ---
  for (int i = 0; i < NUM_DESTINOS; i++) {
    // Botón de Incremento (+)
    bool estadoActualInc = digitalRead(pinesBotonIncremento[i]) == LOW;
    if (estadoActualInc && !botonesIncrementoEstadoAnterior[i]) {
      delay(50); // Debounce
      if (digitalRead(pinesBotonIncremento[i]) == LOW) {
        Serial.println("+" + String(i + 1));
        while (digitalRead(pinesBotonIncremento[i]) == LOW) {
          delay(10);
        }
      }
    }
    botonesIncrementoEstadoAnterior[i] = estadoActualInc;

    // Botón de Decremento (-)
    bool estadoActualDec = digitalRead(pinesBotonDecremento[i]) == LOW;
    if (estadoActualDec && !botonesDecrementoEstadoAnterior[i]) {
      delay(50); // Debounce
      if (digitalRead(pinesBotonDecremento[i]) == LOW) {
        Serial.println("-" + String(i + 1));
        while (digitalRead(pinesBotonDecremento[i]) == LOW) {
          delay(10);
        }
      }
    }
    botonesDecrementoEstadoAnterior[i] = estadoActualDec;
  }
}

// Función para procesar comandos recibidos del PC
void procesarComando(String comando) {
  comando.trim();

  if (comando.startsWith("ENCENDER_DESTINO_")) {
    int destino = comando.substring(17).toInt();
    if (destino >= 1 && destino <= NUM_DESTINOS) {
      digitalWrite(pinesLED[destino - 1], HIGH);
      Serial.println("LED_ENCENDIDO_" + String(destino));
    } else {
      Serial.print("Arduino: Índice de destino fuera de rango (Encender): ");
      Serial.println(destino);
    }
  } else if (comando.startsWith("APAGAR_DESTINO_")) {
    int destino = comando.substring(15).toInt();
    if (destino >= 1 && destino <= NUM_DESTINOS) {
      digitalWrite(pinesLED[destino - 1], LOW);
      Serial.println("LED_APAGADO_" + String(destino));
    } else {
      Serial.print("Arduino: Índice de destino fuera de rango (Apagar): ");
      Serial.println(destino);
    }
  }
  // --- NUEVOS COMANDOS PARA EL DISPLAY MAX7219 ---
  else if (comando.startsWith("DISPLAY_OV_")) {
    // Formato esperado: "DISPLAY_OV_X_PIEZAS_Y"
    int ovIndex = comando.indexOf("_OV_");
    int piezasIndex = comando.indexOf("_PIEZAS_");

    if (ovIndex != -1 && piezasIndex != -1 && ovIndex < piezasIndex) {
      String ovStr = comando.substring(ovIndex + 4, piezasIndex);
      String piezasStr = comando.substring(piezasIndex + 8);

      int ordenDeVenta = ovStr.toInt();
      int piezas = piezasStr.toInt();

      currentDisplayedOV = ordenDeVenta; // Guarda la OV actual

      // El MAX7219 es ideal para mostrar números. Podemos mostrar las piezas.
      // Si tienes un display de 8 dígitos, el número puede ser bastante grande.
      // La función printNumberFromRight() mostrará el número justificado a la derecha.
      printNumberFromRight(piezas);
      Serial.print("DISPLAY_ACTUALIZADO_OV_");
      Serial.println(ordenDeVenta);
    }
  } else if (comando.startsWith("AJUSTAR_OV_")) {
    // Formato esperado: "AJUSTAR_OV_X_VALOR_Y"
    int ovIndex = comando.indexOf("_OV_");
    int valorIndex = comando.indexOf("_VALOR_");

    if (ovIndex != -1 && valorIndex != -1 && ovIndex < valorIndex) {
      String ovStr = comando.substring(ovIndex + 4, valorIndex);
      String valorStr = comando.substring(valorIndex + 7);

      int ordenDeVenta = ovStr.toInt();
      int nuevoValor = valorStr.toInt();

      // Solo actualiza el display si la OV que se está ajustando es la que se mostró por última vez
      if (ordenDeVenta == currentDisplayedOV) {
          printNumberFromRight(nuevoValor); // Muestra el nuevo valor de piezas
          Serial.print("PIEZAS_AJUSTADAS_OV_");
          Serial.println(ordenDeVenta);
      } else {
          Serial.print("Arduino: Intentando ajustar OV ");
          Serial.print(ordenDeVenta);
          Serial.print(" pero display muestra OV ");
          Serial.println(currentDisplayedOV);
      }
    }
  } else if (comando == "CLEAR_DISPLAY") {
    lc.clearDisplay(0); // Limpia el display
    currentDisplayedOV = 0; // Reinicia la OV mostrada
  } else if (comando == "DISPLAY_MESSAGE_COMPLETADO") {
    lc.clearDisplay(0);
    // Para un MAX7219 con 7 segmentos, puedes mostrar "done" o un patrón.
    // Aquí una forma simple de mostrar "done" si es un display de 4 o más dígitos
    // Los caracteres 'd', 'o', 'n', 'e' pueden ser mapeados a los segmentos.
    // La librería LedControl no tiene setChar directo para todas las letras,
    // pero puedes definir tus propios patrones de segmentos si lo necesitas.
    // Por simplicidad, podemos mostrar "8888" o "0000" o dejarlo en blanco.
    // O un simple "done" si el display es de 4 dígitos:
    lc.setChar(0, 3, 'd', false); // d
    lc.setChar(0, 2, 'o', false); // o
    lc.setChar(0, 1, 'n', false); // n
    lc.setChar(0, 0, 'e', false); // e
    currentDisplayedOV = 0;
  }
  // --- FIN NUEVOS COMANDOS PARA EL DISPLAY MAX7219 ---
  else {
    Serial.print("Arduino: Comando desconocido: '");
    Serial.print(comando);
    Serial.println("'");
  }
}

// Función auxiliar para imprimir un número justificado a la derecha en el MAX7219
void printNumberFromRight(long number) {
  lc.clearDisplay(0); // Limpia el display antes de imprimir
  if (number == 0) {
    lc.setDigit(0, 0, 0, false); // Muestra 0 en el dígito más a la derecha
    return;
  }

  boolean negative = false;
  if (number < 0) {
    negative = true;
    number = -number;
  }

  int digit = 0;
  while (number > 0 && digit < 8) { // Asume hasta 8 dígitos
    lc.setDigit(0, digit, number % 10, false);
    number /= 10;
    digit++;
  }
  if (negative) {
    lc.setChar(0, digit, '-', false); // Muestra el signo negativo
  }
}