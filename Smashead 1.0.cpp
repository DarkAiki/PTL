#include <iostream>
#include <map>
#include <set>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <algorithm>  // Para trim
#include <windows.h>  // Para comunicación serial en Windows
#include <queue>      // Para almacenar los mensajes recibidos del Arduino
#include <limits>     // Para std::numeric_limits
#include <cctype>     // Para isprint
#include <string>     // Para usar std::string
#include <vector>     // Para usar std::vector
#include <thread>     // Para std::this_thread::sleep_for
#include <chrono>     // Para std::chrono::milliseconds, std::chrono::seconds
#include <mutex>      // Para std::mutex y std::lock_guard (proteger acceso al puerto serial)
#include <stdexcept>  // Para manejar excepciones como std::invalid_argument, std::out_of_range

// --- Includes específicos para Windows (para la clase SerialPort) ---
// Esto se define antes del include de SerialPort.h para que la implementación de Windows sea elegida.
#ifdef _WIN32
    #include <windows.h> // Necesario para las funciones de la API de Windows para puertos seriales
#endif
// ----------------------------------------

#include "SerialPortH.h" // Incluye la clase de comunicación serial que creamos

// --- Estructura para almacenar la información de cada destino ---
// Esto ayuda a organizar los datos de cada uno de tus 5 destinos
struct DestinoInfo {
    int id;               // ID del destino (1 a 5)
    int ordenDeVenta;     // Número de Orden de Venta (OV) asignada a este destino
    int piezasRequeridas; // Cantidad total de piezas que este destino necesita
    int piezasActuales;   // Cantidad de piezas recolectadas hasta ahora para este destino
    bool ledEncendido;    // Estado actual del LED físico de este destino

    // Constructor: Inicializa un destino con un ID dado y valores por defecto
    DestinoInfo(int _id = 0)
        : id(_id), ordenDeVenta(0), piezasRequeridas(0), piezasActuales(0), ledEncendido(false) {}
};

// --- Variables globales de la aplicación ---
SerialPort arduinoPort; // Objeto que gestiona la comunicación con el Arduino
// Vector para almacenar la información de los 5 destinos.
// std::vector es una forma flexible de manejar colecciones de objetos.
std::vector<DestinoInfo> destinos;
std::string serialReadBuffer; // Buffer para acumular los datos que se leen del puerto serial
std::mutex serialMutex;       // Mutex para proteger el acceso al puerto serial al escribir.
                              // Esto es crucial si tuvieras múltiples hilos intentando escribir al mismo tiempo.

// --- Función para enviar comandos al Arduino de forma segura ---
// Utiliza un mutex para asegurar que solo un hilo escriba a la vez, si fuera necesario.
void sendCommandToArduino(const std::string& command) {
    std::lock_guard<std::mutex> lock(serialMutex); // Bloquea el mutex al entrar, lo libera al salir
    if (arduinoPort.isOpen()) {
        arduinoPort.write(command.c_str(), command.length());
    } else {
        std::cerr << "Error: Puerto serial no abierto. No se pudo enviar el comando: " << command;
    }
}

