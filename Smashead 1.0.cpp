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
#include <cctype>    // Para isprint // NUEVO: Necesario para isprint

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
// Se define aquí para que main() y otras funciones puedan usarla
void processInputMessage(const std::string& mensaje, std::set<int>& pendientes,
                         std::map<int, int>& piezasOriginales, std::map<int, int>& piezasAjustadas,
                         const std::string& sku, const std::string& lote, bool& loop_break);

// Función para inicializar el puerto serial
bool inicializarPuertoSerial(const std::string& puerto) {
    // Abre el puerto serial
    arduino = CreateFileA(puerto.c_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (arduino == INVALID_HANDLE_VALUE) {
        std::cerr << "No se pudo abrir el puerto serial. Asegúrate de que el puerto COM3 esté disponible y el Monitor Serial de Arduino IDE esté cerrado." << std::endl;
        return false;
    }

    // Configura los parámetros del puerto serial (BaudRate, ByteSize, StopBits, Parity)
    DCB dcbSerialParams = {0};
    dcbSerialParams.DCBlength = sizeof(dcbSerialParams);

    if (!GetCommState(arduino, &dcbSerialParams)) {
        std::cerr << "Error obteniendo configuración del puerto." << std::endl;
        CloseHandle(arduino);
        return false;
    }

    dcbSerialParams.BaudRate = CBR_9600; // Tasa de baudios: DEBE COINCIDIR con el Arduino
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
    WriteFile(arduino, mensajeConSalto.c_str(), mensajeConSalto.size(), &bytesEscritos, NULL);
    std::cout << "[ARDUINO <- PC] Enviando: " << mensaje << std::endl;
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
        std::this_thread::sleep_for(std::chrono::milliseconds(1)); // Reducido para mayor reactividad
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

    std::getline(file, linea); // Ignorar el encabezado del CSV

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
        piezasStr = trim(trim(piezasStr)); // Doble trim por si acaso

        if (sku.empty() || lote.empty() || ordenStr.empty() || piezasStr.empty()) {
            std::cerr << "Línea con formato incorrecto (se ignorará): " << linea << std::endl;
            continue;
        }

        int ordenDeVenta = std::stoi(ordenStr);
        int piezas = std::stoi(piezasStr);

        productos[sku].push_back({ordenDeVenta, piezas, lote});
    }

    return productos;
}

