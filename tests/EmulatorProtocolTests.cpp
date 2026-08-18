#include <gtest/gtest.h>

#include "../src/control/ControllerHost.h"
#include "../src/control/EmulatorController.h"
#include "../src/input/InputManager.h"
#include "harness/JuceHarness.h"

#include <juce_graphics/juce_graphics.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

extern "C"
{
#include "mk3_output_map.h"
}

namespace
{
class ProtocolMessageLoop final : public juce::Thread
{
public:
    ProtocolMessageLoop() : juce::Thread("emulator-protocol-message-loop") {}
    ~ProtocolMessageLoop() override { stopLoop(); }

    void startLoop()
    {
        startThread();
        while (!ready.load())
            juce::Thread::sleep(1);
    }

    void stopLoop()
    {
        if (!isThreadRunning())
            return;
        juce::MessageManager::getInstance()->stopDispatchLoop();
        stopThread(5000);
        juce::MessageManager::getInstance()->setCurrentThreadAsMessageThread();
    }

private:
    void run() override
    {
        auto* manager = juce::MessageManager::getInstance();
        manager->setCurrentThreadAsMessageThread();
        ready.store(true);
        manager->runDispatchLoop();
    }

    std::atomic<bool> ready { false };
};

class LoopbackServer
{
public:
    LoopbackServer()
    {
        listenFd = ::socket(AF_INET, SOCK_STREAM, 0);
        EXPECT_GE(listenFd, 0);

        int reuse = 1;
        ::setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

        sockaddr_in address {};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;
        EXPECT_EQ(::bind(listenFd, reinterpret_cast<sockaddr*>(&address), sizeof(address)), 0);
        EXPECT_EQ(::listen(listenFd, 1), 0);

        socklen_t length = sizeof(address);
        EXPECT_EQ(::getsockname(listenFd, reinterpret_cast<sockaddr*>(&address), &length), 0);
        port = ntohs(address.sin_port);
    }

    ~LoopbackServer()
    {
        if (clientFd >= 0)
            ::close(clientFd);
        if (listenFd >= 0)
            ::close(listenFd);
    }

