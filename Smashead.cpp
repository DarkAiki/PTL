// C++ Standard Library
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <queue>
#include <algorithm>
#include <thread>
#include <mutex>
#include <chrono>
#include <limits>
#include <cctype>
#include <windows.h>
#include <conio.h> 
#include <iomanip>

// --- CONFIGURACION FIJA ---
const std::string PUERTO_SERIAL_DEFAULT = "COM8"; 
#define BAUDRATE 9600
#define MAX_BUFFER_SIZE 256
// --------------------------

// Variables globales serial
HANDLE hSerial;
std::mutex serialMutex;
std::queue<std::string> colaMensajesRecibidos;
bool hiloLecturaActivo = true;

std::string nombreArchivoBackorder = "";
std::string archivoCSVGlobal = ""; 

struct EntradaProducto {
    std::string sku;
    std::string lote;
    int ordenDeVenta;
    int piezas;
    int destino;
    bool yaSurtido; // <--- NUEVA BANDERA DE PROTECCIÓN
};

struct DatosCargados {
    std::map<std::string, std::vector<EntradaProducto>> productosPorSKU;
    bool cargadoExitosamente = false;
};

using Producto = EntradaProducto;

// Prototipos Actualizados (ahora reciben el mapa de punteros para marcar como surtido)
void processInputMessage(const std::string& mensaje, std::set<int>& pendientes,
                         std::map<int, int>& piezasOriginales, std::map<int, int>& piezasAjustadas,
                         const std::string& sku, const std::string& lote, bool& loop_break,
                         const std::map<int, int>& destino_a_OV, std::queue<int>& destinosParaConfirmar,
                         std::map<int, EntradaProducto*>& mapaEntradasActivas);

void handleConfirmation(int destino, std::set<int>& pendientes,
                        std::map<int, int>& piezasOriginales, std::map<int, int>& piezasAjustadas,
                        const std::string& sku, const std::string& loteRequerido,
                        const std::map<int, int>& destino_a_OV,
                        std::map<int, EntradaProducto*>& mapaEntradasActivas);

// --- FUNCIONES SERIAL ---
bool inicializarPuertoSerial(const std::string& puerto) {
    hSerial = CreateFile(puerto.c_str(), GENERIC_READ | GENERIC_WRITE, 0, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (hSerial == INVALID_HANDLE_VALUE) {
        std::cerr << "ERROR: Puerto serial '" << puerto << "' no encontrado." << std::endl;
        return false;
    }
    DCB dcb = {0}; dcb.DCBlength = sizeof(dcb); GetCommState(hSerial, &dcb);
    dcb.BaudRate = BAUDRATE; dcb.ByteSize = 8; dcb.StopBits = ONESTOPBIT; dcb.Parity = NOPARITY;
    SetCommState(hSerial, &dcb);
    COMMTIMEOUTS timeouts = {0};
    timeouts.ReadIntervalTimeout = 50; timeouts.ReadTotalTimeoutConstant = 50; timeouts.ReadTotalTimeoutMultiplier = 10;
    timeouts.WriteTotalTimeoutConstant = 50; timeouts.WriteTotalTimeoutMultiplier = 10;
    SetCommTimeouts(hSerial, &timeouts);
    std::cout << "Puerto serial " << puerto << " inicializado." << std::endl;
    return true;
}

void enviarAArduino(const std::string& comando) {
    std::lock_guard<std::mutex> lock(serialMutex);
    std::string msg = comando + "\n";
    DWORD bytesWritten;
    if (hSerial != INVALID_HANDLE_VALUE) WriteFile(hSerial, msg.c_str(), msg.length(), &bytesWritten, NULL);
}

std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\n\r");
    if (std::string::npos == first) return str;
    size_t last = str.find_last_not_of(" \t\n\r");
    return str.substr(first, (last - first + 1));
}

