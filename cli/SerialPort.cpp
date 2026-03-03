#include "SerialPort.h"
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

SerialPort::SerialPort() {}

SerialPort::~SerialPort()
{
    close();
}

bool SerialPort::open(const std::string &path, int baud)
{
    // Step 1: Clear HUPCL to prevent DTR drop from resetting the device
    // (matches meshtastic-python's serial_interface.py behavior)
    {
        int tmpFd = ::open(path.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
        if (tmpFd < 0) {
            fprintf(stderr, "Error: cannot open %s: %s\n", path.c_str(), strerror(errno));
            return false;
        }
        struct termios tmp;
        if (tcgetattr(tmpFd, &tmp) == 0) {
            tmp.c_cflag &= ~HUPCL;
            tcsetattr(tmpFd, TCSAFLUSH, &tmp);
        }
        ::close(tmpFd);
        usleep(100000); // 100ms settle time after clearing HUPCL
    }

    // Step 2: Open the port for real
    fd = ::open(path.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        fprintf(stderr, "Error: cannot open %s: %s\n", path.c_str(), strerror(errno));
        return false;
    }

    // Clear non-blocking after open (we'll use select() for timeouts)
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);

    struct termios tty;
    memset(&tty, 0, sizeof(tty));
    if (tcgetattr(fd, &tty) != 0) {
        fprintf(stderr, "Error: tcgetattr failed: %s\n", strerror(errno));
        ::close(fd);
        fd = -1;
        return false;
    }

    // Baud rate
    speed_t speed;
    switch (baud) {
    case 9600:
        speed = B9600;
        break;
    case 19200:
        speed = B19200;
        break;
    case 38400:
        speed = B38400;
        break;
    case 57600:
        speed = B57600;
        break;
    case 115200:
        speed = B115200;
        break;
    case 230400:
        speed = B230400;
        break;
    default:
        speed = B115200;
        break;
    }
    cfsetispeed(&tty, speed);
    cfsetospeed(&tty, speed);

    // 8N1, no flow control, raw mode
    tty.c_cflag &= ~PARENB;
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;
    tty.c_cflag &= ~CRTSCTS;
    tty.c_cflag |= CLOCAL | CREAD;

    // Raw input
    tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    tty.c_iflag &= ~(IXON | IXOFF | IXANY);
    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);

    // Raw output
    tty.c_oflag &= ~OPOST;

    // Read returns immediately with whatever is available
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 0;

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        fprintf(stderr, "Error: tcsetattr failed: %s\n", strerror(errno));
        ::close(fd);
        fd = -1;
        return false;
    }

    // Flush any stale data
    tcflush(fd, TCIOFLUSH);

    // Assert DTR (tells the device the host is ready)
    int modemBits = TIOCM_DTR;
    ioctl(fd, TIOCMBIS, &modemBits);

    usleep(100000); // 100ms settle time after open

    return true;
}

int SerialPort::write(const uint8_t *data, size_t len)
{
    if (fd < 0)
        return -1;
    ssize_t n = ::write(fd, data, len);
    if (n < 0) {
        fprintf(stderr, "Error: serial write failed: %s\n", strerror(errno));
        return -1;
    }
    tcdrain(fd); // ensure bytes are transmitted
    return static_cast<int>(n);
}

int SerialPort::read(uint8_t *buf, size_t maxLen, int timeoutMs)
{
    if (fd < 0)
        return -1;

    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(fd, &readfds);

    struct timeval tv;
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;

    int sel = select(fd + 1, &readfds, nullptr, nullptr, &tv);
    if (sel < 0) {
        fprintf(stderr, "Error: select failed: %s\n", strerror(errno));
        return -1;
    }
    if (sel == 0)
        return 0; // timeout

    ssize_t n = ::read(fd, buf, maxLen);
    if (n < 0) {
        fprintf(stderr, "Error: serial read failed: %s\n", strerror(errno));
        return -1;
    }
    return static_cast<int>(n);
}

void SerialPort::close()
{
    if (fd >= 0) {
        ::close(fd);
        fd = -1;
    }
}

std::string SerialPort::findPort()
{
    std::vector<std::string> candidates;

    // Scan /dev for matching serial devices
    DIR *dir = opendir("/dev");
    if (!dir)
        return "";

    struct dirent *entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string name = entry->d_name;
#ifdef __APPLE__
        // macOS: /dev/tty.usb*
        if (name.rfind("tty.usb", 0) == 0) {
            candidates.push_back("/dev/" + name);
        }
#else
        // Linux: /dev/ttyUSB* and /dev/ttyACM*
        if (name.rfind("ttyUSB", 0) == 0 || name.rfind("ttyACM", 0) == 0) {
            candidates.push_back("/dev/" + name);
        }
#endif
    }
    closedir(dir);

    if (candidates.empty()) {
        fprintf(stderr, "Error: no serial devices found\n");
        return "";
    }
    if (candidates.size() > 1) {
        fprintf(stderr, "Error: multiple serial devices found, please specify --port:\n");
        for (const auto &c : candidates)
            fprintf(stderr, "  %s\n", c.c_str());
        return "";
    }
    return candidates[0];
}
