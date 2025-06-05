#include <LedControl.h> // Incluye la librería para el display MAX7219

// Configuración de pines para el display MAX7219
#define DIN_PIN 42 // Pin DIN del primer display conectado a D42 del Arduino
#define CLK_PIN 43 // Pin CLK de TODOS los displays conectado a D43 del Arduino
#define CS_PIN 44  // Pin CS (LOAD) de TODOS los displays conectado a D44 del Arduino

// Número de módulos MAX7219 encadenados = NUM_DESTINOS
const int NUM_MAX7219_MODULES = 5;

// Crea una instancia del objeto LedControl
LedControl lc = LedControl(DIN_PIN, CLK_PIN, CS_PIN, NUM_MAX7219_MODULES);

String input = ""; // Buffer para la entrada serial
const int NUM_DESTINOS = 5; // Mantenemos 5 destinos en el código

// Pines para LEDs de destino (originales 5 pines)
int pinesLED[NUM_DESTINOS] = {22, 23, 24, 25, 26};

// Pines para botones de Confirmación (originales 5 pines)
int pinesBotonConfirmacion[NUM_DESTINOS] = {27, 28, 29, 30, 31};
bool botonesConfirmacionEstadoAnterior[NUM_DESTINOS] = {false};

// Pines para botones de Incremento (+) (originales 5 pines)
int pinesBotonIncremento[NUM_DESTINOS] = {32, 33, 34, 35, 36};
bool botonesIncrementoEstadoAnterior[NUM_DESTINOS] = {false};

// Pines para botones de Decremento (-) (originales 5 pines)
int pinesBotonDecremento[NUM_DESTINOS] = {37, 38, 39, 40, 41};
bool botonesDecrementoEstadoAnterior[NUM_DESTINOS] = {false};

// Arrays para almacenar las piezas y OVs para cada destino
// Cada índice corresponde a un destino (0 para Destino 1, 1 para Destino 2, etc.)
int piezasPorDestino[NUM_DESTINOS];
int ovPorDestino[NUM_DESTINOS];

// --- Variables para debounce no bloqueante ---
unsigned long lastDebounceTime[NUM_DESTINOS * 3]; // Una para cada tipo de botón por destino
unsigned long debounceDelay = 50; // milisegundos

