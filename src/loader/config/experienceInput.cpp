#include "experienceInput.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <string>
#ifdef _WIN32
#include <windows.h>
#endif
namespace
{
    std::string pipeName;
    unsigned ownerPid = 0;
    unsigned get(const unsigned char *p, unsigned offset, unsigned bytes)
    {
        unsigned value = 0;
        for (unsigned i = 0; i < bytes; ++i)
            value |= static_cast<unsigned>(p[offset + i]) << (8 * i);
        return value;
    }
    void neutral(ExperienceInputSnapshot &s)
    {
        s = {};
        s.steer = 32768.0 / 65535.0;
    }
#ifdef _WIN32
    HANDLE pipe = INVALID_HANDLE_VALUE;
    OVERLAPPED operation{};
    std::array<unsigned char, 32> buffer{};
    bool pending = false, observed = false, stale = false, failed = false;
    ULONGLONG began = 0, received = 0;
    ExperienceInputSnapshot last{};
#endif
} // namespace
extern "C" int experienceInputDecode(const unsigned char *p, size_t size, ExperienceInputSnapshot *out)
{
    if (!p || !out || size != 32 || std::memcmp(p, "ORI1", 4) || get(p, 4, 2) != 1 || get(p, 6, 2) != 32 || (get(p, 18, 2) & ~479u) != 0)
        return -1;
    for (unsigned i = 20; i < 32; ++i)
        if (p[i])
            return -1;
    ExperienceInputSnapshot value{};
    value.sequence = get(p, 8, 4);
    value.steer = get(p, 12, 2) / 65535.0;
    value.gas = get(p, 14, 2) / 65535.0;
    value.brake = get(p, 16, 2) / 65535.0;
    value.buttons = get(p, 18, 2);
    *out = value;
    return 0;
}
extern "C" int experienceInputConfigure(const char *name, unsigned serverPid)
{
    if (!name || !serverPid || !pipeName.empty())
        return -1;
    std::string value(name), prefix = "\\\\.\\pipe\\OutRunExperience-";
    if (value.size() != prefix.size() + 36 || value.rfind(prefix, 0) != 0)
        return -1;
    auto id = value.substr(prefix.size());
    for (size_t i = 0; i < id.size(); ++i)
    {
        if (i == 8 || i == 13 || i == 18 || i == 23)
        {
            if (id[i] != '-')
                return -1;
        }
        else if (std::string("0123456789abcdefABCDEF").find(id[i]) == std::string::npos)
            return -1;
    }
#ifdef _WIN32
    pipeName = value;
    ownerPid = serverPid;
    began = GetTickCount64();
    neutral(last);
    return 0;
#else
    return -1;
#endif
}
extern "C" int experienceInputEnabled()
{
    return pipeName.empty() ? 0 : 1;
}
extern "C" int experienceInputPoll(ExperienceInputSnapshot *output)
{
    if (!output)
        return -1;
    neutral(*output);
#ifdef _WIN32
    if (pipeName.empty() || failed)
        return -1;
    if (pipe == INVALID_HANDLE_VALUE)
    {
        pipe =
            CreateFileA(pipeName.c_str(), GENERIC_READ | FILE_WRITE_ATTRIBUTES, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        if (pipe == INVALID_HANDLE_VALUE)
            return GetTickCount64() - began < 5000 ? 0 : -1;
        ULONG server = 0;
        if (!GetNamedPipeServerProcessId(pipe, &server) || server != ownerPid)
        {
            failed = true;
            return -1;
        }
        DWORD mode = PIPE_READMODE_MESSAGE;
        if (!SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr))
        {
            failed = true;
            return -1;
        }
        operation.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!operation.hEvent)
        {
            failed = true;
            return -1;
        }
    }
    // Bound work per frame. The host has at most one outstanding packet.
    for (int i = 0; i < 8; ++i)
    {
        DWORD count = 0;
        if (pending)
        {
            if (!GetOverlappedResult(pipe, &operation, &count, FALSE))
            {
                if (GetLastError() == ERROR_IO_INCOMPLETE)
                    break;
                failed = true;
                return -1;
            }
            pending = false;
        }
        else
        {
            ResetEvent(operation.hEvent);
            if (!ReadFile(pipe, buffer.data(), static_cast<DWORD>(buffer.size()), &count, &operation))
            {
                if (GetLastError() == ERROR_IO_PENDING)
                {
                    pending = true;
                    break;
                }
                failed = true;
                return -1;
            }
        }
        ExperienceInputSnapshot next{};
        if (experienceInputDecode(buffer.data(), count, &next) != 0)
        {
            failed = true;
            return -1;
        }
        if (observed && (next.sequence - last.sequence == 0 || next.sequence - last.sequence >= 0x80000000u))
        {
            failed = true;
            return -1;
        }
        last = next;
        received = GetTickCount64();
        if (!observed)
        {
            std::puts("EXPERIENCE_INPUT_RECEIVED=1");
            std::fflush(stdout);
            observed = true;
        }
        if (stale)
        {
            std::puts("Experience input recovered; controls resumed");
            stale = false;
        }
    }
    if (observed && GetTickCount64() - received <= 250)
    {
        *output = last;
        return 1;
    }
    if (observed && !stale)
    {
        std::puts("Experience input stale; controls neutralized");
        std::fflush(stdout);
        stale = true;
    }
    return 0;
#else
    return -1;
#endif
}
extern "C" void experienceInputClose()
{
#ifdef _WIN32
    if (pipe != INVALID_HANDLE_VALUE)
    {
        CancelIoEx(pipe, nullptr);
        if (pending)
        {
            DWORD n = 0;
            GetOverlappedResult(pipe, &operation, &n, TRUE);
        }
        CloseHandle(pipe);
        pipe = INVALID_HANDLE_VALUE;
    }
    if (operation.hEvent)
        CloseHandle(operation.hEvent);
    operation = {};
    pending = observed = stale = failed = false;
#endif
    pipeName.clear();
    ownerPid = 0;
}