    void acceptClient()
    {
        clientFd = ::accept(listenFd, nullptr, nullptr);
        ASSERT_GE(clientFd, 0);

        timeval timeout { 2, 0 };
        ::setsockopt(clientFd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    }

    void closeClient()
    {
        if (clientFd >= 0)
        {
            ::shutdown(clientFd, SHUT_RDWR);
            ::close(clientFd);
            clientFd = -1;
        }
    }

    struct Message
    {
        uint8_t type {};
        std::vector<uint8_t> payload;
    };

    Message receiveMessage()
    {
        std::array<uint8_t, 5> header {};
        EXPECT_TRUE(receiveAll(header.data(), header.size()));

        const uint32_t payloadLength =
            (static_cast<uint32_t>(header[1]) << 24)
            | (static_cast<uint32_t>(header[2]) << 16)
            | (static_cast<uint32_t>(header[3]) << 8)
            | static_cast<uint32_t>(header[4]);

        Message result;
        result.type = header[0];
        result.payload.resize(payloadLength);
        EXPECT_TRUE(receiveAll(result.payload.data(), result.payload.size()));
        return result;
    }

    void sendMessage(uint8_t type, const std::vector<uint8_t>& payload)
    {
        std::array<uint8_t, 5> header {
            type,
            static_cast<uint8_t>((payload.size() >> 24) & 0xff),
            static_cast<uint8_t>((payload.size() >> 16) & 0xff),
            static_cast<uint8_t>((payload.size() >> 8) & 0xff),
            static_cast<uint8_t>(payload.size() & 0xff)
        };
        ASSERT_TRUE(sendAll(header.data(), header.size()));
        ASSERT_TRUE(sendAll(payload.data(), payload.size()));
    }

    int getPort() const noexcept { return port; }

private:
    bool sendAll(const void* source, size_t length)
    {
        const auto* bytes = static_cast<const uint8_t*>(source);
        size_t sent = 0;
        while (sent < length)
        {
            const ssize_t count = ::send(clientFd, bytes + sent, length - sent, MSG_NOSIGNAL);
            if (count <= 0)
                return false;
            sent += static_cast<size_t>(count);
        }
        return true;
    }

    bool receiveAll(void* destination, size_t length)
    {
        auto* bytes = static_cast<uint8_t*>(destination);
        size_t received = 0;
        while (received < length)
        {
            const ssize_t count = ::recv(clientFd, bytes + received, length - received, 0);
            if (count <= 0)
                return false;
            received += static_cast<size_t>(count);
        }
        return true;
    }

    int listenFd { -1 };
    int clientFd { -1 };
    int port { 0 };
};

std::pair<std::string, uint8_t> decodeLedPayload(const std::vector<uint8_t>& payload)
{
    if (payload.size() < 2)
        return {};

    const size_t nameLength = payload[0];
    if (payload.size() != nameLength + 2)
        return {};

    return {
        std::string(reinterpret_cast<const char*>(payload.data() + 1), nameLength),
        payload.back()
    };
}

std::vector<uint8_t> buttonPayload(const std::string& name, bool pressed)
{
    std::vector<uint8_t> payload;
    payload.reserve(name.size() + 2);
    payload.push_back(static_cast<uint8_t>(name.size()));
    payload.insert(payload.end(), name.begin(), name.end());
    payload.push_back(pressed ? 1 : 0);
    return payload;
}

std::vector<uint8_t> padPayload(uint8_t pad, bool pressed, uint16_t pressure)
{
    return {
        pad,
        static_cast<uint8_t>(pressed ? 1 : 0),
        static_cast<uint8_t>((pressure >> 8) & 0xff),
        static_cast<uint8_t>(pressure & 0xff)
    };
}

std::vector<uint8_t> touchstripPayload(uint8_t finger, bool touching, uint16_t position)
{
    return {
        finger,
        static_cast<uint8_t>(touching ? 1 : 0),
        static_cast<uint8_t>((position >> 8) & 0xff),
        static_cast<uint8_t>(position & 0xff)
    };
}

std::vector<uint8_t> knobPayload(const std::string& name, int16_t delta, uint16_t absolute)
{
    std::vector<uint8_t> payload;
    payload.reserve(name.size() + 5);
    payload.push_back(static_cast<uint8_t>(name.size()));
    payload.insert(payload.end(), name.begin(), name.end());
    payload.push_back(static_cast<uint8_t>((static_cast<uint16_t>(delta) >> 8) & 0xff));
    payload.push_back(static_cast<uint8_t>(static_cast<uint16_t>(delta) & 0xff));
    payload.push_back(static_cast<uint8_t>((absolute >> 8) & 0xff));
    payload.push_back(static_cast<uint8_t>(absolute & 0xff));
    return payload;
}

TEST(EmulatorProtocolTest, UsesHardwareLedValidationAndWireEncoding)
{
    LoopbackServer server;
    ControllerHost host(nullptr, false);
    EmulatorController emulator(host, server.getPort());
    ASSERT_TRUE(emulator.isConnected());
    server.acceptClient();

    ASSERT_TRUE(emulator.setMonoLed("play", 255));
    const auto mono = server.receiveMessage();
    EXPECT_EQ(mono.type, 0x11);
    EXPECT_EQ(decodeLedPayload(mono.payload), std::make_pair(std::string("play"), uint8_t { 255 }));

    ASSERT_TRUE(emulator.setIndexedLed("p1", 78));
    const auto indexed = server.receiveMessage();
    EXPECT_EQ(indexed.type, 0x12);
    EXPECT_EQ(decodeLedPayload(indexed.payload), std::make_pair(std::string("p1"), uint8_t { 78 }));

    ASSERT_TRUE(emulator.setIndexedLed("p1", 255));
    const auto whiteAlias = server.receiveMessage();
    EXPECT_EQ(decodeLedPayload(whiteAlias.payload),
              std::make_pair(std::string("p1"), uint8_t { 255 }));

    EXPECT_FALSE(emulator.setMonoLed("p1", 12));
    EXPECT_FALSE(emulator.setIndexedLed("play", 12));
    EXPECT_FALSE(emulator.setMonoLed("notARealMk3Led", 12));

    juce::Image redFrame(juce::Image::RGB, 480, 272, true);
    redFrame.clear(redFrame.getBounds(), juce::Colours::red);
    ASSERT_TRUE(emulator.sendDisplayFrame(1, redFrame));
    const auto display = server.receiveMessage();
    EXPECT_EQ(display.type, 0x10);
    ASSERT_EQ(display.payload.size(), size_t { 1 + 480 * 272 * 2 });
    EXPECT_EQ(display.payload[0], 1);

    EXPECT_EQ(display.payload[1], 0xf8);
    EXPECT_EQ(display.payload[2], 0x00);
}

TEST(EmulatorProtocolTest, EmitsTheSameInputEdgesAsTheHardwareDriver)
{
    testharness::JuceFrameworkContext juceContext;
    ProtocolMessageLoop messageLoop;
    messageLoop.startLoop();
    InputManager inputManager;
    ControllerHost host(&inputManager, false);
    LoopbackServer server;
    EmulatorController emulator(host, server.getPort());
    ASSERT_TRUE(emulator.isConnected());
    server.acceptClient();

    std::atomic<int> buttonEvents { 0 };
    std::atomic<int> padEvents { 0 };
    std::atomic<int> knobEvents { 0 };
    std::atomic<int> stepperEvents { 0 };
    std::atomic<int> touchstripEvents { 0 };
    inputManager.addButtonHandler(
        InputManager::HandlerPriority::Global, "", "play",
        [&](InputEvent&) { ++buttonEvents; });
    inputManager.addPadHandler(
        InputManager::HandlerPriority::Global, "",
        [&](InputEvent&) { ++padEvents; }, uint8_t { 0 });
    inputManager.addTouchstripHandler(
        InputManager::HandlerPriority::Global, "",
        [&](InputEvent&) { ++touchstripEvents; });
    inputManager.addKnobHandler(
        InputManager::HandlerPriority::Global, "", "k1",
        [&](InputEvent&) { ++knobEvents; });
    inputManager.addStepperHandler(
        InputManager::HandlerPriority::Global, "",
        [&](InputEvent&) { ++stepperEvents; });

    // The HID driver reports button and pad edges only. Repeated report state
    // and pressure changes that do not change active status are silent.
    server.sendMessage(0x01, buttonPayload("play", true));
    server.sendMessage(0x01, buttonPayload("play", true));
    server.sendMessage(0x01, buttonPayload("play", false));
    server.sendMessage(0x01, buttonPayload("play", false));

    server.sendMessage(0x02, padPayload(1, true, 0));
    server.sendMessage(0x02, padPayload(1, true, 3000));
    server.sendMessage(0x02, padPayload(1, false, 255));
    server.sendMessage(0x02, padPayload(1, false, 0));

    // Touchstrip callbacks fire for position changes while touched and once
    // for release. Invalid touching/position combinations are not HID states.
    server.sendMessage(0x05, touchstripPayload(1, false, 0));
    server.sendMessage(0x05, touchstripPayload(1, true, 1000));
    server.sendMessage(0x05, touchstripPayload(1, true, 1000));
    server.sendMessage(0x05, touchstripPayload(1, true, 1023));
    server.sendMessage(0x05, touchstripPayload(1, false, 0));
    server.sendMessage(0x05, touchstripPayload(1, false, 0));
    server.sendMessage(0x05, touchstripPayload(1, false, 1000));

    // Like the USB decoder's first complete report, the connection snapshot
    // establishes absolute-control baselines without emitting input events.
    server.sendMessage(0x03, knobPayload("k1", 0, 300));
    server.sendMessage(0x03, knobPayload("k1", 1, 301));
    server.sendMessage(0x03, knobPayload("k1", 1, 301));
    server.sendMessage(0x03, knobPayload("k1", 1, 302));
    server.sendMessage(0x03, knobPayload("k1", -1, 303));
    server.sendMessage(0x03, knobPayload("k1", -303, 999));
    server.sendMessage(0x03, knobPayload("k1", 1, 0));
    server.sendMessage(0x03, knobPayload("k1", -1, 999));

    server.sendMessage(0x04, { 1, 6 });
    server.sendMessage(0x04, { 1, 7 });
    server.sendMessage(0x04, { 1, 7 });
    server.sendMessage(0x04, { 0xff, 8 });
    server.sendMessage(0x04, { 1, 8 });

    for (int attempt = 0; attempt < 50; ++attempt)
    {
        juce::Thread::sleep(10);
        if (buttonEvents.load() == 2
            && padEvents.load() == 2
            && knobEvents.load() == 5
            && stepperEvents.load() == 2
            && touchstripEvents.load() == 3)
            break;
    }

    EXPECT_EQ(buttonEvents.load(), 2);
    EXPECT_EQ(padEvents.load(), 2);
    EXPECT_EQ(knobEvents.load(), 5);
    EXPECT_EQ(stepperEvents.load(), 2);
    EXPECT_EQ(touchstripEvents.load(), 3);
}

TEST(EmulatorProtocolTest, ResetUsesTheCompleteHardwareMapInOneOrderedSequence)
{
    InputManager inputManager;
    ControllerHost host(&inputManager, false);
    LoopbackServer server;
    EmulatorController emulator(host, server.getPort());
    server.acceptClient();

    emulator.resetAllLeds();

    const auto leftDisplay = server.receiveMessage();
    const auto rightDisplay = server.receiveMessage();
    ASSERT_EQ(leftDisplay.type, 0x10);
    ASSERT_EQ(rightDisplay.type, 0x10);
    ASSERT_FALSE(leftDisplay.payload.empty());
    ASSERT_FALSE(rightDisplay.payload.empty());
    EXPECT_EQ(leftDisplay.payload[0], 0);
    EXPECT_EQ(rightDisplay.payload[0], 1);
    EXPECT_TRUE(std::all_of(leftDisplay.payload.begin() + 1, leftDisplay.payload.end(),
                            [](uint8_t value) { return value == 0; }));
    EXPECT_TRUE(std::all_of(rightDisplay.payload.begin() + 1, rightDisplay.payload.end(),
                            [](uint8_t value) { return value == 0; }));

    for (int index = 0; index < mk3_leds_count; ++index)
    {
        const auto message = server.receiveMessage();
        const auto& definition = mk3_leds[index];
        EXPECT_EQ(message.type,
                  definition.type == MK3_LED_TYPE_MONO ? 0x11 : 0x12)
            << definition.name;
        EXPECT_EQ(decodeLedPayload(message.payload),
                  std::make_pair(std::string(definition.name), uint8_t { 0 }))
            << definition.name;
    }
}

TEST(EmulatorProtocolTest, PeerDisconnectUpdatesConnectionState)
{
    InputManager inputManager;
    ControllerHost host(&inputManager, false);
    LoopbackServer server;
    EmulatorController emulator(host, server.getPort());
    server.acceptClient();
    ASSERT_TRUE(emulator.isConnected());

    server.closeClient();
    for (int attempt = 0; attempt < 100 && emulator.isConnected(); ++attempt)
        juce::Thread::sleep(10);

    EXPECT_FALSE(emulator.isConnected());
}
} // namespace
