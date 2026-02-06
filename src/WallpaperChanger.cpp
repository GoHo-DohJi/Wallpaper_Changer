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

#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <string>
#include <vector>
#include <deque>
#include <set>
#include <algorithm>
#include <random>

// ============================================================================
// Constants
// ============================================================================

static const wchar_t* MUTEX_NAME = L"Global\\WallpaperChangerSingleInstanceMutex_v1";
static const wchar_t* REG_KEY_PATH = L"Software\\WallpaperChanger";
static const wchar_t* REG_VALUE_INTERVAL = L"IntervalSeconds";
static const DWORD DEFAULT_INTERVAL_SECONDS = 300; // 5 minutes
static const wchar_t* EVENT_LOG_SOURCE = L"WallpaperChanger";

// Supported wallpaper file extensions (Windows supported formats)
static const wchar_t* SUPPORTED_EXTENSIONS[] = {
    L".jpg", L".jpeg", L".jpe", L".jfif",
    L".png", L".bmp", L".dib",
    L".gif", L".tif", L".tiff", L".wdp"
};
static const size_t NUM_SUPPORTED_EXTENSIONS = sizeof(SUPPORTED_EXTENSIONS) / sizeof(SUPPORTED_EXTENSIONS[0]);

// ============================================================================
// Error State Management (State-Based Logging)
// ============================================================================

enum class ErrorState {
    None,
    FolderNotFound,
    NoImagesFound,
    SetWallpaperFailed,
    ComInitFailed
};

static ErrorState g_currentErrorState = ErrorState::None;

// Log error message to Windows Event Log
void LogEventError(const wchar_t* message) {
    HANDLE hEventLog = RegisterEventSourceW(NULL, EVENT_LOG_SOURCE);
    if (hEventLog != NULL) {
        const wchar_t* messages[] = { message };
        ReportEventW(hEventLog, EVENTLOG_ERROR_TYPE, 0, 1000, NULL, 1, 0, messages, NULL);
        DeregisterEventSource(hEventLog);
    }
}

// Transition error state - logs only on state change (Normal -> Error)
void TransitionErrorState(ErrorState newState, const wchar_t* message) {
    if (newState != g_currentErrorState) {
        if (newState != ErrorState::None && message != nullptr) {
            LogEventError(message);
        }
        g_currentErrorState = newState;
    }
}

// ============================================================================
// Registry Operations
// ============================================================================

DWORD ReadOrCreateRegistryInterval() {
    HKEY hKey = NULL;
    DWORD interval = DEFAULT_INTERVAL_SECONDS;

    LONG result = RegCreateKeyExW(
        HKEY_CURRENT_USER,
        REG_KEY_PATH,
        0,
        NULL,
        REG_OPTION_NON_VOLATILE,
        KEY_READ | KEY_WRITE,
        NULL,
        &hKey,
        NULL
    );

    if (result != ERROR_SUCCESS) {
        return DEFAULT_INTERVAL_SECONDS;
    }

    DWORD valueSize = sizeof(DWORD);
    DWORD valueType = 0;
    DWORD readValue = 0;

    result = RegQueryValueExW(hKey, REG_VALUE_INTERVAL, NULL, &valueType,
                              reinterpret_cast<LPBYTE>(&readValue), &valueSize);

    if (result == ERROR_SUCCESS && valueType == REG_DWORD && readValue > 0) {
        interval = readValue;
    } else {
        // Value doesn't exist, wrong type, or zero - create/set default
        interval = DEFAULT_INTERVAL_SECONDS;
        RegSetValueExW(hKey, REG_VALUE_INTERVAL, 0, REG_DWORD,
                       reinterpret_cast<const BYTE*>(&interval), sizeof(DWORD));
    }

    RegCloseKey(hKey);
    return interval;
}

// ============================================================================
// Path and File Operations
// ============================================================================

