#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include <winsqlite/winsqlite3.h>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>
#include <stdexcept>
#include <system_error>
#pragma comment(lib, "winsqlite3.lib")
#pragma comment(lib, "ole32.lib")

namespace corebase {
namespace fs = std::filesystem;

// SQLite renders numeric columns losslessly enough for display. An empty value
// represents an unavailable specification, while a stored zero remains L"0".
struct HardwareItem {
    std::wstring model, brand, series, form_factor, architecture, tdp_w, release_year;
    std::wstring product_id, created_at, updated_at, source_url;
};
struct GpuItem : HardwareItem {
    std::wstring compute_units, compute_unit_type, base_clock_mhz, boost_clock_mhz;
    std::wstring vram_gb, vram_type, bus_bits, bandwidth_gbps, recommended_psu_w, pcie_interface;
};
struct CpuItem : HardwareItem {
    std::wstring cores, p_cores, e_cores, threads, base_clock_ghz, boost_clock_ghz;
    std::wstring l2_cache_mb, l3_cache_mb, socket, memory_support, pcie_version, max_turbo_power_w, igpu_model;
};
struct SyncLogSummary {
    std::wstring category, brand, status, records_upserted, records_skipped, error_message, started_at, finished_at;
};
struct HardwareCatalog {
    fs::path path;
    std::vector<GpuItem> gpus;
    std::vector<CpuItem> cpus;
    std::vector<SyncLogSummary> logs;
    bool immutable = false;
};

template<class T> struct Field {
    const char* column;
    const wchar_t* label;
    const wchar_t* unit;
    std::wstring T::*member;
};
// The projection is also the display schema, so detail labels and units stay
// attached to their source columns when the table's physical order changes.
inline constexpr Field<HardwareItem> commonFields[] = {
    {"brand", L"品牌", L"", &HardwareItem::brand}, {"model", L"型号", L"", &HardwareItem::model},
    {"series", L"系列", L"", &HardwareItem::series}, {"form_factor", L"形态", L"", &HardwareItem::form_factor},
    {"architecture", L"架构", L"", &HardwareItem::architecture}, {"tdp_w", L"TDP", L"W", &HardwareItem::tdp_w},
    {"release_year", L"发布年份", L"", &HardwareItem::release_year}, {"product_id", L"产品 ID", L"", &HardwareItem::product_id},
    {"created_at", L"首次入库", L"", &HardwareItem::created_at}, {"updated_at", L"更新于", L"", &HardwareItem::updated_at},
    {"source_url", L"来源网址", L"", &HardwareItem::source_url},
};
inline constexpr Field<GpuItem> gpuFields[] = {
    {"vram_gb", L"显存容量", L"GB", &GpuItem::vram_gb}, {"vram_type", L"显存类型", L"", &GpuItem::vram_type},
    {"bus_bits", L"显存位宽", L"bit", &GpuItem::bus_bits}, {"bandwidth_gbps", L"显存带宽", L"GB/s", &GpuItem::bandwidth_gbps},
    {"base_clock_mhz", L"基础频率", L"MHz", &GpuItem::base_clock_mhz}, {"boost_clock_mhz", L"加速频率", L"MHz", &GpuItem::boost_clock_mhz},
    {"compute_units", L"计算单元", L"", &GpuItem::compute_units}, {"compute_unit_type", L"计算单元类型", L"", &GpuItem::compute_unit_type},
    {"pcie_interface", L"PCIe 接口", L"", &GpuItem::pcie_interface}, {"recommended_psu_w", L"推荐电源", L"W", &GpuItem::recommended_psu_w},
};
inline constexpr Field<CpuItem> cpuFields[] = {
    {"cores", L"核心数", L"", &CpuItem::cores}, {"threads", L"线程数", L"", &CpuItem::threads},
    {"p_cores", L"P 核", L"", &CpuItem::p_cores}, {"e_cores", L"E 核", L"", &CpuItem::e_cores},
    {"base_clock_ghz", L"基础频率", L"GHz", &CpuItem::base_clock_ghz}, {"boost_clock_ghz", L"加速频率", L"GHz", &CpuItem::boost_clock_ghz},
    {"l2_cache_mb", L"L2 缓存", L"MB", &CpuItem::l2_cache_mb}, {"l3_cache_mb", L"L3 缓存", L"MB", &CpuItem::l3_cache_mb},
    {"socket", L"插槽", L"", &CpuItem::socket}, {"memory_support", L"内存支持", L"", &CpuItem::memory_support},
    {"pcie_version", L"PCIe 版本", L"", &CpuItem::pcie_version}, {"max_turbo_power_w", L"最大睿频功耗", L"W", &CpuItem::max_turbo_power_w},
    {"igpu_model", L"集成显卡", L"", &CpuItem::igpu_model},
};

inline std::wstring FromUtf8(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring result(n, L'\0');
    if (n) MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), result.data(), n);
    return result;
}
inline fs::path FindDatabase() {
    std::wstring exe(32768, L'\0');
    exe.resize(GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size())));
    auto folder = fs::path(exe).parent_path();
    for (const auto& parent : {fs::current_path(), folder, folder.parent_path()}) {
        auto path = parent / L"hardware.db";
        if (fs::is_regular_file(path)) return path;
    }
    throw std::runtime_error("未找到 hardware.db");
}

