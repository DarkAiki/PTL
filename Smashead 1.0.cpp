#include <iostream>
#include <string>
#include <vector>
#include <limits>   // Para numeric_limits
#include <thread>   // Para std::this_thread::sleep_for
#include <chrono>   // Para std::chrono::milliseconds
#include <mutex>    // Para std::mutex y std::lock_guard
#include <algorithm> // Para std::remove, std::find_if, std::isspace
#include <cctype>   // Para std::isspace

// ================================================================
// CÓDIGO DE SerialPort (Para Windows)
// ================================================================
#ifdef _WIN32
#include <windows.h>
#include <stdio.h> // Para sprintf_s

class SerialPort {
public:
    SerialPort(const std::string& portName, int baudRate)
        : hSerial(INVALID_HANDLE_VALUE), is_open_(false) {
        // Formatear el nombre del puerto para CreateFile (ej. "\\\\.\\COM3")
        std::string fullPortName = "\\\\.\\" + portName;

        hSerial = CreateFileA(
            fullPortName.c_str(),
            GENERIC_READ | GENERIC_WRITE,
            0,
            NULL,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            NULL
        );

        if (hSerial == INVALID_HANDLE_VALUE) {
            DWORD error = GetLastError();
            if (error == ERROR_FILE_NOT_FOUND) {
                std::cerr << "Error: Puerto serial " << portName << " no encontrado." << std::endl;
            } else if (error == ERROR_ACCESS_DENIED) {
                std::cerr << "Error: Acceso denegado al puerto " << portName << ". Podria estar en uso." << std::endl;
            } else {
                std::cerr << "Error al abrir el puerto serial " << portName << ". Codigo de error: " << error << std::endl;
            }
            return;
        }

        DCB dcbSerialParams = { 0 };
        if (!GetCommState(hSerial, &dcbSerialParams)) {
            std::cerr << "Error al obtener el estado del puerto serial." << std::endl;
            CloseHandle(hSerial);
            hSerial = INVALID_HANDLE_VALUE;
            return;
        }

        dcbSerialParams.BaudRate = baudRate;
        dcbSerialParams.ByteSize = 8;
        dcbSerialParams.StopBits = ONESTOPBIT;
        dcbSerialParams.Parity = NOPARITY;

        if (!SetCommState(hSerial, &dcbSerialParams)) {
            std::cerr << "Error al configurar el puerto serial." << std::endl;
            CloseHandle(hSerial);
            hSerial = INVALID_HANDLE_VALUE;
            return;
        }

        // Configurar timeouts
        COMMTIMEOUTS timeouts = { 0 };
        timeouts.ReadIntervalTimeout = 50;
        timeouts.ReadTotalTimeoutConstant = 50;
        timeouts.ReadTotalTimeoutMultiplier = 10;
        timeouts.WriteTotalTimeoutConstant = 50;
        timeouts.WriteTotalTimeoutMultiplier = 10;

        if (!SetCommTimeouts(hSerial, &timeouts)) {
            std::cerr << "Error al configurar los timeouts del puerto serial." << std::endl;
            CloseHandle(hSerial);
            hSerial = INVALID_HANDLE_VALUE;
            return;
        }

        is_open_ = true;
        std::cout << "Puerto serial " << portName << " abierto exitosamente." << std::endl;
    }

    ~SerialPort() {
        if (hSerial != INVALID_HANDLE_VALUE) {
            CloseHandle(hSerial);
            std::cout << "Puerto serial cerrado." << std::endl;
        }
    }

    bool isOpen() const {
        return is_open_;
    }

    int read(char* buffer, unsigned int buf_size) {
        if (!isOpen()) return -1;
        DWORD bytesRead;
        if (!ReadFile(hSerial, buffer, buf_size, &bytesRead, NULL)) {
            // std::cerr << "Error de lectura del puerto serial. Codigo de error: " << GetLastError() << std::endl;
            return -1;
        }
        return bytesRead;
    }

    bool write(const char* buffer, unsigned int buf_size) {
        if (!isOpen()) return false;
        DWORD bytesWritten;
        if (!WriteFile(hSerial, buffer, buf_size, &bytesWritten, NULL)) {
            std::cerr << "Error de escritura en el puerto serial. Codigo de error: " << GetLastError() << std::endl;
            return false;
        }
        return bytesWritten == buf_size;
    }

private:
    HANDLE hSerial;
    bool is_open_;
};

