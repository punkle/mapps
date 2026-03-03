#include "MeshtasticSerial.h"
#include <cstdio>
#include <cstring>

// Meshtastic serial frame magic bytes
static const uint8_t FRAME_MAGIC1 = 0x94;
static const uint8_t FRAME_MAGIC2 = 0xC3;

// XModem Control enum values (from xmodem.proto)
static const uint8_t XMODEM_SOH = 1;
static const uint8_t XMODEM_STX = 2;
static const uint8_t XMODEM_EOT = 4;
static const uint8_t XMODEM_ACK = 6;
static const uint8_t XMODEM_NAK = 21;

// Protobuf wire types
static const uint32_t WT_VARINT = 0;
static const uint32_t WT_FIXED64 = 1;
static const uint32_t WT_LENGTH_DELIMITED = 2;
static const uint32_t WT_FIXED32 = 5;

// XModem chunk size (matches firmware's PB_BYTES_ARRAY_T(128))
static const size_t XMODEM_CHUNK_SIZE = 128;

MeshtasticSerial::MeshtasticSerial(SerialPort &port) : port(port) {}

// ============================================================
// Protobuf encoding helpers
// ============================================================

void MeshtasticSerial::encodeVarint(std::vector<uint8_t> &out, uint64_t value)
{
    do {
        uint8_t byte = value & 0x7F;
        value >>= 7;
        if (value)
            byte |= 0x80;
        out.push_back(byte);
    } while (value);
}

void MeshtasticSerial::encodeTag(std::vector<uint8_t> &out, uint32_t fieldNum, uint32_t wireType)
{
    encodeVarint(out, (fieldNum << 3) | wireType);
}

void MeshtasticSerial::encodeVarintField(std::vector<uint8_t> &out, uint32_t fieldNum, uint64_t value)
{
    encodeTag(out, fieldNum, WT_VARINT);
    encodeVarint(out, value);
}

void MeshtasticSerial::encodeLengthDelimited(std::vector<uint8_t> &out, uint32_t fieldNum, const uint8_t *data, size_t len)
{
    encodeTag(out, fieldNum, WT_LENGTH_DELIMITED);
    encodeVarint(out, len);
    out.insert(out.end(), data, data + len);
}

void MeshtasticSerial::encodeLengthDelimited(std::vector<uint8_t> &out, uint32_t fieldNum, const std::vector<uint8_t> &data)
{
    encodeLengthDelimited(out, fieldNum, data.data(), data.size());
}

void MeshtasticSerial::encodeFixed32Field(std::vector<uint8_t> &out, uint32_t fieldNum, uint32_t value)
{
    encodeTag(out, fieldNum, WT_FIXED32);
    out.push_back(value & 0xFF);
    out.push_back((value >> 8) & 0xFF);
    out.push_back((value >> 16) & 0xFF);
    out.push_back((value >> 24) & 0xFF);
}

void MeshtasticSerial::encodeStringField(std::vector<uint8_t> &out, uint32_t fieldNum, const std::string &value)
{
    encodeLengthDelimited(out, fieldNum, reinterpret_cast<const uint8_t *>(value.data()), value.size());
}

// ============================================================
// Protobuf decoding helpers
// ============================================================

static uint64_t readVarint(const uint8_t *buf, size_t len, size_t &pos)
{
    uint64_t result = 0;
    int shift = 0;
    while (pos < len) {
        uint8_t byte = buf[pos++];
        result |= (uint64_t)(byte & 0x7F) << shift;
        if (!(byte & 0x80))
            return result;
        shift += 7;
        if (shift >= 64)
            break;
    }
    return result;
}

