// inject_win.hpp - start Minecraft and load your enabled mod DLLs into it (Windows only).
//
// Safety rules built in:
//   * only ever targets the Minecraft Bedrock process (Minecraft.Windows.exe)
//   * only loads DLLs from bedrockdeck's own mods folder that you switched on
//   * nothing is hidden: every step is reported back to the user
#pragma once
#include <string>
#include <vector>
#include "mods.hpp"

#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
  #endif
  #ifndef NOMINMAX
  #define NOMINMAX
  #endif
  #include <windows.h>
  #include <aclapi.h>
  #include <sddl.h>
  #include <shellapi.h>
  #include <tlhelp32.h>
#endif

namespace deck {

#ifdef _WIN32

inline std::wstring widen(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

inline DWORD find_minecraft_pid() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof pe;
    DWORD pid = 0;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, L"Minecraft.Windows.exe") == 0) { pid = pe.th32ProcessID; break; }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return pid;
}

inline bool minecraft_running() { return find_minecraft_pid() != 0; }

inline std::string launch_minecraft() {
    HINSTANCE r = ShellExecuteW(nullptr, L"open", L"minecraft:", nullptr, nullptr, SW_SHOWNORMAL);
    if ((INT_PTR)r <= 32) return "Could not start Minecraft. Is Minecraft Bedrock installed on this PC?";
    return "Starting Minecraft...";
}

// Minecraft runs in a sandbox; a DLL must be readable by "ALL APPLICATION PACKAGES" before it can load.
inline bool grant_app_package_access(const std::wstring& path, std::string& err) {
    PSID sid = nullptr;
    if (!ConvertStringSidToSidW(L"S-1-15-2-1", &sid)) { err = "could not create security id"; return false; }
    PACL old_dacl = nullptr;
    PSECURITY_DESCRIPTOR sd = nullptr;
    DWORD rc = GetNamedSecurityInfoW(path.c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, &old_dacl, nullptr, &sd);
    bool ok = false;
    if (rc != ERROR_SUCCESS) {
        err = "could not read file permissions (code " + std::to_string(rc) + ")";
    } else {
        EXPLICIT_ACCESSW ea{};
        ea.grfAccessPermissions = GENERIC_READ | GENERIC_EXECUTE;
        ea.grfAccessMode = GRANT_ACCESS;
        ea.grfInheritance = NO_INHERITANCE;
        ea.Trustee.TrusteeForm = TRUSTEE_IS_SID;
        ea.Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
        ea.Trustee.ptstrName = (LPWSTR)sid;
        PACL new_dacl = nullptr;
        rc = SetEntriesInAclW(1, &ea, old_dacl, &new_dacl);
        if (rc != ERROR_SUCCESS) {
            err = "could not build permissions (code " + std::to_string(rc) + ")";
        } else {
            rc = SetNamedSecurityInfoW((LPWSTR)path.c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, new_dacl, nullptr);
            if (rc == ERROR_SUCCESS) ok = true;
            else err = "could not change file permissions (code " + std::to_string(rc) + ")";
            LocalFree(new_dacl);
        }
    }
    if (sd) LocalFree(sd);
    LocalFree(sid);
    return ok;
}

inline bool inject_dll(DWORD pid, const std::wstring& dll, std::string& err) {
    HANDLE proc = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ, FALSE, pid);
    if (!proc) { err = "could not open the Minecraft process (try running bedrockdeck as the same user)"; return false; }
    size_t bytes = (dll.size() + 1) * sizeof(wchar_t);
    void* remote = VirtualAllocEx(proc, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    bool ok = false;
    if (!remote) {
        err = "could not allocate memory in Minecraft";
    } else if (!WriteProcessMemory(proc, remote, dll.c_str(), bytes, nullptr)) {
        err = "could not write to Minecraft's memory";
    } else {
        auto loadlib = (LPTHREAD_START_ROUTINE)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");
        HANDLE th = CreateRemoteThread(proc, nullptr, 0, loadlib, remote, 0, nullptr);
        if (!th) {
            err = "could not start the loader thread";
        } else {
            WaitForSingleObject(th, 15000);
            DWORD code = 0;
            GetExitCodeThread(th, &code);
            if (code == 0) err = "Windows refused to load the DLL (wrong architecture, missing dependency, or the mod crashed on load)";
            else ok = true;
            CloseHandle(th);
        }
    }
    if (remote) VirtualFreeEx(proc, remote, 0, MEM_RELEASE);
    CloseHandle(proc);
    return ok;
}

inline std::vector<std::string> inject_enabled(const fs::path& data) {
    std::vector<std::string> log;
    DWORD pid = find_minecraft_pid();
    if (!pid) { log.push_back("Minecraft is not running. Start it, wait for the title screen, then inject."); return log; }
    int n = 0;
    for (auto& m : list_mods(data)) {
        if (!m.enabled) continue;
        ++n;
        std::wstring path = widen(S(fs::absolute(mods_dir(data) / P(m.name))));
        std::string err;
        if (!grant_app_package_access(path, err)) { log.push_back(m.name + ": " + err); continue; }
        if (inject_dll(pid, path, err)) log.push_back(m.name + ": loaded");
        else log.push_back(m.name + ": " + err);
    }
    if (!n) log.push_back("No mods are switched on. Add a .dll in the Mods box and tick it.");
    return log;
}

#else  // not Windows: the injector only exists for Minecraft on Windows

inline bool minecraft_running() { return false; }
inline std::string launch_minecraft() { return "Launching Minecraft is only supported on Windows."; }
inline std::vector<std::string> inject_enabled(const fs::path&) { return {"Injecting mods is only supported on Windows."}; }

#endif

}  // namespace deck
