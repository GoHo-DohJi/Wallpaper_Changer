#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

// Required libraries
#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Advapi32.lib")
#pragma comment(lib, "User32.lib")

// Linker
#pragma comment(linker, "/SUBSYSTEM:WINDOWS")

#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <string>
#include <vector>
#include <deque>
#include <algorithm>
#include <random>

// ============================================================================
// RAII COM Pointer (no WRL dependency)
// ============================================================================

template <typename T>
class ScopedComPtr {
    T* m_ptr = nullptr;
public:
    ScopedComPtr() = default;
    ~ScopedComPtr() { Reset(); }
    ScopedComPtr(const ScopedComPtr&) = delete;
    ScopedComPtr& operator=(const ScopedComPtr&) = delete;

    void Reset() {
        if (m_ptr) {
            m_ptr->Release();
            m_ptr = nullptr;
        }
    }

    T*  Get()          const { return m_ptr; }
    T*  operator->()   const { return m_ptr; }
    T** GetAddressOf()       { Reset(); return &m_ptr; }
    explicit operator bool() const { return m_ptr != nullptr; }
};

// ============================================================================
// Constants
// ============================================================================

static const wchar_t* MUTEX_NAME            = L"Global\\WallpaperChangerSingleInstanceMutex_v1";
static const wchar_t* REG_KEY_PATH          = L"Software\\WallpaperChanger";
static const wchar_t* REG_VALUE_INTERVAL    = L"IntervalSeconds";
static const DWORD    DEFAULT_INTERVAL_SEC  = 300;        // 5 minutes
static const DWORD    MAX_INTERVAL_SEC      = 86400;      // 24 hours
static const wchar_t* EVENT_LOG_SOURCE      = L"WallpaperChanger";
static const wchar_t* WND_CLASS_NAME        = L"WallpaperChangerHiddenWnd";

static const wchar_t* SUPPORTED_EXTENSIONS[] = {
    L".jpg", L".jpeg", L".jpe", L".jfif",
    L".png", L".bmp",  L".dib",
    L".gif", L".tif",  L".tiff", L".wdp",
    L".heic", L".heif",
    L".webp",
    L".avif"
};
static const size_t NUM_EXTENSIONS =
    sizeof(SUPPORTED_EXTENSIONS) / sizeof(SUPPORTED_EXTENSIONS[0]);

// ============================================================================
// Global state
// ============================================================================

static HANDLE g_hStopEvent = NULL;          // signalled → graceful exit

// ============================================================================
// Error-State Logging (logs only on state transition)
// ============================================================================

enum class ErrorState {
    None,
    FolderNotFound,
    NoImagesFound,
    SetWallpaperFailed,
    ComInitFailed
};

static ErrorState g_errorState = ErrorState::None;

static void LogEventError(const wchar_t* message) {
    HANDLE hLog = RegisterEventSourceW(NULL, EVENT_LOG_SOURCE);
    if (hLog) {
        const wchar_t* msgs[] = { message };
        ReportEventW(hLog, EVENTLOG_ERROR_TYPE, 0, 1000, NULL, 1, 0, msgs, NULL);
        DeregisterEventSource(hLog);
    }
}

static void TransitionError(ErrorState s, const wchar_t* msg) {
    if (s != g_errorState) {
        if (s != ErrorState::None && msg)
            LogEventError(msg);
        g_errorState = s;
    }
}

// ============================================================================
// Registry: read (or create default) interval
// ============================================================================

