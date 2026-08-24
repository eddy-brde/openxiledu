/*
 * Copyright 2023 ZF Friedrichshafen AG
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdlib.h>
#include <stdio.h>
#include <algorithm>
#include <atomic>
#include <boost/asio.hpp>
#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <glob.h>
#endif

#define XILENV_INTERFACE_TYPE XILENV_DLL_INTERFACE_TYPE
#include "XilEnvExtProc.h"
#include "XilEnvExtProcMain.c"

// Global variable of the external process
double ArduinoOut;
double FireUp = 0;
double AcceleratorPosition = 0.0;

static boost::asio::io_context io;
static std::unique_ptr<boost::asio::serial_port> sp;

// cyclic_test_object() is called once per XilEnv simulation cycle and must
// never block, on any platform. A background thread instead owns the
// serial port and does ordinary *blocking* reads/writes via boost::asio -
// safe here since it's off the cyclic-call thread - and hands off the
// latest values through these atomics. This also means cyclic_test_object()
// naturally always sees the most recently received pedal reading: if the
// Arduino sends faster than XilEnv cycles, older values are simply
// overwritten in the atomic before ever being read, instead of piling up
// in a backlog.
static std::thread g_ReaderThread;
static std::atomic<bool> g_StopReaderThread{false};
static std::atomic<bool> g_PortOpen{false};
static std::atomic<double> g_ArduinoOut{0.0};
static std::atomic<double> g_AcceleratorPosition{0.0};
static std::atomic<double> g_FireUp{0.0};

// Avoids re-logging "Serial port is closed" every single cycle while disconnected.
static bool s_loggedClosed = false;

// How long to wait between reconnect attempts while the port is closed
// (unplugged, not yet flashed, cable wiggled loose, ...). Short enough that
// a beginner plugging the Arduino back in doesn't have to restart anything;
// long enough not to busy-loop.
static const std::chrono::milliseconds kReconnectInterval(1000);

// How long to wait, per candidate port, for a "PedalPos:" line during
// auto-detection - generous enough to cover the ~1-2s an Arduino typically
// takes to reboot after the serial port opens (opening a port toggles DTR,
// which resets most Arduino boards).
static const std::chrono::milliseconds kPortProbeTimeout(2000);

// Serial port name: an explicit XILEDU_ARDUINO_PORT environment variable
// (e.g. "COM5" on Windows, "/dev/ttyUSB0" on Linux) always wins - useful if
// auto-detection below ever guesses wrong (e.g. multiple Arduino-like
// devices attached at once). Empty return means "auto-detect", handled by
// the reader thread.
static std::string ResolveExplicitPort(void)
{
    char envPort[256];
    if (GetEnvironmentVariableA("XILEDU_ARDUINO_PORT", envPort, sizeof(envPort)) > 0) {
        return std::string(envPort);
    }
    return std::string();
}

// Serial ports currently present on the system, to probe when no port was
// explicitly configured. A fixed default (e.g. always "COM3") only works by
// coincidence - port names/numbers are assigned to whatever's plugged in,
// not tied to any particular device - so every present port is a candidate
// here, and ProbePort() below is what actually tells the Arduino apart from
// the rest.
static std::vector<std::string> ListCandidatePorts(void)
{
    std::vector<std::string> ports;
#ifdef _WIN32
    HKEY key;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "HARDWARE\\DEVICEMAP\\SERIALCOMM", 0, KEY_READ, &key) == ERROR_SUCCESS) {
        for (DWORD index = 0; ; ++index) {
            char valueName[256];
            char data[256];
            DWORD valueNameSize = sizeof(valueName);
            DWORD dataSize = sizeof(data);
            DWORD type = 0;
            if (RegEnumValueA(key, index, valueName, &valueNameSize, nullptr, &type,
                               reinterpret_cast<BYTE*>(data), &dataSize) != ERROR_SUCCESS) {
                break;  // no more values
            }
            if (type == REG_SZ) {
                ports.emplace_back(data);
            }
        }
        RegCloseKey(key);
    }
#else
    for (const char* pattern : {"/dev/ttyACM*", "/dev/ttyUSB*"}) {
        glob_t globResult{};
        if (glob(pattern, 0, nullptr, &globResult) == 0) {
            for (std::size_t i = 0; i < globResult.gl_pathc; ++i) {
                ports.emplace_back(globResult.gl_pathv[i]);
            }
        }
        globfree(&globResult);
    }
#endif
    return ports;
}

// Opens `port` on its own throwaway io_context/serial_port (deliberately
// separate from the real connection's global `io`/`sp` - this is purely a
// probe) and waits up to `timeout` for a line starting with "PedalPos:", the
// SerialBridge sketch's signature line. A successful open alone doesn't mean
// anything - lots of non-Arduino devices show up as serial ports (Bluetooth
// virtual COM ports, a modem, ...) - so this is what auto-detection actually
// relies on to recognize the right one.
static bool ProbePort(const std::string& port, unsigned baud, std::chrono::milliseconds timeout)
{
    try {
        boost::asio::io_context probeIo;
        boost::asio::serial_port probeSp(probeIo, port);
        probeSp.set_option(boost::asio::serial_port_base::baud_rate(baud));
        probeSp.set_option(boost::asio::serial_port_base::character_size(8));
        probeSp.set_option(boost::asio::serial_port_base::parity(boost::asio::serial_port_base::parity::none));
        probeSp.set_option(boost::asio::serial_port_base::stop_bits(boost::asio::serial_port_base::stop_bits::one));
        probeSp.set_option(boost::asio::serial_port_base::flow_control(boost::asio::serial_port_base::flow_control::none));

        const auto deadline = std::chrono::steady_clock::now() + timeout;
        std::string line;
        while (std::chrono::steady_clock::now() < deadline) {
            probeIo.restart();
            boost::asio::steady_timer timer(probeIo);
            timer.expires_after(std::chrono::milliseconds(200));
            boost::system::error_code readEc = boost::asio::error::would_block;
            char c = 0;

            timer.async_wait([&](const boost::system::error_code& err) {
                if (!err) {
                    probeSp.cancel();
                }
            });
            probeSp.async_read_some(boost::asio::buffer(&c, 1),
                [&](const boost::system::error_code& err, std::size_t) {
                    readEc = err;
                    timer.cancel();
                });
            probeIo.run();

            if (readEc) {
                continue;  // this 200ms slice found nothing - keep trying until the overall deadline
            }
            if (c == '\n') {
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                if (line.rfind("PedalPos:", 0) == 0) {
                    return true;
                }
                line.clear();
            } else {
                line.push_back(c);
            }
        }
    } catch (const std::exception&) {
        // Port doesn't exist, is already in use, or otherwise can't be
        // opened - not our Arduino either way.
    }
    return false;
}

// Sleeps for kReconnectInterval, but checks g_StopReaderThread every 100ms
// instead of sleeping through it in one go, so shutdown stays responsive.
static void SleepUnlessStopping(std::chrono::milliseconds duration)
{
    const auto step = std::chrono::milliseconds(100);
    for (auto waited = std::chrono::milliseconds(0); waited < duration && !g_StopReaderThread.load(); waited += step) {
        std::this_thread::sleep_for(step);
    }
}

// Runs on a dedicated background thread for the lifetime of the process
// (started once from init_test_object(), stopped from terminate_test_object()).
// Owns the serial port entirely: auto-detects it (unless explicitPort
// overrides that), opens it, reads/writes on it with ordinary *blocking*
// boost::asio calls (safe here - this thread is not on the XilEnv sync
// barrier), and - if the port fails to open, drops out mid read, or isn't
// found at all - keeps retrying every kReconnectInterval instead of giving
// up, so a beginner whose USB cable wiggles loose (or ends up replugged into
// a different port) doesn't need to restart anything.
static void ReaderThreadFunc(std::string explicitPort, unsigned baud)
{
    std::string lastGoodPort;  // tried first on every reconnect, so a plain
                                // cable-wiggle back into the same port
                                // reconnects without a full re-scan
    while (!g_StopReaderThread.load()) {
        std::string port = explicitPort;
        if (port.empty()) {
            std::vector<std::string> candidates = ListCandidatePorts();
            if (!lastGoodPort.empty()) {
                candidates.erase(std::remove(candidates.begin(), candidates.end(), lastGoodPort), candidates.end());
                candidates.insert(candidates.begin(), lastGoodPort);
            }
            for (const std::string& candidate : candidates) {
                if (ProbePort(candidate, baud, kPortProbeTimeout)) {
                    port = candidate;
                    break;
                }
            }
            if (port.empty()) {
                std::cerr << "No Arduino found on any serial port - retrying in "
                          << kReconnectInterval.count() << "ms" << std::endl;
                g_PortOpen.store(false);
                SleepUnlessStopping(kReconnectInterval);
                continue;
            }
            if (port != lastGoodPort) {
                std::cerr << "Found Arduino on " << port << std::endl;
            }
            lastGoodPort = port;
        }

        try {
            io.restart();
            if (!sp) {
                sp = std::make_unique<boost::asio::serial_port>(io);
            }
            if (sp->is_open()) {
                sp->close();
            }
            sp->open(port);
            sp->set_option(boost::asio::serial_port_base::baud_rate(baud));
            sp->set_option(boost::asio::serial_port_base::character_size(8));
            sp->set_option(boost::asio::serial_port_base::parity(boost::asio::serial_port_base::parity::none));
            sp->set_option(boost::asio::serial_port_base::stop_bits(boost::asio::serial_port_base::stop_bits::one));
            sp->set_option(boost::asio::serial_port_base::flow_control(boost::asio::serial_port_base::flow_control::none));
        } catch (const std::exception& ex) {
            std::cerr << "Failed to open serial port " << port << ": " << ex.what()
                      << " - retrying in " << kReconnectInterval.count() << "ms" << std::endl;
            g_PortOpen.store(false);
            SleepUnlessStopping(kReconnectInterval);
            continue;
        }

        std::cerr << "Serial port " << port << " opened" << std::endl;
        g_PortOpen.store(true);
        s_loggedClosed = false;

        std::string line;
        boost::system::error_code ec;
        while (!g_StopReaderThread.load()) {
            char c;
            std::size_t n = boost::asio::read(*sp, boost::asio::buffer(&c, 1), ec);
            if (ec || n == 0) {
                // Port closed (either by us below/in StopReaderThread, or
                // because the Arduino was unplugged) - fall through to the
                // reconnect loop unless we're actually shutting down.
                break;
            }

            if (c != '\n') {
                line.push_back(c);
                continue;
            }

            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            try {
                std::string prefix = "PedalPos:";
                if (line.rfind(prefix, 0) == 0) {           // check prefix at start
                    std::string valueStr = line.substr(prefix.size());
                    double out = std::stod(valueStr);
                    g_ArduinoOut.store(out);
                    g_AcceleratorPosition.store(out / 1024 * 100);  // map to 0..100 %
                }
            } catch (...) {
                // Ignoring garbage line
            }
            line.clear();

            std::string msg = "FireUp:" + std::to_string(static_cast<int>(g_FireUp.load())) + "\n";
            boost::asio::write(*sp, boost::asio::buffer(msg), ec);
            // Ignore write errors here: a truly dead port will fail the next
            // read() above and end the loop; no need to duplicate that check.
        }

        g_PortOpen.store(false);
        if (g_StopReaderThread.load()) {
            break;  // clean shutdown - don't attempt to reconnect
        }
        std::cerr << "Serial port " << port << " closed - retrying in "
                  << kReconnectInterval.count() << "ms" << std::endl;
        SleepUnlessStopping(kReconnectInterval);
    }
}

// This will be called first if the process is started
void reference_varis (void)
{
    REFERENCE_DOUBLE_VAR(ArduinoOut, "ArduinoOut");
    REF_DIR_DOUBLE_VAR(READ_ONLY_REFERENCE, FireUp, "FireUp", "");
    REF_DIR_DOUBLE_VAR(READ_WRITE_REFERENCE, AcceleratorPosition, "AcceleratorPosition", "");
}

// This function will be called next to reference_varis
int init_test_object(void)
{
    // The reader thread owns the whole connect/reconnect lifecycle from
    // here on, so it's only started once - a later re-init (e.g. a
    // simulation reset) just leaves it running rather than bouncing the
    // port closed and reopening it.
    if (!g_ReaderThread.joinable()) {
        std::string explicitPort = ResolveExplicitPort();
        unsigned baud = 115200;
        g_StopReaderThread.store(false);
        g_ReaderThread = std::thread(ReaderThreadFunc, explicitPort, baud);
    }

    return 0;   // == 0 -> No error continue
        // != 0 -> Error do not continue
}

// This will call every simulated cycle. Does no I/O at all - just hands the
// latest values back and forth with the reader thread via atomics - so it
// can never block regardless of platform.
void cyclic_test_object(void)
{
    if (!g_PortOpen.load()) {
        if (!s_loggedClosed) {
            std::cerr << "Serial port is closed" << std::endl;
            s_loggedClosed = true;
        }
        return;
    }

    g_FireUp.store(FireUp);
    ArduinoOut = g_ArduinoOut.load();
    AcceleratorPosition = g_AcceleratorPosition.load();
}

// This will be called if the external processs will be terminated
void terminate_test_object(void)
{
    g_StopReaderThread.store(true);
    if (sp) {
        // The reader thread is blocked inside a synchronous read() (or
        // sleeping between reconnect attempts) - closing the port here is
        // what unblocks an in-flight read (with an error), letting the
        // thread notice g_StopReaderThread and exit.
        boost::system::error_code ignored;
        sp->close(ignored);
    }
    if (g_ReaderThread.joinable()) {
        g_ReaderThread.join();
    }
    g_PortOpen.store(false);
}
