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
#include <algorithm> // Para trim
#include <windows.h> // Para comunicación serial en Windows
#include <mutex>     // Para proteger el acceso a las variables compartidas entre hilos
#include <queue>     // Para almacenar los mensajes recibidos del Arduino
#include <limits>    // Para std::numeric_limits
#include <cctype>    // Para isprint

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
                         const std::string& sku, const std::string& lote, bool& loop_break);

// Función para inicializar el puerto serial
bool inicializarPuertoSerial(const std::string& puerto) {
    arduino = CreateFileA(puerto.c_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (arduino == INVALID_HANDLE_VALUE) {
        std::cerr << "No se pudo abrir el puerto serial. Asegúrate de que el puerto COM esté disponible y el Monitor Serial de Arduino IDE esté cerrado." << std::endl;
        return false;
    }
    DCB dcbSerialParams = {0};
    dcbSerialParams.DCBlength = sizeof(dcbSerialParams);
    if (!GetCommState(arduino, &dcbSerialParams)) {
        std::cerr << "Error obteniendo configuración del puerto." << std::endl;
        CloseHandle(arduino);
        return false;
    }
    dcbSerialParams.BaudRate = CBR_9600;
    dcbSerialParams.ByteSize = 8;
    dcbSerialParams.StopBits = ONESTOPBIT;
    dcbSerialParams.Parity   = NOPARITY;
    if (!SetCommState(arduino, &dcbSerialParams)) {
        std::cerr << "Error configurando el puerto." << std::endl;
        CloseHandle(arduino);
        return false;
    }
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
    std::string mensajeConSalto = mensaje + "\n";
    DWORD bytesEscritos;
    WriteFile(arduino, mensajeConSalto.c_str(), mensajeConSalto.size(), &bytesEscritos, NULL);
    std::cout << "[ARDUINO <- PC] Enviando: " << mensaje << std::endl;
}

// --- NUEVA FUNCIÓN PARA ENVIAR DATOS AL DISPLAY ---
void enviarDatosDisplay(int ordenDeVenta, int piezas) {
    std::string mensaje = "DISPLAY_OV_" + std::to_string(ordenDeVenta) + "_PIEZAS_" + std::to_string(piezas);
    enviarAArduino(mensaje);
}

// --- NUEVA FUNCIÓN PARA AJUSTAR PIEZAS EN EL DISPLAY ---
void ajustarPiezasDisplay(int ordenDeVenta, int nuevoValor) {
    std::string mensaje = "AJUSTAR_OV_" + std::to_string(ordenDeVenta) + "_VALOR_" + std::to_string(nuevoValor);
    enviarAArduino(mensaje);
}


// Función para eliminar espacios en blanco y saltos de línea al inicio y final de una cadena
std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\n\r");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\n\r");
    return str.substr(first, last - first + 1);
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
                        std::cout << "[PC <- ARDUINO] Recibido: " << linea << std::endl;
                        std::lock_guard<std::mutex> lock(mtx);
                        colaMensajesRecibidos.push(linea);
                    }
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
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

