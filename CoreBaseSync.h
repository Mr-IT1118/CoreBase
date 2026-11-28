#pragma once
#include "CoreBaseData.h"
#include <algorithm>
#include <fstream>
#include <iterator>
#include <optional>
namespace corebase {
struct SyncResult {
    bool ok = false;
    std::wstring message;
};
namespace detail {
inline std::wstring Env(const wchar_t* key) {
    DWORD size = GetEnvironmentVariableW(key, nullptr, 0);
    if (!size) return {};
    std::wstring value(size, L'\0');
    value.resize(GetEnvironmentVariableW(key, value.data(), size));
    return value;
}
inline std::vector<fs::path> PythonPaths(const fs::path& root) {
    std::vector<fs::path> paths;
    auto add = [&](fs::path p) {
        std::error_code ec;
        if (!p.empty() && fs::is_regular_file(p, ec) && std::find(paths.begin(), paths.end(), p) == paths.end())
            paths.push_back(std::move(p));
    };
    add(Env(L"COREBASE_PYTHON"));
    for (const auto& dir : {root, fs::current_path()})
        for (auto venv : {L".venv", L"venv"}) add(dir / venv / L"Scripts/python.exe");
    auto active = Env(L"VIRTUAL_ENV");
    if (!active.empty()) add(fs::path(active) / L"Scripts/python.exe");
    for (auto name : {L"py.exe", L"python.exe"}) {
        wchar_t found[32768];
        DWORD n = SearchPathW(nullptr, name, nullptr, 32768, found, nullptr);
        if (n && n < 32768) add(found);
    }
    auto windows = Env(L"WINDIR");
    if (!windows.empty()) add(fs::path(windows) / L"py.exe");
    add(L"G:\\Python\\python.exe");
    add(L"I:\\python\\python.exe");
    add(L"I:\\Pyhton\\python.exe");
    return paths;
}
// Each child joins the job before its first instruction. Closing the window
// cancels the whole tree, including Chromium and any py.exe launcher child.
inline std::optional<DWORD> Run(const fs::path& exe, const std::wstring& args, const fs::path& root,
                                 HANDLE job, HANDLE cancel, DWORD timeout) {
    std::wstring command = L"\"" + exe.wstring() + L"\" " + args;
    STARTUPINFOW startup{sizeof(STARTUPINFOW)};
    PROCESS_INFORMATION info{};
    if (!CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | CREATE_SUSPENDED,
                        nullptr, root.c_str(), &startup, &info)) return {};
    Handle process(info.hProcess), thread(info.hThread);
    if (!AssignProcessToJobObject(job, process.get()) || ResumeThread(thread.get()) == DWORD(-1)) {
        TerminateProcess(process.get(), 1);
        WaitForSingleObject(process.get(), INFINITE);
        throw std::runtime_error("无法启动后台 Python 进程");
    }
    HANDLE events[] = {cancel, process.get()};
    DWORD wait = WaitForMultipleObjects(2, events, FALSE, timeout);
    if (wait != WAIT_OBJECT_0 + 1) {
        TerminateJobObject(job, 1);
        WaitForSingleObject(process.get(), INFINITE);
        if (wait == WAIT_TIMEOUT) return {};
        throw std::runtime_error("同步已取消");
    }
    DWORD exitCode = 1;
    GetExitCodeProcess(process.get(), &exitCode);
    return exitCode;
}
} // namespace detail

inline SyncResult RunSync(const fs::path& root, const std::wstring& brand, const std::wstring& category, HANDLE cancel) {
    try {
        detail::Handle job(CreateJobObjectW(nullptr, nullptr));
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!job || !SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
            throw std::runtime_error("无法创建后台同步任务");
        if (!fs::is_regular_file(root / L"crawler_bridge.py")) throw std::runtime_error("数据库目录中未找到 crawler_bridge.py");
        auto paths = detail::PythonPaths(root);
        fs::path python;
        for (const auto& path : paths) {
            auto code = detail::Run(path, L"-c \"from playwright.sync_api import sync_playwright\"", root, job.get(), cancel, 12000);
            if (code && *code == 0) { python = path; break; }
        }
        if (python.empty()) return {false, paths.empty() ? L"未找到 Python。请设置 COREBASE_PYTHON 指向解释器。"
            : L"缺少 Playwright。请在采集环境运行 python -m pip install playwright，再运行 python -m playwright install chromium。"};
        detail::TempDirectory temp;
        auto status = temp.path / L"status.json";
        auto args = L"-X utf8 crawler_bridge.py --brand " + brand + L" --category " + category
            + L" --status-json \"" + status.wstring() + L"\"";
        auto code = detail::Run(python, args, root, job.get(), cancel, INFINITE);
        std::ifstream stream(status, std::ios::binary);
        if (!code || !stream) return {false, L"采集进程未返回状态，请检查 Python 环境和数据库目录权限。"};
        std::string bytes{std::istreambuf_iterator<char>(stream), {}};
        sqlite3* raw = nullptr;
        int rc = sqlite3_open(":memory:", &raw);
        std::unique_ptr<sqlite3, decltype(&sqlite3_close)> db(raw, sqlite3_close);
        if (rc != SQLITE_OK) throw std::runtime_error("无法解析同步结果");
        sqlite3_stmt* rawStmt = nullptr;
        rc = sqlite3_prepare_v2(raw, "SELECT json_extract(?1,'$.ok'),json_extract(?1,'$.message')", -1, &rawStmt, nullptr);
        std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> stmt(rawStmt, sqlite3_finalize);
        if (rc != SQLITE_OK || sqlite3_bind_text(rawStmt, 1, bytes.data(), static_cast<int>(bytes.size()), SQLITE_STATIC) != SQLITE_OK
            || sqlite3_step(rawStmt) != SQLITE_ROW || sqlite3_column_type(rawStmt,1) != SQLITE_TEXT)
            throw std::runtime_error("无法解析同步结果 JSON");
        return {*code == 0 && sqlite3_column_int(rawStmt,0) == 1, detail::Column(rawStmt,1)};
    } catch (const std::exception& e) {
        return {false, FromUtf8(e.what())};
    }
}
} // namespace corebase
