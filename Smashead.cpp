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
#include <algorithm>
#include <mutex>
#include <queue>
#include <limits>
#include <cctype>
#include <iomanip>
#include <windows.h>
#include <condition_variable>

// --- ESTRUCTURAS Y ALIAS (sin cambios) ---
struct EntradaProducto {
    int ordenDeVenta;
    int piezas;
    std::string lote;
};
using Producto = std::vector<EntradaProducto>;

// --- VARIABLES GLOBALES Y SINCRONIZACIÓN ---
HANDLE arduino;
std::mutex mtx;
std::queue<std::string> colaMensajesRecibidos;
bool hiloLecturaActivo = true;
std::condition_variable cv;

// --- PROTOTIPOS DE FUNCIONES ---
void processInputMessage(const std::string& mensaje, std::set<int>& pendientes,
                         std::map<int, int>& piezasOriginales, std::map<int, int>& piezasAjustadas,
                         const std::string& sku, const std::string& lote, bool& sesionTerminada,
                         std::ofstream& archivoBackorder);

// --- FUNCIÓN trim (sin cambios) ---
std::string trim(const std::string& str) {
    const char* whitespace = " \t\n\r\f\v";
    size_t first = str.find_first_not_of(whitespace);
    if (std::string::npos == first) {
        return "";
    }
    size_t last = str.find_last_not_of(whitespace);
    return str.substr(first, (last - first + 1));
}

// --- CONFIGURACIÓN DEL PUERTO SERIAL (OPTIMIZADO) ---
bool inicializarPuertoSerial(const std::string& puerto) {
    arduino = CreateFileA(puerto.c_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (arduino == INVALID_HANDLE_VALUE) {
        std::cerr << "No se pudo abrir el puerto serial. Asegurate de que el puerto " << puerto << " este disponible." << std::endl;
        return false;
    }
    DCB dcbSerialParams = {0};
    dcbSerialParams.DCBlength = sizeof(dcbSerialParams);
    if (!GetCommState(arduino, &dcbSerialParams)) {
        std::cerr << "Error obteniendo configuracion del puerto." << std::endl;
        CloseHandle(arduino);
        return false;
    }

    dcbSerialParams.BaudRate = CBR_115200;
    dcbSerialParams.ByteSize = 8;
    dcbSerialParams.StopBits = ONESTOPBIT;
    dcbSerialParams.Parity = NOPARITY;

    if (!SetCommState(arduino, &dcbSerialParams)) {
        std::cerr << "Error configurando el puerto." << std::endl;
        CloseHandle(arduino);
        return false;
    }
    COMMTIMEOUTS timeouts = {0};
    timeouts.ReadIntervalTimeout = MAXDWORD;
    timeouts.ReadTotalTimeoutConstant = 1;
    timeouts.ReadTotalTimeoutMultiplier = 1;
    timeouts.WriteTotalTimeoutConstant = 50;
    timeouts.WriteTotalTimeoutMultiplier = 10;
    if (!SetCommTimeouts(arduino, &timeouts)) {
        std::cerr << "Error configurando timeouts del puerto." << std::endl;
        CloseHandle(arduino);
        return false;
    }
    return true;
}

// --- FUNCIÓN enviarAArduino (sin cambios) ---
void enviarAArduino(const std::string& mensaje) {
    std::string mensajeConSalto = mensaje + "\n";
    DWORD bytesEscritos;
    if (!WriteFile(arduino, mensajeConSalto.c_str(), mensajeConSalto.size(), &bytesEscritos, NULL)) {
        std::cerr << "Error al escribir en el puerto serial." << std::endl;
    }
}

// --- LECTURA DEL ARDUINO (OPTIMIZADO) ---
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
                        std::lock_guard<std::mutex> lock(mtx);
                        colaMensajesRecibidos.push(linea);
                        cv.notify_one();
                    }
                }
            }
        }
    }
}

// --- FUNCIÓN obtenerMensajeSerial (sin cambios) ---
std::string obtenerMensajeSerial() {
    std::lock_guard<std::mutex> lock(mtx);
    if (!colaMensajesRecibidos.empty()) {
        std::string mensaje = colaMensajesRecibidos.front();
        colaMensajesRecibidos.pop();
        return mensaje;
    }
    return "";
}