bool MeshtasticSerial::decodeFields(const uint8_t *buf, size_t len, std::vector<PbField> &fields)
{
    size_t pos = 0;
    while (pos < len) {
        uint64_t tag = readVarint(buf, len, pos);
        uint32_t fieldNum = tag >> 3;
        uint32_t wireType = tag & 0x07;

        PbField f;
        memset(&f, 0, sizeof(f));
        f.fieldNum = fieldNum;
        f.wireType = wireType;

        switch (wireType) {
        case WT_VARINT:
            f.varintVal = readVarint(buf, len, pos);
            break;
        case WT_FIXED64:
            if (pos + 8 > len)
                return false;
            pos += 8;
            break;
        case WT_LENGTH_DELIMITED: {
            uint64_t dataLen = readVarint(buf, len, pos);
            if (pos + dataLen > len)
                return false;
            f.data = buf + pos;
            f.dataLen = (size_t)dataLen;
            pos += (size_t)dataLen;
            break;
        }
        case WT_FIXED32:
            if (pos + 4 > len)
                return false;
            f.fixed32Val = buf[pos] | (buf[pos + 1] << 8) | (buf[pos + 2] << 16) | (buf[pos + 3] << 24);
            pos += 4;
            break;
        default:
            return false;
        }

        fields.push_back(f);
    }
    return true;
}

const MeshtasticSerial::PbField *MeshtasticSerial::findField(const std::vector<PbField> &fields, uint32_t fieldNum)
{
    for (const auto &f : fields) {
        if (f.fieldNum == fieldNum)
            return &f;
    }
    return nullptr;
}

// ============================================================
// CRC-16 CCITT (from firmware/src/xmodem.cpp:67-81)
// ============================================================

uint16_t MeshtasticSerial::crc16_ccitt(const uint8_t *buffer, size_t length)
{
    uint16_t crc16 = 0;
    while (length != 0) {
        crc16 = (uint8_t)(crc16 >> 8) | (crc16 << 8);
        crc16 ^= *buffer;
        crc16 ^= (uint8_t)(crc16 & 0xff) >> 4;
        crc16 ^= (crc16 << 8) << 4;
        crc16 ^= ((crc16 & 0xff) << 4) << 1;
        buffer++;
        length--;
    }
    return crc16;
}

// ============================================================
// Serial framing
// ============================================================

bool MeshtasticSerial::sendFrame(const std::vector<uint8_t> &payload)
{
    if (payload.size() > 0xFFFF) {
        fprintf(stderr, "Error: frame too large (%zu bytes)\n", payload.size());
        return false;
    }

    uint8_t header[4];
    header[0] = FRAME_MAGIC1;
    header[1] = FRAME_MAGIC2;
    header[2] = (payload.size() >> 8) & 0xFF;
    header[3] = payload.size() & 0xFF;

    if (port.write(header, 4) != 4)
        return false;
    if (port.write(payload.data(), payload.size()) != (int)payload.size())
        return false;
    return true;
}

std::vector<uint8_t> MeshtasticSerial::readFrame(int timeoutMs)
{
    uint8_t buf[1];
    int state = 0;
    uint16_t frameLen = 0;
    std::vector<uint8_t> payload;
    int elapsed = 0;
    const int pollMs = 10;

    while (elapsed < timeoutMs) {
        int n = port.read(buf, 1, pollMs);
        if (n < 0)
            return {};
        if (n == 0) {
            elapsed += pollMs;
            continue;
        }

        switch (state) {
        case 0:
            if (buf[0] == FRAME_MAGIC1)
                state = 1;
            // else: discard non-frame bytes (firmware debug log output)
            break;
        case 1:
            if (buf[0] == FRAME_MAGIC2)
                state = 2;
            else if (buf[0] == FRAME_MAGIC1)
                state = 1;
            else
                state = 0;
            break;
        case 2:
            frameLen = (uint16_t)buf[0] << 8;
            state = 3;
            break;
        case 3:
            frameLen |= buf[0];
            if (frameLen == 0)
                return {};
            payload.reserve(frameLen);
            state = 4;
            break;
        case 4:
            payload.push_back(buf[0]);
            if (payload.size() == frameLen)
                return payload;
            break;
        }
    }
    return {};
}

// ============================================================
// XModem message encoding/parsing
// ============================================================

std::vector<uint8_t> MeshtasticSerial::encodeXModem(uint8_t control, uint16_t seq, uint16_t crc, const uint8_t *buffer,
                                                     size_t bufLen)
{
    std::vector<uint8_t> out;
    encodeVarintField(out, 1, control);
    if (seq > 0)
        encodeVarintField(out, 2, seq);
    if (crc > 0)
        encodeVarintField(out, 3, crc);
    if (buffer && bufLen > 0)
        encodeLengthDelimited(out, 4, buffer, bufLen);
    return out;
}

