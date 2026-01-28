#include "App.h"
#include "MainFrame.h"
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

// Apply dark mode to window title bar (Windows 10 1809+ / Windows 11)
static void ApplyWindowsDarkMode(wxWindow* window)
{
    if (!IsWindowsDarkMode())
        return;
        
    HWND hwnd = window->GetHWND();
    if (!hwnd)
        return;
    
    // DWMWA_USE_IMMERSIVE_DARK_MODE = 20 (Windows 10 20H1+)
    // For older Windows 10, use undocumented value 19
    BOOL darkMode = TRUE;
    
    // Try the official attribute first (Windows 10 20H1+)
    if (FAILED(DwmSetWindowAttribute(hwnd, 20, &darkMode, sizeof(darkMode)))) {
        // Fall back to undocumented attribute for older Windows 10
        DwmSetWindowAttribute(hwnd, 19, &darkMode, sizeof(darkMode));
    }
}
#endif

bool App::OnInit()
{
    // Set UTF-8 as the default encoding for proper Unicode support
    // This is critical for displaying emoji and international characters
#ifdef __WXMSW__
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
    // Apply Windows dark mode to the title bar if system is in dark mode
    ApplyWindowsDarkMode(frame);
    
    // If dark mode is enabled, we need to also set appropriate colors
    if (IsWindowsDarkMode()) {
        frame->SetBackgroundColour(wxColour(32, 32, 32));
    }
#endif
    
    frame->Show(true);
    return true;
}
