String input = "";
const int NUM_DESTINOS = 10;

// Pines para LEDs y botones de confirmación (existentes)
int pinesLED[NUM_DESTINOS] = {22, 23, 24, 25, 26, 27, 28, 29, 30, 31};
int pinesBotonConfirmacion[NUM_DESTINOS] = {32, 33, 34, 35, 36, 37, 38, 39, 40, 41}; // Renombrado para claridad
bool botonesConfirmacionEstadoAnterior[NUM_DESTINOS] = {false}; // Renombrado para claridad

// --- NUEVOS PINES PARA BOTONES +/- ---
int pinesBotonIncremento[NUM_DESTINOS] = {42, 43, 44, 45, 46, 47, 48, 49, 50, 51};
int pinesBotonDecremento[NUM_DESTINOS] = {A0, A1, A2, A3, A4, A5, A6, A7, A8, A9}; // A0-A9 son D54-D63

// Estados anteriores para debouncing de botones +/-
bool botonesIncrementoEstadoAnterior[NUM_DESTINOS] = {false};
bool botonesDecrementoEstadoAnterior[NUM_DESTINOS] = {false};

void setup() {
  Serial.begin(9600);
  while (!Serial) {
    ; // Espera a que el puerto serial se conecte. Necesario para algunas placas.
  }
  Serial.println("Arduino: Iniciado.");

  for (int i = 0; i < NUM_DESTINOS; i++) {
    pinMode(pinesLED[i], OUTPUT);
    digitalWrite(pinesLED[i], LOW); // Asegura que todos los LEDs estén apagados al inicio

    pinMode(pinesBotonConfirmacion[i], INPUT_PULLUP);
    
    // --- NUEVO: Configuración de pines para botones +/- ---
    pinMode(pinesBotonIncremento[i], INPUT_PULLUP);
    pinMode(pinesBotonDecremento[i], INPUT_PULLUP);
  }
}

void loop() {
  // Leer botones de Confirmación ("boton_X")
  for (int i = 0; i < NUM_DESTINOS; i++) {
    bool estadoActual = digitalRead(pinesBotonConfirmacion[i]) == LOW; // LOW porque es INPUT_PULLUP
    if (estadoActual && !botonesConfirmacionEstadoAnterior[i]) {
      // Botón acaba de ser presionado
      delay(50); // Retardo para anti-rebote (debounce)
      if (digitalRead(pinesBotonConfirmacion[i]) == LOW) { // Confirma que sigue presionado
        Serial.println("boton_" + String(i + 1));
        // Esperar a que el botón sea soltado para evitar múltiples detecciones
        while (digitalRead(pinesBotonConfirmacion[i]) == LOW) {
          delay(10);
        }
      }
    }
    botonesConfirmacionEstadoAnterior[i] = estadoActual; // Actualiza el estado anterior
  }

  // --- NUEVO: Leer botones de Incremento y Decremento ---
  for (int i = 0; i < NUM_DESTINOS; i++) {
    // Botón de Incremento (+)
    bool estadoActualInc = digitalRead(pinesBotonIncremento[i]) == LOW;
    if (estadoActualInc && !botonesIncrementoEstadoAnterior[i]) {
      delay(50); // Anti-rebote
      if (digitalRead(pinesBotonIncremento[i]) == LOW) { // Confirma
        Serial.println("+" + String(i + 1)); // Envía mensaje como "+1", "+2", etc.
        while (digitalRead(pinesBotonIncremento[i]) == LOW) { // Espera a soltar
          delay(10);
        }
      }
    }
    botonesIncrementoEstadoAnterior[i] = estadoActualInc;

    // Botón de Decremento (-)
    bool estadoActualDec = digitalRead(pinesBotonDecremento[i]) == LOW;
    if (estadoActualDec && !botonesDecrementoEstadoAnterior[i]) {
      delay(50); // Anti-rebote
      if (digitalRead(pinesBotonDecremento[i]) == LOW) { // Confirma
        Serial.println("-" + String(i + 1)); // Envía mensaje como "-1", "-2", etc.
        while (digitalRead(pinesBotonDecremento[i]) == LOW) { // Espera a soltar
          delay(10);
        }
      }
    }
    botonesDecrementoEstadoAnterior[i] = estadoActualDec;
  }

  // Leer comandos del puerto serial (código existente para encender/apagar LEDs)
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n') {
      procesarComando(input);
      input = ""; // Limpia el buffer de entrada para el siguiente comando
    } else if (c != '\r') { // Ignora el retorno de carro (CR) si viene del PC
      input += c;
    }
  }
}

void procesarComando(String comando) {
  // Esta función (existente) maneja "ENCENDER_DESTINO_X" y "APAGAR_DESTINO_X"
  // No necesita cambios para los nuevos botones, ya que los mensajes "+X" y "-X"
  // serán procesados directamente por el programa C++.

  if (comando.startsWith("ENCENDER_DESTINO_")) {
    String idxStr = comando.substring(17);  
    int idx = idxStr.toInt();  
    if (idx >= 1 && idx <= NUM_DESTINOS) {
      digitalWrite(pinesLED[idx - 1], HIGH);
      Serial.println("LED_ENCENDIDO_" + String(idx)); 
    } else {
      Serial.print("Arduino: Índice de destino fuera de rango (Encender): ");
      Serial.println(idx);
    }
  } else if (comando.startsWith("APAGAR_DESTINO_")) {
    String idxStr = comando.substring(15);
    int idx = idxStr.toInt();
    if (idx >= 1 && idx <= NUM_DESTINOS) {
      digitalWrite(pinesLED[idx - 1], LOW);
      Serial.println("LED_APAGADO_" + String(idx)); 
    } else {
      Serial.print("Arduino: Índice de destino fuera de rango (Apagar): ");
      Serial.println(idx);
    }
  } else {
    Serial.print("Arduino: Comando desconocido: '"); 
    Serial.print(comando);
    Serial.println("'");
  }
}