// Función auxiliar para procesar los mensajes (ya sea de Arduino o entrada manual)
void processInputMessage(const std::string& mensaje, std::set<int>& pendientes,
                         std::map<int, int>& piezasOriginales, std::map<int, int>& piezasAjustadas,
                         const std::string& sku, const std::string& lote, bool& loop_break) {
    
    // El comando "salir" se maneja en el bucle principal porque afecta el 'break'
    // También se puede manejar aquí para una salida más directa desde esta función si es necesario.
    if (mensaje == "salir") {
        loop_break = true; // Indica al bucle principal que debe salir
        std::cout << "PROCESS_INPUT_MSG: Comando 'salir' recibido." << std::endl; // NUEVO DEBUG
        return;
    }

    // --- NUEVO DEBUG DETALLADO ---
    std::cout << "PROCESS_INPUT_MSG: Entrando con mensaje: '";
    for (char c : mensaje) { // Imprimir caracteres uno por uno para ver invisibles
        if (isprint(c)) {
            std::cout << c;
        } else {
            std::cout << "[" << static_cast<int>(static_cast<unsigned char>(c)) << "]"; // Muestra código ASCII de no imprimibles
        }
    }
    std::cout << "' (Longitud: " << mensaje.length() << ")" << std::endl;
    std::cout << "PROCESS_INPUT_MSG: 'pendientes' actualmente contiene: { ";
    bool first_p = true;
    for (int p : pendientes) {
        if (!first_p) std::cout << ", ";
        std::cout << p;
        first_p = false;
    }
    std::cout << " }" << std::endl;
    // --- FIN NUEVO DEBUG DETALLADO ---

    if (mensaje.rfind("boton_", 0) == 0) {
        std::cout << "PROCESS_INPUT_MSG: Mensaje COMIENZA con 'boton_'." << std::endl; // NUEVO DEBUG
        int ordenDeVentaConfirmada = 0; // Inicializar por si stoi falla
        try { // --- NUEVO TRY-CATCH ---
            ordenDeVentaConfirmada = std::stoi(mensaje.substr(6));
            std::cout << "DEBUG: Procesando boton_" << ordenDeVentaConfirmada << ". Orden parseada: " << ordenDeVentaConfirmada << std::endl;
            std::cout << "DEBUG: Resultado de pendientes.count(" << ordenDeVentaConfirmada << "): " << pendientes.count(ordenDeVentaConfirmada) << " (1=encontrado, 0=no encontrado)" << std::endl;

            if (pendientes.count(ordenDeVentaConfirmada)) { // Usa count para verificar existencia
                pendientes.erase(ordenDeVentaConfirmada); // Elimina la orden de venta de pendientes
                enviarAArduino("APAGAR_DESTINO_" + std::to_string(ordenDeVentaConfirmada));
                int original = piezasOriginales[ordenDeVentaConfirmada];
                int final = piezasAjustadas[ordenDeVentaConfirmada];
                registrarBackorder(sku, ordenDeVentaConfirmada, original, final, lote);
                std::cout << "ORDEN DE VENTA " << ordenDeVentaConfirmada << " confirmada con piezas: " << final << ".\n";
            } else {
                // --- MODIFICADO DEBUG ELSE ---
                std::cout << "Orden de venta " << ordenDeVentaConfirmada << " (parseada de '" << mensaje << "') NO ENCONTRADA en pendientes o ya confirmada.\n";
            }
        } catch (const std::invalid_argument& ia) {
            std::cerr << "ERROR STOI (invalid_argument): No se pudo convertir '" << mensaje.substr(6) << "' a entero. Mensaje original: '" << mensaje << "'" << std::endl;
        } catch (const std::out_of_range& oor) {
            std::cerr << "ERROR STOI (out_of_range): Número fuera de rango en '" << mensaje.substr(6) << "'. Mensaje original: '" << mensaje << "'" << std::endl;
        }
        // --- FIN NUEVO TRY-CATCH ---
    }    
    // Manejo de mensajes de confirmación de LED (ignoramos en este punto)
    else if (mensaje.rfind("LED_ENCENDIDO_", 0) == 0 || mensaje.rfind("LED_APAGADO_", 0) == 0) {
        // Ignorar estas confirmaciones para evitar "Entrada no válida"
        std::cout << "PROCESS_INPUT_MSG: Mensaje de confirmacion LED ignorado: '" << mensaje << "'" << std::endl; // NUEVO DEBUG OPCIONAL
    }
    // Manejo del mensaje de inicio de Arduino (ignoramos para limpiar la consola)
    else if (mensaje == "Arduino: Iniciado.") {
        // Ignorar
        std::cout << "PROCESS_INPUT_MSG: Mensaje 'Arduino: Iniciado.' ignorado." << std::endl; // NUEVO DEBUG OPCIONAL
    }
    else if ((mensaje[0] == '+' || mensaje[0] == '-') && mensaje.length() > 1) {
        int destino = 0;
        try { // --- NUEVO TRY-CATCH para +/- ---
            destino = std::stoi(mensaje.substr(1));
            if (pendientes.count(destino)) {
                int& actual = piezasAjustadas[destino];
                int original = piezasOriginales[destino];
                if (mensaje[0] == '+') {
                    actual = (actual + 1 > original) ? original : actual + 1; // No exceder el original
                } else if (mensaje[0] == '-') {
                    actual = (actual == 0) ? 0 : actual - 1; // No bajar de 0
                }
                std::cout << "[DISPLAY ORDEN DE VENTA " << destino << "]: " << actual << std::endl;
                // Aquí podrías enviar un comando al Arduino para actualizar un display si tu hardware lo soporta
                // enviarAArduino("ACTUALIZAR_DISPLAY_" + std::to_string(destino) + "_" + std::to_string(actual));
            } else {
                std::cout << "Orden de venta " << destino << " (para ajuste +/-) no válida o ya confirmada.\n";
            }
        } catch (const std::invalid_argument& ia) {
            std::cerr << "ERROR STOI (+/- invalid_argument): No se pudo convertir '" << mensaje.substr(1) << "' a entero. Mensaje original: '" << mensaje << "'" << std::endl;
        } catch (const std::out_of_range& oor) {
            std::cerr << "ERROR STOI (+/- out_of_range): Número fuera de rango en '" << mensaje.substr(1) << "'. Mensaje original: '" << mensaje << "'" << std::endl;
        }
        // --- FIN NUEVO TRY-CATCH para +/- ---
    } else {
        // Solo imprime si no es un mensaje vacío o un salto de línea residual
        if (!mensaje.empty() && mensaje != "\r") {    
            std::cout << "Entrada no válida. Usa 'boton_X', '+X' o '-X'. Mensaje recibido: '" << mensaje << "'" << std::endl; // MODIFICADO DEBUG
        }
    }
}


