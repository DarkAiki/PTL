// C++ Standard Library
#include <iostream>
#include <map>
#include <vector>
#include <set>
#include <string>
#include <thread>
#include <chrono>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <algorithm>
#include <mutex>
#include <queue>
#include <limits>
#include <cctype>

// Windows API Header - Incluir al final y con NOMINMAX
#define NOMINMAX
#include <windows.h>

// Estructura para representar una entrada de producto
struct EntradaProducto {
    int ordenDeVenta;
    int piezas;
    std::string lote;
};

// Alias para facilitar la lectura del código
using Producto = std::vector<EntradaProducto>; // Múltiples entradas por SKU

// Handle global para el puerto serial de Arduino
HANDLE arduino;

// Mutex para proteger el acceso a las variables compartidas entre hilos
std::mutex mtx;
// Cola para almacenar los mensajes recibidos del Arduino
std::queue<std::string> colaMensajesRecibidos;
// Bandera para controlar la ejecución del hilo de lectura
bool hiloLecturaActivo = true;

// Prototipo de la función auxiliar de procesamiento de mensajes
void processInputMessage(const std::string& mensaje, std::set<int>& pendientes,
                         std::map<int, int>& piezasOriginales, std::map<int, int>& piezasAjustadas,
                         const std::string& sku, const std::string& lote, bool& loop_break);