std::vector<uint8_t> MeshtasticSerial::wrapInToRadio_XModem(const std::vector<uint8_t> &xmodemPayload)
{
    std::vector<uint8_t> out;
    encodeLengthDelimited(out, 5, xmodemPayload);
    return out;
}

int MeshtasticSerial::parseXModemResponse(const std::vector<uint8_t> &frame)
{
    std::vector<PbField> fromRadioFields;
    if (!decodeFields(frame.data(), frame.size(), fromRadioFields))
        return -1;

    const PbField *xmodemField = findField(fromRadioFields, 12);
    if (!xmodemField || xmodemField->wireType != WT_LENGTH_DELIMITED)
        return -1;

    std::vector<PbField> xmodemFields;
    if (!decodeFields(xmodemField->data, xmodemField->dataLen, xmodemFields))
        return -1;

    const PbField *controlField = findField(xmodemFields, 1);
    if (!controlField)
        return -1;

    return (int)controlField->varintVal;
}

std::vector<uint8_t> MeshtasticSerial::parseXModemData(const std::vector<uint8_t> &frame, int &controlOut)
{
    controlOut = -1;

    std::vector<PbField> fromRadioFields;
    if (!decodeFields(frame.data(), frame.size(), fromRadioFields))
        return {};

    const PbField *xmodemField = findField(fromRadioFields, 12);
    if (!xmodemField || xmodemField->wireType != WT_LENGTH_DELIMITED)
        return {};

    std::vector<PbField> xmodemFields;
    if (!decodeFields(xmodemField->data, xmodemField->dataLen, xmodemFields))
        return {};

    const PbField *controlField = findField(xmodemFields, 1);
    if (!controlField)
        return {};
    controlOut = (int)controlField->varintVal;

    // Extract buffer data (field 4)
    const PbField *bufferField = findField(xmodemFields, 4);
    if (bufferField && bufferField->wireType == WT_LENGTH_DELIMITED) {
        return std::vector<uint8_t>(bufferField->data, bufferField->data + bufferField->dataLen);
    }
    return {};
}

int MeshtasticSerial::waitForXModemResponse(int timeoutMs)
{
    int elapsed = 0;
    while (elapsed < timeoutMs) {
        auto frame = readFrame(1000);
        if (frame.empty()) {
            elapsed += 1000;
            continue;
        }

        int control = parseXModemResponse(frame);
        if (control >= 0)
            return control;

        // Not an XModem frame (e.g. queued mesh packet), keep waiting
        elapsed += 100;
    }
    return -1;
}

// ============================================================
// Admin message helpers
// ============================================================

std::vector<uint8_t> MeshtasticSerial::buildAdminToRadio(const std::vector<uint8_t> &adminMsg, bool wantResponse)
{
    // Data { portnum = ADMIN_APP(6), payload = adminMsg, want_response = wantResponse }
    std::vector<uint8_t> dataMsg;
    encodeVarintField(dataMsg, 1, 6); // ADMIN_APP = 6
    encodeLengthDelimited(dataMsg, 2, adminMsg);
    if (wantResponse)
        encodeVarintField(dataMsg, 3, 1);

    // MeshPacket { to = myNodeNum, decoded = dataMsg }
    std::vector<uint8_t> meshPacket;
    encodeFixed32Field(meshPacket, 2, myNodeNum);
    encodeLengthDelimited(meshPacket, 4, dataMsg);

    // ToRadio { packet = meshPacket }
    std::vector<uint8_t> toRadio;
    encodeLengthDelimited(toRadio, 1, meshPacket);
    return toRadio;
}