// --- Lógica para manejar las respuestas del Arduino (datos leídos del puerto serial) ---
void handleArduinoResponse(const std::string& response) {
    // std::cout << "DEBUG: Respuesta Arduino RAW: '" << response << "'" << std::endl; // Para depuración

    // Elimina el salto de línea para facilitar el procesamiento del string
    std::string trimmedResponse = response;
    // La función pop_back() es de C++11 y elimina el último carácter si no está vacío
    if (!trimmedResponse.empty() && trimmedResponse.back() == '\n') {
        trimmedResponse.pop_back();
    }

    // Comandos de Incremento (+) o Decremento (-)
    if (trimmedResponse.rfind("+", 0) == 0 || trimmedResponse.rfind("-", 0) == 0) {
        try {
            int destinoId = std::stoi(trimmedResponse.substr(1)); // Extrae el número de destino (ej. de "+1" a 1)

            // Validar que el ID del destino esté en el rango correcto
            if (destinoId >= 1 && destinoId <= destinos.size()) {
                // Actualizar las piezas actuales en nuestra lógica C++
                if (trimmedResponse.rfind("+", 0) == 0) {
                    destinos[destinoId - 1].piezasActuales++;
                } else { // Es un comando de decremento
                    if (destinos[destinoId - 1].piezasActuales > 0) { // No permitir bajar de 0
                        destinos[destinoId - 1].piezasActuales--;
                    }
                }

                std::cout << "Destino " << destinoId << ": Piezas actuales -> " << destinos[destinoId - 1].piezasActuales << std::endl;

                // Enviar el comando AJUSTAR_DESTINO al Arduino para que actualice el display de ESE destino
                std::string cmd = "AJUSTAR_DESTINO_" + std::to_string(destinoId) +
                                  "_VALOR_" + std::to_string(destinos[destinoId - 1].piezasActuales) + "\n";
                sendCommandToArduino(cmd);

                // --- Lógica de Negocio: Verificar si el destino está completo ---
                // Si las piezas actuales igualan o superan las requeridas
                if (destinos[destinoId - 1].ledEncendido && // Solo si el LED está encendido
                    destinos[destinoId - 1].piezasActuales >= destinos[destinoId - 1].piezasRequeridas &&
                    destinos[destinoId - 1].piezasRequeridas > 0) // Evitar "completado" si no hay piezas requeridas
                {
                    std::cout << "¡Destino " << destinoId << " completado! Piezas: "
                              << destinos[destinoId - 1].piezasActuales << "/"
                              << destinos[destinoId - 1].piezasRequeridas << std::endl;
                    // Desactivar el LED y el display de este destino
                    std::string cmdApagar = "APAGAR_DESTINO_" + std::to_string(destinoId) + "\n";
                    sendCommandToArduino(cmdApagar);
                    destinos[destinoId - 1].ledEncendido = false; // Actualizar estado interno en C++

                    // Aquí puedes añadir más lógica: notificar al usuario, guardar en una base de datos,
                    // pasar al siguiente destino, etc.
                }
            }
        } catch (const std::invalid_argument& e) {
            std::cerr << "Error de conversion en respuesta '" << trimmedResponse << "': " << e.what() << std::endl;
        } catch (const std::out_of_range& e) {
            std::cerr << "Valor fuera de rango en respuesta '" << trimmedResponse << "': " << e.what() << std::endl;
        }
    }
    // Puedes añadir más lógica aquí para otros tipos de respuestas del Arduino,
    // como "boton_X" (si el botón de confirmación necesita una acción en C++),
    // o mensajes de depuración del Arduino como "LED_ENCENDIDO_X".
    else if (trimmedResponse.rfind("boton_", 0) == 0) {
        std::cout << "Boton de confirmacion: " << trimmedResponse << std::endl;
        // Si al presionar el botón de confirmación en el Arduino quieres "finalizar" el destino
        // sin importar las piezas, puedes llamar a finalizarDestino() aquí.
        // int destinoId = std::stoi(trimmedResponse.substr(6));
        // finalizarDestino(destinoId);
    }
    else {
        // Imprime otras respuestas del Arduino (mensajes de depuración, confirmaciones)
        std::cout << "Arduino Info: " << trimmedResponse << std::endl;
    }
}

// --- FUNCIONES DE ALTO NIVEL PARA LA LÓGICA DE TU APLICACIÓN ---
// Estas funciones simulan las acciones que tu aplicación C++ realizaría.

// Inicializa o reasigna una orden de venta a un destino específico
void iniciarOrdenVentaParaDestino(int destinoId, int ov, int totalPiezas) {
    if (destinoId < 1 || destinoId > destinos.size()) {
        std::cerr << "Error: ID de destino invalido: " << destinoId << std::endl;
        return;
    }

    // Actualizar el estado interno de tu aplicación C++
    destinos[destinoId - 1].ordenDeVenta = ov;
    destinos[destinoId - 1].piezasRequeridas = totalPiezas;
    destinos[destinoId - 1].piezasActuales = 0; // Al iniciar una OV, las piezas actuales suelen ser 0

    std::cout << "Preparando Destino " << destinoId << " con OV: " << ov
              << ", Piezas requeridas: " << totalPiezas << std::endl;

    // Enviar el comando ASIGNAR_DESTINO al Arduino.
    // El Arduino guarda estos datos y los usa cuando el LED se enciende.
    std::string comandoAsignar = "ASIGNAR_DESTINO_" + std::to_string(destinoId) +
                                 "_OV_" + std::to_string(ov) +
                                 "_PIEZAS_" + std::to_string(totalPiezas) + "\n";
    sendCommandToArduino(comandoAsignar);

    // Activar el destino (encender LED y display con las piezas iniciales)
    std::string comandoEncender = "ENCENDER_DESTINO_" + std::to_string(destinoId) + "\n";
    sendCommandToArduino(comandoEncender);
    destinos[destinoId - 1].ledEncendido = true; // Actualizar el estado interno en C++
}