namespace detail {
inline std::string ReadUri(const fs::path& path, bool immutable) {
    std::string uri = "file:";
    constexpr char hex[] = "0123456789ABCDEF";
    for (unsigned char c : fs::absolute(path).generic_u8string()) {
        if (c == '%' || c == '?' || c == '#') {
            uri += '%'; uri += hex[c >> 4]; uri += hex[c & 15];
        } else uri += static_cast<char>(c);
    }
    return uri + (immutable ? "?mode=ro&immutable=1" : "?mode=ro");
}
inline std::wstring Column(sqlite3_stmt* stmt, int i) {
    if (sqlite3_column_type(stmt, i) == SQLITE_NULL) return {};
    const auto* value = static_cast<const wchar_t*>(sqlite3_column_text16(stmt, i));
    if (!value) throw std::bad_alloc();
    return {value, static_cast<size_t>(sqlite3_column_bytes16(stmt, i)) / sizeof(wchar_t)};
}
template<class Row> void Query(sqlite3* db, const std::string& sql, Row row) {
    sqlite3_stmt* raw = nullptr;
    int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &raw, nullptr);
    std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> stmt(raw, sqlite3_finalize);
    if (rc == SQLITE_OK) {
        while ((rc = sqlite3_step(raw)) == SQLITE_ROW) row(raw);
        if (rc == SQLITE_DONE) return;
    }
    throw std::runtime_error(sqlite3_errmsg(db));
}
template<class T, size_t N>
void ReadItems(sqlite3* db, const char* table, const Field<T> (&fields)[N], std::vector<T>& target) {
    std::string sql = "SELECT ";
    for (const auto& f : commonFields) sql += std::string(f.column) + ",";
    for (const auto& f : fields) sql += std::string(f.column) + ",";
    sql.pop_back();
    Query(db, sql + " FROM " + table + " ORDER BY model COLLATE NOCASE,product_id", [&](auto* stmt) {
        T item;
        int column = 0;
        for (const auto& f : commonFields) item.*f.member = Column(stmt, column++);
        for (const auto& f : fields) item.*f.member = Column(stmt, column++);
        target.push_back(std::move(item));
    });
}
inline HardwareCatalog Read(const fs::path& path, bool immutable = false) {
    sqlite3* raw = nullptr;
    int rc = sqlite3_open_v2(ReadUri(path, immutable).c_str(), &raw, SQLITE_OPEN_READONLY | SQLITE_OPEN_URI, nullptr);
    std::unique_ptr<sqlite3, decltype(&sqlite3_close)> db(raw, sqlite3_close);
    if (rc != SQLITE_OK) throw std::runtime_error(sqlite3_errstr(rc));
    HardwareCatalog result;
    result.immutable = immutable;
    auto ignore = [](sqlite3_stmt*) {};
    Query(raw, "BEGIN", ignore);
    ReadItems(raw, "gpus", gpuFields, result.gpus);
    ReadItems(raw, "cpus", cpuFields, result.cpus);
    Query(raw, "SELECT category,brand,status,records_upserted,records_skipped,error_message,started_at,finished_at "
        "FROM sync_logs WHERE id IN (SELECT max(id) FROM sync_logs GROUP BY brand,category)", [&](auto* stmt) {
        result.logs.push_back({Column(stmt,0), Column(stmt,1), Column(stmt,2), Column(stmt,3),
            Column(stmt,4), Column(stmt,5), Column(stmt,6), Column(stmt,7)});
    });
    Query(raw, "COMMIT", ignore);
    return result;
}

