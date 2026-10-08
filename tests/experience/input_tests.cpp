#include "../../src/loader/config/experienceInput.h"
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <stdexcept>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#endif
unsigned checks = 0;
void check(bool value, const char *message)
{
    ++checks;
    if (!value)
        throw std::runtime_error(message);
}
int main()
{
    try
    {
        std::array<unsigned char, 32> packet{};
        packet[0] = 'O';
        packet[1] = 'R';
        packet[2] = 'I';
        packet[3] = '1';
        packet[4] = 1;
        packet[6] = 32;
        packet[8] = 1;
        packet[12] = packet[13] = 255;
        packet[14] = packet[15] = 255;
        packet[18] = 0x15;
        ExperienceInputSnapshot value{};
        check(experienceInputDecode(packet.data(), packet.size(), &value) == 0, "production decoder accepts versioned snapshot");
        check(value.steer == 1 && value.gas == 1 && value.brake == 0 && value.buttons == 0x15 && value.sequence == 1,
              "production analog and button values");
        auto invalid = packet;
        invalid[4] = 2;
        check(experienceInputDecode(invalid.data(), invalid.size(), &value) == -1, "unknown protocol rejected");
        invalid = packet;
        invalid[20] = 1;
        check(experienceInputDecode(invalid.data(), invalid.size(), &value) == -1, "reserved bytes rejected");
        invalid = packet;
        invalid[19] = 2;
        check(experienceInputDecode(invalid.data(), invalid.size(), &value) == -1, "unknown flags rejected");
        check(experienceInputDecode(packet.data(), 31, &value) == -1, "truncated snapshot rejected");
        check(experienceInputDecode(nullptr, 32, &value) == -1, "null snapshot rejected");
        check(experienceInputConfigure("ordinary-path", 1) == -1, "non-pipe path rejected");
        check(experienceInputEnabled() == 0, "legacy launch unchanged without channel");
#ifdef _WIN32
        char name[100]{};
        std::snprintf(name, sizeof(name), "\\\\.\\pipe\\OutRunExperience-00000000-0000-0000-0000-%012llu",
                      static_cast<unsigned long long>(GetTickCount64() % 1000000000000ull));
        HANDLE server =
            CreateNamedPipeA(name, PIPE_ACCESS_OUTBOUND | FILE_FLAG_OVERLAPPED,
                             PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, 32, 32, 0, nullptr);
        check(server != INVALID_HANDLE_VALUE, "native fixture server created");
        OVERLAPPED connection{};
        connection.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        check(!ConnectNamedPipe(server, &connection) && GetLastError() == ERROR_IO_PENDING, "native connection pending");
        check(experienceInputConfigure(name, GetCurrentProcessId()) == 0 && experienceInputEnabled(), "reviewed pipe configured");
        check(experienceInputPoll(&value) == 0 && value.gas == 0 && value.buttons == 0, "before delivery all actions neutral");
        DWORD n = 0;
        check(WaitForSingleObject(connection.hEvent, 1000) == WAIT_OBJECT_0 && GetOverlappedResult(server, &connection, &n, FALSE),
              "native client connected");
        OVERLAPPED write{};
        write.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        auto send = [&] {
            ResetEvent(write.hEvent);
            BOOL result = WriteFile(server, packet.data(), static_cast<DWORD>(packet.size()), &n, &write);
            if (!result && GetLastError() == ERROR_IO_PENDING)
            {
                check(WaitForSingleObject(write.hEvent, 1000) == WAIT_OBJECT_0 && GetOverlappedResult(server, &write, &n, FALSE),
                      "fixture write completes bounded");
            }
            else
                check(result && n == 32, "fixture snapshot delivered");
        };
        send();
        check(experienceInputPoll(&value) == 1 && value.gas == 1 && value.buttons == 0x15, "production receiver delivers logical actions");
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        check(experienceInputPoll(&value) == 0 && value.gas == 0 && value.brake == 0 && value.buttons == 0,
              "watchdog neutralizes stale held pedals/buttons");
        packet[8] = 2;
        send();
        check(experienceInputPoll(&value) == 1 && value.sequence == 2, "fresh snapshot resumes after watchdog");
        send();
        check(experienceInputPoll(&value) == -1 && value.gas == 0, "replayed sequence fails neutral");
        experienceInputClose();
        check(experienceInputEnabled() == 0, "receiver resources released");
        DisconnectNamedPipe(server);
        CloseHandle(server);
        CloseHandle(connection.hEvent);
        CloseHandle(write.hEvent);
        server =
            CreateNamedPipeA(name, PIPE_ACCESS_OUTBOUND | FILE_FLAG_OVERLAPPED,
                             PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, 32, 32, 0, nullptr);
        check(server != INVALID_HANDLE_VALUE, "repeated channel fixture created");
        connection = {};
        connection.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        check(!ConnectNamedPipe(server, &connection) && GetLastError() == ERROR_IO_PENDING, "wrong-owner fixture connection pending");
        check(experienceInputConfigure(name, GetCurrentProcessId() + 1) == 0, "wrong-owner configuration can be parsed");
        check(experienceInputPoll(&value) == -1 && value.buttons == 0, "production receiver rejects wrong server PID");
        experienceInputClose();
        CloseHandle(server);
        CloseHandle(connection.hEvent);
#endif
        std::cout << "experience_input: " << checks << " checks passed\n";
        return 0;
    }
    catch (const std::exception &e)
    {
        experienceInputClose();
        std::cerr << e.what() << '\n';
        return 1;
    }
}
