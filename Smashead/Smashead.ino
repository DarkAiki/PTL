#include <Wire.h>
#include <TM1637Display.h>

/* * CONFIGURACIÓN DE HARDWARE MULTI-MODULO
 * v5.1 - Corrección de Lógica LED (Active HIGH)
 */

// --- CONFIGURACIÓN DE CANTIDAD DE DESTINOS ---
const int NUM_DESTINOS = 2; 

// Estructura que define un Módulo PTL completo
struct ModuloPTL {
  int id;                  
  int pinClk;              
  int pinDio;              
  byte pcfAddr;            
  
  TM1637Display* displayObj; 
  int cantidad;            
  bool activo;             
  byte estadoPCF;          
};

// --- MAPEO DE HARDWARE ---
ModuloPTL destinos[NUM_DESTINOS] = {
  // { ID, CLK, DIO, DIRECCION_I2C, ... }
  { 1, 26, 27, 0x20, NULL, 0, false, 0xF7 }, // Inicializamos estado en 0xF7 (LED apagado/LOW)
  { 2, 28, 29, 0x21, NULL, 0, false, 0xF7 } 
};

// Máscaras de bits
const byte MASK_BTN_CONFIRM = 0x01; // P0
const byte MASK_BTN_UP      = 0x02; // P1
const byte MASK_BTN_DOWN    = 0x04; // P2
const byte MASK_LED_AVISO   = 0x08; // P3

String inputString = "";

void setup() {
  Serial.begin(9600); 
  inputString.reserve(100);
  Wire.begin();

  // Inicializar cada módulo configurado
  for (int i = 0; i < NUM_DESTINOS; i++) {
    destinos[i].displayObj = new TM1637Display(destinos[i].pinClk, destinos[i].pinDio);
    destinos[i].displayObj->setBrightness(0x0f);
    
    // Inicializar PCF8574
    // IMPORTANTE: Ponemos P0-P2 en HIGH (1) para que funcionen como entradas.
    // Ponemos P3 en LOW (0) para que el LED inicie APAGADO.
    // 1111 0111 = 0xF7
    actualizarPCF(i, 0xF7); 
    
    // Test visual rápido
    uint8_t allOn[] = { 0xff, 0xff, 0xff, 0xff };
    destinos[i].displayObj->setSegments(allOn);
    delay(200);
    mostrarEspera(i);
  }

  Serial.println(F("SYSTEM_READY"));
}

void loop() {
  verificarComandosSeriales();
  
  for (int i = 0; i < NUM_DESTINOS; i++) {
    if (destinos[i].activo) {
      gestionarBotones(i);
    }
  }
}

void actualizarPCF(int idx, byte nuevoEstado) {
  destinos[idx].estadoPCF = nuevoEstado;
  Wire.beginTransmission(destinos[idx].pcfAddr);
  Wire.write(destinos[idx].estadoPCF);
  Wire.endTransmission();
}

// CORRECCIÓN AQUÍ: Invertimos la lógica
void setLed(int idx, bool on) {
  if (on) {
    // Queremos Encender -> Enviamos HIGH (1) al bit P3
    // Usamos OR para poner el bit en 1
    destinos[idx].estadoPCF |= MASK_LED_AVISO; 
  } else {
    // Queremos Apagar -> Enviamos LOW (0) al bit P3
    // Usamos AND con el inverso para poner el bit en 0
    destinos[idx].estadoPCF &= ~MASK_LED_AVISO; 
  }
  actualizarPCF(idx, destinos[idx].estadoPCF);
}

