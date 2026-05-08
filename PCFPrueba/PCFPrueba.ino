#include <Wire.h>

void setup() {
  Serial.begin(115200);
  while (!Serial);
  
  // CONFIGURACIÓN CRÍTICA PARA EVITAR CONGELAMIENTO
  // Solo funciona en Arduino Mega/Uno/Nano (Arquitectura AVR)
  Wire.begin();
  
  // Si ocurre un problema, se rinde a los 3000 microsegundos y reinicia el bus
  // true = resetear el bus si hay bloqueo
  #if defined(ARDUINO_ARCH_AVR)
    Wire.setWireTimeout(3000, true); 
  #endif

  Serial.println("\n--- TEST DE DIAGNOSTICO PCF8574 (ANTI-FREEZE) ---");
  Serial.println("1. Verificando conexion I2C...");
  
  byte error, address;
  int nDevices = 0;

  for(address = 1; address < 127; address++ ) {
    Wire.beginTransmission(address);
    error = Wire.endTransmission();

    if (error == 0) {
      Serial.print("SUCCESS: Dispositivo en 0x");
      if (address < 16) Serial.print("0");
      Serial.println(address, HEX);
      nDevices++;
      probarPines(address); // Prueba de parpadeo
      
    } else if (error == 4) {
      Serial.print("ERROR DESCONOCIDO en 0x");
      Serial.println(address, HEX);
    } else if (error == 5) {
      // Este es el error de Timeout que antes congelaba tu placa
      Serial.println("!!! ERROR FATAL: TIMEOUT (Bloqueo de Bus) !!!");
      Serial.println(" CAUSA: SDA o SCL estan en corto a Tierra (GND).");
      Serial.println(" ACCION: Desconecta la placa inmediatamente y revisa soldaduras.");
      return; // Detener el escaneo
    }
  }
  
  if (nDevices == 0) {
    Serial.println("FALLO: No se encontraron dispositivos.");
  } else {
    Serial.println("--- DIAGNOSTICO TERMINADO ---");
  }
}

void loop() {}

void probarPines(byte addr) {
  Serial.println("   -> Probando pines (parpadeo)...");
  
  // Encender
  Wire.beginTransmission(addr);
  Wire.write(0xFF); // VCC
  Wire.endTransmission();
  delay(500);
  
  // Apagar
  Wire.beginTransmission(addr);
  Wire.write(0x00); // GND
  Wire.endTransmission();
  delay(500);
  
  // Restaurar
  Wire.beginTransmission(addr);
  Wire.write(0xFF); 
  Wire.endTransmission();
}