// --- GESTIÓN DE ARCHIVO BACKORDER (OPTIMIZADO) ---
void registrarBackorder(std::ofstream& archivo, const std::string& sku, int ordenDeVenta, int piezasOriginales, int piezasFinales, const std::string& lote) {
    if (!archivo.is_open()) {
        std::cerr << "Error: El archivo de backorder no esta abierto para escritura." << std::endl;
        return;
    }
    archivo << sku << "," << lote << "," << ordenDeVenta << "," << piezasOriginales << "," << piezasFinales << "\n";
    archivo.flush();
}

// --- FUNCIÓN cargarProductosDesdeCSV (sin cambios) ---
std::map<std::string, Producto> cargarProductosDesdeCSV(const std::string& archivo) {
    std::map<std::string, Producto> productos;
    std::ifstream file(archivo);
    std::string linea;
    if (!file.is_open()) {
        std::cerr << "No se pudo abrir el archivo: " << archivo << std::endl;
        return productos;
    }
    if (std::getline(file, linea)) { // Leer encabezado
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
            if (sku.empty() || lote.empty() || ordenStr.empty() || piezasStr.empty()) continue;
            try {
                productos[sku].push_back({std::stoi(ordenStr), std::stoi(piezasStr), lote});
            } catch (const std::exception& e) {
                std::cerr << "Error de conversion en linea: " << linea << ". Error: " << e.what() << std::endl;
            }
        }
    }
    return productos;
}

// --- PROCESAMIENTO DE MENSAJES (sin cambios) ---
void processInputMessage(const std::string& mensaje, std::set<int>& pendientes,
                         std::map<int, int>& piezasOriginales, std::map<int, int>& piezasAjustadas,
                         const std::string& sku, const std::string& lote, bool& sesionTerminada,
                         std::ofstream& archivoBackorder) {
    if (mensaje == "salir") {
        sesionTerminada = true;
        return;
    }
    if (mensaje.rfind("boton_", 0) == 0) {
        try {
            int odvConfirmada = std::stoi(mensaje.substr(6));
            if (pendientes.count(odvConfirmada)) {
                pendientes.erase(odvConfirmada);
                enviarAArduino("APAGAR_" + std::to_string(odvConfirmada));
                registrarBackorder(archivoBackorder, sku, odvConfirmada, piezasOriginales.at(odvConfirmada), piezasAjustadas.at(odvConfirmada), lote);
                std::cout << "CONFIRMADA: Orden " << odvConfirmada << " (" << piezasAjustadas.at(odvConfirmada) << " piezas).\n";
            }
        } catch (const std::exception& e) {}
    } else if ((mensaje[0] == '+' || mensaje[0] == '-') && mensaje.length() > 1) {
        try {
            int destino = std::stoi(mensaje.substr(1));
            if (pendientes.count(destino)) {
                int& actual = piezasAjustadas.at(destino);
                int original = piezasOriginales.at(destino);
                if (mensaje[0] == '+') actual = (actual >= original) ? 0 : actual + 1;
                else actual = (actual <= 0) ? original : actual - 1;
                std::cout << "[AJUSTE] O.V. " << destino << ": " << actual << std::endl;
                enviarAArduino("ACTUALIZAR_" + std::to_string(destino) + "_" + std::to_string(actual));
            }
        } catch (const std::exception& e) {}
    }
}