struct TempDirectory {
    fs::path path;
    TempDirectory() {
        GUID guid{};
        wchar_t name[40]{};
        if (FAILED(CoCreateGuid(&guid)) || !StringFromGUID2(guid, name, 40))
            throw std::runtime_error("无法生成临时目录名");
        path = fs::temp_directory_path() / (std::wstring(L"CoreBase-") + name);
        if (!fs::create_directory(path)) throw std::runtime_error("无法创建临时目录");
    }
    ~TempDirectory() { std::error_code ec; fs::remove_all(path, ec); }
    TempDirectory(const TempDirectory&) = delete;
    TempDirectory& operator=(const TempDirectory&) = delete;
};
struct CloseHandleDeleter { void operator()(HANDLE h) const { CloseHandle(h); } };
using Handle = std::unique_ptr<void, CloseHandleDeleter>;

// Deny writes and deletion while copying the database and its WAL as one
// generation. A writer with an open handle makes the copy fail safely.
inline Handle PinFile(const fs::path& path, bool optional = false) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h != INVALID_HANDLE_VALUE) return Handle(h);
    DWORD error = GetLastError();
    if (optional && error == ERROR_FILE_NOT_FOUND) return {};
    throw std::system_error(static_cast<int>(error), std::system_category());
}
inline void CopySnapshot(const fs::path& source, const fs::path& dest) {
    auto main = PinFile(source);
    fs::path wal = source.wstring() + L"-wal", shm = source.wstring() + L"-shm";
    auto journal = PinFile(wal, true);
    fs::copy_file(source, dest);
    if (journal) fs::copy_file(wal, dest.wstring() + L"-wal");
    if (fs::exists(shm)) fs::copy_file(shm, dest.wstring() + L"-shm");
    // copy_file preserves the read-only attribute. SQLite may need a writable
    // shm mapping in this private directory even with a read-only connection.
    for (const auto& entry : fs::directory_iterator(dest.parent_path()))
        fs::permissions(entry.path(), fs::perms::owner_write, fs::perm_options::add);
}
} // namespace detail

inline bool LoadCatalogFromDb(HardwareCatalog& catalog, std::wstring& error, fs::path path = {}) {
    try {
        if (path.empty()) path = FindDatabase();
        HardwareCatalog result;
        try {
            result = detail::Read(path);
        } catch (const std::exception&) {
            fs::path copy;
            std::unique_ptr<detail::TempDirectory> temp;
            try {
                temp = std::make_unique<detail::TempDirectory>();
                copy = temp->path / L"hardware.db";
                detail::CopySnapshot(path, copy);
            } catch (const std::exception&) {
                copy.clear();
            }
            result = copy.empty() ? detail::Read(path, true) : detail::Read(copy);
        }
        result.path = fs::absolute(path);
        catalog = std::move(result);
        error.clear();
        return true;
    } catch (const std::exception& e) {
        error = FromUtf8(e.what());
        return false;
    }
}
} // namespace corebase
