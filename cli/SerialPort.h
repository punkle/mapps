#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class SerialPort
{
  public:
    SerialPort();
    ~SerialPort();

    // Open a serial port with the given baud rate (8N1, no flow control, raw mode)
    bool open(const std::string &path, int baud = 115200);

    // Write bytes to the port. Returns number of bytes written, or -1 on error.
    int write(const uint8_t *data, size_t len);

    // Read bytes with timeout. Returns number of bytes read, 0 on timeout, -1 on error.
    int read(uint8_t *buf, size_t maxLen, int timeoutMs);

    // Close the port
    void close();

    bool isOpen() const { return fd >= 0; }

    // Auto-detect a serial port. Scans /dev/tty.usb* (macOS) and /dev/ttyUSB* + /dev/ttyACM* (Linux).
    // Returns the path, or empty string if none/multiple found.
    static std::string findPort();

  private:
    int fd = -1;
};