void hiloLecturaSerial() {
    char buffer[MAX_BUFFER_SIZE];
    DWORD bytesRead;
    std::string currentData = "";
    while (hiloLecturaActivo) {
        if (hSerial != INVALID_HANDLE_VALUE && ReadFile(hSerial, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0) {
            buffer[bytesRead] = '\0';
            currentData += std::string(buffer);
            size_t newlinePos;
            while ((newlinePos = currentData.find('\n')) != std::string::npos) {
                std::string message = currentData.substr(0, newlinePos);
                message = trim(message);
                if (!message.empty()) {
                    std::lock_guard<std::mutex> lock(serialMutex);
                    colaMensajesRecibidos.push(message);
                }
                currentData.erase(0, newlinePos + 1);
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

std::string obtenerMensajeSerial() {
    std::lock_guard<std::mutex> lock(serialMutex);
    if (!colaMensajesRecibidos.empty()) {
        std::string mensaje = colaMensajesRecibidos.front();
        colaMensajesRecibidos.pop();
        return mensaje;
    }
    return "";
}

// --- LOGICA PRINCIPAL ---

void registrarBackorder(const std::string& sku, int ordenDeVenta, int destino,
                        int piezasOriginales, int piezasSurtidas,
                        const std::string& loteRequerido, const std::string& loteConfirmado,
                        const std::string& motivo) {
    std::ofstream archivo(nombreArchivoBackorder, std::ios::app);
    if (!archivo.is_open()) return;
    archivo.seekp(0, std::ios::end);
    if (archivo.tellp() == 0) archivo << "SKU,LoteRequerido,LoteConfirmado,Orden de venta,Destino,PiezasRequeridas,CantidadSurtida,Motivo\n";
    archivo << sku << "," << loteRequerido << "," << loteConfirmado << "," << ordenDeVenta << ","
            << destino << "," << piezasOriginales << "," << piezasSurtidas << "," << motivo << "\n";
}

DatosCargados cargarProductosDesdeCSV(const std::string& archivo) {
    DatosCargados datos;
    std::ifstream file(archivo);
    std::string line;

    if (!file.is_open()) {
        std::cerr << "ERROR: No se pudo abrir el archivo CSV." << std::endl;
        return datos;
    }
    std::getline(file, line); // Saltar encabezado

    std::map<std::string, int> asignacionDestinos;
    int siguienteDestinoDisponible = 1;

    while (std::getline(file, line)) {
        std::stringstream ss(line);
        std::string sku, lote, ordenStr, piezasStr;
        std::getline(ss, sku, ','); std::getline(ss, lote, ',');
        std::getline(ss, ordenStr, ','); std::getline(ss, piezasStr, ',');

        sku = trim(sku); lote = trim(lote); ordenStr = trim(ordenStr); piezasStr = trim(piezasStr); 
        size_t posEspacio = lote.find(' '); if (posEspacio != std::string::npos) lote = lote.substr(0, posEspacio);

        if (sku.empty()) continue;

        try {
            int orden = std::stoi(ordenStr);
            int piezas = std::stoi(piezasStr);
            
            int destinoAsignado;
            if (asignacionDestinos.find(ordenStr) != asignacionDestinos.end()) {
                destinoAsignado = asignacionDestinos[ordenStr];
            } else {
                destinoAsignado = siguienteDestinoDisponible;
                asignacionDestinos[ordenStr] = destinoAsignado;
                siguienteDestinoDisponible++;
            }

            // Inicializamos yaSurtido en false
            datos.productosPorSKU[sku].push_back({sku, lote, orden, piezas, destinoAsignado, false});

        } catch (...) {}
    }

    if (datos.productosPorSKU.empty()) {
        std::cerr << "ERROR: CSV invalido." << std::endl;
    } else {
        std::cout << "Carga exitosa. Destinos asignados: " << (siguienteDestinoDisponible - 1) << std::endl;
        datos.cargadoExitosamente = true;
    }
    return datos;
}

std::string generarNombreArchivo() {
    auto now = std::chrono::system_clock::now();
    std::time_t now_c = std::chrono::system_clock::to_time_t(now);
    std::tm* time_info = std::localtime(&now_c);
    std::ostringstream oss;
    oss << "backorders_" << std::put_time(time_info, "%Y-%m-%d_%H-%M-%S") << ".csv";
    return oss.str();
}

bool hayEntradaEnConsola() {
    #ifdef _WIN32
        return _kbhit(); 
    #else
        return false;
    #endif
}

int readIntInRange(int minVal, int maxVal) {
    int val = -1;
    while (!(std::cin >> val) || val < minVal || val > maxVal) {
        std::cout << "  Entrada invalida. Ingrese numero (" << minVal << "-" << maxVal << "): ";
        std::cin.clear(); std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    }
    return val;
}

// --- LOGICA DE CONFIRMACION ACTUALIZADA ---
void handleConfirmation(int destino, std::set<int>& pendientes,
                        std::map<int, int>& piezasOriginales, std::map<int, int>& piezasAjustadas,
                        const std::string& sku, const std::string& loteRequerido,
                        const std::map<int, int>& destino_a_OV,
                        std::map<int, EntradaProducto*>& mapaEntradasActivas) {

    int original = piezasOriginales.at(destino);
    int surtidoInicial = piezasAjustadas.at(destino);
    int odv = destino_a_OV.at(destino);

    std::string respuesta = "";
    while (respuesta != "s" && respuesta != "n") {
        std::cout << "\n==================== CONFIRMACION REQUERIDA ====================" << std::endl;
        std::cout << "Destino: " << destino << " (OV: " << odv << ")" << std::endl;
        std::cout << "  Requerido: " << original << " | Surtido: " << surtidoInicial << std::endl;
        std::cout << "  ¿Es correcta esta cantidad? (s/n): ";
        std::getline(std::cin, respuesta); respuesta = trim(respuesta);
        std::transform(respuesta.begin(), respuesta.end(), respuesta.begin(), ::tolower);
    }

    if (respuesta == "n") {
        std::cout << "  CANCELADO. Ajuste piezas en modulo." << std::endl;
        return;
    }

    // CASO SIMPLE: Coincide
    if (surtidoInicial == original) {
        registrarBackorder(sku, odv, destino, original, original, loteRequerido, loteRequerido, "OK");
        pendientes.erase(destino);
        enviarAArduino("APAGAR_DESTINO_" + std::to_string(destino));
        
        // MARCAR COMO SURTIDO
        if(mapaEntradasActivas.count(destino)) mapaEntradasActivas[destino]->yaSurtido = true;
        
        return;
    }

    // CASO COMPLEJO: Ajuste o Cambio de Lote
    int piezasFaltantes = original - surtidoInicial;
    int piezasPendientes = piezasFaltantes;
    int piezasSurtidasTotal = surtidoInicial;
    
    std::vector<std::pair<std::string, int>> lotesUsados;
    lotesUsados.push_back({loteRequerido, surtidoInicial});

    bool seguirComplementando = true;
    while (piezasPendientes > 0 && seguirComplementando) {
        std::string resp = "";
        while (resp != "s" && resp != "n") {
            std::cout << "\n  >> Faltan " << piezasPendientes << ". ¿Complementar con otro lote? (s/n): ";
            std::getline(std::cin, resp); resp = trim(resp);
            std::transform(resp.begin(), resp.end(), resp.begin(), ::tolower);
        }

        if (resp == "n") { seguirComplementando = false; break; }

        std::string loteComp;
        std::cout << "  >> Ingrese LOTE COMPLEMENTO: ";
        std::getline(std::cin, loteComp); loteComp = trim(loteComp);
        
        size_t pos = loteComp.find(' '); if (pos != std::string::npos) loteComp = loteComp.substr(0, pos);

        if (loteComp.empty() || loteComp == loteRequerido) {
            std::cout << "  Lote invalido o duplicado." << std::endl;
            continue;
        }

        std::cout << "  >> Cantidad del lote (" << loteComp << ") [1-" << piezasPendientes << "]: ";
        int cant = readIntInRange(1, piezasPendientes);
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

        lotesUsados.push_back({loteComp, cant});
        piezasPendientes -= cant;
        piezasSurtidasTotal += cant;
    }

    std::string motivoFinal = (piezasPendientes == 0) ? "Cambio de lote - Completo" : "Falta de material";
    if (lotesUsados.size() > 1 && piezasPendientes > 0) motivoFinal = "Cambio de lote - Incompleto";

    for (size_t i = 0; i < lotesUsados.size(); ++i) {
        int req = (i==0) ? original : 0;
        std::string subMotivo = motivoFinal + ((i==0) ? " (Principal)" : " (Suplementario)");
        registrarBackorder(sku, odv, destino, req, lotesUsados[i].second, loteRequerido, lotesUsados[i].first, subMotivo);
    }
    
    pendientes.erase(destino);
    enviarAArduino("APAGAR_DESTINO_" + std::to_string(destino));
    
    // MARCAR COMO SURTIDO
    if(mapaEntradasActivas.count(destino)) mapaEntradasActivas[destino]->yaSurtido = true;
    
    std::cout << "  Destino " << destino << " registrado." << std::endl;
}

void processInputMessage(const std::string& mensaje, std::set<int>& pendientes,
                         std::map<int, int>& piezasOriginales, std::map<int, int>& piezasAjustadas,
                         const std::string& sku, const std::string& lote, bool& loop_break,
                         const std::map<int, int>& destino_a_OV, std::queue<int>& destinosParaConfirmar,
                         std::map<int, EntradaProducto*>& mapaEntradasActivas) {

    if (mensaje == "exit" || mensaje == "salir") { loop_break = true; return; }

    if (mensaje.rfind("boton_", 0) == 0) {
        try {
            int dest = std::stoi(mensaje.substr(6));
            if (pendientes.count(dest)) {
                int orig = piezasOriginales[dest];
                int act = piezasAjustadas[dest];
                int odv = destino_a_OV.at(dest);

                if (orig == act) {
                    pendientes.erase(dest);
                    enviarAArduino("APAGAR_DESTINO_" + std::to_string(dest));
                    registrarBackorder(sku, odv, dest, orig, act, lote, lote, "OK");
                    std::cout << "DESTINO " << dest << " confirmado." << std::endl;
                    
                    // MARCAR COMO SURTIDO
                    if(mapaEntradasActivas.count(dest)) mapaEntradasActivas[dest]->yaSurtido = true;
                    
                } else {
                    std::cout << "ALERTA: Diferencia en Destino " << dest << ". Esperando confirmacion..." << std::endl;
                    destinosParaConfirmar.push(dest);
                }
            }
        } catch (...) {}
    } 
    else if (mensaje[0] == '+' || mensaje[0] == '-') {
        try {
            char op = mensaje[0];
            int dest = std::stoi(mensaje.substr(1));
            if (pendientes.count(dest)) {
                int orig = piezasOriginales[dest];
                int& act = piezasAjustadas[dest];
                if (op == '+') act = (act >= orig) ? 0 : act + 1;
                else act = (act <= 0) ? orig : act - 1;
                enviarAArduino("ACTUALIZAR_" + std::to_string(dest) + "_" + std::to_string(act));
                std::cout << "Destino " << dest << " ajustado: " << act << std::endl;
            }
        } catch (...) {}
    }
}

// --- MAIN ---
int main() {
    std::cout << "Programa PTL v5.0 (Proteccion Doble Escaneo)" << std::endl;
    if (!inicializarPuertoSerial(PUERTO_SERIAL_DEFAULT)) return 1;
    std::thread serialThread(hiloLecturaSerial);

    DatosCargados datos;
    bool csvCargado = false;
    
    while (!csvCargado) {
        std::cout << "\nArchivo CSV (ej. pedidos.csv): ";
        std::getline(std::cin, archivoCSVGlobal);
        archivoCSVGlobal = trim(archivoCSVGlobal);
        datos = cargarProductosDesdeCSV(archivoCSVGlobal);
        if (datos.cargadoExitosamente) {
            csvCargado = true;
            nombreArchivoBackorder = generarNombreArchivo();
            std::cout << "Backorders: " << nombreArchivoBackorder << std::endl;
        } else {
            std::cout << "Presione ENTER para reintentar o 'exit' para salir." << std::endl;
            std::string chk; std::getline(std::cin, chk);
            if (trim(chk) == "exit") { hiloLecturaActivo = false; if (serialThread.joinable()) serialThread.join(); return 0; }
        }
    }
    
    while (true) {
        std::cout << "\n>>> Escanee SKU (o 'exit'): ";
        std::string sku;
        if (!(std::cin >> sku)) break; 
        sku = trim(sku);
        if (sku == "exit") break;

        if (datos.productosPorSKU.find(sku) == datos.productosPorSKU.end()) {
            std::cout << "SKU no encontrado." << std::endl;
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            continue;
        }
        
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n'); 
        std::cout << "Escanee LOTE: ";
        std::string lote;
        std::getline(std::cin, lote); lote = trim(lote);
        size_t pos = lote.find(' '); if (pos != std::string::npos) lote = lote.substr(0, pos);

        const std::string loteReq = datos.productosPorSKU.at(sku).front().lote; 

        if (loteReq != lote) {
            std::cout << "Lote incorrecto. Requerido: " << loteReq << std::endl;
            continue;
        }

        std::cout << "--- SURTIDO: " << sku << " ---" << std::endl;
        std::set<int> pendientes;
        std::map<int, int> piezasOriginales;
        std::map<int, int> piezasAjustadas;
        std::map<int, int> destino_a_OV;
        std::queue<int> destinosParaConfirmar; 
        
        // Mapa para poder actualizar la bandera 'yaSurtido' de la entrada original
        std::map<int, EntradaProducto*> mapaEntradasActivas;

        bool todoYaSurtido = true; // Asumimos que todo está surtido hasta encontrar uno que no

        // IMPORTANTE: Usamos 'auto&' para modificar la entrada original
        for (auto& entrada : datos.productosPorSKU.at(sku)) {
            if (entrada.lote == loteReq) {
                // VERIFICACION: Si ya fue surtido, lo saltamos
                if (entrada.yaSurtido) {
                    continue; 
                }

                todoYaSurtido = false; // Encontramos al menos uno pendiente
                int dest = entrada.destino;
                if (pendientes.count(dest)) continue; 
                
                pendientes.insert(dest);
                piezasOriginales[dest] = entrada.piezas;
                piezasAjustadas[dest] = entrada.piezas;
                destino_a_OV[dest] = entrada.ordenDeVenta; 
                
                // Guardamos el puntero para actualizarlo despues
                mapaEntradasActivas[dest] = &entrada;

                enviarAArduino("ENCENDER_" + std::to_string(dest) + "_" + std::to_string(entrada.piezas));
                std::cout << "  -> Destino " << dest << ": " << entrada.piezas << " pzs" << std::endl;
            }
        }
        
        if (todoYaSurtido) {
            std::cout << "AVISO: Este SKU/Lote ya fue surtido por completo en todas las ordenes." << std::endl;
            continue; // Volver a pedir SKU
        }
        
        bool scanFinished = false;
        while (!pendientes.empty() && !scanFinished) {
            while (!destinosParaConfirmar.empty() && !scanFinished) {
                int d = destinosParaConfirmar.front(); destinosParaConfirmar.pop();
                if (pendientes.count(d)) handleConfirmation(d, pendientes, piezasOriginales, piezasAjustadas, sku, loteReq, destino_a_OV, mapaEntradasActivas);
                if (pendientes.empty()) scanFinished = true;
            }
            if (scanFinished || pendientes.empty()) break;

            std::string msg = obtenerMensajeSerial();
            while (!msg.empty() && !scanFinished) { 
                processInputMessage(msg, pendientes, piezasOriginales, piezasAjustadas, sku, loteReq, scanFinished, destino_a_OV, destinosParaConfirmar, mapaEntradasActivas);
                if (scanFinished) break;
                msg = obtenerMensajeSerial();
            }

            if (hayEntradaEnConsola()) {
                std::string man; std::cin >> man; man = trim(man);
                if (man.rfind("boton_", 0) == 0 || man == "exit" || man[0] == '+' || man[0] == '-') {
                     processInputMessage(man, pendientes, piezasOriginales, piezasAjustadas, sku, loteReq, scanFinished, destino_a_OV, destinosParaConfirmar, mapaEntradasActivas);
                }
                if (std::cin.peek() == '\n') std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        if (!pendientes.empty()) {
            for (int d : pendientes) {
                enviarAArduino("APAGAR_DESTINO_" + std::to_string(d));
                registrarBackorder(sku, destino_a_OV[d], d, piezasOriginales[d], 0, loteReq, loteReq, "Cancelado"); 
                // NOTA: Si se cancela, NO lo marcamos como surtido, para permitir re-intento.
            }
        } else {
            std::cout << "--- COMPLETO ---" << std::endl;
        }
    }

    hiloLecturaActivo = false;
    if (serialThread.joinable()) serialThread.join();
    if (hSerial != INVALID_HANDLE_VALUE) CloseHandle(hSerial);
    return 0;
}