int main() {
    // Intenta inicializar el puerto serial
    if (!inicializarPuertoSerial("\\\\.\\COM3")) { // Asegúrate que este es tu puerto COM correcto
        return 1; // Sale si no se puede abrir el puerto
    }

    // Inicia el hilo de lectura del puerto serial
    std::thread hiloLectura(leerDeArduino);

    // Espera un poco para que el Arduino se reinicie y el puerto esté listo
    std::this_thread::sleep_for(std::chrono::seconds(2));

    std::string archivoCSV;
    std::cout << "Ingresa el nombre del archivo CSV con los productos: ";
    std::cin >> archivoCSV;
    // Limpiar el buffer de entrada después de leer el nombre del archivo
    std::cin.clear(); // Limpiar banderas de error si las hubiera
    // std::string dummy_line; // No necesitas declarar de nuevo si ya existe una global o la pasas
    // std::getline(std::cin, dummy_line); // Consumir el resto de la línea (incluyendo el '\n')
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n'); // Manera más robusta de limpiar buffer


    // Carga los productos desde el archivo CSV
    auto productos = cargarProductosDesdeCSV(archivoCSV);
    if (productos.empty()) {
        std::cout << "No se cargaron productos. Saliendo...\n";
        // Detiene el hilo de lectura y espera a que termine antes de salir
        hiloLecturaActivo = false;
        if (hiloLectura.joinable()) hiloLectura.join(); // Asegurarse que es joinable
        CloseHandle(arduino);
        return 1;
    }

    bool continuar = true;
    while (continuar) {
        std::string sku, lote;
        std::cout << "\nEscanea un producto (SKU Lote) o escribe 'salir_programa' para terminar: "; // Modificado prompt
        std::cin >> sku;
        if (sku == "salir_programa") {
            continuar = false;
            break;
        }
        std::cin >> lote;
        // Limpiar el buffer de entrada después de leer SKU y Lote
        std::cin.clear(); // Limpiar banderas de error si las hubiera
        // std::getline(std::cin, dummy_line); // Consumir el resto de la línea (incluyendo el '\n')
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n'); // Manera más robusta


        sku = trim(sku);
        lote = trim(lote);

        if (productos.count(sku)) { // Usa count para verificar si el SKU existe
            auto& entradas = productos[sku];
            std::vector<EntradaProducto> entradasValidas;

            // Filtra las entradas por lote
            for (const auto& entrada : entradas) {
                if (entrada.lote == lote) {
                    entradasValidas.push_back(entrada);
                }
            }
            
            if (entradasValidas.empty()) {
                std::cout << "No hay entradas para ese lote o el lote no coincide con el SKU.\n";
                continue;
            }

            std::set<int> pendientes; // Órdenes de venta pendientes de confirmación
            std::map<int, int> piezasAjustadas; // Piezas ajustadas para cada orden de venta
            std::map<int, int> piezasOriginales; // Piezas originales para cada orden de venta

            // Activa las órdenes de venta y envía comandos al Arduino
            for (const auto& entrada : entradasValidas) {
                int odv = entrada.ordenDeVenta;
                pendientes.insert(odv);
                piezasOriginales[odv] = entrada.piezas;
                piezasAjustadas[odv] = entrada.piezas;
                std::cout << "Activando ORDEN DE VENTA " << odv << " - Piezas: " << entrada.piezas << " - Lote: " << entrada.lote << std::endl;
                enviarAArduino("ENCENDER_DESTINO_" + std::to_string(odv));
                std::cout << "[DISPLAY ORDEN DE VENTA " << odv << "]: " << entrada.piezas << std::endl;
            }

            // --- DEBUG: Contenido inicial de 'pendientes' ---
            std::cout << "DEBUG: Contenido inicial de 'pendientes': { ";
            bool first_m = true;
            for (int ov : pendientes) {
                if(!first_m) std::cout << ", ";
                std::cout << ov;
                first_m = false;
            }
            std::cout << " }" << std::endl;
            // --- FIN DEBUG ---

            bool current_scan_finished = false; // Esta variable se pasa a processInputMessage para que pueda ser modificada
            static bool prompt_displayed_this_cycle = false; // Para evitar imprimir el prompt muchas veces

            // Bucle para esperar confirmaciones de botones o ajustes de piezas
            while (!pendientes.empty() && !current_scan_finished) {
                std::string mensajeRecibido = obtenerMensajeSerial(); // (1) Intentar obtener mensaje del Arduino primero

                if (!mensajeRecibido.empty()) { // (A) Si hay un mensaje de Arduino, procesarlo
                    prompt_displayed_this_cycle = false; // Resetear el prompt si se recibe algo serial
                    // --- MODIFICADO DEBUG ---
                    std::cout << "DEBUG_SERIAL: Recibido de Arduino '" << mensajeRecibido << "' (Longitud: " << mensajeRecibido.length() << ")" << std::endl;
                    processInputMessage(mensajeRecibido, pendientes, piezasOriginales, piezasAjustadas, sku, lote, current_scan_finished);
                } else { // (B) Si no hay mensaje del Arduino, revisar entrada manual
                    if (!prompt_displayed_this_cycle && !current_scan_finished) { // No mostrar prompt si ya terminamos
                        std::cout << "[Esperando botón/comando o entrada manual ('salir' para terminar este producto)]: ";
                        prompt_displayed_this_cycle = true;
                    }

                    std::string inputManual = "";
                    // Esta parte necesita ser no bloqueante para que los mensajes seriales se sigan procesando
                    // Usar std::cin.rdbuf()->in_avail() es una forma, pero puede ser dependiente de la plataforma/terminal
                    // Una forma más simple para probar es un timeout corto en la espera de input manual
                    // O reestructurar para que el input manual sea más explícito.
                    // Por ahora, mantendremos la lógica de peek, pero puede ser menos reactiva.

                    if (std::cin.rdbuf()->in_avail() > 0) { // Chequea si hay algo en el buffer de std::cin
                        if (std::cin.peek() != EOF && std::cin.peek() != '\n') {
                            std::getline(std::cin, inputManual); 
                            inputManual = trim(inputManual);
                            prompt_displayed_this_cycle = false; 
                        } else {
                            // Si solo hay un '\n' o EOF, consumir el '\n' si existe
                            if(std::cin.peek() == '\n') std::cin.ignore();
                        }
                    }


                    if (!inputManual.empty()) { // (C) Si se obtuvo entrada manual, procesarla
                        // --- MODIFICADO DEBUG ---
                        std::cout << "DEBUG_MANUAL: Ingresado manualmente '" << inputManual << "' (Longitud: " << inputManual.length() << ")" << std::endl;
                        if (inputManual == "salir") { // Comando para salir del producto actual
                            current_scan_finished = true; // Esto romperá el bucle interno
                            std::cout << "Saliendo del producto actual..." << std::endl;
                        } else {
                             processInputMessage(inputManual, pendientes, piezasOriginales, piezasAjustadas, sku, lote, current_scan_finished);
                        }
                    } else { // (D) Si no hay mensajes de Arduino ni entrada manual, esperar un poco
                        std::this_thread::sleep_for(std::chrono::milliseconds(20)); // Aumentado un poco el sleep
                    }
                }
                if (current_scan_finished && pendientes.empty()) {
                    std::cout << "Todas las órdenes procesadas o se indicó salir del producto actual." << std::endl;
                } else if (current_scan_finished && !pendientes.empty()){
                    std::cout << "Saliendo del producto actual, aun habían pendientes." << std::endl;
                }
            }

            // Si salimos del bucle y aún hay pendientes, podrías querer apagarlos o registrarlos
            if (!pendientes.empty()) {
                std::cout << "ADVERTENCIA: El producto se completó o se interrumpió con las siguientes OVs aun activas en 'pendientes': ";
                for (int ov_restante : pendientes) {
                    std::cout << ov_restante << " ";
                    enviarAArduino("APAGAR_DESTINO_" + std::to_string(ov_restante)); // Apagar LEDs restantes
                    // Decidir si registrar como no confirmado o con piezas 0
                    // registrarBackorder(sku, ov_restante, piezasOriginales[ov_restante], 0, lote); 
                }
                std::cout << std::endl;
            }


            std::string opcion;
            std::cout << "¿Deseas escanear otro producto? (s/n) o 'salir_programa': ";
            std::cin >> opcion;
            opcion = trim(opcion);
            // std::cin.clear(); // Ya no es necesario si usamos ignore() bien
            // std::getline(std::cin, dummy_line); // Limpiar buffer después de la opción
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');


            if (opcion == "salir_programa" || opcion == "n" || opcion == "N") {
                continuar = false;
            }
        } else {
            std::cout << "Producto no reconocido.\n";
        }
    }

    std::cout << "Programa finalizado.\n";
    // Detiene el hilo de lectura y espera a que termine
    hiloLecturaActivo = false;
    if (hiloLectura.joinable()) hiloLectura.join(); // Asegurarse que es joinable
    // Cierra el handle del puerto serial
    CloseHandle(arduino);
    return 0;
}