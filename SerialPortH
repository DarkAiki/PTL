#ifndef SERIAL_PORT_H
#define SERIAL_PORT_H

#include <string>
#include <vector>

// --- Includes específicos para Windows ---
#ifdef _WIN32
    #include <windows.h> // Necesario para las funciones de la API de Windows para puertos seriales
#endif
// ----------------------------------------

class SerialPort {
public:
    SerialPort();
    ~SerialPort();

    // Abre el puerto serial. Retorna true en caso de éxito.
    // portName: Nombre del puerto (ej. "COM3", "COM4")
    // baudRate: Velocidad en baudios (ej. 9600, 115200)
    bool open(const std::string& portName, int baudRate);

    // Cierra el puerto serial.
    void close();

    // Escribe datos en el puerto serial. Retorna el número de bytes escritos, o -1 si falla.
    // data: Puntero al buffer de datos a escribir
    // length: Número de bytes a escribir
    int write(const char* data, int length);

    // Lee datos del puerto serial. Retorna el número de bytes leídos, o -1 si falla.
    // buffer: Puntero al buffer donde se almacenarán los datos leídos
    // bufferSize: Tamaño máximo del buffer
    int read(char* buffer, int bufferSize);

    // Verifica si el puerto está abierto.
    bool isOpen() const;

private:
    #ifdef _WIN32
        // HANDLE es el tipo de datos para un descriptor de archivo en Windows
        HANDLE hSerial;
    #else
        // Para otros sistemas operativos, usaríamos int (Linux/macOS)
        int hSerial; // Solo como placeholder si no estás en Windows
    #endif
    bool _isOpen; // Una bandera para saber si el puerto está abierto
};

#endif // SERIAL_PORT_H