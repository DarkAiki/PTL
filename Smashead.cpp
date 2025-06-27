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
#include <iomanip> // Necesario para std::put_time
#include <windows.h>

// Estructura para representar una entrada de producto
struct EntradaProducto {
    int ordenDeVenta;
    int piezas;
    std::string lote;
};

// Alias para facilitar la lectura del código
using Producto = std::vector<EntradaProducto>;

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
                         const std::string& sku, const std::string& lote, bool& sesionTerminada,
                         const std::string& backorderFilename); // Se añade el nombre del archivo

// Función para inicializar el puerto serial
bool inicializarPuertoSerial(const std::string& puerto) {
    // Abre el puerto serial
    arduino = CreateFileA(puerto.c_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (arduino == INVALID_HANDLE_VALUE) {
        std::cerr << "No se pudo abrir el puerto serial. Asegurate de que el puerto " << puerto << " este disponible y el Monitor Serial de Arduino IDE este cerrado." << std::endl;
        return false;
    }

    // Configuración de los parámetros del puerto serial (BaudRate, ByteSize, StopBits, Parity)
    DCB dcbSerialParams = {0};
    dcbSerialParams.DCBlength = sizeof(dcbSerialParams);

    if (!GetCommState(arduino, &dcbSerialParams)) {
        std::cerr << "Error obteniendo configuracion del puerto." << std::endl;
        CloseHandle(arduino);
        return false;
    }

    dcbSerialParams.BaudRate = CBR_9600;
    dcbSerialParams.ByteSize = 8;       // Bits de datos
    dcbSerialParams.StopBits = ONESTOPBIT; // Bits de parada
    dcbSerialParams.Parity   = NOPARITY;   // Paridad

    if (!SetCommState(arduino, &dcbSerialParams)) {
        std::cerr << "Error configurando el puerto." << std::endl;
        CloseHandle(arduino);
        return false;
    }

    // Configura los timeouts para las operaciones de lectura/escritura
    COMMTIMEOUTS timeouts = {0};
    timeouts.ReadIntervalTimeout = 50;
    timeouts.ReadTotalTimeoutConstant = 50;
    timeouts.ReadTotalTimeoutMultiplier = 10;
    timeouts.WriteTotalTimeoutConstant = 50;
    timeouts.WriteTotalTimeoutMultiplier = 10;

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
        std::cout << "[PC -> ARDUINO] Enviando: " << mensaje << std::endl;
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
    char buffer[256];
    DWORD bytesLeidos;
    std::string serialBuffer = "";

    while (hiloLecturaActivo) {
        if (ReadFile(arduino, buffer, sizeof(buffer) - 1, &bytesLeidos, NULL)) {
            if (bytesLeidos > 0) {
                buffer[bytesLeidos] = '\0';
                serialBuffer += buffer;

                size_t newlinePos;
                while ((newlinePos = serialBuffer.find('\n')) != std::string::npos) {
                    std::string linea = serialBuffer.substr(0, newlinePos);
                    serialBuffer.erase(0, newlinePos + 1);

                    linea = trim(linea);

                    if (!linea.empty()) {
                        // Mensaje de diagnóstico para ver lo que llega del Arduino
                        std::cout << "[ARDUINO -> PC] Recibido: " << linea << std::endl;
                        std::lock_guard<std::mutex> lock(mtx);
                        colaMensajesRecibidos.push(linea);
                    }
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}

// Función para obtener un mensaje de la cola de mensajes seriales
std::string obtenerMensajeSerial() {
    std::lock_guard<std::mutex> lock(mtx);
    if (!colaMensajesRecibidos.empty()) {
        std::string mensaje = colaMensajesRecibidos.front();
        colaMensajesRecibidos.pop();
        return mensaje;
    }
    return "";
}

// Función para registrar backorders en un archivo CSV.
void registrarBackorder(const std::string& filename, const std::string& sku, int ordenDeVenta, int piezasOriginales, int piezasFinales, const std::string& lote) {
    // Abre el archivo en modo de añadir (append). Como el nombre es único, la primera escritura creará el archivo.
    std::ofstream archivo(filename, std::ios::app);
    if (!archivo.is_open()) {
        std::cerr << "Error al abrir " << filename << " para escritura." << std::endl;
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
            continue;
        }

        try {
            int ordenDeVenta = std::stoi(ordenStr);
            int piezas = std::stoi(piezasStr);
            productos[sku].push_back({ordenDeVenta, piezas, lote});
        } catch (const std::exception& e) {
            std::cerr << "Error de conversion en linea (se ignorara): " << linea << ". Error: " << e.what() << std::endl;
        }
    }
    return productos;
}

// Función auxiliar para procesar los mensajes.
void processInputMessage(const std::string& mensaje, std::set<int>& pendientes,
                         std::map<int, int>& piezasOriginales, std::map<int, int>& piezasAjustadas,
                         const std::string& sku, const std::string& lote, bool& sesionTerminada,
                         const std::string& backorderFilename) {
    
    if (mensaje == "salir") {
        sesionTerminada = true;
        std::cout << "Comando 'salir' procesado. Terminando sesion del producto." << std::endl;
        return;
    }

    if (mensaje.rfind("boton_", 0) == 0) {
        try {
            int odvConfirmada = std::stoi(mensaje.substr(6));
            if (pendientes.count(odvConfirmada)) {
                pendientes.erase(odvConfirmada);
                enviarAArduino("APAGAR_" + std::to_string(odvConfirmada));
                int original = piezasOriginales.at(odvConfirmada);
                int final_val = piezasAjustadas.at(odvConfirmada);
                registrarBackorder(backorderFilename, sku, odvConfirmada, original, final_val, lote);
                std::cout << "CONFIRMADA: Orden de venta " << odvConfirmada << " con " << final_val << " piezas.\n";
            } else {
                std::cout << "AVISO: Orden de venta " << odvConfirmada << " ya fue confirmada o no estaba pendiente.\n";
            }
        } catch (const std::exception& e) {
            std::cerr << "ERROR: Mensaje de boton malformado: '" << mensaje << "'. " << e.what() << std::endl;
        }
    } else if ((mensaje[0] == '+' || mensaje[0] == '-') && mensaje.length() > 1) {
        try {
            int destino = std::stoi(mensaje.substr(1));
            if (pendientes.count(destino)) {
                int& actual = piezasAjustadas.at(destino);
                int original = piezasOriginales.at(destino);

                // Contador cíclico
                if (mensaje[0] == '+') {
                    if (actual >= original) {
                        actual = 0; // Si está en el máximo, pasa a 0
                    } else {
                        actual++;   // Si no, incrementa normalmente
                    }
                } else {
                    if (actual <= 0) {
                        actual = original; // Si está en 0, pasa al máximo
                    } else {
                        actual--;          // Si no, decrementa normalmente
                    }
                }

                std::cout << "[AJUSTE] Cantidad para O.V. " << destino << " es ahora: " << actual << std::endl;
                enviarAArduino("ACTUALIZAR_" + std::to_string(destino) + "_" + std::to_string(actual));
            } else {
                std::cout << "AVISO: No se puede ajustar la O.V. " << destino << " (no valida o ya confirmada).\n";
            }
        } catch (const std::exception& e) {
            std::cerr << "ERROR: Mensaje de ajuste +/- malformado: '" << mensaje << "'. " << e.what() << std::endl;
        }
    } else if (mensaje.rfind("CONFIRMACION_", 0) == 0 || mensaje.rfind("Arduino:", 0) == 0) {
        // Ignorar confirmaciones del Arduino y mensaje de inicio para no mostrar "Entrada no valida"
    } else {
        if (!mensaje.empty()) {
            std::cout << "Entrada no valida. Usa 'boton_X', '+X', '-X' o 'salir'. Mensaje recibido: '" << mensaje << "'" << std::endl;
        }
    }
}

// Función para verificar si hay entrada disponible en la consola (no bloqueante, solo Windows).
bool hayEntradaEnConsola() {
    HANDLE hInput = GetStdHandle(STD_INPUT_HANDLE);
    DWORD events = 0;
    GetNumberOfConsoleInputEvents(hInput, &events);
    if (events > 0) {
        INPUT_RECORD buffer;
        DWORD a;
        PeekConsoleInput(hInput, &buffer, 1, &a);
        if (buffer.EventType == KEY_EVENT && buffer.Event.KeyEvent.bKeyDown) {
            return true;
        } else {
            ReadConsoleInput(hInput, &buffer, 1, &a); 
            return false;
        }
    }
    return false;
}

int main() {
    //Generación de nombre de archivo único para backorders
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::tm buf;
    localtime_s(&buf, &in_time_t); // localtime_s para seguridad en Windows
    std::stringstream ss;
    ss << std::put_time(&buf, "backorders_%Y-%m-%d_%H-%M-%S.csv");
    std::string backorderFilename = ss.str();
    std::cout << "Los registros de esta sesion se guardaran en: " << backorderFilename << std::endl;

    if (!inicializarPuertoSerial("\\\\.\\COM3")) {
        std::cout << "Presiona Enter para salir." << std::endl;
        std::cin.get();
        return 1;
    }

    std::thread hiloLectura(leerDeArduino);
    std::cout << "Comunicacion con Arduino iniciada. Esperando 2 segundos..." << std::endl;
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

    bool continuarPrograma = true;
    while (continuarPrograma) {
        std::cout << "\n------------------------------------------------------------\n";
        std::cout << "Escanea un producto (SKU Lote) o escribe 'salir_programa' para terminar: ";
        
        std::string sku;
        std::cin >> sku;
        if (std::cin.eof() || sku == "salir_programa") {
            continuarPrograma = false;
            break;
        }
        std::string lote;
        std::cin >> lote;
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

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

            enviarAArduino("APAGAR_TODO");

            for (const auto& entrada : entradasValidas) {
                int odv = entrada.ordenDeVenta;
                pendientes.insert(odv);
                piezasOriginales[odv] = entrada.piezas;
                piezasAjustadas[odv] = entrada.piezas;
                
                std::cout << "ACTIVANDO O.V. " << odv << " - Piezas: " << entrada.piezas << std::endl;
                enviarAArduino("ENCENDER_" + std::to_string(odv) + "_" + std::to_string(entrada.piezas));
            }

            bool sesionActualTerminada = false;
            std::cout << "\n--- Sesion de surtido iniciada. Esperando confirmaciones de botones. ---" << std::endl;
            std::cout << "--- Puede escribir 'salir' en la consola para cancelar este producto. ---\n" << std::endl;

            while (!pendientes.empty() && !sesionActualTerminada) {
                std::string mensajeRecibido;
                while (!(mensajeRecibido = obtenerMensajeSerial()).empty()) {
                    processInputMessage(mensajeRecibido, pendientes, piezasOriginales, piezasAjustadas, sku, lote, sesionActualTerminada, backorderFilename);
                    if (sesionActualTerminada) break;
                }
                if (sesionActualTerminada) break;

                if (hayEntradaEnConsola()) {
                    std::string inputManual;
                    std::cin >> inputManual; 
                    
                    inputManual = trim(inputManual);
                    if (!inputManual.empty()) {
                         processInputMessage(inputManual, pendientes, piezasOriginales, piezasAjustadas, sku, lote, sesionActualTerminada, backorderFilename);
                    }
                }
                
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }

            if (!pendientes.empty()) {
                std::cout << "ADVERTENCIA: Se salio con O.V. activas. Apagando LEDs restantes y registrando como no confirmadas..." << std::endl;
                for (int ov_restante : pendientes) {
                    enviarAArduino("APAGAR_" + std::to_string(ov_restante));
                    registrarBackorder(backorderFilename, sku, ov_restante, piezasOriginales[ov_restante], 0, lote); 
                }
            }

            std::cout << "Procesamiento del producto SKU " << sku << " Lote " << lote << " finalizado." << std::endl;
        } else {
            std::cout << "SKU no reconocido.\n";
        }
    }

    std::cout << "Programa finalizado. Limpiando y cerrando conexion..." << std::endl;
    enviarAArduino("APAGAR_TODO");
    hiloLecturaActivo = false;
    if (hiloLectura.joinable()) hiloLectura.join();
    CloseHandle(arduino);
    return 0;
}