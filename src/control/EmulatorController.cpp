#include "EmulatorController.h"
#include "ControllerHost.h"
#include "../devices/DisplayFrameConversion.h"

#include <juce_graphics/juce_graphics.h>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstring>

extern "C"
{
#include "mk3_output_map.h"
#include "mk3_input_map.h"
}

namespace
{
constexpr auto kPollInterval = std::chrono::milliseconds(10);
constexpr int kReadBufSize   = 4096;

void logEmu(const juce::String& message)
{
    juce::Logger::writeToLog("[EmulatorController] " + message);
}

const mk3_led_definition_t* findLedDefinition(const std::string& name)
{
    for (int i = 0; i < mk3_leds_count; ++i)
    {
        if (name == mk3_leds[i].name)
            return &mk3_leds[i];
    }
    return nullptr;
}

bool isHardwareButton(const std::string& name)
{
    for (int i = 0; i < mk3_buttons_count; ++i)
    {
        if (name == mk3_buttons[i].name)
            return true;
    }
    return false;
}

bool isHardwareKnob(const std::string& name)
{
    for (int i = 0; i < mk3_knob_names_count; ++i)
    {
        if (name == mk3_knob_names[i])
            return true;
    }
    return false;
}

bool isDisplayKnob(const std::string& name)
{
    return name.size() == 2 && name[0] == 'k'
        && name[1] >= '1' && name[1] <= '8';
}

uint16_t maximumKnobValue(const std::string& name)
{
    if (isDisplayKnob(name))
        return 999;
    return 4095;
}
} // namespace

EmulatorController::EmulatorController(ControllerHost& hostRef, int port)
    : host(hostRef), gestureProcessor(hostRef, *this)
{
    displayBuffer.malloc(static_cast<size_t>(kDisplayWidth)
                         * static_cast<size_t>(kDisplayHeight) * 2);

    if (port == 0)
    {
        port = 9999;
        const char* portEnv = std::getenv("MK3_EMU_PORT");
        if (portEnv != nullptr)
        {
            const int envPort = std::atoi(portEnv);
            if (envPort > 0 && envPort <= 65535)
                port = envPort;
        }
    }

    if (!tryConnect(port))
        return;

    running.store(true, std::memory_order_relaxed);
    pollThread = std::thread([this]() { pollLoop(); });
    logEmu("Connected and polling for input.");
}

EmulatorController::~EmulatorController()
{
    shutdown();
}

