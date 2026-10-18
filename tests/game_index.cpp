#include "data/game_index.h"
#include <iostream>
#include <stdexcept>

using namespace beachlife::data;
void expect(bool value)
{
    if (!value)
    {
        throw std::runtime_error("Game mapping check failed.");
    }
}
template <class F> void rejects(F fn)
{
    bool failed = false;
    try
    {
        fn();
    }
    catch (const std::exception&)
    {
        failed = true;
    }
    expect(failed);
}

int main()
{
    CabinetIndex cab;
    cab.files = {{"Setup.exe", "", "cabinet", 20},
        {"BeachLife.ini", "", "cabinet", 4},
        {"screen.tga", "Data\\Screens", "cabinet", 8},
        {"link.url", "Web", "cabinet", 1},
        {"", "", "placeholder", 0},
        {"loose.pis", "Data\\Script", "external", 3}};
    cab.groups = {
        {"<Support>Files", 0, 0}, {"Ess_English", 1, 1}, {"UK Comp", 2, 4}, {"UK Uncomp", 5, 5}};
    IsoIndex iso;
    IsoEntry loose;
    loose.path = "DATA/Script/Loose.pis";
    loose.size = 3;
    iso.entries.push_back(loose);
    const auto mapped = map_game_files(cab, iso);
    expect(mapped.files.size() == 3 && mapped.total_bytes == 15 && mapped.language == "en");
    expect(mapped.files[0].path == "BeachLife.ini");
    expect(mapped.files[2].external && mapped.files[2].source_path == loose.path);
    rejects(
        [&]
        {
            map_game_files(cab, IsoIndex{});
        });
    auto bad = cab;
    bad.files[5].size = 4;
    rejects(
        [&]
        {
            map_game_files(bad, iso);
        });
    bad = cab;
    bad.files[2].directory = "../Data";
    rejects(
        [&]
        {
            map_game_files(bad, iso);
        });
    bad = cab;
    bad.files[2].name = "../screen.tga";
    rejects(
        [&]
        {
            map_game_files(bad, iso);
        });
    bad = cab;
    bad.groups[1].name = "Ess_German";
    rejects(
        [&]
        {
            map_game_files(bad, iso);
        });
    bad = cab;
    bad.groups.push_back(cab.groups[2]);
    rejects(
        [&]
        {
            map_game_files(bad, iso);
        });
    bad = cab;
    bad.files[3] = bad.files[2];
    bad.files[3].name = "SCREEN.TGA";
    rejects(
        [&]
        {
            map_game_files(bad, iso);
        });
    bad = cab;
    bad.files[3] = {"Screens", "Data", "cabinet", 8};
    rejects(
        [&]
        {
            map_game_files(bad, iso);
        });
    bad = cab;
    bad.files[2].name = "\xe9.tga";
    rejects(
        [&]
        {
            map_game_files(bad, iso);
        });
    std::cout << "Game path mapping, support exclusion, loose files and ambiguity checks passed.\n";
}