// Función para inicializar el puerto serial
bool inicializarPuertoSerial(const std::string& puerto) {
    // Abre el puerto serial
    arduino = CreateFileA(puerto.c_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (arduino == INVALID_HANDLE_VALUE) {
        std::cerr << "No se pudo abrir el puerto serial. Asegurate de que el puerto " << puerto << " este disponible y el Monitor Serial de Arduino IDE este cerrado." << std::endl;
        return false;
    }

    // Configura los parámetros del puerto serial (BaudRate, ByteSize, StopBits, Parity)
    DCB dcbSerialParams = {0};
    dcbSerialParams.DCBlength = sizeof(dcbSerialParams);

    if (!GetCommState(arduino, &dcbSerialParams)) {
        std::cerr << "Error obteniendo configuracion del puerto." << std::endl;
        CloseHandle(arduino);
        return false;
    }

    dcbSerialParams.BaudRate = CBR_9600; // Tasa de baudios: DEBE COINCIDIR con el Arduino
    dcbSerialParams.ByteSize = 8;        // Bits de datos
    dcbSerialParams.StopBits = ONESTOPBIT; // Bits de parada
    dcbSerialParams.Parity   = NOPARITY;   // Paridad

    if (!SetCommState(arduino, &dcbSerialParams)) {
        std::cerr << "Error configurando el puerto." << std::endl;
        CloseHandle(arduino);
        return false;
    }

    // Configura los timeouts para las operaciones de lectura/escritura
    COMMTIMEOUTS timeouts = {0};
    timeouts.ReadIntervalTimeout = 50;         // Máximo tiempo entre caracteres (ms)
    timeouts.ReadTotalTimeoutConstant = 50;    // Tiempo adicional por lectura (ms)
    timeouts.ReadTotalTimeoutMultiplier = 10;  // Multiplicador por byte para lectura (ms)
    timeouts.WriteTotalTimeoutConstant = 50;   // Tiempo adicional por escritura (ms)
    timeouts.WriteTotalTimeoutMultiplier = 10; // Multiplicador por byte para escritura (ms)

    if (!SetCommTimeouts(arduino, &timeouts)) {
        std::cerr << "Error configurando timeouts del puerto." << std::endl;
        CloseHandle(arduino);
        return false;
    }

    return true;
}

// Función para enviar un mensaje al Arduino
void enviarAArduino(const std::string& mensaje) {
    // Agrega un salto de línea al final del mensaje, como espera el Arduino
    std::string mensajeConSalto = mensaje + "\n";
    DWORD bytesEscritos;
    // Escribe el mensaje en el puerto serial
    if (!WriteFile(arduino, mensajeConSalto.c_str(), mensajeConSalto.size(), &bytesEscritos, NULL)) {
        std::cerr << "Error al escribir en el puerto serial." << std::endl;
    } else {
        std::cout << "[ARDUINO <- PC] Enviando: " << mensaje << std::endl;
    }
}

// Función para eliminar espacios en blanco y saltos de línea al inicio y final de una cadena
std::string trim(const std::string& str) {
    const char* whitespace = " \t\n\r\f\v";
    size_t first = str.find_first_not_of(whitespace);
    if (std::string::npos == first) {
        return "";
    }
    size_t last = str.find_last_not_of(whitespace);
    return str.substr(first, (last - first + 1));
}

// Hilo para leer continuamente del puerto serial
void leerDeArduino() {
    char buffer[256]; // Buffer para los datos recibidos
    DWORD bytesLeidos; // Número de bytes leídos
    std::string serialBuffer = ""; // Buffer para acumular caracteres hasta un salto de línea

    while (hiloLecturaActivo) {
        // Intenta leer del puerto serial
        if (ReadFile(arduino, buffer, sizeof(buffer) - 1, &bytesLeidos, NULL)) {
            if (bytesLeidos > 0) {
                buffer[bytesLeidos] = '\0'; // Null-terminate el buffer para tratarlo como string
                serialBuffer += buffer; // Añade los bytes leídos al buffer acumulador

                // Procesa el buffer acumulador línea por línea
                size_t newlinePos;
                while ((newlinePos = serialBuffer.find('\n')) != std::string::npos) {
                    std::string linea = serialBuffer.substr(0, newlinePos);
                    serialBuffer.erase(0, newlinePos + 1); // Elimina la línea procesada del buffer

                    linea = trim(linea); // Elimina espacios y retornos de carro

                    if (!linea.empty()) {
                        std::cout << "[PC <- ARDUINO] Recibido: " << linea << std::endl;
                        // Protege el acceso a la cola con un mutex antes de añadir el mensaje
                        std::lock_guard<std::mutex> lock(mtx);
                        colaMensajesRecibidos.push(linea);
                    }
                }
            }
        }
        // Pequeña pausa para no saturar la CPU
        std::this_thread::sleep_for(std::chrono::milliseconds(10)); // Reducido para mayor reactividad
    }
}

// Función para obtener un mensaje de la cola de mensajes seriales
std::string obtenerMensajeSerial() {
    // Protege el acceso a la cola con un mutex
    std::lock_guard<std::mutex> lock(mtx);
    if (!colaMensajesRecibidos.empty()) {
        std::string mensaje = colaMensajesRecibidos.front();
        colaMensajesRecibidos.pop();
        return mensaje;
    }
    return ""; // Retorna una cadena vacía si no hay mensajes
}

// Función para registrar backorders en un archivo CSV
void registrarBackorder(const std::string& sku, int ordenDeVenta, int piezasOriginales, int piezasFinales, const std::string& lote) {
    // Abre el archivo en modo de añadir (append)
    std::ofstream archivo("backorders.csv", std::ios::app);
    if (!archivo.is_open()) {
        std::cerr << "Error al abrir backorders.csv para escritura." << std::endl;
        return;
    }
    
    // Comprueba si el archivo está vacío para escribir el encabezado
    archivo.seekp(0, std::ios::end);
    if (archivo.tellp() == 0) {
        archivo << "SKU,Lote,Orden de venta,Piezas,CantidadConfirmada\n";
    }
    
    archivo << sku << "," << lote << "," << ordenDeVenta << "," << piezasOriginales << "," << piezasFinales << "\n";
}

// Función para cargar productos desde un archivo CSV
std::map<std::string, Producto> cargarProductosDesdeCSV(const std::string& archivo) {
    std::map<std::string, Producto> productos;
    std::ifstream file(archivo);
    std::string linea;
    if (!file.is_open()) {
        std::cerr << "No se pudo abrir el archivo: " << archivo << std::endl;
        return productos;
    }

    // Ignorar el encabezado del CSV
    if (!std::getline(file, linea)) {
         std::cerr << "Archivo CSV vacio o no se pudo leer el encabezado." << std::endl;
         return productos;
    }

    while (std::getline(file, linea)) {
        std::stringstream ss(linea);
        std::string sku, lote, ordenStr, piezasStr;

        std::getline(ss, sku, ',');
        std::getline(ss, lote, ',');
        std::getline(ss, ordenStr, ',');
        std::getline(ss, piezasStr, ',');

        sku = trim(sku);
        lote = trim(lote);
        ordenStr = trim(ordenStr);
        piezasStr = trim(piezasStr); 

        if (sku.empty() || lote.empty() || ordenStr.empty() || piezasStr.empty()) {
            std::cerr << "Linea con formato incorrecto (se ignorara): " << linea << std::endl;
            continue;
        }

        try {
            int ordenDeVenta = std::stoi(ordenStr);
            int piezas = std::stoi(piezasStr);
            productos[sku].push_back({ordenDeVenta, piezas, lote});
        } catch (const std::invalid_argument& e) {
            std::cerr << "Error de conversion a numero en linea (se ignorara): " << linea << ". Error: " << e.what() << std::endl;
        } catch (const std::out_of_range& e) {
            std::cerr << "Numero fuera de rango en linea (se ignorara): " << linea << ". Error: " << e.what() << std::endl;
        }
    }

    return productos;
}

// Función auxiliar para procesar los mensajes (ya sea de Arduino o entrada manual)
void processInputMessage(const std::string& mensaje, std::set<int>& pendientes,
                         std::map<int, int>& piezasOriginales, std::map<int, int>& piezasAjustadas,
                         const std::string& sku, const std::string& lote, bool& loop_break) {
    
    if (mensaje == "salir") {
        loop_break = true; // Indica al bucle principal que debe salir
        std::cout << "Comando 'salir' recibido." << std::endl;
        return;
    }

    if (mensaje.rfind("boton_", 0) == 0) { // rfind es como starts_with
        int ordenDeVentaConfirmada = 0;
        try {
            ordenDeVentaConfirmada = std::stoi(mensaje.substr(6));
            std::cout << "DEBUG: Procesando boton_" << ordenDeVentaConfirmada << ". Orden parseada: " << ordenDeVentaConfirmada << std::endl;

            if (pendientes.count(ordenDeVentaConfirmada)) { // Usa count para verificar existencia
                pendientes.erase(ordenDeVentaConfirmada); // Elimina la orden de venta de pendientes
                enviarAArduino("APAGAR_DESTINO_" + std::to_string(ordenDeVentaConfirmada));
                int original = piezasOriginales[ordenDeVentaConfirmada];
                int final_val = piezasAjustadas[ordenDeVentaConfirmada];
                registrarBackorder(sku, ordenDeVentaConfirmada, original, final_val, lote);
                std::cout << "ORDEN DE VENTA " << ordenDeVentaConfirmada << " confirmada con piezas: " << final_val << ".\n";
            } else {
                std::cout << "Orden de venta " << ordenDeVentaConfirmada << " NO ENCONTRADA en pendientes o ya confirmada.\n";
            }
        } catch (const std::invalid_argument& ia) {
            std::cerr << "ERROR STOI (invalid_argument): No se pudo convertir '" << mensaje.substr(6) << "' a entero. Mensaje original: '" << mensaje << "'" << std::endl;
        } catch (const std::out_of_range& oor) {
            std::cerr << "ERROR STOI (out_of_range): Numero fuera de rango en '" << mensaje.substr(6) << "'. Mensaje original: '" << mensaje << "'" << std::endl;
        }
    } else if (mensaje.rfind("LED_ENCENDIDO_", 0) == 0 || mensaje.rfind("LED_APAGADO_", 0) == 0) {
        // Ignorar estas confirmaciones para evitar "Entrada no válida"
    } else if (mensaje == "Arduino: Iniciado.") {
        // Ignorar
    } else if ((mensaje[0] == '+' || mensaje[0] == '-') && mensaje.length() > 1) {
        int destino = 0;
        try {
            destino = std::stoi(mensaje.substr(1));
            if (pendientes.count(destino)) {
                int& actual = piezasAjustadas.at(destino); // Usamos .at() para lanzar excepción si no existe
                int original = piezasOriginales.at(destino);
                if (mensaje[0] == '+') {
                    if (actual < original) actual++;
                } else { // mensaje[0] == '-'
                    if (actual > 0) actual--;
                }
                std::cout << "[DISPLAY ORDEN DE VENTA " << destino << "]: " << actual << std::endl;
                // Opcional: enviar a Arduino para actualizar display
                // enviarAArduino("ACTUALIZAR_DISPLAY_" + std::to_string(destino) + "_" + std::to_string(actual));
            } else {
                std::cout << "Orden de venta " << destino << " (para ajuste +/-) no valida o ya confirmada.\n";
            }
        } catch (const std::invalid_argument& ia) {
            std::cerr << "ERROR STOI (+/- invalid_argument): No se pudo convertir '" << mensaje.substr(1) << "' a entero." << std::endl;
        } catch (const std::out_of_range& oor) {
            std::cerr << "ERROR STOI (+/- out_of_range): Numero fuera de rango en '" << mensaje.substr(1) << "'." << std::endl;
        }
    } else {
        if (!mensaje.empty()) {
            std::cout << "Entrada no valida. Usa 'boton_X', '+X' o '-X'. Mensaje recibido: '" << mensaje << "'" << std::endl;
        }
    }
}

// Función para verificar si hay entrada disponible en la consola (no bloqueante)
bool hayEntradaEnConsola() {
    // Esta implementación es específica de Windows
    HANDLE hInput = GetStdHandle(STD_INPUT_HANDLE);
    DWORD events;
    INPUT_RECORD buffer;

    // Mira si hay eventos en el buffer de entrada de la consola sin removerlos
    PeekConsoleInput(hInput, &buffer, 1, &events);
    // Si hay eventos y al menos uno de ellos es una tecla presionada, retorna true
    if (events > 0) {
        for (DWORD i = 0; i < events; ++i) {
            // Lee los eventos para ver si hay una tecla presionada
            ReadConsoleInput(hInput, &buffer, 1, &events);
            if (buffer.EventType == KEY_EVENT && buffer.Event.KeyEvent.bKeyDown) {
                // Hay una tecla presionada, la devolvemos al buffer para que std::cin la lea
                WriteConsoleInput(hInput, &buffer, 1, &events);
                return true;
            }
        }
    }
    return false;
}


int main() {
    // NOTA: Reemplaza "COM3" con el puerto COM correcto de tu Arduino.
    // El formato para puertos > 9 es "\\\\.\\COM10", "\\\\.\\COM11", etc.
    if (!inicializarPuertoSerial("\\\\.\\COM3")) {
        std::cout << "Presiona Enter para salir." << std::endl;
        std::cin.get();
        return 1;
    }

    std::thread hiloLectura(leerDeArduino);
    std::cout << "Esperando al Arduino..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(2));

    std::string archivoCSV;
    std::cout << "Ingresa el nombre del archivo CSV con los productos: ";
    std::getline(std::cin, archivoCSV);
    archivoCSV = trim(archivoCSV);


    auto productos = cargarProductosDesdeCSV(archivoCSV);
    if (productos.empty()) {
        std::cout << "No se cargaron productos o el archivo no existe. Saliendo...\n";
        hiloLecturaActivo = false;
        if (hiloLectura.joinable()) hiloLectura.join();
        CloseHandle(arduino);
        return 1;
    }

    bool continuar = true;
    while (continuar) {
        std::string sku, lote;
        std::cout << "\nEscanea un producto (SKU Lote) o escribe 'salir_programa' para terminar: ";
        std::cin >> sku;
        if (sku == "salir_programa") {
            continuar = false;
            break;
        }
        std::cin >> lote;
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n'); // Limpiar buffer robusto

        sku = trim(sku);
        lote = trim(lote);

        if (productos.count(sku)) {
            auto& entradas = productos.at(sku);
            std::vector<EntradaProducto> entradasValidas;

            for (const auto& entrada : entradas) {
                if (entrada.lote == lote) {
                    entradasValidas.push_back(entrada);
                }
            }
            
            if (entradasValidas.empty()) {
                std::cout << "No hay entradas para el SKU '" << sku << "' con el lote '" << lote << "'.\n";
                continue;
            }

            std::set<int> pendientes;
            std::map<int, int> piezasAjustadas;
            std::map<int, int> piezasOriginales;

            for (const auto& entrada : entradasValidas) {
                int odv = entrada.ordenDeVenta;
                pendientes.insert(odv);
                piezasOriginales[odv] = entrada.piezas;
                piezasAjustadas[odv] = entrada.piezas;
                std::cout << "Activando ORDEN DE VENTA " << odv << " - Piezas: " << entrada.piezas << " - Lote: " << entrada.lote << std::endl;
                enviarAArduino("ENCENDER_DESTINO_" + std::to_string(odv));
                std::cout << "[DISPLAY ORDEN DE VENTA " << odv << "]: " << entrada.piezas << std::endl;
            }

            bool current_scan_finished = false;
            
            while (!pendientes.empty() && !current_scan_finished) {
                // (1) Procesar todos los mensajes de Arduino en la cola
                std::string mensajeRecibido = obtenerMensajeSerial();
                while (!mensajeRecibido.empty()) {
                    processInputMessage(mensajeRecibido, pendientes, piezasOriginales, piezasAjustadas, sku, lote, current_scan_finished);
                     if (current_scan_finished) break; // Salir si un mensaje de Arduino terminó el escaneo
                    mensajeRecibido = obtenerMensajeSerial();
                }

                 if (current_scan_finished || pendientes.empty()) break;

                // (2) Revisar entrada manual de forma no bloqueante
                if (hayEntradaEnConsola()) {
                    std::string inputManual;
                    std::getline(std::cin, inputManual);
                    inputManual = trim(inputManual);
                    if (!inputManual.empty()) {
                        std::cout << "DEBUG_MANUAL: Ingresado manualmente '" << inputManual << "'" << std::endl;
                        if (inputManual == "salir") {
                            current_scan_finished = true;
                            std::cout << "Saliendo del producto actual..." << std::endl;
                        } else {
                            processInputMessage(inputManual, pendientes, piezasOriginales, piezasAjustadas, sku, lote, current_scan_finished);
                        }
                    }
                }
                
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }

            if (!pendientes.empty()) {
                std::cout << "ADVERTENCIA: Se salio con OVs aun activas. Apagando LEDs..." << std::endl;
                for (int ov_restante : pendientes) {
                    enviarAArduino("APAGAR_DESTINO_" + std::to_string(ov_restante));
                    // Opcional: registrar como no confirmado (0 piezas)
                    // registrarBackorder(sku, ov_restante, piezasOriginales[ov_restante], 0, lote); 
                }
            }

            std::cout << "Procesamiento del producto finalizado." << std::endl;

            std::string opcion;
            std::cout << "\n¿Deseas escanear otro producto? (s/n): ";
            std::getline(std::cin, opcion);
            opcion = trim(opcion);

            if (opcion != "s" && opcion != "S") {
                continuar = false;
            }
        } else {
            std::cout << "Producto no reconocido.\n";
        }
    }

    std::cout << "Programa finalizado.\n";
    hiloLecturaActivo = false;
    if (hiloLectura.joinable()) hiloLectura.join();
    CloseHandle(arduino);
    return 0;
}
