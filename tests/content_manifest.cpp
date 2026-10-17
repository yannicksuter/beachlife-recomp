#include "data/content_manifest.h"
#include <iostream>
#include <stdexcept>

using namespace beachlife::data;
void expect(bool value)
{
    if (!value)
    {
        throw std::runtime_error("Content manifest check failed.");
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
    ContentManifest input;
    input.source_sha256 = std::string(64, 'a');
    input.profile = "uk-english-v1";
    input.language = "en";
    input.files = {
        {{"BeachLife.ini", 2}, std::string(64, 'b')}, {{"Data/a.bin", 3}, std::string(64, 'c')}};
    input.total_bytes = 5;
    const auto json = content_manifest_json(input);
    const auto parsed = parse_content_manifest(json);
    expect(parsed.identity() == input.identity() && parsed.files.size() == 2 &&
           parsed.files[1].size == 3);
    auto other = input;
    other.source_sha256[0] = 'b';
    expect(other.identity() != input.identity());
    other = input;
    other.files[1].path = "Data/../bad";
    rejects(
        [&]
        {
            content_manifest_json(other);
        });
    other = input;
    other.files.push_back({{"data/A.bin", 0}, std::string(64, 'd')});
    rejects(
        [&]
        {
            content_manifest_json(other);
        });
    other = input;
    other.total_bytes = 6;
    rejects(
        [&]
        {
            content_manifest_json(other);
        });
    other = input;
    other.files[1].sha256 = "bad";
    rejects(
        [&]
        {
            content_manifest_json(other);
        });
    other = input;
    other.files.erase(other.files.begin());
    other.total_bytes = 3;
    rejects(
        [&]
        {
            content_manifest_json(other);
        });
    for (const auto& invalid : {"{\"format\":1," + json.substr(1),
             "{\"extra\":true," + json.substr(1),
             json.substr(0, json.size() - 2),
             std::string("[]")})
    {
        rejects(
            [&]
            {
                parse_content_manifest(invalid);
            });
    }
    auto changed = json;
    changed.replace(changed.find("\"format\":1"), 10, "\"format\":2");
    rejects(
        [&]
        {
            parse_content_manifest(changed);
        });
    // Preparation must never publish a document larger than mounting accepts.
    other = input;
    for (int i = 0; i < 8300; ++i)
    {
        other.files.push_back(
            {{"Data/" + std::string(3980, 'x') + std::to_string(i), 0}, std::string(64, 'd')});
    }
    rejects(
        [&]
        {
            content_manifest_json(other);
        });
    std::cout
        << "Content identities, manifest roundtrip, invalid records and duplicate fields passed.\n";
}