void gestionarBotones(int i) {
  Wire.requestFrom(destinos[i].pcfAddr, (uint8_t)1);
  if (Wire.available()) {
    byte lectura = Wire.read();
    
    // --- BOTÓN CONFIRMAR (P0) ---
    if (!(lectura & MASK_BTN_CONFIRM)) {
      delay(50); 
      Wire.requestFrom(destinos[i].pcfAddr, (uint8_t)1);
      if (!(Wire.read() & MASK_BTN_CONFIRM)) {
        confirmarDestino(i);
        do { Wire.requestFrom(destinos[i].pcfAddr, (uint8_t)1); delay(10); } 
        while (!(Wire.read() & MASK_BTN_CONFIRM));
      }
    }
    
    // --- BOTÓN SUBIR (P1) ---
    else if (!(lectura & MASK_BTN_UP)) {
      delay(150);
      Serial.print(F("+")); Serial.println(destinos[i].id);
      destinos[i].cantidad++;
      destinos[i].displayObj->showNumberDec(destinos[i].cantidad);
      do { Wire.requestFrom(destinos[i].pcfAddr, (uint8_t)1); delay(10); } 
      while (!(Wire.read() & MASK_BTN_UP));
    }
    
    // --- BOTÓN BAJAR (P2) ---
    else if (!(lectura & MASK_BTN_DOWN)) {
      delay(150);
      Serial.print(F("-")); Serial.println(destinos[i].id);
      if (destinos[i].cantidad > 0) {
          destinos[i].cantidad--;
          destinos[i].displayObj->showNumberDec(destinos[i].cantidad);
      }
      do { Wire.requestFrom(destinos[i].pcfAddr, (uint8_t)1); delay(10); } 
      while (!(Wire.read() & MASK_BTN_DOWN));
    }
  }
}

void verificarComandosSeriales() {
  while (Serial.available()) {
    char inChar = (char)Serial.read();
    if (inChar == '\n') {
      procesarComando(inputString);
      inputString = "";
    } else {
      inputString += inChar;
    }
  }
}

void procesarComando(String cmd) {
  cmd.trim();
  int firstUnderscore = cmd.indexOf('_');
  int lastUnderscore = cmd.lastIndexOf('_');
  
  if (firstUnderscore != -1) {
    String accion = cmd.substring(0, firstUnderscore);
    int targetId; 
    int qty = 0;

    if (lastUnderscore != firstUnderscore) {
      targetId = cmd.substring(firstUnderscore + 1, lastUnderscore).toInt();
      qty = cmd.substring(lastUnderscore + 1).toInt();
    } else {
      targetId = cmd.substring(firstUnderscore + 1).toInt();
    }
    
    int idx = -1;
    for(int i=0; i<NUM_DESTINOS; i++) {
      if(destinos[i].id == targetId) {
        idx = i;
        break;
      }
    }

    if (idx != -1) {
      if (accion == "ENCENDER" || accion == "ACTUALIZAR") {
        destinos[idx].cantidad = qty;
        destinos[idx].activo = true;
        destinos[idx].displayObj->showNumberDec(qty);
        setLed(idx, true); // <--- Esto ahora mandará HIGH para encender
      } 
      else if (accion == "APAGAR" || accion == "APAGAR_DESTINO") { 
        resetModulo(idx);
      }
    } else {
       if (cmd == "APAGAR_TODO") {
         for(int i=0; i<NUM_DESTINOS; i++) resetModulo(i);
       }
    }
  } else if (cmd == "APAGAR_TODO") {
      for(int i=0; i<NUM_DESTINOS; i++) resetModulo(i);
  }
}

void confirmarDestino(int idx) {
  Serial.print(F("boton_"));
  Serial.println(destinos[idx].id);
  
  uint8_t done[] = { 
    SEG_B | SEG_C | SEG_D | SEG_E | SEG_G,          // d
    SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F,  // O
    SEG_C | SEG_E | SEG_G,                          // n
    SEG_A | SEG_D | SEG_E | SEG_F | SEG_G           // E
  };
  destinos[idx].displayObj->setSegments(done);
  delay(500);
  resetModulo(idx);
}

void resetModulo(int idx) {
  destinos[idx].activo = false;
  destinos[idx].cantidad = 0;
  setLed(idx, false); // <--- Esto ahora mandará LOW para apagar
  mostrarEspera(idx);
}

void mostrarEspera(int idx) {
  uint8_t seg[] = { SEG_G, SEG_G, SEG_G, SEG_G }; // ----
  destinos[idx].displayObj->setSegments(seg);
}