// --- FUNCIÓN PRINCIPAL ---
int main() {
    // Generación de nombre de archivo
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::tm buf;
    localtime_s(&buf, &in_time_t);
    std::stringstream ss;
    ss << std::put_time(&buf, "backorders_%Y-%m-%d_%H-%M-%S.csv");
    std::string backorderFilename = ss.str();

    std::ofstream archivoBackorder(backorderFilename, std::ios::app);
    if (!archivoBackorder.is_open()) {
        std::cerr << "Error fatal: no se pudo crear el archivo " << backorderFilename << std::endl;
        return 1;
    }
    std::cout << "Los registros se guardaran en: " << backorderFilename << std::endl;
    archivoBackorder << "SKU,Lote,Orden de venta,Piezas,CantidadConfirmada\n";

    if (!inicializarPuertoSerial("\\\\.\\COM8")) { // Asegúrate que el puerto COM es correcto
        std::cout << "Presiona Enter para salir." << std::endl;
        std::cin.get();
        return 1;
    }

    std::thread hiloLectura(leerDeArduino);
    std::cout << "Comunicacion con Arduino iniciada. Esperando 2 segundos..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(2));
    while(!obtenerMensajeSerial().empty()); 

    std::string archivoCSV;
    std::cout << "Ingresa el nombre del archivo CSV: ";
    std::getline(std::cin, archivoCSV);
    auto productos = cargarProductosDesdeCSV(trim(archivoCSV));
    if (productos.empty()) {
        std::cout << "No se cargaron productos. Saliendo...\n";
        hiloLecturaActivo = false;
        cv.notify_all();
        if (hiloLectura.joinable()) hiloLectura.join();
        CloseHandle(arduino);
        archivoBackorder.close();
        return 1;
    }

    bool continuarPrograma = true;
    while (continuarPrograma) {
        std::cout << "\n------------------------------------------------------------\n";
        std::cout << "Escanea un producto (SKU Lote) o escribe 'salir_programa': ";
        
        // --- INICIO: LÓGICA DE ENTRADA MANUAL ESTÁNDAR ---
        std::string sku;
        std::cin >> sku;

        if (std::cin.eof() || sku == "salir_programa") {
            continuarPrograma = false;
            continue; // Sale del bucle para finalizar el programa
        }
        
        std::string lote;
        std::cin >> lote;
        // Limpia el buffer de entrada por si se presiona Enter más de una vez
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        // --- FIN: LÓGICA DE ENTRADA MANUAL ESTÁNDAR ---

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

// ... dentro de la función main, en el bucle 'while (continuarPrograma)'

            std::set<int> pendientes;
            std::map<int, int> piezasAjustadas, piezasOriginales;

            enviarAArduino("APAGAR_TODO");
<<<<<<< HEAD
<<<<<<< HEAD
            std::this_thread::sleep_for(std::chrono::milliseconds(50));

            // --- INICIO: MODIFICACIÓN CLAVE PARA MAPEO FIJO ---
            for (const auto& entrada : entradasValidas) {
                int odv = entrada.ordenDeVenta;
=======
            std::this_thread::sleep_for(std::chrono::milliseconds(50)); // Pequeña pausa para que Arduino procese

            // --- INICIO DE LA MODIFICACIÓN ---
            // Este bucle ahora filtra las órdenes de venta.
            for (const auto& entrada : entradasValidas) {
                int odv = entrada.ordenDeVenta;

                // ¡AQUÍ ESTÁ EL FILTRO!
                // Solo procesamos órdenes de venta que correspondan a un display físico (1-4).
>>>>>>> cc836d7a8e3650434501174a3adc1ffb2f7cdce9
=======
            std::this_thread::sleep_for(std::chrono::milliseconds(50)); // Pequeña pausa para que Arduino procese

            // --- INICIO DE LA MODIFICACIÓN ---
            // Este bucle ahora filtra las órdenes de venta.
            for (const auto& entrada : entradasValidas) {
                int odv = entrada.ordenDeVenta;

                // ¡AQUÍ ESTÁ EL FILTRO!
                // Solo procesamos órdenes de venta que correspondan a un display físico (1-4).
>>>>>>> cc836d7a8e3650434501174a3adc1ffb2f7cdce9
                if (odv >= 1 && odv <= 4) {
                    pendientes.insert(odv);
                    piezasOriginales[odv] = entrada.piezas;
                    piezasAjustadas[odv] = entrada.piezas;
<<<<<<< HEAD
<<<<<<< HEAD
                    std::cout << "ACTIVANDO O.V. #" << odv << " en Display " << odv << " - Piezas: " << entrada.piezas << std::endl;
                    enviarAArduino("ENCENDER_" + std::to_string(odv) + "_" + std::to_string(entrada.piezas));
                } else {
                    std::cout << "ADVERTENCIA: O.V. #" << odv << " ignorada (fuera del rango de displays 1-4)." << std::endl;
                }
            }
            // --- FIN: MODIFICACIÓN CLAVE ---

            if (pendientes.empty()) {
                std::cout << "Sin ordenes validas (1-4) para este producto. Volviendo al escaner.\n";
                continue;
            }

            bool sesionActualTerminada = false;
            std::cout << "\n--- Sesion iniciada. Esperando confirmaciones... ---\n" << std::endl;
=======
                    
                    // El mensaje es más claro: La O.V. #X se activa en el Display X.
                    std::cout << "ACTIVANDO O.V. #" << odv << " en Display " << odv << " - Piezas: " << entrada.piezas << std::endl;
                    enviarAArduino("ENCENDER_" + std::to_string(odv) + "_" + std::to_string(entrada.piezas));
                } else {
                    // Si la O.V. está fuera de rango, se notifica en consola y se ignora.
                    std::cout << "ADVERTENCIA: La O.V. #" << odv << " del archivo CSV se ha ignorado (fuera del rango de displays 1-4)." << std::endl;
                }
            }
            // --- FIN DE LA MODIFICACIÓN ---

            // Añadimos una comprobación: si después del filtro no quedó ninguna orden
            // pendiente, no tiene sentido iniciar la sesión de surtido.
            if (pendientes.empty()) {
                std::cout << "No hay órdenes de venta válidas (en el rango 1-4) para este producto. Volviendo al escaner." << std::endl;
                continue; // Esta línea es importante, salta al siguiente escaneo de producto.
            }

            bool sesionActualTerminada = false;
            std::cout << "\n--- Sesion iniciada. Esperando confirmaciones. Escribe 'salir' para cancelar. ---\n" << std::endl;
>>>>>>> cc836d7a8e3650434501174a3adc1ffb2f7cdce9

=======
                    
                    // El mensaje es más claro: La O.V. #X se activa en el Display X.
                    std::cout << "ACTIVANDO O.V. #" << odv << " en Display " << odv << " - Piezas: " << entrada.piezas << std::endl;
                    enviarAArduino("ENCENDER_" + std::to_string(odv) + "_" + std::to_string(entrada.piezas));
                } else {
                    // Si la O.V. está fuera de rango, se notifica en consola y se ignora.
                    std::cout << "ADVERTENCIA: La O.V. #" << odv << " del archivo CSV se ha ignorado (fuera del rango de displays 1-4)." << std::endl;
                }
            }
            // --- FIN DE LA MODIFICACIÓN ---

            // Añadimos una comprobación: si después del filtro no quedó ninguna orden
            // pendiente, no tiene sentido iniciar la sesión de surtido.
            if (pendientes.empty()) {
                std::cout << "No hay órdenes de venta válidas (en el rango 1-4) para este producto. Volviendo al escaner." << std::endl;
                continue; // Esta línea es importante, salta al siguiente escaneo de producto.
            }

            bool sesionActualTerminada = false;
            std::cout << "\n--- Sesion iniciada. Esperando confirmaciones. Escribe 'salir' para cancelar. ---\n" << std::endl;