bool EmulatorController::tryConnect(int port)
{
    const int socketFd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (socketFd < 0)
    {
        logEmu("Failed to create socket.");
        return false;
    }

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(static_cast<uint16_t>(port));
    ::inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    if (::connect(socketFd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0)
    {
        logEmu("No emulator server at 127.0.0.1:" + juce::String(port) + " — emulator mirror disabled.");
        ::close(socketFd);
        return false;
    }

    // Disable Nagle for low latency
    int flag = 1;
    ::setsockopt(socketFd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

    // Non-blocking for poll
    int flags = ::fcntl(socketFd, F_GETFL, 0);
    ::fcntl(socketFd, F_SETFL, flags | O_NONBLOCK);

    sockFd.store(socketFd, std::memory_order_release);

    return true;
}

bool EmulatorController::isConnected() const noexcept
{
    return sockFd.load(std::memory_order_acquire) >= 0;
}

void EmulatorController::shutdown()
{
    // relaxed — poll loop checks this flag each iteration; no data-dependent
    // ordering, join() below provides the synchronization we need.
    running.store(false, std::memory_order_relaxed);
    if (pollThread.joinable())
        pollThread.join();

    const int socketFd = sockFd.exchange(-1, std::memory_order_acq_rel);
    if (socketFd >= 0)
    {
        std::scoped_lock lock(outputMutex);
        ::close(socketFd);
        logEmu("Disconnected.");
    }
}

// --- Output: Display ---

bool EmulatorController::sendDisplayFrame(int screenIndex, const juce::Image& sourceImage)
{
    if (!isConnected() || screenIndex < 0 || screenIndex > 1 || sourceImage.isNull())
        return false;

    juce::Image image = sourceImage;
    if (image.getWidth() != kDisplayWidth || image.getHeight() != kDisplayHeight)
        image = sourceImage.rescaled(kDisplayWidth, kDisplayHeight);

    std::scoped_lock lock(outputMutex);
    auto* buffer = displayBuffer.getData();
    if (buffer == nullptr)
        return false;

    const uint32_t pixelBytes = static_cast<uint32_t>(kDisplayWidth * kDisplayHeight * 2);
    if (!DisplayFrameConversion::toRgb565BigEndian(image, { buffer, pixelBytes }))
        return false;
    const uint32_t payloadLen = 1 + pixelBytes;

    // Header: [type][len BE][screen_index]
    uint8_t header[6];
    header[0] = kMsgDisplay;
    header[1] = static_cast<uint8_t>((payloadLen >> 24) & 0xFF);
    header[2] = static_cast<uint8_t>((payloadLen >> 16) & 0xFF);
    header[3] = static_cast<uint8_t>((payloadLen >> 8) & 0xFF);
    header[4] = static_cast<uint8_t>(payloadLen & 0xFF);
    header[5] = static_cast<uint8_t>(screenIndex);

    if (!sendAll(header, 6))
        return false;
    return sendAll(buffer, pixelBytes);
}

bool EmulatorController::clearDisplay(int screenIndex, uint16_t color)
{
    if (!isConnected() || screenIndex < 0 || screenIndex > 1)
        return false;

    std::scoped_lock lock(outputMutex);
    auto* buffer = displayBuffer.getData();
    if (buffer == nullptr)
        return false;

    const size_t pixelCount = static_cast<size_t>(kDisplayWidth * kDisplayHeight);
    for (size_t i = 0; i < pixelCount; ++i)
    {
        buffer[i * 2] = static_cast<uint8_t>((color >> 8) & 0xff);
        buffer[i * 2 + 1] = static_cast<uint8_t>(color & 0xff);
    }

    const uint32_t pixelBytes = static_cast<uint32_t>(pixelCount * 2);
    const uint32_t payloadLen = 1 + pixelBytes;

    uint8_t header[6];
    header[0] = kMsgDisplay;
    header[1] = static_cast<uint8_t>((payloadLen >> 24) & 0xFF);
    header[2] = static_cast<uint8_t>((payloadLen >> 16) & 0xFF);
    header[3] = static_cast<uint8_t>((payloadLen >> 8) & 0xFF);
    header[4] = static_cast<uint8_t>(payloadLen & 0xFF);
    header[5] = static_cast<uint8_t>(screenIndex);

    if (!sendAll(header, 6))
        return false;
    return sendAll(buffer, pixelBytes);
}

void EmulatorController::clearAllDisplays()
{
    std::scoped_lock lock(outputMutex);
    clearDisplay(0, 0);
    clearDisplay(1, 0);
}

// --- Output: LEDs ---

bool EmulatorController::setMonoLed(const std::string& ledName, uint8_t brightness)
{
    if (!isConnected())
        return false;

    const auto* definition = findLedDefinition(ledName);
    if (definition == nullptr || definition->type != MK3_LED_TYPE_MONO)
        return false;

    const auto nameLen = static_cast<uint8_t>(std::min(ledName.size(), size_t(255)));
    uint8_t payload[258];
    payload[0] = nameLen;
    std::memcpy(payload + 1, ledName.data(), nameLen);
    payload[1 + nameLen] = brightness;

    std::scoped_lock lock(outputMutex);
    return sendMessage(kMsgLedMono, payload, static_cast<uint32_t>(1 + nameLen + 1));
}

bool EmulatorController::setIndexedLed(const std::string& ledName, uint8_t colorIndex)
{
    if (!isConnected())
        return false;

    const auto* definition = findLedDefinition(ledName);
    if (definition == nullptr || definition->type != MK3_LED_TYPE_INDEXED)
        return false;

    const auto nameLen = static_cast<uint8_t>(std::min(ledName.size(), size_t(255)));
    uint8_t payload[258];
    payload[0] = nameLen;
    std::memcpy(payload + 1, ledName.data(), nameLen);
    payload[1 + nameLen] = colorIndex;

    std::scoped_lock lock(outputMutex);
    return sendMessage(kMsgLedIndexed, payload, static_cast<uint32_t>(1 + nameLen + 1));
}

void EmulatorController::setButtonActive(const std::string& buttonName, bool active)
{
    gestureProcessor.setButtonActive(buttonName, active);
}

void EmulatorController::setButtonBrightness(const std::string& buttonName, uint8_t brightness)
{
    gestureProcessor.setButtonBrightness(buttonName, brightness);
}

void EmulatorController::resetAllLeds()
{
    std::scoped_lock lock(outputMutex);
    clearAllDisplays();
    for (int i = 0; i < mk3_leds_count; ++i)
    {
        const auto& definition = mk3_leds[i];
        if (definition.type == MK3_LED_TYPE_MONO)
            setMonoLed(definition.name, 0);
        else
            setIndexedLed(definition.name, 0);
    }
    gestureProcessor.invalidateLedOutputCache();
}

// --- Network helpers ---

bool EmulatorController::sendAll(const void* data, size_t len)
{
    const auto* p = static_cast<const uint8_t*>(data);
    size_t remaining = len;
    while (remaining > 0)
    {
        const int socketFd = sockFd.load(std::memory_order_acquire);
        if (socketFd < 0)
            return false;
        ssize_t sent = ::send(socketFd, p, remaining, MSG_NOSIGNAL);
        if (sent > 0)
        {
            p += sent;
            remaining -= static_cast<size_t>(sent);
        }
        else if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
        {
            // Non-blocking socket buffer full — wait briefly and retry
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        else
        {
            // Connection lost
            int expected = socketFd;
            if (sockFd.compare_exchange_strong(
                    expected, -1, std::memory_order_acq_rel))
                ::close(socketFd);
            return false;
        }
    }
    return true;
}

bool EmulatorController::sendMessage(uint8_t type, const void* payload, uint32_t payloadLen)
{
    uint8_t header[5];
    header[0] = type;
    header[1] = static_cast<uint8_t>((payloadLen >> 24) & 0xFF);
    header[2] = static_cast<uint8_t>((payloadLen >> 16) & 0xFF);
    header[3] = static_cast<uint8_t>((payloadLen >> 8) & 0xFF);
    header[4] = static_cast<uint8_t>(payloadLen & 0xFF);

    if (!sendAll(header, 5))
        return false;
    if (payloadLen > 0 && payload != nullptr)
        return sendAll(payload, payloadLen);
    return true;
}

// --- Input polling ---

void EmulatorController::pollLoop()
{
    uint8_t readBuf[kReadBufSize];
    int readBufLen = 0;

    while (running.load(std::memory_order_relaxed))
    {
        const int socketFd = sockFd.load(std::memory_order_acquire);
        if (socketFd < 0)
            break;

        // Drain available data (non-blocking)
        bool connectionLost = false;
        while (readBufLen < kReadBufSize)
        {
            ssize_t n = ::recv(socketFd, readBuf + readBufLen,
                               static_cast<size_t>(kReadBufSize - readBufLen), 0);
            if (n > 0)
            {
                readBufLen += static_cast<int>(n);
                continue;
            }
            if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR))
                connectionLost = true;
            if (n <= 0)
                break;
        }

        if (connectionLost)
        {
            std::scoped_lock lock(outputMutex);
            int expected = socketFd;
            if (sockFd.compare_exchange_strong(
                    expected, -1, std::memory_order_acq_rel))
                ::close(socketFd);
            break;
        }

        // Process complete messages: [type:1][length:4 BE][payload]
        while (readBufLen >= 5)
        {
            uint8_t type = readBuf[0];
            uint32_t payloadLen =
                (static_cast<uint32_t>(readBuf[1]) << 24) |
                (static_cast<uint32_t>(readBuf[2]) << 16) |
                (static_cast<uint32_t>(readBuf[3]) << 8) |
                 static_cast<uint32_t>(readBuf[4]);

            uint32_t msgTotal = 5 + payloadLen;

            if (msgTotal > kReadBufSize)
            {
                readBufLen = 0;
                break;
            }

            if (static_cast<uint32_t>(readBufLen) < msgTotal)
                break;

            processMessage(type, readBuf + 5, payloadLen);

            int remaining = readBufLen - static_cast<int>(msgTotal);
            if (remaining > 0)
                std::memmove(readBuf, readBuf + msgTotal, static_cast<size_t>(remaining));
            readBufLen = remaining;
        }

        if (!running.load(std::memory_order_relaxed))
            break;

        std::this_thread::sleep_for(kPollInterval);
    }

    running.store(false, std::memory_order_relaxed);
}

void EmulatorController::processMessage(uint8_t type, const uint8_t* payload, uint32_t len)
{
    switch (type)
    {
    case kMsgButton:
    {
        if (len < 2)
            return;
        uint8_t nameLen = payload[0];
        if (len != static_cast<uint32_t>(1 + nameLen + 1))
            return;

        std::string name(reinterpret_cast<const char*>(payload + 1), nameLen);
        if (payload[1 + nameLen] > 1)
            return;
        bool pressed = payload[1 + nameLen] != 0;

        if (!isHardwareButton(name))
            return;

        auto [state, inserted] = buttonPressedStates.try_emplace(name, false);
        juce::ignoreUnused(inserted);
        if (state->second == pressed)
            return;
        state->second = pressed;

        gestureProcessor.handleButton(name, pressed);
        break;
    }

    case kMsgPad:
    {
        if (len != 4)
            return;
        uint8_t padIndex = payload[0];
        if (payload[1] > 1)
            return;
        bool pressed = payload[1] != 0;
        uint16_t pressure = static_cast<uint16_t>(
            (static_cast<uint16_t>(payload[2]) << 8) | payload[3]);
        if (pressure > 0x0ffd)
            return;

        if (padIndex == 0 || padIndex > 16)
            return;
        uint8_t physicalPad = padIndex - 1;

        if (padPressedStates[physicalPad] == pressed)
            return;
        padPressedStates[physicalPad] = pressed;

        gestureProcessor.handlePad(physicalPad, pressed, pressure);
        break;
    }

    case kMsgKnob:
    {
        if (len < 2)
            return;
        uint8_t nameLen = payload[0];
        if (len != static_cast<uint32_t>(1 + nameLen + 4))
            return;

        std::string name(reinterpret_cast<const char*>(payload + 1), nameLen);
        if (!isHardwareKnob(name))
            return;
        int16_t delta = static_cast<int16_t>(
            (static_cast<uint16_t>(payload[1 + nameLen]) << 8) |
             static_cast<uint16_t>(payload[2 + nameLen]));
        uint16_t absolute = static_cast<uint16_t>(
            (static_cast<uint16_t>(payload[3 + nameLen]) << 8) |
             static_cast<uint16_t>(payload[4 + nameLen]));

        const bool wraps = isDisplayKnob(name);
        const int modulus = static_cast<int>(maximumKnobValue(name)) + 1;
        const int halfRange = modulus / 2;
        const int minimumDelta = wraps ? -halfRange : -32768;
        const int maximumDelta = wraps ? halfRange : 32767;
        if (delta < minimumDelta || delta > maximumDelta
            || absolute > maximumKnobValue(name))
            return;

        auto position = knobPositions.find(name);
        if (position == knobPositions.end())
        {
            knobPositions.emplace(name, absolute);
            // The USB decoder consumes the first absolute report as its
            // baseline. The emulator sends the same snapshot on connection.
            break;
        }
        if (position->second == absolute)
            return;

        int expectedDelta = static_cast<int>(absolute) - static_cast<int>(position->second);
        if (wraps && expectedDelta > halfRange)
            expectedDelta -= modulus;
        else if (wraps && expectedDelta < -halfRange)
            expectedDelta += modulus;
        if (delta != expectedDelta)
            return;
        position->second = absolute;

        gestureProcessor.handleKnob(name, delta, absolute);
        break;
    }

    case kMsgStepper:
    {
        if (len != 2)
            return;
        auto direction = static_cast<int8_t>(payload[0]);
        uint8_t position = payload[1];

        if ((direction != -1 && direction != 1) || position > 15)
            return;

        if (!stepperPositionKnown)
        {
            stepperPosition = position;
            stepperPositionKnown = true;
            break;
        }

        if (position == stepperPosition)
            return;

        const bool jumpBackward = stepperPosition == 0x00 && position == 0x0f;
        const bool jumpForward = stepperPosition == 0x0f && position == 0x00;
        const bool increment = ((stepperPosition < position) && !jumpBackward) || jumpForward;
        const int8_t expectedDirection = increment ? 1 : -1;
        if (direction != expectedDirection)
            return;
        stepperPosition = position;

        gestureProcessor.handleStepper(direction, position);
        break;
    }

    case kMsgTouchstrip:
    {
        if (len != 4 || payload[1] > 1)
            return;

        const uint8_t finger = payload[0];
        const bool touching = payload[1] != 0;
        const uint16_t position = static_cast<uint16_t>(
            (static_cast<uint16_t>(payload[2]) << 8) | payload[3]);

        if (finger != 1 || position > 0x03ff || touching != (position != 0))
            return;

        const auto fingerIndex = static_cast<size_t>(finger - 1);
        auto& previousPosition = touchstripPositions[fingerIndex];
        if (!touchstripPositionKnown[fingerIndex])
        {
            previousPosition = position;
            touchstripPositionKnown[fingerIndex] = true;
            break;
        }
        if (previousPosition == position)
            return;
        previousPosition = position;

        gestureProcessor.handleTouchstrip(finger, touching, position);
        break;
    }

    default:
        break;
    }
}