// Función para registrar backorders en un archivo CSV
void registrarBackorder(const std::string& sku, int ordenDeVenta, int piezasOriginales, int piezasFinales, const std::string& lote) {
    static bool encabezadoEscrito = false;
    std::ofstream archivo("backorders.csv", std::ios::app);
    if (!encabezadoEscrito && archivo.tellp() == 0) {
        archivo << "SKU,Lote,Orden de venta,Piezas,CantidadConfirmada\n";
        encabezadoEscrito = true;
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
    std::getline(file, linea); // Ignorar encabezado
    while (std::getline(file, linea)) {
        std::stringstream ss(linea);
        std::string sku_csv, lote_csv, ordenStr, piezasStr;
        std::getline(ss, sku_csv, ',');
        std::getline(ss, lote_csv, ',');
        std::getline(ss, ordenStr, ',');
        std::getline(ss, piezasStr, ',');
        sku_csv = trim(sku_csv);
        lote_csv = trim(lote_csv);
        ordenStr = trim(ordenStr);
        piezasStr = trim(trim(piezasStr));
        if (sku_csv.empty() || lote_csv.empty() || ordenStr.empty() || piezasStr.empty()) {
            std::cerr << "Línea con formato incorrecto (se ignorará): " << linea << std::endl;
            continue;
        }
        try {
            int ordenDeVenta = std::stoi(ordenStr);
            int piezas = std::stoi(piezasStr);
            productos[sku_csv].push_back({ordenDeVenta, piezas, lote_csv});
        } catch (const std::exception& e) {
            std::cerr << "Error convirtiendo datos de CSV en línea: " << linea << " - " << e.what() << std::endl;
        }
    }
    return productos;
}

// Función auxiliar para procesar los mensajes
void processInputMessage(const std::string& mensaje, std::set<int>& pendientes,
                         std::map<int, int>& piezasOriginales, std::map<int, int>& piezasAjustadas,
                         const std::string& sku, const std::string& lote, bool& loop_break) { // loop_break es pasada por referencia
    
    if (mensaje == "salir") {
        loop_break = true; // Modifica la variable original en main()
        std::cout << "PROCESS_INPUT_MSG: Comando 'salir' (programa) recibido." << std::endl;
        return;
    }

    std::cout << "PROCESS_INPUT_MSG: Entrando con mensaje: '";
    for (char c : mensaje) {
        if (isprint(c)) {
            std::cout << c;
        } else {
            std::cout << "[" << static_cast<int>(static_cast<unsigned char>(c)) << "]";
        }
    }
    std::cout << "' (Longitud: " << mensaje.length() << ")" << std::endl;
    std::cout << "PROCESS_INPUT_MSG: 'pendientes' actualmente contiene: { ";
    bool first_p_print = true; // Renombrado para evitar colisión si 'first_p' se usa en otro lugar
    for (int p_val : pendientes) { // Renombrado para evitar colisión
        if (!first_p_print) std::cout << ", ";
        std::cout << p_val;
        first_p_print = false;
    }
    std::cout << " }" << std::endl;

    if (mensaje.rfind("boton_", 0) == 0) {
        std::cout << "PROCESS_INPUT_MSG: Mensaje COMIENZA con 'boton_'." << std::endl;
        int ordenDeVentaConfirmada = 0;
        try {
            ordenDeVentaConfirmada = std::stoi(mensaje.substr(6));
            std::cout << "DEBUG: Procesando boton_" << ordenDeVentaConfirmada << ". Orden parseada: " << ordenDeVentaConfirmada << std::endl;
            std::cout << "DEBUG: Resultado de pendientes.count(" << ordenDeVentaConfirmada << "): " << pendientes.count(ordenDeVentaConfirmada) << " (1=encontrado, 0=no encontrado)" << std::endl;
            if (pendientes.count(ordenDeVentaConfirmada)) {
                pendientes.erase(ordenDeVentaConfirmada);
                enviarAArduino("APAGAR_DESTINO_" + std::to_string(ordenDeVentaConfirmada));
                
                int original_piezas = piezasOriginales[ordenDeVentaConfirmada]; // Renombrado
                int final_piezas = piezasAjustadas[ordenDeVentaConfirmada];
                registrarBackorder(sku, ordenDeVentaConfirmada, original_piezas, final_piezas, lote);
                std::cout << "ORDEN DE VENTA " << ordenDeVentaConfirmada << " confirmada con piezas: " << final_piezas << ".\n";
            } else {
                std::cout << "Orden de venta " << ordenDeVentaConfirmada << " (parseada de '" << mensaje << "') NO ENCONTRADA en pendientes o ya confirmada.\n";
            }
        } catch (const std::invalid_argument& ia) {
            std::cerr << "ERROR STOI (boton_ invalid_argument): No se pudo convertir '" << mensaje.substr(6) << "' a entero. Mensaje original: '" << mensaje << "'" << std::endl;
        } catch (const std::out_of_range& oor) {
            std::cerr << "ERROR STOI (boton_ out_of_range): Número fuera de rango en '" << mensaje.substr(6) << "'. Mensaje original: '" << mensaje << "'" << std::endl;
        }
    }    // Manejo de mensajes de confirmación de LED, DISPLAY_ACTUALIZADO, PIEZAS_AJUSTADAS (NUEVO)
    else if (mensaje.rfind("LED_ENCENDIDO_", 0) == 0 || mensaje.rfind("LED_APAGADO_", 0) == 0 || 
             mensaje.rfind("DISPLAY_ACTUALIZADO_OV_", 0) == 0 || mensaje.rfind("PIEZAS_AJUSTADAS_OV_", 0) == 0) {
        std::cout << "PROCESS_INPUT_MSG: Mensaje de confirmacion de Arduino ignorado: '" << mensaje << "'" << std::endl;
    }
    else if (mensaje == "Arduino: Iniciado.") {
        std::cout << "PROCESS_INPUT_MSG: Mensaje 'Arduino: Iniciado.' ignorado." << std::endl;
    }
    else if ((mensaje[0] == '+' || mensaje[0] == '-') && mensaje.length() > 1) {
        int destino = 0;
        try { 
            destino = std::stoi(mensaje.substr(1));
            if (pendientes.count(destino)) {
                int& actual = piezasAjustadas[destino];     
                int original = piezasOriginales[destino]; 

                if (mensaje[0] == '+') {
                    if (actual >= original) { 
                        actual = 0;           
                    } else {
                        actual++;             
                    }
                } else if (mensaje[0] == '-') {
                    if (actual <= 0) {    
                        actual = original;    
                    } else {
                        actual--;             
                    }
                }
                std::cout << "[DISPLAY ORDEN DE VENTA " << destino << "]: " << actual << std::endl;
                // --- NUEVO: Enviar el valor ajustado al display ---
                ajustarPiezasDisplay(destino, actual);
            } else {
                std::cout << "Orden de venta " << destino << " (para ajuste +/-) no válida o ya confirmada.\n";
            }
        } catch (const std::invalid_argument& ia) {
            std::cerr << "ERROR STOI (+/- invalid_argument): No se pudo convertir '" << mensaje.substr(1) << "' a entero. Mensaje original: '" << mensaje << "'" << std::endl;
        } catch (const std::out_of_range& oor) {
            std::cerr << "ERROR STOI (+/- out_of_range): Número fuera de rango en '" << mensaje.substr(1) << "'. Mensaje original: '" << mensaje << "'" << std::endl;
        }
    } 
    else {
        if (!mensaje.empty() && mensaje != "\r") {    
            std::cout << "Entrada no válida. Usa 'boton_X', '+X', '-X' o 'salir'. Mensaje recibido: '" << mensaje << "'" << std::endl;
        }
    }
}


int main() {
    std::string puertoCom = "\\\\.\\COM3"; 
    // Para hacerlo configurable:
    // std::cout << "Ingresa el puerto COM (ej. \\\\.\\COM3 o /dev/ttyUSB0): ";
    // std::getline(std::cin, puertoCom);
    // puertoCom = trim(puertoCom);

    if (!inicializarPuertoSerial(puertoCom)) {
        return 1;
    }

    std::thread hiloLectura(leerDeArduino);
    std::this_thread::sleep_for(std::chrono::seconds(2));

    std::string archivoCSV;
    std::cout << "Ingresa el nombre del archivo CSV con los productos: ";
    std::getline(std::cin, archivoCSV); 
    archivoCSV = trim(archivoCSV);


    auto productos = cargarProductosDesdeCSV(archivoCSV);
    if (productos.empty()) {
        std::cout << "No se cargaron productos o el archivo '" << archivoCSV << "' no existe/está vacío. Saliendo...\n";
        hiloLecturaActivo = false;
        if (hiloLectura.joinable()) hiloLectura.join();
        CloseHandle(arduino);
        return 1;
    }

    bool continuar_programa = true; // Renombrado de 'continuar' para claridad
    while (continuar_programa) {
        std::string sku_scan, lote_scan; 
        std::cout << "\nEscanea un producto (SKU Lote) o escribe 'salir_programa' para terminar: ";
        
        std::string linea_entrada_main; // Renombrado
        std::getline(std::cin, linea_entrada_main);
        linea_entrada_main = trim(linea_entrada_main);
        
        // Manejar "salir_programa" aquí antes de intentar parsear SKU y Lote
        if (linea_entrada_main == "salir_programa") {
            continuar_programa = false;
            break; // Sale del bucle while(continuar_programa)
        }

        std::stringstream ss_entrada_main(linea_entrada_main); // Renombrado
        ss_entrada_main >> sku_scan >> lote_scan;
        
        sku_scan = trim(sku_scan); 
        lote_scan = trim(lote_scan);

        if (sku_scan.empty()){ 
            continue; 
        }

        // Bandera para ser modificada por processInputMessage si se comanda "salir" (para todo el programa)
        bool solicitar_salida_programa = false; 

        if (productos.count(sku_scan)) {
            auto& entradas_producto = productos[sku_scan];
            std::vector<EntradaProducto> entradasValidas_producto;
            for (const auto& entrada_csv : entradas_producto) {
                if (entrada_csv.lote == lote_scan) {
                    entradasValidas_producto.push_back(entrada_csv);
                }
            }
            
            if (entradasValidas_producto.empty()) {
                std::cout << "No hay entradas para el SKU '" << sku_scan << "' con el lote '" << lote_scan << "'.\n";
                continue;
            }

            std::set<int> pendientes_producto;
            std::map<int, int> piezasAjustadas_producto;
            std::map<int, int> piezasOriginales_producto;

            for (const auto& entrada_valida : entradasValidas_producto) {
                int odv = entrada_valida.ordenDeVenta;
                pendientes_producto.insert(odv);
                piezasOriginales_producto[odv] = entrada_valida.piezas;
                piezasAjustadas_producto[odv] = entrada_valida.piezas;
                std::cout << "Activando ORDEN DE VENTA " << odv << " - Piezas: " << entrada_valida.piezas << " - Lote: " << entrada_valida.lote << std::endl;
                enviarAArduino("ENCENDER_DESTINO_" + std::to_string(odv));
                // --- NUEVO: Enviar el número de piezas al display para la OV activa ---
                enviarDatosDisplay(odv, entrada_valida.piezas); //Nueva función
            }

            std::cout << "DEBUG: Contenido inicial de 'pendientes_producto': { ";
            bool first_m_print = true;
            for (int ov_val : pendientes_producto) {
                if(!first_m_print) std::cout << ", ";
                std::cout << ov_val;
                first_m_print = false;
            }
            std::cout << " }" << std::endl;

            bool finalizar_scan_actual = false;
            bool prompt_mostrado_ciclo = false;

            while (!pendientes_producto.empty() && !finalizar_scan_actual && continuar_programa) {
                std::string mensajeRecibido = obtenerMensajeSerial();

                if (!mensajeRecibido.empty()) {
                    prompt_mostrado_ciclo = false;
                    std::cout << "DEBUG_SERIAL: Recibido de Arduino '" << mensajeRecibido << "' (Longitud: " << mensajeRecibido.length() << ")" << std::endl;
                    processInputMessage(mensajeRecibido, pendientes_producto, piezasOriginales_producto, piezasAjustadas_producto, sku_scan, lote_scan, solicitar_salida_programa);
                } else {
                    if (!prompt_mostrado_ciclo && !finalizar_scan_actual) {
                        std::cout << "[Esperando botón/comando, 'salir_prod' (producto) o 'salir' (programa)]: ";
                        prompt_mostrado_ciclo = true;
                    }
                    
                    // --- DECLARACIÓN DE inputManual ESTÁ AQUÍ ---
                    std::string inputManual = ""; 
                    // --- FIN DE DECLARACIÓN ---
                    
                    if (std::cin.rdbuf()->in_avail() > 0) {
                        char peek_char = std::cin.peek();
                        if (peek_char != EOF) {
                            if (peek_char != '\n') {
                                std::getline(std::cin, inputManual); 
                                inputManual = trim(inputManual);
                            } else { 
                                std::cin.get(); // Cambiado de std::cin.ignore()
                            }
                            prompt_mostrado_ciclo = false; 
                        }
                    }

                    if (!inputManual.empty()) {
                        std::cout << "DEBUG_MANUAL: Ingresado manualmente '" << inputManual << "' (Longitud: " << inputManual.length() << ")" << std::endl;
                        if (inputManual == "salir_prod") { 
                            finalizar_scan_actual = true;
                            std::cout << "Saliendo del producto actual..." << std::endl;
                            // --- NUEVO: Limpiar display al salir del producto actual ---
                            enviarAArduino("CLEAR_DISPLAY");
                        } else {
                             // "salir" (para todo el programa) se maneja dentro de processInputMessage
                             processInputMessage(inputManual, pendientes_producto, piezasOriginales_producto, piezasAjustadas_producto, sku_scan, lote_scan, solicitar_salida_programa);
                        }
                    } else {
                        std::this_thread::sleep_for(std::chrono::milliseconds(50)); 
                    }
                }
                
                if (solicitar_salida_programa) { 
                    continuar_programa = false; 
                    finalizar_scan_actual = true; // También finaliza el scan actual para salir de este bucle interno
                }
            } // Fin del bucle while (!pendientes_producto.empty()...)

            // Lógica después de procesar un producto
            if (solicitar_salida_programa) {
                 std::cout << "Solicitud de finalización de programa procesada." << std::endl;
                 // --- NUEVO: Limpiar display al finalizar el programa ---
                 enviarAArduino("CLEAR_DISPLAY");
            } else if (finalizar_scan_actual && !pendientes_producto.empty()){ // Si se usó "salir_prod" y quedaron pendientes
                 std::cout << "ADVERTENCIA: El producto se interrumpió con las siguientes OVs aun activas: ";
                 for (int ov_restante : pendientes_producto) {
                     std::cout << ov_restante << " ";
                     enviarAArduino("APAGAR_DESTINO_" + std::to_string(ov_restante)); 
                 }
                 std::cout << std::endl;
            } else if (pendientes_producto.empty()) {
                std::cout << "Todas las órdenes del producto '" << sku_scan << "' Lote '" << lote_scan << "' han sido procesadas." << std::endl;
                // --- NUEVO: Mensaje de "Todas las OVs procesadas" en display ---
                enviarAArduino("CLEAR_DISPLAY");
                enviarAArduino("DISPLAY_MESSAGE_COMPLETADO");
            }
            
            // Si 'salir_programa' se activó (solicitar_salida_programa es true y continuar_programa es false)
            if (!continuar_programa) {
                break; // Rompe el bucle exterior while(continuar_programa)
            }
            
            std::string opcion_continuar;
            std::cout << "¿Deseas escanear otro producto? (s/n) o escribe 'salir_programa': ";
            std::getline(std::cin, opcion_continuar); 
            opcion_continuar = trim(opcion_continuar);

            if (opcion_continuar == "salir_programa" || opcion_continuar == "n" || opcion_continuar == "N") {
                continuar_programa = false;
            } else if (opcion_continuar != "s" && opcion_continuar != "S") {
                std::cout << "Opción no reconocida, se interpretará como 'sí' para continuar..." << std::endl;
            }
        } else { // Fin de if (productos.count(sku_scan))
             if (!sku_scan.empty()) { 
                std::cout << "Producto con SKU '" << sku_scan << "' no reconocido.\n";
            }
        }
    } // Fin del bucle while (continuar_programa)

    std::cout << "Programa finalizado.\n";
    hiloLecturaActivo = false;
    if (hiloLectura.joinable()) hiloLectura.join();
    CloseHandle(arduino);
    return 0;
}