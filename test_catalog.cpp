// Integration smoke test. Build with cl /std:c++17 /utf-8 /EHsc test_catalog.cpp
#include "CoreBaseData.h"
#include <algorithm>
#include <cassert>
#include <iostream>

int wmain(int argc, wchar_t** argv)
{
    using namespace corebase;
    HardwareCatalog catalog;
    std::wstring error;
    if (!LoadCatalogFromDb(catalog, error, argc > 1 ? argv[1] : fs::path{})) {
        std::wcerr << error << L'\n';
        return 1;
    }
    std::wcout << L"Database: " << catalog.path.wstring() << L'\n';
    for (const auto* brand : {L"NVIDIA", L"AMD", L"Intel"}) {
        auto matches = [brand](const auto& row) { return row.brand == brand; };
        auto sample = std::find_if(catalog.gpus.begin(), catalog.gpus.end(), matches);
        assert(sample != catalog.gpus.end());
        assert(!sample->vram_gb.empty() && !sample->vram_type.empty());
        std::wcout << brand << L" GPU: " << std::count_if(catalog.gpus.begin(), catalog.gpus.end(), matches)
            << L" | " << sample->model << L" | " << sample->vram_gb << L" GB " << sample->vram_type
            << L" | " << sample->bus_bits << L" bit | TDP "
            << (sample->tdp_w.empty() ? L"--" : sample->tdp_w + L" W") << L'\n';
    }
    auto intel = [](const auto& row) { return row.brand == L"Intel"; };
    auto cpu = std::find_if(catalog.cpus.begin(), catalog.cpus.end(), [&](const auto& row) {
        return intel(row) && !row.cores.empty() && !row.threads.empty() && !row.boost_clock_ghz.empty();
    });
    assert(cpu != catalog.cpus.end());
    std::wcout << L"Intel CPU: " << std::count_if(catalog.cpus.begin(), catalog.cpus.end(), intel)
        << L" | " << cpu->model << L" | " << cpu->cores << L"C/" << cpu->threads << L"T | "
        << cpu->boost_clock_ghz << L" GHz\n";
    std::wcout << L"Latest sync summaries: " << catalog.logs.size() << L'\n';
}