bool MeshtasticSerial::requestSessionKey()
{
    if (myNodeNum == 0)
        return false;

    // AdminMessage { get_config_request = DEVICE_CONFIG(0) } — field 2, varint
    std::vector<uint8_t> adminMsg;
    encodeVarintField(adminMsg, 2, 0);

    auto toRadio = buildAdminToRadio(adminMsg, true);
    if (!sendFrame(toRadio)) {
        fprintf(stderr, "Error: failed to send get_config_request\n");
        return false;
    }

    // Read responses, looking for an admin response with session_passkey
    for (int i = 0; i < 20; i++) {
        auto frame = readFrame(3000);
        if (frame.empty())
            break;

        std::vector<PbField> fromRadioFields;
        if (!decodeFields(frame.data(), frame.size(), fromRadioFields))
            continue;

        // FromRadio field 2 = packet (MeshPacket)
        const PbField *packetField = findField(fromRadioFields, 2);
        if (!packetField || packetField->wireType != WT_LENGTH_DELIMITED)
            continue;

        std::vector<PbField> meshFields;
        if (!decodeFields(packetField->data, packetField->dataLen, meshFields))
            continue;

        // MeshPacket field 4 = decoded (Data)
        const PbField *decodedField = findField(meshFields, 4);
        if (!decodedField || decodedField->wireType != WT_LENGTH_DELIMITED)
            continue;

        std::vector<PbField> dataFields;
        if (!decodeFields(decodedField->data, decodedField->dataLen, dataFields))
            continue;

        // Data field 2 = payload (AdminMessage bytes)
        const PbField *payloadField = findField(dataFields, 2);
        if (!payloadField || payloadField->wireType != WT_LENGTH_DELIMITED)
            continue;

        std::vector<PbField> adminFields;
        if (!decodeFields(payloadField->data, payloadField->dataLen, adminFields))
            continue;

        // AdminMessage field 101 = session_passkey (bytes)
        const PbField *keyField = findField(adminFields, 101);
        if (keyField && keyField->wireType == WT_LENGTH_DELIMITED && keyField->dataLen > 0) {
            sessionPasskey.assign(keyField->data, keyField->data + keyField->dataLen);
            return true;
        }
    }

    fprintf(stderr, "Warning: could not obtain session passkey\n");
    return false;
}

// ============================================================
// Connection handshake
// ============================================================

bool MeshtasticSerial::connect()
{
    fileManifest.clear();

    // Send 32 wake bytes (0xC3) to wake the serial interface
    uint8_t wakeBuf[32];
    memset(wakeBuf, 0xC3, sizeof(wakeBuf));
    port.write(wakeBuf, sizeof(wakeBuf));

    // Wait for device to wake up and drain any stale data
    // (matches meshtastic-python's 200ms total wake delay)
    {
        uint8_t discard[512];
        port.read(discard, sizeof(discard), 100);
        port.read(discard, sizeof(discard), 100);
    }

    // Send ToRadio.want_config_id with a nonce
    uint32_t configNonce = 0x12345678;
    std::vector<uint8_t> toRadio;
    encodeVarintField(toRadio, 3, configNonce);
    if (!sendFrame(toRadio)) {
        fprintf(stderr, "Error: failed to send config request\n");
        return false;
    }

    // Read FromRadio responses until config_complete_id matches our nonce
    int maxFrames = 500;
    for (int i = 0; i < maxFrames; i++) {
        // Use a longer timeout for the first frame (device may still be waking up)
        int timeout = (i == 0) ? 10000 : 5000;
        auto frame = readFrame(timeout);
        if (frame.empty()) {
            fprintf(stderr, "Error: timeout waiting for config response (frame %d)\n", i);
            return false;
        }

        std::vector<PbField> fields;
        if (!decodeFields(frame.data(), frame.size(), fields))
            continue;

        // Check for MyNodeInfo (FromRadio field 3)
        const PbField *myInfoField = findField(fields, 3);
        if (myInfoField && myInfoField->wireType == WT_LENGTH_DELIMITED) {
            std::vector<PbField> infoFields;
            if (decodeFields(myInfoField->data, myInfoField->dataLen, infoFields)) {
                const PbField *nodeNumField = findField(infoFields, 1);
                if (nodeNumField)
                    myNodeNum = (uint32_t)nodeNumField->varintVal;
            }
        }

        // Check for FileInfo (FromRadio field 15)
        const PbField *fileInfoField = findField(fields, 15);
        if (fileInfoField && fileInfoField->wireType == WT_LENGTH_DELIMITED) {
            std::vector<PbField> fiFields;
            if (decodeFields(fileInfoField->data, fileInfoField->dataLen, fiFields)) {
                DeviceFileInfo dfi;
                const PbField *nameField = findField(fiFields, 1);
                if (nameField && nameField->wireType == WT_LENGTH_DELIMITED)
                    dfi.name = std::string(reinterpret_cast<const char *>(nameField->data), nameField->dataLen);
                const PbField *sizeField = findField(fiFields, 2);
                if (sizeField)
                    dfi.size = (uint32_t)sizeField->varintVal;
                if (!dfi.name.empty())
                    fileManifest.push_back(dfi);
            }
        }

        // Check for config_complete_id (FromRadio field 7)
        const PbField *completeField = findField(fields, 7);
        if (completeField && completeField->wireType == WT_VARINT) {
            if ((uint32_t)completeField->varintVal == configNonce) {
                return true;
            }
        }
    }

    fprintf(stderr, "Error: config handshake did not complete\n");
    return false;
}

