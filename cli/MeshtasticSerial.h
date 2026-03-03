#pragma once

#include "SerialPort.h"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

struct DeviceFileInfo {
    std::string name;
    uint32_t size;
};

// Meshtastic serial protocol layer: framing, config handshake, XModem file transfer
class MeshtasticSerial
{
  public:
    explicit MeshtasticSerial(SerialPort &port);

    // Connect to the device: send wake bytes, perform config handshake.
    // Returns true on success, populates myNodeNum and file manifest.
    bool connect();

    // Send a single file via XModem to the given device path (e.g. "/apps/counter/app.json").
    // Calls progressCb(bytesSent, totalBytes) after each chunk if provided.
    bool sendFile(const std::string &devicePath, const uint8_t *data, size_t dataLen,
                  std::function<void(size_t, size_t)> progressCb = nullptr);

    // Download a file from the device via XModem STX. Returns file contents.
    bool downloadFile(const std::string &devicePath, std::vector<uint8_t> &outData);

    // Delete a file on the device via admin delete_file_request
    bool deleteFile(const std::string &devicePath);

    // Send a reboot command (reboot in N seconds)
    bool sendReboot(int seconds = 2);

    uint32_t getMyNodeNum() const { return myNodeNum; }

    // File manifest captured during config handshake
    const std::vector<DeviceFileInfo> &getFileManifest() const { return fileManifest; }

  private:
    SerialPort &port;
    uint32_t myNodeNum = 0;
    std::vector<DeviceFileInfo> fileManifest;
    std::vector<uint8_t> sessionPasskey; // captured from admin responses
    bool sessionPasskeyAttempted = false; // true once we've tried (even if it failed)

    // Request a session passkey by sending a get_config_request and parsing the response
    bool requestSessionKey();

    // -- Framing --
    bool sendFrame(const std::vector<uint8_t> &payload);
    std::vector<uint8_t> readFrame(int timeoutMs = 3000);

    // -- Minimal protobuf encoding --
    static void encodeVarint(std::vector<uint8_t> &out, uint64_t value);
    static void encodeTag(std::vector<uint8_t> &out, uint32_t fieldNum, uint32_t wireType);
    static void encodeVarintField(std::vector<uint8_t> &out, uint32_t fieldNum, uint64_t value);
    static void encodeLengthDelimited(std::vector<uint8_t> &out, uint32_t fieldNum, const uint8_t *data, size_t len);
    static void encodeLengthDelimited(std::vector<uint8_t> &out, uint32_t fieldNum, const std::vector<uint8_t> &data);
    static void encodeFixed32Field(std::vector<uint8_t> &out, uint32_t fieldNum, uint32_t value);
    static void encodeStringField(std::vector<uint8_t> &out, uint32_t fieldNum, const std::string &value);

    // -- Minimal protobuf decoding --
    struct PbField {
        uint32_t fieldNum;
        uint32_t wireType;
        uint64_t varintVal;
        uint32_t fixed32Val;
        const uint8_t *data;
        size_t dataLen;
    };
    static bool decodeFields(const uint8_t *buf, size_t len, std::vector<PbField> &fields);
    static const PbField *findField(const std::vector<PbField> &fields, uint32_t fieldNum);

    // -- XModem helpers --
    static uint16_t crc16_ccitt(const uint8_t *buffer, size_t length);
    static std::vector<uint8_t> encodeXModem(uint8_t control, uint16_t seq, uint16_t crc16, const uint8_t *buffer,
                                             size_t bufLen);
    static std::vector<uint8_t> wrapInToRadio_XModem(const std::vector<uint8_t> &xmodemPayload);
    int parseXModemResponse(const std::vector<uint8_t> &frame);
    // Parse XModem data from a FromRadio frame. Returns data chunk, or empty if not xmodem/eot.
    // Sets controlOut to the control value.
    std::vector<uint8_t> parseXModemData(const std::vector<uint8_t> &frame, int &controlOut);
    int waitForXModemResponse(int timeoutMs = 5000);

    // Build a ToRadio.packet wrapping an admin message addressed to self
    std::vector<uint8_t> buildAdminToRadio(const std::vector<uint8_t> &adminMsg, bool wantResponse = false);
};