#else
// Placeholder para sistemas no Windows (Linux, macOS)
class SerialPort {
public:
    SerialPort(const std::string& portName, int baudRate) {
        std::cerr << "Advertencia: SerialPort no implementado para este sistema operativo. Los comandos seriales no funcionaran." << std::endl;
        // En una implementación real, se usarían termios.h y fcntl.h
    }
    ~SerialPort() {}
    bool isOpen() const { return false; }
    int read(char* buffer, unsigned int buf_size) { return -1; }
    bool write(const char* buffer, unsigned int buf_size) { return false; }
};
#endif

// ================================================================
// FIN CÓDIGO DE SerialPort
// ================================================================

// Función para recortar (trim) espacios en blanco de un string
// Elimina espacios, tabulaciones, saltos de línea, retornos de carro
std::string trim(const std::string& str) {
    auto wsfront = std::find_if_not(str.begin(), str.end(), [](int c){return std::isspace(c);});
    auto wsback = std::find_if_not(str.rbegin(), str.rend(), [](int c){return std::isspace(c);}).base();
    return (wsback <= wsfront ? "" : std::string(wsfront, wsback));
}


// Definición de la estructura para cada destino
struct DestinoInfo {
    int id;
    int ordenDeVenta;
    int piezasRequeridas;
    int piezasActuales;
    bool completado;

    DestinoInfo(int _id) : id(_id), ordenDeVenta(0), piezasRequeridas(0), piezasActuales(0), completado(false) {}
};

// Variables globales para los destinos
const int NUM_DESTINOS = 5;
DestinoInfo destinos[NUM_DESTINOS] = {
    DestinoInfo(1), DestinoInfo(2), DestinoInfo(3), DestinoInfo(4), DestinoInfo(5)
};

// Instancia global del puerto serial y mutex para control de acceso
SerialPort arduinoPort("COM3", 9600); // !!! ASEGÚRATE DE QUE ESTE SEA EL PUERTO COM CORRECTO !!!
std::mutex serialMutex; // Protege el acceso al puerto serial

// Función para enviar comandos al Arduino
void sendCommandToArduino(const std::string& command) {
    std::lock_guard<std::mutex> lock(serialMutex);
    if (arduinoPort.isOpen()) {
        arduinoPort.write(command.c_str(), command.length());
        // Usamos la función trim() personalizada para la impresión
        std::cout << "C++ DEBUG (SEND): Enviando al Arduino: '" << trim(command) << "'" << std::endl;
    } else {
        // Usamos la función trim() personalizada para la impresión
        std::cerr << "Error: Puerto serial no abierto. No se pudo enviar el comando: " << trim(command) << std::endl;
    }
}