static DWORD ReadOrCreateInterval() {
    HKEY  hKey     = NULL;
    DWORD interval = DEFAULT_INTERVAL_SEC;

    if (RegCreateKeyExW(HKEY_CURRENT_USER, REG_KEY_PATH, 0, NULL,
            REG_OPTION_NON_VOLATILE, KEY_READ | KEY_WRITE,
            NULL, &hKey, NULL) != ERROR_SUCCESS)
        return DEFAULT_INTERVAL_SEC;

    DWORD size = sizeof(DWORD), type = 0, val = 0;
    LONG  rc = RegQueryValueExW(hKey, REG_VALUE_INTERVAL, NULL, &type,
                                reinterpret_cast<LPBYTE>(&val), &size);

    if (rc == ERROR_SUCCESS && type == REG_DWORD && val > 0) {
        interval = (val < MAX_INTERVAL_SEC) ? val : MAX_INTERVAL_SEC;
    } else {
        RegSetValueExW(hKey, REG_VALUE_INTERVAL, 0, REG_DWORD,
                       reinterpret_cast<const BYTE*>(&interval), sizeof(DWORD));
    }

    RegCloseKey(hKey);
    return interval;
}

// ============================================================================
// Paths & file helpers
// ============================================================================

static std::wstring GetBackgroundFolder() {
    wchar_t*     raw = nullptr;
    std::wstring result;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, NULL, &raw)) && raw) {
        result = raw;
        result += L"\\BACKGROUND";
        CoTaskMemFree(raw);
    }
    return result;
}

static bool FolderExists(const std::wstring& path) {
    DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

static bool FileExists(const std::wstring& path) {
    DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static bool IsSupportedImage(const std::wstring& name) {
    size_t dot = name.rfind(L'.');
    if (dot == std::wstring::npos) return false;

    std::wstring ext = name.substr(dot);
    for (auto& ch : ext) ch = static_cast<wchar_t>(towlower(ch));

    for (size_t i = 0; i < NUM_EXTENSIONS; ++i)
        if (ext == SUPPORTED_EXTENSIONS[i]) return true;
    return false;
}

static std::vector<std::wstring> ScanImages(const std::wstring& folder) {
    std::vector<std::wstring> out;
    if (folder.empty()) return out;

    WIN32_FIND_DATAW fd{};
    HANDLE hFind = FindFirstFileW((folder + L"\\*").c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return out;

    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (IsSupportedImage(fd.cFileName))
            out.push_back(folder + L"\\" + fd.cFileName);
    } while (FindNextFileW(hFind, &fd));

    FindClose(hFind);
    return out;
}

// ============================================================================
// Monitor count (IDesktopWallpaper is the single source of truth)
// ============================================================================

static UINT GetMonitorCount() {
    ScopedComPtr<IDesktopWallpaper> dw;
    HRESULT hr = CoCreateInstance(__uuidof(DesktopWallpaper), NULL, CLSCTX_ALL,
                    __uuidof(IDesktopWallpaper),
                    reinterpret_cast<void**>(dw.GetAddressOf()));
    if (SUCCEEDED(hr) && dw) {
        UINT n = 0;
        if (SUCCEEDED(dw->GetMonitorDevicePathCount(&n)) && n > 0)
            return n;
    }
    int n = GetSystemMetrics(SM_CMONITORS);
    return (n > 0) ? static_cast<UINT>(n) : 1;
}

// ============================================================================
// Image Queue Manager (shuffle-all, then consume; re-shuffle when empty)
// ============================================================================

class ImageQueueManager {
    std::deque<std::wstring>  m_queue;
    std::vector<std::wstring> m_cache;
    bool                      m_cacheValid = false;
    std::wstring              m_folder;
    std::mt19937              m_rng{ std::random_device{}() };

    void Refill() {
        if (!m_cacheValid) {
            m_cache      = ScanImages(m_folder);
            m_cacheValid = true;
        }
        m_queue.assign(m_cache.begin(), m_cache.end());
        std::shuffle(m_queue.begin(), m_queue.end(), m_rng);
    }

public:
    void SetFolder(const std::wstring& f) { m_folder = f; }

    void InvalidateCache() {
        m_cacheValid = false;
        m_queue.clear();
    }

    // Returns up to `need` valid image paths
    std::vector<std::wstring> Take(UINT need) {
        std::vector<std::wstring> out;
        const size_t maxAttempts = static_cast<size_t>(need) * 3 + 10;
        size_t       attempts    = 0;

        while (out.size() < need && attempts++ < maxAttempts) {
            if (m_queue.empty()) {
                Refill();
                if (m_queue.empty()) break;     // no images at all
            }
            std::wstring img = m_queue.front();
            m_queue.pop_front();
            if (FileExists(img))
                out.push_back(std::move(img));
        }
        return out;
    }
};

// ============================================================================
// Set wallpaper on every monitor (COM already initialised)
// ============================================================================

static bool ApplyWallpapers(const std::vector<std::wstring>& images) {
    if (images.empty()) return false;

    ScopedComPtr<IDesktopWallpaper> dw;
    HRESULT hr = CoCreateInstance(__uuidof(DesktopWallpaper), NULL, CLSCTX_ALL,
                    __uuidof(IDesktopWallpaper),
                    reinterpret_cast<void**>(dw.GetAddressOf()));
    if (FAILED(hr) || !dw) return false;

    UINT count = 0;
    hr = dw->GetMonitorDevicePathCount(&count);
    if (FAILED(hr) || count == 0) return false;

    bool ok = true;
    for (UINT i = 0; i < count; ++i) {
        LPWSTR monId = nullptr;
        hr = dw->GetMonitorDevicePathAt(i, &monId);
        if (SUCCEEDED(hr) && monId) {
            size_t idx = (i < images.size()) ? i : images.size() - 1;
            if (FAILED(dw->SetWallpaper(monId, images[idx].c_str())))
                ok = false;
            CoTaskMemFree(monId);
        }
    }
    return ok;
}

// ============================================================================
// Hidden message-only window  (receives WM_ENDSESSION, WM_CLOSE, …)
// ============================================================================

static LRESULT CALLBACK HiddenWndProc(HWND hw, UINT msg,
                                       WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_QUERYENDSESSION:
        return TRUE;                          // allow shutdown
    case WM_ENDSESSION:
        if (wp && g_hStopEvent) SetEvent(g_hStopEvent);
        return 0;
    case WM_CLOSE:
    case WM_DESTROY:
        if (g_hStopEvent) SetEvent(g_hStopEvent);
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hw, msg, wp, lp);
    }
}