void setup() {
  Serial.begin(9600);
  while (!Serial) {
    ; // Espera a que el puerto serial se conecte.
  }

  // Inicializa cada display MAX7219
  for (int i = 0; i < NUM_MAX7219_MODULES; i++) {
    lc.shutdown(i, false); // Saca el display del modo de ahorro de energía
    lc.setIntensity(i, 1); // Establece el brillo
    lc.clearDisplay(i);    // Limpia el display
    // Muestra 0 en todos los dígitos al inicio de cada display
    for (int j = 0; j < 8; j++) {
      lc.setDigit(i, j, 0, false);
    }
  }

  // Inicializa las piezas y OVs de todos los destinos a 0
  for (int i = 0; i < NUM_DESTINOS; i++) {
    piezasPorDestino[i] = 0;
    ovPorDestino[i] = 0;
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
    bool reading = digitalRead(pinesBotonConfirmacion[i]);

    if (reading != botonesConfirmacionEstadoAnterior[i]) {
      lastDebounceTime[i] = millis();
    }

    if ((millis() - lastDebounceTime[i]) > debounceDelay) {
      if (reading == LOW && !botonesConfirmacionEstadoAnterior[i]) {
        Serial.println("boton_" + String(i + 1));
      }
    }
    botonesConfirmacionEstadoAnterior[i] = reading;
  }

  // --- Leer botones de Incremento y Decremento ---
  for (int i = 0; i < NUM_DESTINOS; i++) {
    // Botón de Incremento (+)
    bool readingInc = digitalRead(pinesBotonIncremento[i]);
    int indexInc = NUM_DESTINOS + i;

    if (readingInc != botonesIncrementoEstadoAnterior[i]) {
      lastDebounceTime[indexInc] = millis();
    }

    if ((millis() - lastDebounceTime[indexInc]) > debounceDelay) {
      if (readingInc == LOW && !botonesIncrementoEstadoAnterior[i]) {
        Serial.println("+" + String(i + 1));
      }
    }
    botonesIncrementoEstadoAnterior[i] = readingInc;

    // Botón de Decremento (-)
    bool readingDec = digitalRead(pinesBotonDecremento[i]);
    int indexDec = (NUM_DESTINOS * 2) + i;

    if (readingDec != botonesDecrementoEstadoAnterior[i]) {
      lastDebounceTime[indexDec] = millis();
    }

    if ((millis() - lastDebounceTime[indexDec]) > debounceDelay) {
      if (readingDec == LOW && !botonesDecrementoEstadoAnterior[i]) {
        Serial.println("-" + String(i + 1));
      }
    }
    botonesDecrementoEstadoAnterior[i] = readingDec;
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
      // Cuando se enciende un LED, mostramos las piezas de ese destino en su display
      displayDestinoInfo(destino, piezasPorDestino[destino - 1]);
    } else {
      Serial.print("Arduino: Índice de destino fuera de rango (Encender): ");
      Serial.println(destino);
    }
  } else if (comando.startsWith("APAGAR_DESTINO_")) {
    int destino = comando.substring(15).toInt();
    if (destino >= 1 && destino <= NUM_DESTINOS) {
      digitalWrite(pinesLED[destino - 1], LOW);
      Serial.println("LED_APAGADO_" + String(destino));
      // Cuando se apaga el LED, limpiamos el display de ese destino
      if (destino >= 1 && destino <= NUM_MAX7219_MODULES) { // Asegura que el índice del módulo es válido
        lc.clearDisplay(destino - 1);
        printNumberFromRight(0, destino - 1); // Opcional: muestra 0 en el display al apagar LED
      }
    } else {
      Serial.print("Arduino: Índice de destino fuera de rango (Apagar): ");
      Serial.println(destino);
    }
  }
  // Comando para asignar OV y piezas a un destino específico
  else if (comando.startsWith("ASIGNAR_DESTINO_")) {
    // Formato esperado: "ASIGNAR_DESTINO_X_OV_Y_PIEZAS_Z"
    int destIndex = comando.indexOf("_DESTINO_");
    int ovIndex = comando.indexOf("_OV_");
    int piezasIndex = comando.indexOf("_PIEZAS_");

    if (destIndex != -1 && ovIndex != -1 && piezasIndex != -1 &&
        destIndex < ovIndex && ovIndex < piezasIndex) {
      String destStr = comando.substring(destIndex + 9, ovIndex);
      String ovStr = comando.substring(ovIndex + 4, piezasIndex);
      String piezasStr = comando.substring(piezasIndex + 8);

      int destino = destStr.toInt();
      int ordenDeVenta = ovStr.toInt();
      int piezas = piezasStr.toInt();

      if (destino >= 1 && destino <= NUM_DESTINOS) {
        piezasPorDestino[destino - 1] = piezas;
        ovPorDestino[destino - 1] = ordenDeVenta;
        Serial.print("Arduino: Destino ");
        Serial.print(destino);
        Serial.print(" asignado con OV ");
        Serial.print(ordenDeVenta);
        Serial.print(" y ");
        Serial.print(piezas);
        Serial.println(" piezas.");
        // Si el LED de este destino está encendido, actualiza su display
        if (digitalRead(pinesLED[destino - 1]) == HIGH) {
            displayDestinoInfo(destino, piezas);
        }
      } else {
        Serial.print("Arduino: Índice de destino fuera de rango (Asignar): ");
        Serial.println(destino);
      }
    }
  }
  // Comando para ajustar las piezas de un destino específico
  else if (comando.startsWith("AJUSTAR_DESTINO_")) {
    // Formato esperado: "AJUSTAR_DESTINO_X_VALOR_Y"
    int destIndex = comando.indexOf("_DESTINO_");
    int valorIndex = comando.indexOf("_VALOR_");

    if (destIndex != -1 && valorIndex != -1 && destIndex < valorIndex) {
      String destStr = comando.substring(destIndex + 9, valorIndex);
      String valorStr = comando.substring(valorIndex + 7);

      int destino = destStr.toInt();
      int nuevoValor = valorStr.toInt();

      if (destino >= 1 && destino <= NUM_DESTINOS) {
        piezasPorDestino[destino - 1] = nuevoValor;
        Serial.print("Arduino: Piezas de destino ");
        Serial.print(destino);
        Serial.print(" ajustadas a ");
        Serial.println(nuevoValor);

        // Si el LED de este destino está encendido, actualiza su display
        if (digitalRead(pinesLED[destino - 1]) == HIGH) {
          displayDestinoInfo(destino, nuevoValor);
        }
      } else {
        Serial.print("Arduino: Índice de destino fuera de rango (Ajustar): ");
        Serial.println(destino);
      }
    }
  }
  // Comando para limpiar TODOS los displays
  else if (comando == "CLEAR_ALL_DISPLAYS") { // Nuevo comando para limpiar todos
    for (int i = 0; i < NUM_MAX7219_MODULES; i++) {
      lc.clearDisplay(i);
      printNumberFromRight(0, i); // Muestra 0 en todos al limpiar
    }
    // Reinicializar los datos de piezas y OVs (opcional)
    for (int i = 0; i < NUM_DESTINOS; i++) {
        piezasPorDestino[i] = 0;
        ovPorDestino[i] = 0;
    }
    Serial.println("Arduino: Todos los displays y datos de destino limpiados.");
  }
  // Comando para un mensaje de completado general (opcional, para todos los displays)
  else if (comando == "DISPLAY_ALL_COMPLETED") { // Nuevo comando para mostrar mensaje en todos
    for (int i = 0; i < NUM_MAX7219_MODULES; i++) {
      lc.clearDisplay(i);
      // o dejarlo en blanco o 0. Aquí lo mostraremos en todos.
      lc.setChar(i, 3, 'd', false);
      lc.setChar(i, 2, 'o', false);
      lc.setChar(i, 1, 'n', false);
      lc.setChar(i, 0, 'e', false);
    }
    Serial.println("Arduino: Mensaje 'Completado' en todos los displays.");
  }
  else {
    Serial.print("Arduino: Comando desconocido: '");
    Serial.print(comando);
    Serial.println("'");
  }
}