// Función para manejar las respuestas del Arduino
void handleArduinoResponse(const std::string& response) {
    // Usamos la función trim() personalizada para procesar la respuesta
    std::string trimmedResponse = trim(response);

    // Añade esta línea para ver qué se recibe en C++
    std::cout << "C++ DEBUG (RECV): Recibido del Arduino: '" << trimmedResponse << "'" << std::endl;

    // Comandos de Confirmación (ej. "boton_1")
    if (trimmedResponse.rfind("boton_", 0) == 0) { // rfind con 0 para verificar prefijo
        try {
            int destinoId = std::stoi(trimmedResponse.substr(6));
            if (destinoId >= 1 && destinoId <= NUM_DESTINOS) {
                if (destinos[destinoId - 1].ordenDeVenta != 0) { // Solo si hay una OV asignada
                    std::cout << "C++: Boton de Confirmacion para Destino " << destinoId << " presionado." << std::endl;
                    // Aquí iría la lógica para "confirmar" el destino.
                    // Por ejemplo, marcarlo como completado, apagar su LED.
                    destinos[destinoId - 1].completado = true;
                    std::cout << "C++: Destino " << destinoId << " marcado como completado." << std::endl;
                    std::string cmd = "APAGAR_DESTINO_" + std::to_string(destinoId) + "\n";
                    sendCommandToArduino(cmd);
                    // Opcional: Podrías limpiar el display de ese destino si lo apagas.
                    // sendCommandToArduino("AJUSTAR_DESTINO_" + std::to_string(destinoId) + "_VALOR_0\n");
                } else {
                    std::cout << "C++: Boton de Confirmacion para Destino " << destinoId << " presionado, pero no hay OV asignada." << std::endl;
                }
            }
        } catch (const std::invalid_argument& e) {
            std::cerr << "C++ Error: ID de destino invalido en respuesta de boton: " << e.what() << std::endl;
        } catch (const std::out_of_range& e) {
            std::cerr << "C++ Error: ID de destino fuera de rango en respuesta de boton: " << e.what() << std::endl;
        }
    }
    // Comandos de Incremento (+) o Decremento (-)
    else if (trimmedResponse.rfind("+", 0) == 0 || trimmedResponse.rfind("-", 0) == 0) {
        try {
            int destinoId = std::stoi(trimmedResponse.substr(1)); // Extrae el ID después de '+' o '-'
            char type = trimmedResponse[0]; // '+' o '-'

            if (destinoId >= 1 && destinoId <= NUM_DESTINOS) {
                if (destinos[destinoId - 1].ordenDeVenta != 0) { // Solo si hay una OV asignada
                    if (type == '+') {
                        destinos[destinoId - 1].piezasActuales++;
                        std::cout << "C++: Incrementado Destino " << destinoId << ". Piezas: " << destinos[destinoId - 1].piezasActuales << std::endl;
                    } else { // type == '-'
                        if (destinos[destinoId - 1].piezasActuales > 0) { // Evitar números negativos
                            destinos[destinoId - 1].piezasActuales--;
                            std::cout << "C++: Decrementado Destino " << destinoId << ". Piezas: " << destinos[destinoId - 1].piezasActuales << std::endl;
                        } else {
                            std::cout << "C++: Piezas para Destino " << destinoId << " ya en 0, no se puede decrementar mas." << std::endl;
                        }
                    }

                    // Enviar el comando AJUSTAR_DESTINO al Arduino para que actualice el display de ESE destino
                    std::string cmd = "AJUSTAR_DESTINO_" + std::to_string(destinoId) +
                                      "_VALOR_" + std::to_string(destinos[destinoId - 1].piezasActuales) + "\n";
                    sendCommandToArduino(cmd);

                    // Lógica para verificar si se ha completado la OV
                    if (destinos[destinoId - 1].piezasActuales >= destinos[destinoId - 1].piezasRequeridas && destinos[destinoId - 1].piezasRequeridas != 0) {
                        std::cout << "C++: Destino " << destinoId << " ha completado la OV (Piezas actuales: "
                                  << destinos[destinoId - 1].piezasActuales << ", Requeridas: " << destinos[destinoId - 1].piezasRequeridas << ")." << std::endl;
                        // Aquí podrías decidir qué hacer cuando se completa (ej. destello de LED, sonido, marcar completado, etc.)
                        // Por ahora, solo se imprime el mensaje. El botón de confirmación finaliza la OV.
                    }
                } else {
                    std::cout << "C++: Boton de Inc/Dec para Destino " << destinoId << " presionado, pero no hay OV asignada." << std::endl;
                }
            }
        } catch (const std::invalid_argument& e) {
            std::cerr << "C++ Error: ID de destino invalido en respuesta de inc/dec: " << e.what() << std::endl;
        } catch (const std::out_of_range& e) {
            std::cerr << "C++ Error: ID de destino fuera de rango en respuesta de inc/dec: " << e.what() << std::endl;
        }
    }
    // Puedes añadir más lógica para otras respuestas del Arduino si las necesitas
    // Por ejemplo, para procesar "LED_ENCENDIDO_X", "LED_APAGADO_X", etc.
    // ...
}