static HWND CreateHiddenWindow(HINSTANCE hInst) {
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = HiddenWndProc;
    wc.hInstance      = hInst;
    wc.lpszClassName  = WND_CLASS_NAME;
    if (!RegisterClassExW(&wc)) return NULL;
    return CreateWindowExW(0, WND_CLASS_NAME, L"", 0,
                           0, 0, 0, 0, HWND_MESSAGE, NULL, hInst, NULL);
}

// ============================================================================
// File-system watcher helper
// ============================================================================

static HANDLE CreateFolderWatch(const std::wstring& folder) {
    if (!FolderExists(folder)) return NULL;
    HANDLE h = FindFirstChangeNotificationW(
        folder.c_str(), FALSE,
        FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE |
        FILE_NOTIFY_CHANGE_SIZE);
    return (h == INVALID_HANDLE_VALUE) ? NULL : h;
}

// ============================================================================
// Entry point
// ============================================================================

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE /*prev*/,
                    LPWSTR /*cmdLine*/, int /*show*/) {

    // ── single instance ─────────────────────────────────────────────────
    HANDLE hMutex = CreateMutexW(NULL, TRUE, MUTEX_NAME);
    if (!hMutex || GetLastError() == ERROR_ALREADY_EXISTS) {
        if (hMutex) CloseHandle(hMutex);
        return 1;
    }

    // ── COM once ────────────────────────────────────────────────────────
    HRESULT hrCom   = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    bool    comInit = SUCCEEDED(hrCom);
    if (FAILED(hrCom) && hrCom != RPC_E_CHANGED_MODE) {
        LogEventError(L"COM initialization failed");
        CloseHandle(hMutex);
        return 1;
    }

    // ── stop event ──────────────────────────────────────────────────────
    g_hStopEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!g_hStopEvent) {
        if (comInit) CoUninitialize();
        CloseHandle(hMutex);
        return 1;
    }

    // ── hidden window (system messages) ─────────────────────────────────
    HWND hWnd = CreateHiddenWindow(hInstance);

    // ── background folder ───────────────────────────────────────────────
    std::wstring bgFolder = GetBackgroundFolder();
    if (bgFolder.empty()) {
        LogEventError(L"Failed to determine LocalAppData path");
        if (hWnd) DestroyWindow(hWnd);
        CloseHandle(g_hStopEvent);
        if (comInit) CoUninitialize();
        CloseHandle(hMutex);
        return 1;
    }

    // ── image queue & folder watcher ────────────────────────────────────
    ImageQueueManager queue;
    queue.SetFolder(bgFolder);

    HANDLE hWatch = CreateFolderWatch(bgFolder);

    // ════════════════════════════════════════════════════════════════════
    // Main loop
    // ════════════════════════════════════════════════════════════════════
    bool running = true;

    while (running) {

        // ── read interval (may be changed at runtime via registry) ──────
        DWORD intervalSec = ReadOrCreateInterval();
        DWORD intervalMs  = intervalSec * 1000;   // safe: max 86 400 000

        // ── change wallpaper ────────────────────────────────────────────
        if (!FolderExists(bgFolder)) {
            TransitionError(ErrorState::FolderNotFound,
                L"Background folder not found: AppData\\Local\\BACKGROUND");
            if (hWatch) { FindCloseChangeNotification(hWatch); hWatch = NULL; }
        } else {
            // re-create watcher if it was lost
            if (!hWatch) {
                hWatch = CreateFolderWatch(bgFolder);
                queue.InvalidateCache();
            }

            UINT monCount = GetMonitorCount();
            auto images   = queue.Take(monCount);

            if (images.empty()) {
                TransitionError(ErrorState::NoImagesFound,
                    L"No supported images in AppData\\Local\\BACKGROUND");
            } else if (ApplyWallpapers(images)) {
                TransitionError(ErrorState::None, nullptr);
            } else {
                TransitionError(ErrorState::SetWallpaperFailed,
                    L"Failed to set wallpaper via IDesktopWallpaper");
            }
        }

        // ── wait: timeout / stop / folder change / messages ─────────────
        ULONGLONG startTick = GetTickCount64();
        DWORD     remaining = intervalMs;

        while (running && remaining > 0) {

            HANDLE  handles[2];
            DWORD   nHandles = 0;
            handles[nHandles++] = g_hStopEvent;
            if (hWatch) handles[nHandles++] = hWatch;

            DWORD wr = MsgWaitForMultipleObjects(
                           nHandles, handles, FALSE, remaining, QS_ALLINPUT);

            if (wr == WAIT_OBJECT_0) {
                // stop event
                running = false;
                break;
            }

            if (hWatch && wr == WAIT_OBJECT_0 + 1) {
                // folder contents changed → invalidate cache
                queue.InvalidateCache();
                if (!FindNextChangeNotification(hWatch)) {
                    // watcher broken — recreate next iteration
                    FindCloseChangeNotification(hWatch);
                    hWatch = NULL;
                }
                // don't break — finish waiting for the interval
            }

            if (wr == WAIT_OBJECT_0 + nHandles) {
                // window messages
                MSG msg;
                while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
                    if (msg.message == WM_QUIT) { running = false; break; }
                    TranslateMessage(&msg);
                    DispatchMessageW(&msg);
                }
                if (!running) break;
            }

            if (wr == WAIT_TIMEOUT) break;      // time to change wallpaper

            if (wr == WAIT_FAILED) break;        // unexpected

            // recalculate remaining time
            ULONGLONG elapsed = GetTickCount64() - startTick;
            remaining = (elapsed >= intervalMs)
                            ? 0
                            : static_cast<DWORD>(intervalMs - elapsed);
        }
    }

    // ── cleanup ─────────────────────────────────────────────────────────
    if (hWatch) FindCloseChangeNotification(hWatch);
    if (hWnd)   DestroyWindow(hWnd);
    UnregisterClassW(WND_CLASS_NAME, hInstance);
    CloseHandle(g_hStopEvent);
    if (comInit) CoUninitialize();
    CloseHandle(hMutex);

    return 0;
}