// ============================================================
// XModem file upload
// ============================================================

bool MeshtasticSerial::sendFile(const std::string &devicePath, const uint8_t *data, size_t dataLen,
                                std::function<void(size_t, size_t)> progressCb)
{
    // ESP32-S3 native USB CDC may poll serial only every ~20 seconds when
    // HWCDC::isPlugged() returns false. We need long timeouts for the first
    // XModem response. After the firmware processes the first packet, the
    // XModem handler's sendControl() triggers onNowHasData() -> setIntervalFromNow(0)
    // which should wake the serial thread for faster subsequent responses.
    // If that doesn't work, all packets will need the long timeout.
    static const int FIRST_XMODEM_TIMEOUT_MS = 30000;
    static const int XMODEM_TIMEOUT_MS = 25000;

    // Quick drain of any stale data (don't wait long — we want to send XModem ASAP)
    {
        uint8_t drain[512];
        port.read(drain, sizeof(drain), 50);
    }

    // Step 1: Send SOH with seq=0, buffer=target filename (include null terminator)
    std::vector<uint8_t> pathBuf(devicePath.begin(), devicePath.end());
    pathBuf.push_back('\0'); // null terminator for C string on device
    auto xmodemInit = encodeXModem(XMODEM_SOH, 0, 0, pathBuf.data(), pathBuf.size());
    auto toRadio = wrapInToRadio_XModem(xmodemInit);

    if (!sendFrame(toRadio)) {
        fprintf(stderr, "Error: failed to send XModem init\n");
        return false;
    }

    int response = waitForXModemResponse(FIRST_XMODEM_TIMEOUT_MS);
    if (response != XMODEM_ACK) {
        fprintf(stderr, "Error: device NAK'd file open for '%s' (response=%d)\n", devicePath.c_str(), response);
        return false;
    }

    // Step 2: Send data in 128-byte chunks
    size_t offset = 0;
    uint16_t seq = 1;
    while (offset < dataLen) {
        size_t chunkLen = dataLen - offset;
        if (chunkLen > XMODEM_CHUNK_SIZE)
            chunkLen = XMODEM_CHUNK_SIZE;

        uint16_t crc = crc16_ccitt(data + offset, chunkLen);
        auto xmodemChunk = encodeXModem(XMODEM_SOH, seq, crc, data + offset, chunkLen);
        auto toRadioChunk = wrapInToRadio_XModem(xmodemChunk);

        if (!sendFrame(toRadioChunk)) {
            fprintf(stderr, "Error: failed to send XModem chunk seq=%u\n", seq);
            return false;
        }

        response = waitForXModemResponse(XMODEM_TIMEOUT_MS);
        if (response != XMODEM_ACK) {
            fprintf(stderr, "Error: device NAK'd chunk seq=%u (response=%d)\n", seq, response);
            return false;
        }

        offset += chunkLen;
        seq++;

        if (progressCb)
            progressCb(offset, dataLen);
    }

    // Step 3: Send EOT
    auto xmodemEot = encodeXModem(XMODEM_EOT, 0, 0, nullptr, 0);
    auto toRadioEot = wrapInToRadio_XModem(xmodemEot);
    if (!sendFrame(toRadioEot)) {
        fprintf(stderr, "Error: failed to send XModem EOT\n");
        return false;
    }

    response = waitForXModemResponse(XMODEM_TIMEOUT_MS);
    if (response != XMODEM_ACK) {
        fprintf(stderr, "Error: device NAK'd EOT (response=%d)\n", response);
        return false;
    }

    return true;
}

// ============================================================
// XModem file download
// ============================================================