// Función auxiliar para imprimir un número justificado a la derecha en un display MAX7219 específico
// Ahora toma el módulo (index) como parámetro
void printNumberFromRight(long number, int moduleIndex) {
  if (moduleIndex < 0 || moduleIndex >= NUM_MAX7219_MODULES) return; // Validación de índice

  lc.clearDisplay(moduleIndex); // Limpia solo el display del módulo especificado
  if (number == 0) {
    lc.setDigit(moduleIndex, 0, 0, false); // Muestra 0 en el dígito más a la derecha
    return;
  }

  boolean negative = false;
  if (number < 0) {
    negative = true;
    number = -number;
  }

  int digit = 0;
  while (number > 0 && digit < 8) { // Asume hasta 8 dígitos
    lc.setDigit(moduleIndex, digit, number % 10, false);
    number /= 10;
    digit++;
  }
  if (negative) {
    if (digit < 8) {
        lc.setChar(moduleIndex, digit, '-', false); // Muestra el signo negativo
    }
  }
}

// NUEVA FUNCIÓN: Muestra las piezas de un destino específico en SU display
void displayDestinoInfo(int destino, int piezas) {
    if (destino >= 1 && destino <= NUM_DESTINOS && (destino - 1) < NUM_MAX7219_MODULES) {
        printNumberFromRight(piezas, destino - 1); // Muestra las piezas en el display correspondiente
        Serial.print("Arduino: Display de Destino ");
        Serial.print(destino);
        Serial.print(" mostrando piezas: ");
        Serial.println(piezas);
    } else {
        Serial.print("Arduino: Intento de mostrar destino ");
        Serial.print(destino);
        Serial.println(" fuera de rango o sin display asignado.");
    }
}