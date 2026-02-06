#include "App.h"
#include "MainFrame.h"
#include "Theme.h"
#include <wx/image.h>
#include <wx/config.h>
#include <wx/fileconf.h>
#include <wx/stdpaths.h>
#include <wx/tooltip.h>
#include <wx/fontmap.h>
#include <locale>

#ifdef __WXMSW__
#include <wx/msw/private.h>
#include <windows.h>
#include <dwmapi.h>
// dwmapi.lib is linked via CMakeLists.txt

// Undocumented Windows API for dark mode menus (Windows 10 1903+)
// This is required for Win32 apps to get dark context menus and menu bars
enum PreferredAppMode {
    Default = 0,
    AllowDark = 1,
    ForceDark = 2,
    ForceLight = 3,
    Max = 4
};

typedef PreferredAppMode (WINAPI *SetPreferredAppModeFunc)(PreferredAppMode);
typedef void (WINAPI *FlushMenuThemesFunc)();
typedef BOOL (WINAPI *AllowDarkModeForWindowFunc)(HWND, BOOL);

// Global function pointers - kept alive for the app lifetime
static AllowDarkModeForWindowFunc g_AllowDarkModeForWindow = nullptr;
static FlushMenuThemesFunc g_FlushMenuThemes = nullptr;

// Initialize dark mode for the entire application (call once at startup)
static void InitWindowsDarkMode()
{
    // Load uxtheme.dll and get the undocumented functions
    // Don't free the library - we need these functions for the app lifetime
    HMODULE hUxTheme = LoadLibraryExW(L"uxtheme.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (hUxTheme) {
        // SetPreferredAppMode is ordinal 135 in Windows 10 1903+
        auto SetPreferredAppMode = reinterpret_cast<SetPreferredAppModeFunc>(
            GetProcAddress(hUxTheme, MAKEINTRESOURCEA(135)));
        
        // FlushMenuThemes is ordinal 136
        g_FlushMenuThemes = reinterpret_cast<FlushMenuThemesFunc>(
            GetProcAddress(hUxTheme, MAKEINTRESOURCEA(136)));
        
        // AllowDarkModeForWindow is ordinal 133
        g_AllowDarkModeForWindow = reinterpret_cast<AllowDarkModeForWindowFunc>(
            GetProcAddress(hUxTheme, MAKEINTRESOURCEA(133)));
        
        if (SetPreferredAppMode) {
            // Force dark mode for this application regardless of some system heuristics
            SetPreferredAppMode(ForceDark);
        }
        
        if (g_FlushMenuThemes) {
            // Force menus to update their theme
            g_FlushMenuThemes();
        }
        
        // Note: Don't FreeLibrary - we need these function pointers to remain valid
    }
}

// Check if Windows is using dark mode
static bool IsWindowsDarkMode()
{
    // Check the AppsUseLightTheme registry key (0 = dark mode, 1 = light mode)
    HKEY hKey;
    DWORD value = 1; // Default to light mode
    DWORD size = sizeof(value);
    
    if (RegOpenKeyExW(HKEY_CURRENT_USER, 
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        RegQueryValueExW(hKey, L"AppsUseLightTheme", nullptr, nullptr, 
            reinterpret_cast<LPBYTE>(&value), &size);
        RegCloseKey(hKey);
    }
    
    return value == 0;
}

// Apply dark mode to window title bar and menu bar (Windows 10 1809+ / Windows 11)
static void ApplyWindowsDarkMode(wxWindow* window, bool darkMode)
{
    if (!darkMode)
        return;
        
    HWND hwnd = window->GetHWND();
    if (!hwnd)
        return;
    
    // Allow dark mode for this specific window (affects menu bar)
    if (g_AllowDarkModeForWindow) {
        g_AllowDarkModeForWindow(hwnd, TRUE);
    }
    
    // DWMWA_USE_IMMERSIVE_DARK_MODE = 20 (Windows 10 20H1+)
    // For older Windows 10, use undocumented value 19
    BOOL useDarkMode = TRUE;
    
    // Try the official attribute first (Windows 10 20H1+)
    if (FAILED(DwmSetWindowAttribute(hwnd, 20, &useDarkMode, sizeof(useDarkMode)))) {
        // Fall back to undocumented attribute for older Windows 10
        DwmSetWindowAttribute(hwnd, 19, &useDarkMode, sizeof(useDarkMode));
    }
    
    // On Windows 11, also try setting caption color for better dark mode integration
    // DWMWA_CAPTION_COLOR = 35
    COLORREF captionColor = RGB(18, 18, 24);  // Match our dark theme background
    DwmSetWindowAttribute(hwnd, 35, &captionColor, sizeof(captionColor));
    
    // Flush menu themes after setting window dark mode
    if (g_FlushMenuThemes) {
        g_FlushMenuThemes();
    }
    
    // Force a redraw of the non-client area to apply changes
    SetWindowPos(hwnd, NULL, 0, 0, 0, 0, 
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    
    // Also redraw the menu bar explicitly
    DrawMenuBar(hwnd);
}
#endif

bool App::OnInit()
{
    // Set UTF-8 as the default encoding for proper Unicode support
    // This is critical for displaying emoji and international characters
#ifdef __WXMSW__
    // Initialize Windows dark mode support FIRST (before creating any windows)
    // This enables dark mode for menus and context menus
    if (ThemeManager::Get().IsDarkTheme()) {
        InitWindowsDarkMode();
    }
    
    // On Windows, set console and internal encoding to UTF-8
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    
    // Enable UTF-8 for the C++ standard library
    std::locale::global(std::locale(""));
#else
    // On Linux/macOS, use the system locale (usually UTF-8)
    std::locale::global(std::locale(""));
#endif
    
    // Ensure wxWidgets uses UTF-8 internally
    wxFontMapper::Get()->SetConfigPath(wxT("/wxWindows/FontMapper"));
    
    // Initialize all image handlers (JPEG, PNG, GIF, BMP, etc.)
    // This is required before wxImage can load these formats
    wxInitAllImageHandlers();
    
    // Make tooltips appear instantly
    wxToolTip::SetDelay(0);
    
    // Set up persistent config file
    SetAppName("teleliter");
    SetVendorName("teleliter");
    wxConfig::Set(new wxFileConfig("teleliter", "", 
        wxStandardPaths::Get().GetUserConfigDir() + "/teleliter.conf",
        "", wxCONFIG_USE_LOCAL_FILE));

    MainFrame *frame = new MainFrame("Teleliter", wxPoint(50, 50), wxSize(1024, 600));
    
#ifdef __WXMSW__
    // Apply Windows dark mode to the title bar based on theme setting
    ApplyWindowsDarkMode(frame, ThemeManager::Get().IsDarkTheme());
#endif
    
    frame->Show(true);
    return true;
}