bool MeshtasticSerial::downloadFile(const std::string &devicePath, std::vector<uint8_t> &outData)
{
    outData.clear();

    // Send STX with seq=0, buffer=filepath to request download
    auto xmodemInit =
        encodeXModem(XMODEM_STX, 0, 0, reinterpret_cast<const uint8_t *>(devicePath.c_str()), devicePath.size());
    auto toRadio = wrapInToRadio_XModem(xmodemInit);
    if (!sendFrame(toRadio)) {
        fprintf(stderr, "Error: failed to send XModem download request\n");
        return false;
    }

    // Read data chunks. Device sends SOH packets, we ACK each one.
    // When device has no more data, it sends EOT.
    // Use long timeout for first chunk (ESP32-S3 USB CDC polling gap).
    bool firstChunk = true;
    for (int i = 0; i < 10000; i++) {
        auto frame = readFrame(firstChunk ? 30000 : 25000);
        firstChunk = false;
        if (frame.empty()) {
            fprintf(stderr, "Error: timeout waiting for download data\n");
            return false;
        }

        int control;
        auto chunk = parseXModemData(frame, control);

        if (control == XMODEM_NAK) {
            fprintf(stderr, "Error: device NAK'd download for '%s'\n", devicePath.c_str());
            return false;
        }

        if (control == XMODEM_EOT) {
            // Transfer complete, send ACK
            auto ack = encodeXModem(XMODEM_ACK, 0, 0, nullptr, 0);
            sendFrame(wrapInToRadio_XModem(ack));
            return true;
        }

        if (control == XMODEM_SOH && !chunk.empty()) {
            outData.insert(outData.end(), chunk.begin(), chunk.end());

            // Send ACK
            auto ack = encodeXModem(XMODEM_ACK, 0, 0, nullptr, 0);
            sendFrame(wrapInToRadio_XModem(ack));

            // If chunk is less than full size, expect EOT next
            // (firmware sets isEOT when buffer.size < max)
        }

        if (control < 0) {
            // Not an xmodem frame, skip
            continue;
        }
    }

    fprintf(stderr, "Error: download exceeded maximum chunks\n");
    return false;
}

// ============================================================
// File deletion via admin message
// ============================================================

bool MeshtasticSerial::deleteFile(const std::string &devicePath)
{
    if (myNodeNum == 0) {
        fprintf(stderr, "Error: node number unknown\n");
        return false;
    }

    // Get session key if we don't have one (only try once per connection)
    if (sessionPasskey.empty() && !sessionPasskeyAttempted) {
        sessionPasskeyAttempted = true;
        requestSessionKey(); // warning printed inside if it fails
    }

    // AdminMessage { delete_file_request = devicePath } — field 22, string
    std::vector<uint8_t> adminMsg;
    encodeStringField(adminMsg, 22, devicePath);

    // Include session passkey if we have one (field 101, bytes)
    if (!sessionPasskey.empty()) {
        encodeLengthDelimited(adminMsg, 101, sessionPasskey);
    }

    auto toRadio = buildAdminToRadio(adminMsg, false);
    if (!sendFrame(toRadio)) {
        fprintf(stderr, "Error: failed to send delete command\n");
        return false;
    }

    return true;
}

// ============================================================
// Reboot command
// ============================================================

bool MeshtasticSerial::sendReboot(int seconds)
{
    if (myNodeNum == 0) {
        fprintf(stderr, "Error: node number unknown, cannot send reboot\n");
        return false;
    }

    // Get session key if we don't have one (only try once per connection)
    if (sessionPasskey.empty() && !sessionPasskeyAttempted) {
        sessionPasskeyAttempted = true;
        requestSessionKey();
    }

    // AdminMessage { reboot_seconds = N } (field 97, varint)
    std::vector<uint8_t> adminMsg;
    encodeVarintField(adminMsg, 97, (uint64_t)seconds);

    // Include session passkey if we have one (field 101, bytes)
    if (!sessionPasskey.empty()) {
        encodeLengthDelimited(adminMsg, 101, sessionPasskey);
    }

    auto toRadio = buildAdminToRadio(adminMsg, false);
    if (!sendFrame(toRadio)) {
        fprintf(stderr, "Error: failed to send reboot command\n");
        return false;
    }

    return true;
}