// Simula la finalización de un destino (ej. después de recolectar todas las piezas o manualmente)
void finalizarDestino(int destinoId) {
    if (destinoId < 1 || destinoId > destinos.size()) {
        std::cerr << "Error: ID de destino invalido: " << destinoId << std::endl;
        return;
    }
    
    std::cout << "Finalizando Destino " << destinoId << std::endl;

    // Apagar el LED y limpiar el display de este destino
    std::string comandoApagar = "APAGAR_DESTINO_" + std::to_string(destinoId) + "\n";
    sendCommandToArduino(comandoApagar);
    destinos[destinoId - 1].ledEncendido = false; // Actualizar estado interno en C++

    // Opcional: Reiniciar las piezas y OV para este destino si se considera "completado y listo para otra tarea"
    destinos[destinoId - 1].ordenDeVenta = 0;
    destinos[destinoId - 1].piezasRequeridas = 0;
    destinos[destinoId - 1].piezasActuales = 0;
}

// Función para limpiar todos los displays y resetear los datos internos de los destinos
void clearAllDisplays() {
    std::cout << "Limpiando todos los displays y reseteando destinos..." << std::endl;
    sendCommandToArduino("CLEAR_ALL_DISPLAYS\n");
    // Reiniciar los datos internos de todos los destinos
    for (auto& dest : destinos) { // Itera sobre cada DestinoInfo en el vector
        dest.ordenDeVenta = 0;
        dest.piezasRequeridas = 0;
        dest.piezasActuales = 0;
        dest.ledEncendido = false;
    }
}

// Función para mostrar el mensaje de "COMPLETADO" en todos los displays
void displayAllCompleted() {
    std::cout << "Mostrando mensaje 'Completado' en todos los displays..." << std::endl;
    sendCommandToArduino("DISPLAY_ALL_COMPLETED\n");
}


