#include "SerialPort.h"
#include <iostream> // Para mensajes de depuración/error
#include <stdexcept> // Para excepciones si queremos manejar errores de forma más C++ moderna

// --- Implementación específica para Windows ---
#ifdef _WIN32

SerialPort::SerialPort() : hSerial(INVALID_HANDLE_VALUE), _isOpen(false) {
    // Constructor: Inicializa el handle del puerto a un valor inválido y la bandera a false
}

SerialPort::~SerialPort() {
    close(); // Asegura que el puerto se cierre cuando el objeto SerialPort se destruye
}

bool SerialPort::open(const std::string& portName, int baudRate) {
    if (_isOpen) {
        std::cerr << "Error: El puerto " << portName << " ya esta abierto." << std::endl;
        return false;
    }

    // 1. Abrir el puerto serial
    // "COMx" es el nombre del puerto. Debemos añadir "\\\\.\\" para puertos COM > 9,
    // pero para COM1-COM9 simplemente "COM1" funciona. Es más seguro usarlo siempre.
    std::string fullPortName = "\\\\.\\" + portName;

    hSerial = CreateFileA(fullPortName.c_str(), // Nombre del puerto (ej. "\\\\.\\COM3")
                          GENERIC_READ | GENERIC_WRITE, // Acceso de lectura y escritura
                          0,                            // No compartir
                          NULL,                         // Seguridad por defecto
                          OPEN_EXISTING,                // Abrir solo si existe
                          FILE_ATTRIBUTE_NORMAL,        // Atributos de archivo normales
                          NULL);                        // Sin plantilla

    if (hSerial == INVALID_HANDLE_VALUE) {
        // Podríamos obtener un código de error más específico con GetLastError()
        std::cerr << "Error: No se pudo abrir el puerto " << portName << ". Codigo: " << GetLastError() << std::endl;
        return false;
    }

    // 2. Configurar el puerto (velocidad, bits de datos, paridad, bits de parada)
    DCB dcbSerialParams = { 0 }; // Estructura para la configuración del puerto
    dcbSerialParams.DCBlength = sizeof(dcbSerialParams);

    if (!GetCommState(hSerial, &dcbSerialParams)) {
        std::cerr << "Error: Fallo al obtener el estado del puerto " << portName << ". Codigo: " << GetLastError() << std::endl;
        close();
        return false;
    }

    // Configurar la velocidad de baudios
    dcbSerialParams.BaudRate = baudRate;
    dcbSerialParams.ByteSize = 8;             // 8 bits de datos
    dcbSerialParams.StopBits = ONESTOPBIT;    // 1 bit de parada
    dcbSerialParams.Parity = NOPARITY;        // Sin paridad
    dcbSerialParams.fDtrControl = DTR_CONTROL_DISABLE; // Deshabilitar DTR (opcional, a veces ayuda con algunos Arduinos)
    dcbSerialParams.fRtsControl = RTS_CONTROL_DISABLE; // Deshabilitar RTS (opcional)

    if (!SetCommState(hSerial, &dcbSerialParams)) {
        std::cerr << "Error: Fallo al establecer el estado del puerto " << portName << ". Codigo: " << GetLastError() << std::endl;
        close();
        return false;
    }

    // 3. Configurar los tiempos de espera (timeouts) para lectura/escritura
    // Esto es muy importante para evitar que las funciones de lectura/escritura se bloqueen indefinidamente.
    COMMTIMEOUTS timeouts = { 0 };
    timeouts.ReadIntervalTimeout = 50;           // Máx. tiempo entre caracteres (ms)
    timeouts.ReadTotalTimeoutConstant = 50;      // Constante para el tiempo total de lectura (ms)
    timeouts.ReadTotalTimeoutMultiplier = 10;    // Multiplicador para el tiempo total de lectura (ms/byte)
    timeouts.WriteTotalTimeoutConstant = 50;     // Constante para el tiempo total de escritura (ms)
    timeouts.WriteTotalTimeoutMultiplier = 10;   // Multiplicador para el tiempo total de escritura (ms/byte)

    if (!SetCommTimeouts(hSerial, &timeouts)) {
        std::cerr << "Error: Fallo al establecer los tiempos de espera del puerto " << portName << ". Codigo: " << GetLastError() << std::endl;
        close();
        return false;
    }

    _isOpen = true; // El puerto está abierto y configurado correctamente
    std::cout << "Puerto serial '" << portName << "' abierto correctamente a " << baudRate << " baudios." << std::endl;
    return true;
}

void SerialPort::close() {
    if (_isOpen && hSerial != INVALID_HANDLE_VALUE) {
        CloseHandle(hSerial); // Cierra el handle del puerto
        hSerial = INVALID_HANDLE_VALUE; // Reinicia el handle
        _isOpen = false;
        std::cout << "Puerto serial cerrado." << std::endl;
    }
}

int SerialPort::write(const char* data, int length) {
    if (!_isOpen || hSerial == INVALID_HANDLE_VALUE) {
        std::cerr << "Error: Puerto serial no esta abierto para escribir." << std::endl;
        return -1;
    }

    DWORD bytesWritten; // Cantidad de bytes realmente escritos
    if (!WriteFile(hSerial, data, length, &bytesWritten, NULL)) {
        std::cerr << "Error al escribir en el puerto serial. Codigo: " << GetLastError() << std::endl;
        return -1;
    }
    // std::cout << "DEBUG: Escritos " << bytesWritten << " bytes: " << std::string(data, bytesWritten); // Descomenta para depuración
    return bytesWritten;
}

int SerialPort::read(char* buffer, int bufferSize) {
    if (!_isOpen || hSerial == INVALID_HANDLE_VALUE) {
        // No imprime error aquí porque se llama continuamente y llenaría la consola
        return -1;
    }

    DWORD bytesRead; // Cantidad de bytes realmente leídos
    if (!ReadFile(hSerial, buffer, bufferSize, &bytesRead, NULL)) {
        // Esto puede ocurrir si el puerto se desconecta inesperadamente
        // std::cerr << "Error al leer del puerto serial. Codigo: " << GetLastError() << std::endl; // Descomenta para depuración
        return -1;
    }
    return bytesRead;
}

bool SerialPort::isOpen() const {
    return _isOpen;
}

#else // Si NO es _WIN32 (por ejemplo, Linux/macOS, aunque no está implementado aquí)

// Implementaciones placeholder para sistemas no Windows
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