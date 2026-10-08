#include "../../src/loader/config/experienceRuntime.h"
extern "C"
{
#include "../../src/loader/config/iniParser.h"
#include "../../src/loader/hardware/lindbergh/eepromSettings.h"
}
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iostream>
#include <vector>
#include <chrono>
namespace fs = std::filesystem;
void check(bool value, const char *message)
{
    if (!value)
        throw std::runtime_error(message);
}
void write(const fs::path &p, const std::string &s)
{
    std::ofstream o(p);
    o << s;
    check(o.good(), "fixture write");
}
std::string read(const fs::path &p)
{
    std::ifstream i(p);
    std::ostringstream o;
    o << i.rdbuf();
    return o.str();
}
int entry(std::vector<std::string> strings)
{
    std::vector<char *> argv;
    for (auto &s : strings)
        argv.push_back(s.data());
    return experienceEntry(static_cast<int>(argv.size()), argv.data());
}
int main()
{
    fs::path temp;
    try
    {
        temp = fs::temp_directory_path() /
               ("experience runtime spaces-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directory(temp);
        auto initial = fs::current_path();
        auto game = temp / "game", data = temp / "durable data", session = temp / "session files", deps = temp / "dependencies";
        for (auto p : {game, data, session, deps})
            fs::create_directory(p);
        write(game / "controls.ini", "[ControllerGUIDs]\nP1_GUID = sentinel\n");
        auto controls = data / "custom controls.ini";
        write(controls, "[Driving]\nSteer = original\n");
        char effective[1024];
        fs::current_path(game);
        check(experienceControlsPath(controls.string().c_str(), effective, sizeof(effective)) == 1, "custom path selection");
        const char *guids[] = {"new-guid", "second-guid"};
        for (int i = 0; i < 3; ++i)
            check(experienceSaveGuids(effective, guids, 2) == 0, "repeat custom writeback");
        check(read(game / "controls.ini").find("sentinel") != std::string::npos, "game default untouched");
        check(read(controls).find("original") != std::string::npos && read(controls).find("new-guid") != std::string::npos,
              "bindings preserved with updated GUIDs");
        char legacy[1024];
        check(experienceControlsPath("", legacy, sizeof(legacy)) == 1, "default path");
        check(fs::path(legacy) == game / "controls.ini", "backward compatible default");
        fs::current_path(initial);
        auto config = session / "runtime configuration.ini";
        write(config, "[Paths]\nEEPROM_PATH = \"" + (data / "eeprom.bin").generic_string() + "\"\nSRAM_PATH = \"" +
                          (data / "sram.bin").generic_string() + "\"\n");
        std::vector<std::string> args = {"loader",          "-c",
                                         config.string(),   "-o",
                                         controls.string(), "-L",
                                         deps.string(),     "--experience-data",
                                         data.string(),     "--experience-session",
                                         session.string(),  "--experience-preflight"};
        check(entry(args) == 64, "missing dependency rejection");
        for (auto name : {"libstdc++.so.5", "libstdc++.so.6", "libgcc_s_dw2-1.dll"})
            write(deps / name, "synthetic dependency existence only");
        check(entry(args) == 0, "strict preflight spaces");
        auto wrong = args;
        wrong[2] = (session / "missing").string();
        check(entry(wrong) == 64, "missing config rejects fallback");
        wrong = args;
        wrong[4] = (data / "missing controls.ini").string();
        check(entry(wrong) == 64, "missing controls rejects fallback");
        wrong = args;
        wrong.insert(wrong.end(), {"-o", controls.string()});
        check(entry(wrong) == 64, "duplicate controls argument rejected");
        auto validConfig = read(config);
        write(config, "[Paths]\nEEPROM_PATH = " + (game / "eeprom.bin").generic_string() +
                          "\nSRAM_PATH = " + (data / "sram.bin").generic_string() + "\n");
        check(entry(args) == 64, "unowned EEPROM destination rejected");
        write(config, validConfig);
        check(entry(args) == 0, "preflight recovers after rejected configuration");
        args.pop_back();
        check(entry(args) == -1 && experienceActive(), "activate scoped roots");
        char mapped[1024];
        check(experienceMapPath("/home/disk1/rankingdata/table.dat", mapped, sizeof(mapped)) == 1 &&
                  fs::path(mapped) == data / "rankingdata/table.dat",
              "durable ranking map");
        check(experienceMapPath("/tmp/segaboot/test", mapped, sizeof(mapped)) == 1 && fs::path(mapped) == session / "tmp/segaboot/test",
              "temporary bootstrap map");
        check(experienceMapPath("rankingdata/../../game/controls.ini", mapped, sizeof(mapped)) < 0, "traversal rejected");
        check(experienceMapPath("/tmp/test", mapped, 2) < 0, "mapped path cannot truncate");
        check(!experienceWritablePathAllowed((game / "controls.ini").string().c_str()), "unowned game write blocked");
        check(experienceSaveGuids((game / "controls.ini").string().c_str(), guids, 2) < 0,
              "writeback cannot overwrite unowned game config");
        check(experienceSaveGuids(data.string().c_str(), guids, 2) < 0, "failed INI write is not success");
        // Actual EEPROM settings implementation must initialize the borrowed configured stream.
        auto save = data / "eeprom.bin";
        FILE *stream = std::fopen(save.string().c_str(), "w+b");
        check(stream != nullptr, "EEPROM stream");
        fs::current_path(game);
        check(eepromSettingsInit(stream) == 0, "configured EEPROM initializes");
        std::fclose(stream);
        check(!fs::exists(game / "eeprom.bin") && fs::file_size(save) > 0, "no relative EEPROM write");
        fs::current_path(initial);
        auto before = read(save);
        fs::remove_all(session);
        check(read(save) == before && fs::exists(data / "custom controls.ini"), "session cleanup retains durable data");
        check(read(game / "controls.ini").find("sentinel") != std::string::npos, "sentinel remains unchanged");
        fs::remove_all(temp);
        std::cout << "Runtime effective-path, default, preflight and save tests passed\n";
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        std::error_code ec;
        if (!temp.empty())
            fs::remove_all(temp, ec);
        return 1;
    }
}