// Función para leer datos del puerto serial en un hilo separado
void readSerialThread() {
    char buffer[256]; // Buffer para la lectura serial
    std::string serialBuffer = ""; // Acumula caracteres recibidos

    while (true) {
        std::lock_guard<std::mutex> lock(serialMutex); // Bloquea el mutex para acceso al puerto
        int bytesRead = arduinoPort.read(buffer, sizeof(buffer) - 1); // Deja espacio para el nulo terminador

        if (bytesRead > 0) {
            buffer[bytesRead] = '\0'; // Asegura que el buffer sea nulo-terminado
            serialBuffer += buffer;

            // Busca el terminador de línea '\n'
            size_t pos = serialBuffer.find('\n');
            while (pos != std::string::npos) {
                std::string response = serialBuffer.substr(0, pos + 1); // Incluye el '\n' para procesar
                handleArduinoResponse(response); // Procesa la respuesta
                serialBuffer.erase(0, pos + 1); // Elimina la respuesta procesada del buffer
                pos = serialBuffer.find('\n'); // Busca el siguiente '\n'
            }
        }
        // Pequeño retraso para evitar consumir la CPU excesivamente
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}


// Función principal
int main() {
    // Verificar si el puerto serial se abrió correctamente
    if (!arduinoPort.isOpen()) {
        std::cerr << "ERROR FATAL: No se pudo conectar con el puerto serial " << "COM3" << ". Verifique que el Arduino este conectado y que el puerto no este en uso." << std::endl;
        std::cout << "Presione cualquier tecla para salir...";
        std::cin.get(); // Espera una tecla
        return 1;
    }

    // Iniciar el hilo de lectura serial
    std::thread serialReadWorker(readSerialThread);
    serialReadWorker.detach(); // Desvincula el hilo para que se ejecute independientemente

    std::cout << "--- Sistema de Recoleccion - Consola de Control ---" << std::endl;
    std::cout << "Comandos disponibles:" << std::endl;
    std::cout << "  'i' <id> <ov> <piezas> - Iniciar Orden de Venta para un destino (ej: i 1 12345 50)" << std::endl;
    std::cout << "  'f' <id>              - Finalizar/Apagar un destino (ej: f 1)" << std::endl;
    std::cout << "  'c'                   - Limpiar todos los displays y resetear datos" << std::endl;
    std::cout << "  'd'                   - Mostrar 'Completado' en todos los displays" << std::endl;
    std::cout << "  's'                   - Mostrar estado actual de los destinos" << std::endl;
    std::cout << "  'q'                   - Salir del programa" << std::endl;
    std::cout << "-----------------------------------------------------" << std::endl;

    std::string line;
    while (true) {
        std::cout << "\nIngrese comando: ";
        std::getline(std::cin, line);

        if (line.empty()) {
            continue;
        }

        // Dividir el comando en tokens
        std::vector<std::string> tokens;
        std::string currentToken;
        for (char c : line) {
            if (c == ' ') {
                if (!currentToken.empty()) {
                    tokens.push_back(currentToken);
                    currentToken = "";
                }
            } else {
                currentToken += c;
            }
        }
        if (!currentToken.empty()) {
            tokens.push_back(currentToken);
        }

        if (tokens.empty()) {
            continue;
        }

        std::string command = tokens[0];

        if (command == "q") {
            std::cout << "Saliendo del programa..." << std::endl;
            break;
        } else if (command == "i") {
            if (tokens.size() == 4) {
                try {
                    int id = std::stoi(tokens[1]);
                    int ov = std::stoi(tokens[2]);
                    int piezasReq = std::stoi(tokens[3]);

                    if (id >= 1 && id <= NUM_DESTINOS) {
                        // Almacenar OV y piezas requeridas en el programa C++
                        destinos[id - 1].ordenDeVenta = ov;
                        destinos[id - 1].piezasRequeridas = piezasReq;
                        destinos[id - 1].piezasActuales = 0; // Se inicializa en 0 al iniciar OV
                        destinos[id - 1].completado = false; // Asegurarse de que no esté completado

                        // 1. Enviar comando para encender el LED en Arduino
                        std::string cmdLed = "ENCENDER_DESTINO_" + std::to_string(id) + "\n";
                        sendCommandToArduino(cmdLed);
                        // Usamos la función trim() personalizada para la impresión
                        std::cout << "C++: Enviado " << trim(cmdLed) << " a Arduino." << std::endl; 

                        // 2. ENVIAR COMANDO PARA AJUSTAR EL DISPLAY A LAS PIEZAS ACTUALES (0 al inicio)
                        // Esto hará que el display muestre "0" al encender el LED.
                        std::string cmdDisplay = "AJUSTAR_DESTINO_" + std::to_string(id) +
                                                 "_VALOR_" + std::to_string(destinos[id - 1].piezasActuales) + "\n";
                        sendCommandToArduino(cmdDisplay);
                        // Usamos la función trim() personalizada para la impresión
                        std::cout << "C++: Enviado " << trim(cmdDisplay) << " a Arduino." << std::endl; 
                        std::cout << "C++: Destino " << id << " iniciado. OV: " << ov << ", Piezas requeridas: " << piezasReq << ", Piezas actuales: 0." << std::endl;

                    } else {
                        std::cout << "C++: ID de destino fuera de rango (1-" << NUM_DESTINOS << ")." << std::endl;
                    }
                } catch (const std::invalid_argument& e) {
                    std::cerr << "C++ Error: Argumento invalido para el comando 'i'. " << e.what() << std::endl;
                } catch (const std::out_of_range& e) {
                    std::cerr << "C++ Error: Numero fuera de rango para el comando 'i'. " << e.what() << std::endl;
                }
            } else {
                std::cout << "Uso: i <id> <ov> <piezas>" << std::endl;
            }
        } else if (command == "f") {
            if (tokens.size() == 2) {
                try {
                    int id = std::stoi(tokens[1]);
                    if (id >= 1 && id <= NUM_DESTINOS) {
                        destinos[id - 1].ordenDeVenta = 0;
                        destinos[id - 1].piezasRequeridas = 0;
                        destinos[id - 1].piezasActuales = 0;
                        destinos[id - 1].completado = false; // Resetear estado

                        std::string cmd = "APAGAR_DESTINO_" + std::to_string(id) + "\n";
                        sendCommandToArduino(cmd);
                        std::cout << "C++: Destino " << id << " finalizado/apagado." << std::endl;
                    } else {
                        std::cout << "C++: ID de destino fuera de rango (1-" << NUM_DESTINOS << ")." << std::endl;
                    }
                } catch (const std::invalid_argument& e) {
                    std::cerr << "C++ Error: Argumento invalido para el comando 'f'. " << e.what() << std::endl;
                } catch (const std::out_of_range& e) {
                    std::cerr << "C++ Error: Numero fuera de rango para el comando 'f'. " << e.what() << std::endl;
                }
            } else {
                std::cout << "Uso: f <id>" << std::endl;
            }
        } else if (command == "c") {
            std::string cmd = "CLEAR_ALL_DISPLAYS\n";
            sendCommandToArduino(cmd);
            std::cout << "C++: Enviado comando para limpiar todos los displays." << std::endl;
            // Resetear todos los datos de destinos en C++
            for (int i = 0; i < NUM_DESTINOS; ++i) {
                destinos[i].ordenDeVenta = 0;
                destinos[i].piezasRequeridas = 0;
                destinos[i].piezasActuales = 0;
                destinos[i].completado = false;
                std::string cmdLedOff = "APAGAR_DESTINO_" + std::to_string(destinos[i].id) + "\n";
                sendCommandToArduino(cmdLedOff); // Asegurarse de apagar LEDs también
            }
            std::cout << "C++: Datos de todos los destinos reseteados." << std::endl;
        } else if (command == "d") {
            std::string cmd = "DISPLAY_ALL_COMPLETED\n";
            sendCommandToArduino(cmd);
            std::cout << "C++: Enviado comando para mostrar 'Completado' en todos los displays." << std::endl;
        } else if (command == "s") {
            std::cout << "\n--- Estado Actual de Destinos ---" << std::endl;
            for (int i = 0; i < NUM_DESTINOS; ++i) {
                std::cout << "Destino " << destinos[i].id << ":" << std::endl;
                std::cout << "  OV: " << (destinos[i].ordenDeVenta == 0 ? "N/A" : std::to_string(destinos[i].ordenDeVenta)) << std::endl;
                std::cout << "  Piezas Requeridas: " << destinos[i].piezasRequeridas << std::endl;
                std::cout << "  Piezas Actuales: " << destinos[i].piezasActuales << std::endl;
                std::cout << "  Completado: " << (destinos[i].completado ? "Si" : "No") << std::endl;
            }
            std::cout << "---------------------------------" << std::endl;
        } else {
            std::cout << "C++: Comando desconocido. Intente de nuevo." << std::endl;
        }
    }

    return 0;
}