std::wstring GetBackgroundFolderPath() {
    wchar_t* localAppDataPath = nullptr;
    std::wstring resultPath;

    HRESULT hr = SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, NULL, &localAppDataPath);
    if (SUCCEEDED(hr) && localAppDataPath != nullptr) {
        resultPath = localAppDataPath;
        resultPath += L"\\BACKGROUND";
        CoTaskMemFree(localAppDataPath);
    }

    return resultPath;
}

bool IsSupportedImageFile(const std::wstring& filename) {
    size_t dotPos = filename.rfind(L'.');
    if (dotPos == std::wstring::npos) {
        return false;
    }

    std::wstring extension = filename.substr(dotPos);

    // Convert to lowercase for case-insensitive comparison
    for (wchar_t& ch : extension) {
        ch = static_cast<wchar_t>(towlower(ch));
    }

    for (size_t i = 0; i < NUM_SUPPORTED_EXTENSIONS; i++) {
        if (extension == SUPPORTED_EXTENSIONS[i]) {
            return true;
        }
    }

    return false;
}

std::vector<std::wstring> ScanImagesInFolder(const std::wstring& folderPath) {
    std::vector<std::wstring> images;

    if (folderPath.empty()) {
        return images;
    }

    std::wstring searchPattern = folderPath + L"\\*";
    WIN32_FIND_DATAW findData = {};
    HANDLE hFind = FindFirstFileW(searchPattern.c_str(), &findData);

    if (hFind == INVALID_HANDLE_VALUE) {
        return images;
    }

    do {
        // Skip directories
        if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            continue;
        }

        std::wstring filename = findData.cFileName;
        if (IsSupportedImageFile(filename)) {
            std::wstring fullPath = folderPath + L"\\" + filename;
            images.push_back(fullPath);
        }
    } while (FindNextFileW(hFind, &findData));

    FindClose(hFind);
    return images;
}

// ============================================================================
// Monitor Operations
// ============================================================================

UINT GetCurrentMonitorCount() {
    int count = GetSystemMetrics(SM_CMONITORS);
    return (count > 0) ? static_cast<UINT>(count) : 1;
}

// ============================================================================
// Image Queue Manager (Simplified)
// ============================================================================
// Simple sequential consumption:
// - Take from queue until empty
// - When empty, create new shuffled queue
// - Continue taking
// ============================================================================

class ImageQueueManager {
private:
    std::deque<std::wstring> m_queue;           // Current shuffled queue
    std::wstring m_folderPath;
    std::mt19937 m_rng;

    bool FileExists(const std::wstring& path) const {
        DWORD attribs = GetFileAttributesW(path.c_str());
        return (attribs != INVALID_FILE_ATTRIBUTES) && 
               !(attribs & FILE_ATTRIBUTE_DIRECTORY);
    }

    void RefillQueue() {
        std::vector<std::wstring> images = ScanImagesInFolder(m_folderPath);
        m_queue.assign(images.begin(), images.end());
        std::shuffle(m_queue.begin(), m_queue.end(), m_rng);
    }

public:
    ImageQueueManager() : m_rng(std::random_device{}()) {}

    void SetFolderPath(const std::wstring& path) {
        m_folderPath = path;
    }

    std::vector<std::wstring> GetImagesForUpdate(UINT monitorCount) {
        std::vector<std::wstring> result;

        while (result.size() < monitorCount) {
            // Queue empty? Refill it
            if (m_queue.empty()) {
                RefillQueue();
                
                // No images at all - exit
                if (m_queue.empty()) {
                    break;
                }
            }
            
            // Take next from queue
            std::wstring img = m_queue.front();
            m_queue.pop_front();
            
            // Use only if file still exists
            if (FileExists(img)) {
                result.push_back(img);
            }
        }

        return result;
    }

    size_t GetQueueRemainingCount() const { return m_queue.size(); }
};

// ============================================================================
// Wallpaper Setting via IDesktopWallpaper
// ============================================================================

