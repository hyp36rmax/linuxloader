#include "experienceRuntime.h"
extern "C"
{
#include "iniParser.h"
}
#include <filesystem>
#include <string>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <cstdlib>
#include <stdexcept>
#include <map>
#ifndef EXPERIENCE_BUILD_REVISION
#define EXPERIENCE_BUILD_REVISION "unversioned"
#endif
namespace
{
    namespace fs = std::filesystem;
    fs::path dataRoot, sessionRoot;
    bool active = false;
    bool inside(const fs::path &path, const fs::path &root)
    {
        auto relative = fs::weakly_canonical(path).lexically_relative(root);
        return !relative.empty() && *relative.begin() != "..";
    }
    int copy(const fs::path &p, char *output, size_t capacity)
    {
        auto text = p.string();
        if (!output || text.size() + 1 > capacity)
        {
            errno = ENAMETOOLONG;
            return -1;
        }
        std::memcpy(output, text.c_str(), text.size() + 1);
        return 1;
    }
    fs::path directory(const std::string &value)
    {
        auto p = fs::canonical(value);
        if (!fs::is_directory(p))
            throw std::runtime_error("Expected an existing directory");
        return p;
    }
    void report()
    {
        std::printf("LINDBERGH_EXPERIENCE=1\nCONTRACT=1\nBASE_REVISION=9aa6e3e45ccfbbc60eb0f975aa9d3d158a13706c\nBUILD_REVISION=%s\nARCH="
                    "x86\nCONTROLS_EFFECTIVE_PATH=1\nCONFIG_SPACES=1\nSAVE_ROOTS=1\nGAMEPLAY_READINESS=0\nCOMPLETE_ISOLATION=0\n",
                    EXPERIENCE_BUILD_REVISION);
    }
} // namespace
extern "C" int experienceEntry(int argc, char **argv)
{
    if (argc == 2 && std::strcmp(argv[1], "--experience-capabilities") == 0)
    {
        report();
        return 0;
    }
    bool requested = false, preflight = false;
    std::map<std::string, std::string> values;
    for (int i = 1; i < argc; ++i)
    {
        std::string key = argv[i];
        if (key == "--experience-data" || key == "--experience-session" || key == "--experience-preflight")
            requested = true;
    }
    if (!requested)
        return -1;
    try
    {
        if (std::strcmp(EXPERIENCE_BUILD_REVISION, "unversioned") == 0)
            throw std::runtime_error("Runtime build revision unavailable");
        for (int i = 1; i < argc; ++i)
        {
            std::string key = argv[i];
            if (key == "--experience-preflight")
            {
                preflight = true;
                continue;
            }
            if (key == "-c" || key == "-o" || key == "-L" || key == "--experience-data" || key == "--experience-session")
            {
                if (i + 1 >= argc || values.count(key))
                    throw std::runtime_error("Missing or duplicate integration argument");
                std::string value = argv[++i];
                for (unsigned char c : value)
                    if (c < 32 || c > 126)
                        throw std::runtime_error("Integration currently requires ASCII paths");
                if (value.size() >= 240)
                    throw std::runtime_error("Integration path exceeds reviewed Windows path budget");
                values[key] = value;
            }
        }
        for (auto key : {"-c", "-o", "-L", "--experience-data", "--experience-session"})
            if (!values.count(key))
                throw std::runtime_error("Required integration argument absent");
        auto config = fs::canonical(values.at("-c")), controls = fs::canonical(values.at("-o"));
        if (!fs::is_regular_file(config) || !fs::is_regular_file(controls))
            throw std::runtime_error("Configuration file missing");
        auto data = directory(values.at("--experience-data")), session = directory(values.at("--experience-session")),
             deps = directory(values.at("-L"));
        if (inside(data, session) || inside(session, data) || !inside(config, session) || !inside(controls, data))
            throw std::runtime_error("Unsafe integration path ownership");
        for (auto name : {"libstdc++.so.5", "libstdc++.so.6", "libgcc_s_dw2-1.dll"})
            if (!fs::is_regular_file(deps / name))
                throw std::runtime_error(std::string("Missing dependency: ") + name);
        auto ini = iniLoad(config.string().c_str());
        if (!ini)
            throw std::runtime_error("Cannot parse configuration");
        bool safe = true;
        for (auto key : {"EEPROM_PATH", "SRAM_PATH"})
        {
            auto value = iniGetValue(ini, "Paths", key);
            if (!value)
                safe = false;
            else
            {
                std::string path = value;
                if (path.size() >= 2 && path.front() == '"' && path.back() == '"')
                    path = path.substr(1, path.size() - 2);
                if (!inside(fs::path(path), data))
                    safe = false;
            }
        }
        iniFree(ini);
        if (!safe)
            throw std::runtime_error("EEPROM/SRAM paths must be inside durable data root");
        if (preflight)
        {
            std::printf("EXPERIENCE_PREFLIGHT=1\n");
            return 0;
        }
        dataRoot = data;
        sessionRoot = session;
        fs::create_directories(dataRoot / "rankingdata");
        fs::create_directories(sessionRoot / "tmp" / "segaboot");
        active = true;
        return -1;
    }
    catch (const std::exception &e)
    {
        std::fprintf(stderr, "Experience preflight failed: %s\n", e.what());
        return 64;
    }
}
extern "C" int experienceActive()
{
    return active ? 1 : 0;
}
extern "C" int experienceMapPath(const char *path, char *output, size_t capacity)
{
    if (!active || !path)
        return 0;
    try
    {
        std::string name = path;
        for (auto &c : name)
            if (c == '\\')
                c = '/';
        while (name.rfind("./", 0) == 0)
            name.erase(0, 2);
        fs::path root;
        std::string tail;
        for (auto prefix : {std::string("/home/disk1/rankingdata"), std::string("rankingdata")})
            if (name == prefix || name.rfind(prefix + "/", 0) == 0)
            {
                root = dataRoot / "rankingdata";
                tail = name.substr(prefix.size());
                break;
            }
        if (root.empty())
            for (auto prefix : {std::string("/var/tmp"), std::string("/tmp"), std::string("tmp")})
                if (name == prefix || name.rfind(prefix + "/", 0) == 0)
                {
                    root = sessionRoot / "tmp";
                    tail = name.substr(prefix.size());
                    break;
                }
        if (root.empty() && name == "warning")
        {
            root = sessionRoot / "tmp";
            tail = "/warning";
        }
        if (root.empty())
            return 0;
        if (!tail.empty() && tail[0] == '/')
            tail.erase(0, 1);
        auto relative = fs::path(tail);
        for (auto &component : relative)
            if (component == ".." || component == "." || component.string().find(':') != std::string::npos)
            {
                errno = EACCES;
                return -1;
            }
        auto target = root / relative;
        if (!inside(target, root))
        {
            errno = EACCES;
            return -1;
        }
        return copy(target, output, capacity);
    }
    catch (...)
    {
        errno = EINVAL;
        return -1;
    }
}
extern "C" int experienceWritablePathAllowed(const char *path)
{
    if (!active)
        return 1;
    try
    {
        if (!path)
            return 0;
        return inside(fs::absolute(path), dataRoot) || inside(fs::absolute(path), sessionRoot);
    }
    catch (...)
    {
        return 0;
    }
}
extern "C" int experienceControlsPath(const char *requested, char *output, size_t capacity)
{
    try
    {
        fs::path selected = (requested && *requested) ? fs::path(requested) : fs::path("controls.ini");
        if (!fs::is_regular_file(selected))
        {
            if (active)
            {
                errno = ENOENT;
                return -1;
            }
            selected = "controls.ini";
        }
        return copy(fs::absolute(selected).lexically_normal(), output, capacity);
    }
    catch (...)
    {
        errno = EINVAL;
        return -1;
    }
}
extern "C" int experienceSaveGuids(const char *path, const char *const *guids, int count)
{
    if (!path || !guids || count < 0 || count > 8 || !experienceWritablePathAllowed(path))
        return -1;
    IniConfig *ini = iniLoad(path);
    if (!ini)
        ini = static_cast<IniConfig *>(std::calloc(1, sizeof(IniConfig)));
    if (!ini)
        return -1;
    bool ok = true;
    for (int i = 0; i < count; ++i)
        if (guids[i] && *guids[i])
        {
            char key[32];
            std::snprintf(key, sizeof(key), "P%d_GUID", i + 1);
            if (!iniSetValue(ini, "ControllerGUIDs", key, guids[i]))
                ok = false;
        }
    int result = ok ? iniSave(ini, path) : -1;
    iniFree(ini);
    return result > 0 ? 0 : -1;
}
extern "C" const char *experienceTemporaryRoot()
{
    static std::string text;
    if (!active)
        return nullptr;
    text = (sessionRoot / "tmp").string();
    return text.c_str();
}
extern "C" void experienceInitialized()
{
    if (active)
    {
        std::puts("EXPERIENCE_RUNTIME_INITIALIZED=1");
        std::fflush(stdout);
    }
}
