#include "data/game_path.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

using beachlife::data::normalize_game_path;

void expect(bool valid)
{
    if (!valid)
    {
        throw std::runtime_error("Game path check failed.");
    }
}

int main()
{
    const auto a = normalize_game_path(".\\Data//Main Menu/./Picture.TGA");
    const auto b = normalize_game_path("data/main menu/picture.tga");
    expect(a.display == "Data/Main Menu/Picture.TGA" && a.key == b.key);
    expect(normalize_game_path("Data/Gr\xc3\xbc\xc3\x9f"
                               "e.txt")
               .display == "Data/Gr\xc3\xbc\xc3\x9f"
                           "e.txt");
    expect(normalize_game_path("Data/Conifer.tga").key == "data/conifer.tga");
    expect(normalize_game_path("A/file/").key == "a/file");
    for (const std::string_view bad : {"",
             ".",
             "..",
             "../a",
             "a/../b",
             "/a",
             "\\a",
             "C:a",
             "C:\\a",
             "\\\\host\\file",
             "a:stream",
             "a/fi*le",
             "a/f?le",
             "a/\"b",
             "a/<b",
             "a/b>",
             "a|b",
             "a.",
             "a ",
             "a/.. ",
             "a/COM1.txt",
             "NUL",
             "aux.ini",
             "lpt9",
             "CONOUT$",
             "a\nfile",
             "\xc0\xaf",
             "\xed\xa0\x80",
             "\xf4\x90\x80\x80",
             "\xe2\x82",
             "\xf0\x80\x80\xaf",
             "\x80"})
    {
        bool rejected = false;
        try
        {
            (void)normalize_game_path(bad);
        }
        catch (const std::invalid_argument&)
        {
            rejected = true;
        }
        expect(rejected);
    }
    for (const auto& bad : {std::string("a\0b", 3), std::string(4097, 'a')})
    {
        bool rejected = false;
        try
        {
            (void)normalize_game_path(bad);
        }
        catch (const std::invalid_argument&)
        {
            rejected = true;
        }
        expect(rejected);
    }
    std::cout << "Relative paths, case lookup, Unicode and invalid-name checks passed.\n";
}