// --- Función Principal del Programa C++ ---
int main() {
    // Inicializar los 5 destinos.
    // Los IDs irán de 1 a 5, correspondientes a los índices 0 a 4 en el vector.
    for (int i = 0; i < 5; ++i) {
        destinos.emplace_back(i + 1);
    }

    // --- Configuración y apertura del puerto serial ---
    // ¡¡¡IMPORTANTE: CAMBIA ESTO AL PUERTO COM CORRECTO DE TU ARDUINO!!!
    // Por ejemplo: "COM3", "COM4", etc.
    const std::string serialPortName = "COM1"; // <<< ¡¡¡MODIFICA ESTO!!!
    const int baudRate = 9600;                 // Debe coincidir con el baud rate en tu sketch de Arduino

    // Intenta abrir el puerto serial. Si falla, el programa termina.
    if (!arduinoPort.open(serialPortName, baudRate)) {
        std::cerr << "ERROR FATAL: No se pudo abrir el puerto serial " << serialPortName << std::endl;
        return 1; // Termina el programa con un código de error
    }

    // Darle tiempo al Arduino para que se inicialice completamente.
    // Esto es buena práctica para evitar que el C++ envíe comandos antes de que el Arduino esté listo.
    std::this_thread::sleep_for(std::chrono::seconds(2));

    std::cout << "\n--- Sistema de Recoleccion - Consola de Control ---\n";
    std::cout << "Comandos disponibles:\n";
    std::cout << "  'i' <id> <ov> <piezas> - Iniciar Orden de Venta para un destino (ej: i 1 12345 50)\n";
    std::cout << "  'f' <id>             - Finalizar/Apagar un destino (ej: f 1)\n";
    std::cout << "  'c'                  - Limpiar todos los displays y resetear datos\n";
    std::cout << "  'd'                  - Mostrar 'Completado' en todos los displays\n";
    std::cout << "  's'                  - Mostrar estado actual de los destinos\n";
    std::cout << "  'q'                  - Salir del programa\n";
    std::cout << "-----------------------------------------------------\n";

    // --- Bucle principal del programa ---
    // Este bucle se encarga de:
    // 1. Leer comandos del usuario desde la consola.
    // 2. Leer respuestas del Arduino desde el puerto serial.
    char commandChar;
    bool running = true;
    while (running) {
        // --- Leer del puerto serial (non-blocking) ---
        char buffer[256];
        int bytesRead = arduinoPort.read(buffer, sizeof(buffer) - 1);
        if (bytesRead > 0) {
            buffer[bytesRead] = '\0'; // Null-terminate el string para que sea válido en C++
            serialReadBuffer += buffer; // Añade los bytes leídos al buffer acumulador

            // Procesar el buffer acumulado por líneas completas.
            // Si hay un '\n', significa que un comando del Arduino ha terminado.
            size_t newlinePos;
            while ((newlinePos = serialReadBuffer.find('\n')) != std::string::npos) {
                std::string line = serialReadBuffer.substr(0, newlinePos + 1); // Extrae la línea completa
                handleArduinoResponse(line); // Procesa la línea (la respuesta del Arduino)
                serialReadBuffer.erase(0, newlinePos + 1); // Elimina la línea procesada del buffer
            }
        }

        // --- Leer comandos del usuario desde la consola (no bloqueante) ---
        // Verificamos si hay algo en el buffer de entrada estándar antes de intentar leer
        if (std::cin.rdbuf()->in_avail() > 0) {
            std::string lineInput;
            std::getline(std::cin, lineInput); // Lee toda la línea de entrada del usuario
            std::istringstream iss(lineInput); // Para parsear la línea por palabras
            
            iss >> commandChar; // Lee el primer carácter como el comando

            if (commandChar == 'i') { // Iniciar Orden de Venta
                int id, ov, piezas;
                if (iss >> id >> ov >> piezas) {
                    iniciarOrdenVentaParaDestino(id, ov, piezas);
                } else {
                    std::cout << "Formato incorrecto. Uso: i <id> <ov> <piezas>\n";
                }
            } else if (commandChar == 'f') { // Finalizar Destino
                int id;
                if (iss >> id) {
                    finalizarDestino(id);
                } else {
                    std::cout << "Formato incorrecto. Uso: f <id>\n";
                }
            } else if (commandChar == 'c') { // Limpiar todo
                clearAllDisplays();
            } else if (commandChar == 'd') { // Mostrar completado
                displayAllCompleted();
            } else if (commandChar == 's') { // Mostrar estado actual
                std::cout << "\n--- Estado Actual de Destinos ---\n";
                for (const auto& dest : destinos) {
                    std::cout << "Destino " << dest.id << ": "
                              << "OV=" << dest.ordenDeVenta << ", "
                              << "Req=" << dest.piezasRequeridas << ", "
                              << "Act=" << dest.piezasActuales << ", "
                              << "LED=" << (dest.ledEncendido ? "ENCENDIDO" : "APAGADO") << "\n";
                }
                std::cout << "---------------------------------\n";
            } else if (commandChar == 'q') { // Salir
                running = false;
            } else {
                std::cout << "Comando desconocido. Ingresa 'i', 'f', 'c', 'd', 's', o 'q'.\n";
            }
        }

        // Pequeña pausa para no saturar la CPU.
        // Es importante en bucles de sondeo como este.
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Cierra el puerto serial al finalizar el programa
    arduinoPort.close();
    std::cout << "Programa finalizado. Adios!\n";

    return 0;
}


// --- Implementación específica para Windows (SerialPort.cpp integrado aquí) ---
// Normalmente esto estaría en SerialPort.cpp, pero para tener todo en un solo archivo
// y evitar problemas de linkeo para pruebas, lo incluimos directamente.
// NOTA: Si compilas esto junto con un SerialPort.cpp separado, tendras errores de definicion multiple.
// Asegúrate de que solo este archivo contenga la implementación de la clase SerialPort.

#ifdef _WIN32

// Constructor: Inicializa el handle del puerto a un valor inválido y la bandera a false
SerialPort::SerialPort() : hSerial(INVALID_HANDLE_VALUE), _isOpen(false) {}

// Destructor: Asegura que el puerto se cierre cuando el objeto SerialPort se destruye
SerialPort::~SerialPort() {
    close(); 
}

bool SerialPort::open(const std::string& portName, int baudRate) {
    if (_isOpen) {
        std::cerr << "Error: El puerto " << portName << " ya esta abierto." << std::endl;
        return false;
    }

    // 1. Abrir el puerto serial
    // Para puertos COM con números mayores a 9 (ej. COM10, COM11), se necesita el prefijo "\\\\.\\"
    // Para COM1-COM9, "COM3" funciona, pero es más robusto usar el prefijo siempre.
    std::string fullPortName = "\\\\.\\" + portName;

    hSerial = CreateFileA(fullPortName.c_str(),     // Nombre del puerto (ej. "\\\\.\\COM3")
                          GENERIC_READ | GENERIC_WRITE, // Acceso de lectura y escritura
                          0,                            // No compartir el puerto con otras aplicaciones
                          NULL,                         // Atributos de seguridad por defecto
                          OPEN_EXISTING,                // Abrir solo si el puerto ya existe
                          FILE_ATTRIBUTE_NORMAL,        // Atributos de archivo normales
                          NULL);                        // Sin plantilla

    if (hSerial == INVALID_HANDLE_VALUE) {
        // Podríamos obtener un código de error más específico con GetLastError() para depuración
        std::cerr << "Error: No se pudo abrir el puerto " << portName << ". Codigo: " << GetLastError() << std::endl;
        return false;
    }

    // 2. Configurar el puerto (velocidad, bits de datos, paridad, bits de parada)
    DCB dcbSerialParams = { 0 }; // Estructura para la configuración del puerto
    dcbSerialParams.DCBlength = sizeof(dcbSerialParams);

    // Obtener la configuración actual del puerto
    if (!GetCommState(hSerial, &dcbSerialParams)) {
        std::cerr << "Error: Fallo al obtener el estado del puerto " << portName << ". Codigo: " << GetLastError() << std::endl;
        CloseHandle(hSerial); // Cierra el handle si falla
        return false;
    }

    // Establecer los nuevos parámetros de configuración
    dcbSerialParams.BaudRate = baudRate;         // Velocidad de baudios
    dcbSerialParams.ByteSize = 8;                // 8 bits de datos
    dcbSerialParams.StopBits = ONESTOPBIT;       // 1 bit de parada
    dcbSerialParams.Parity = NOPARITY;           // Sin paridad
    // Deshabilitar el control de flujo DTR/RTS si no lo necesitas, puede ser importante para algunos Arduinos
    dcbSerialParams.fDtrControl = DTR_CONTROL_DISABLE;
    dcbSerialParams.fRtsControl = RTS_CONTROL_DISABLE;
    dcbSerialParams.fBinary = TRUE;              // Modo binario (esencial para comunicación de bajo nivel)
    dcbSerialParams.fOutxCtsFlow = FALSE;        // Deshabilitar control de flujo CTS
    dcbSerialParams.fOutxDsrFlow = FALSE;        // Deshabilitar control de flujo DSR
    dcbSerialParams.fOutX = FALSE;               // Deshabilitar control de flujo XON/XOFF de salida
    dcbSerialParams.fInX = FALSE;                // Deshabilitar control de flujo XON/XOFF de entrada
    dcbSerialParams.fErrorChar = FALSE;          // No reemplazar caracteres con errores
    dcbSerialParams.fNull = FALSE;               // No descartar bytes nulos

    // Aplicar la nueva configuración al puerto
    if (!SetCommState(hSerial, &dcbSerialParams)) {
        std::cerr << "Error: Fallo al establecer el estado del puerto " << portName << ". Codigo: " << GetLastError() << std::endl;
        CloseHandle(hSerial);
        return false;
    }

    // 3. Configurar los tiempos de espera (timeouts) para lectura/escritura
    // Esto es muy importante para evitar que las funciones de lectura/escritura se bloqueen indefinidamente.
    COMMTIMEOUTS timeouts = { 0 };
    timeouts.ReadIntervalTimeout = 50;           // Máximo tiempo permitido entre caracteres de entrada (ms)
    timeouts.ReadTotalTimeoutConstant = 50;      // Constante para el tiempo total de lectura (ms)
    timeouts.ReadTotalTimeoutMultiplier = 10;    // Multiplicador para el tiempo total de lectura (ms/byte)
    timeouts.WriteTotalTimeoutConstant = 50;     // Constante para el tiempo total de escritura (ms)
    timeouts.WriteTotalTimeoutMultiplier = 10;   // Multiplicador para el tiempo total de escritura (ms/byte)

    if (!SetCommTimeouts(hSerial, &timeouts)) {
        std::cerr << "Error: Fallo al establecer los tiempos de espera del puerto " << portName << ". Codigo: " << GetLastError() << std::endl;
        CloseHandle(hSerial);
        return false;
    }

    _isOpen = true; // El puerto está abierto y configurado correctamente
    std::cout << "Puerto serial '" << portName << "' abierto correctamente a " << baudRate << " baudios." << std::endl;
    return true;
}

void SerialPort::close() {
    if (_isOpen && hSerial != INVALID_HANDLE_VALUE) {
        CloseHandle(hSerial); // Cierra el handle del puerto
        hSerial = INVALID_HANDLE_VALUE; // Reinicia el handle para indicar que no hay puerto abierto
        _isOpen = false;
        std::cout << "Puerto serial cerrado." << std::endl;
    }
}

// Escribe datos en el puerto serial
int SerialPort::write(const char* data, int length) {
    if (!_isOpen || hSerial == INVALID_HANDLE_VALUE) {
        std::cerr << "Error: Puerto serial no esta abierto para escribir." << std::endl;
        return -1;
    }

    DWORD bytesWritten; // Cantidad de bytes realmente escritos
    // WriteFile devuelve TRUE si la operación es exitosa, FALSE en caso contrario
    if (!WriteFile(hSerial, data, length, &bytesWritten, NULL)) {
        std::cerr << "Error al escribir en el puerto serial. Codigo: " << GetLastError() << std::endl;
        return -1;
    }
    // std::cout << "DEBUG: Escritos " << bytesWritten << " bytes: " << std::string(data, bytesWritten); // Descomenta para depuración detallada
    return bytesWritten;
}

// Lee datos del puerto serial
int SerialPort::read(char* buffer, int bufferSize) {
    if (!_isOpen || hSerial == INVALID_HANDLE_VALUE) {
        // No imprime error aquí porque esta función se llama continuamente
        // y un error constante podría llenar la consola.
        return -1;
    }

    DWORD bytesRead; // Cantidad de bytes realmente leídos
    // ReadFile devuelve TRUE si la operación es exitosa, FALSE en caso contrario
    if (!ReadFile(hSerial, buffer, bufferSize, &bytesRead, NULL)) {
        // Un error común es si el dispositivo se desconecta inesperadamente.
        // std::cerr << "Error al leer del puerto serial. Codigo: " << GetLastError() << std::endl; // Descomenta para depuración
        return -1;
    }
    return bytesRead;
}

// Verifica si el puerto está abierto
bool SerialPort::isOpen() const {
    return _isOpen;
}

#else // Si NO es _WIN32 (este bloque se ignora en compilación de Windows)

// Implementaciones placeholder para sistemas no Windows
// Estos solo existen para que el código compile en entornos no Windows si no hay una implementación real.
SerialPort::SerialPort() : hSerial(0), _isOpen(false) {}
SerialPort::~SerialPort() {}
bool SerialPort::open(const std::string& portName, int baudRate) {
    std::cerr << "Error: La implementacion de SerialPort es solo para Windows." << std::endl;
    return false;
}
void SerialPort::close() { /* no-op */ }
int SerialPort::write(const char* data, int length) { return -1; }
int SerialPort::read(char* buffer, int bufferSize) { return -1; }
bool SerialPort::isOpen() const { return false; }

#endif // _WIN32