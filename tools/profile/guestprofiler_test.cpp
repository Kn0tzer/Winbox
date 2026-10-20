// Native unit test for the guest-profile per-file aggregation (no wasm, no
// boxedwine runtime). Build:
//   g++ -std=c++17 -I source/kernel tools/profile/guestprofiler_test.cpp -o /tmp/guestprofiler_test
#include "guestprofiler.h"

#include <cassert>
#include <iostream>

int main() {
    std::vector<BwGuestProfileEntry> entries = {
        {7, "notepad.exe", "C:\\windows\\system32\\notepad.exe", 10},
        {7, "notepad.exe", "C:\\windows\\system32\\user32.dll", 3},
        {7, "notepad.exe", "C:\\windows\\system32\\notepad.exe", 5},
        {12, "hl2.exe", "C:\\windows\\system32\\d3d9.dll", 100},
        {12, "hl2.exe", "(unmapped)", 2},
    };
    const std::string json = bwGuestProfileAggregateJson(entries);

    // Duplicate (pid, file) rows are summed; total is the sum of all samples.
    assert(json.find("\"totalSamples\":120") != std::string::npos);
    assert(json.find("{\"pid\":7,\"exe\":\"notepad.exe\",\"files\":[{\"path\":\"C:\\\\windows\\\\system32\\\\notepad.exe\",\"samples\":15},{\"path\":\"C:\\\\windows\\\\system32\\\\user32.dll\",\"samples\":3}]}") != std::string::npos);
    // Processes are ordered by pid (7 before 12) and files by samples desc.
    assert(json.find("\"pid\":7") < json.find("\"pid\":12"));
    assert(json.find("d3d9.dll") < json.find("(unmapped)"));

    // Escaping: quotes, backslashes and control characters must survive.
    assert(bwGuestProfileJsonEscape("a\"b\\c\n\t\x01") == "a\\\"b\\\\c\\n\\t\\u0001");

    // Empty input is still valid JSON.
    const std::string empty = bwGuestProfileAggregateJson({});
    assert(empty == "{\"totalSamples\":0,\"processes\":[]}");

    std::cout << "guestprofiler aggregation tests passed\n";
    return 0;
}