bool SetWallpaperForMonitors(const std::vector<std::wstring>& imagePaths) {
    if (imagePaths.empty()) {
        return false;
    }

    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    bool needUninit = SUCCEEDED(hr);
    
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        TransitionErrorState(ErrorState::ComInitFailed, L"COM initialization failed");
        return false;
    }

    bool success = false;
    IDesktopWallpaper* pDesktopWallpaper = nullptr;

    // CLSID for DesktopWallpaper: {C2CF3110-460E-4fc1-B9D0-8A1C0C9CC4BD}
    hr = CoCreateInstance(__uuidof(DesktopWallpaper), NULL, CLSCTX_ALL,
                          IID_PPV_ARGS(&pDesktopWallpaper));

    if (SUCCEEDED(hr) && pDesktopWallpaper != nullptr) {
        UINT monitorCount = 0;
        hr = pDesktopWallpaper->GetMonitorDevicePathCount(&monitorCount);

        if (SUCCEEDED(hr) && monitorCount > 0) {
            success = true;

            for (UINT i = 0; i < monitorCount; i++) {
                LPWSTR monitorId = nullptr;
                hr = pDesktopWallpaper->GetMonitorDevicePathAt(i, &monitorId);

                if (SUCCEEDED(hr) && monitorId != nullptr) {
                    // Select image for this monitor
                    size_t imageIndex = (i < imagePaths.size()) ? i : (imagePaths.size() - 1);

                    hr = pDesktopWallpaper->SetWallpaper(monitorId, imagePaths[imageIndex].c_str());
                    if (FAILED(hr)) {
                        success = false;
                    }

                    CoTaskMemFree(monitorId);
                }
            }
        }

        pDesktopWallpaper->Release();
    }

    if (needUninit) {
        CoUninitialize();
    }

    return success;
}

// ============================================================================
// Application Entry Point
// ============================================================================

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, 
                    LPWSTR lpCmdLine, int nCmdShow) {
    // Suppress unused parameter warnings
    (void)hInstance;
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;

    // ========================================================================
    // Single Instance Protection
    // ========================================================================
    HANDLE hMutex = CreateMutexW(NULL, TRUE, MUTEX_NAME);
    if (hMutex == NULL) {
        return 1;
    }

    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(hMutex);
        return 1; // Another instance is already running
    }

    // ========================================================================
    // Initialize
    // ========================================================================
    std::wstring backgroundFolder = GetBackgroundFolderPath();
    if (backgroundFolder.empty()) {
        LogEventError(L"Failed to determine LocalAppData path");
        CloseHandle(hMutex);
        return 1;
    }

    ImageQueueManager imageQueue;
    imageQueue.SetFolderPath(backgroundFolder);

    // ========================================================================
    // Main Loop
    // ========================================================================
    while (true) {
        // Read interval from registry (allows runtime changes)
        DWORD intervalSeconds = ReadOrCreateRegistryInterval();

        // Get current monitor count (handles monitor connect/disconnect)
        UINT monitorCount = GetCurrentMonitorCount();

        // Check if background folder exists
        DWORD folderAttribs = GetFileAttributesW(backgroundFolder.c_str());
        
        if (folderAttribs == INVALID_FILE_ATTRIBUTES ||
            !(folderAttribs & FILE_ATTRIBUTE_DIRECTORY)) {
            TransitionErrorState(ErrorState::FolderNotFound,
                L"Background folder not found: AppData\\Local\\BACKGROUND");
        } else {
            // Get images for this update
            std::vector<std::wstring> selectedImages = imageQueue.GetImagesForUpdate(monitorCount);

            if (selectedImages.empty()) {
                TransitionErrorState(ErrorState::NoImagesFound,
                    L"No supported images found in AppData\\Local\\BACKGROUND");
            } else {
                // Set wallpapers for all monitors
                if (SetWallpaperForMonitors(selectedImages)) {
                    TransitionErrorState(ErrorState::None, nullptr);
                } else {
                    TransitionErrorState(ErrorState::SetWallpaperFailed,
                        L"Failed to set wallpaper using IDesktopWallpaper interface");
                }
            }
        }

        // Sleep for configured interval
        Sleep(intervalSeconds * 1000);
    }

    // Cleanup (never reached in normal operation)
    CloseHandle(hMutex);
    return 0;

}