>>>>>>> cc836d7a8e3650434501174a3adc1ffb2f7cdce9
            // El resto del bucle de la sesión de surtido no necesita cambios...
            while (!pendientes.empty() && !sesionActualTerminada) {

                std::string mensajeRecibido;
                while (!(mensajeRecibido = obtenerMensajeSerial()).empty()) {
                    processInputMessage(mensajeRecibido, pendientes, piezasOriginales, piezasAjustadas, sku, lote, sesionActualTerminada, archivoBackorder);
                    if (sesionActualTerminada) break;
                }
                if (sesionActualTerminada) break;
                // Pequeña pausa para evitar consumo excesivo de CPU en este bucle
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }

            if (!pendientes.empty()) {
                std::cout << "ADVERTENCIA: Se salio con O.V. pendientes. Apagando y registrando..." << std::endl;
                for (int ov_restante : pendientes) {
                    enviarAArduino("APAGAR_" + std::to_string(ov_restante));
                    registrarBackorder(archivoBackorder, sku, ov_restante, piezasOriginales[ov_restante], 0, lote); 
                }
            }
            std::cout << "Procesamiento del producto SKU " << sku << " Lote " << lote << " finalizado." << std::endl;
        } else {
            if(!sku.empty()) std::cout << "SKU no reconocido.\n";
        }
    }

    std::cout << "Programa finalizado. Limpiando y cerrando conexion..." << std::endl;
    enviarAArduino("APAGAR_TODO");
    hiloLecturaActivo = false;
    cv.notify_all();
    if (hiloLectura.joinable()) hiloLectura.join();
    CloseHandle(arduino);
    archivoBackorder.close();
    return 0;
}