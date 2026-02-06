#ifndef THEME_H
#define THEME_H

#include <wx/wx.h>
#include <wx/colour.h>
#include <wx/config.h>
#include <functional>

#ifdef __WXMSW__
#include <windows.h>
#endif

// Theme types
enum class ThemeType {
    Light,
    Dark,
    System  // Follow system preference
};

// Theme color structure - all colors needed for the UI
struct ThemeColors {
    // Main window colors
    wxColour windowBg;          // Main window background
    wxColour windowFg;          // Main window text
    
    // Panel/control colors
    wxColour panelBg;           // Panel background
    wxColour controlBg;         // Input controls background
    wxColour controlFg;         // Input controls text
    wxColour controlBorder;     // Control borders
    
    // Chat area colors
    wxColour chatBg;            // Chat display background
    wxColour chatFg;            // Chat display default text
    
    // List/tree colors
    wxColour listBg;            // List/tree background
    wxColour listFg;            // List/tree text
    wxColour listSelectionBg;   // Selected item background
    wxColour listSelectionFg;   // Selected item text
    wxColour listHoverBg;       // Hovered item background
    
    // Status bar colors
    wxColour statusBarBg;       // Status bar background
    wxColour statusBarFg;       // Status bar text
    
    // Accent colors (semantic)
    wxColour accentPrimary;     // Primary accent (links, highlights)
    wxColour accentSuccess;     // Success/online (green)
    wxColour accentWarning;     // Warning (orange/yellow)
    wxColour accentError;       // Error (red)
    wxColour accentInfo;        // Info messages (blue/cyan)
    
    // Message colors
    wxColour timestampColor;    // Timestamp text
    wxColour senderColor;       // Default sender name
    wxColour linkColor;         // Hyperlinks
    wxColour mentionColor;      // @mentions
    wxColour readReceiptColor;  // Read receipts (✓✓)
    
    // User colors for sender names (16 distinct colors)
    wxColour userColors[16];
    
    // Dividers and borders
    wxColour dividerColor;      // Separator lines
    wxColour borderColor;       // General borders
    
    // Muted/secondary text
    wxColour mutedText;         // Secondary/gray text
    wxColour placeholderText;   // Placeholder text in inputs
};

// Theme manager singleton
class ThemeManager {
public:
    static ThemeManager& Get() {
        static ThemeManager instance;
        return instance;
    }
    
    // Get current theme colors
    const ThemeColors& GetColors() const { return m_colors; }
    
    // Get current theme type
    ThemeType GetThemeType() const { return m_themeType; }
    
    // Set theme
    void SetTheme(ThemeType type) {
        m_themeType = type;
        ApplyTheme();
        SaveThemePreference();
    }
    
    // Check if current theme is dark
    bool IsDarkTheme() const {
        if (m_themeType == ThemeType::System) {
            return IsSystemDarkMode();
        }
        return m_themeType == ThemeType::Dark;
    }
    
    // Load theme from config
    void LoadThemePreference() {
        wxConfig* config = dynamic_cast<wxConfig*>(wxConfig::Get());
        if (config) {
            int themeInt = config->ReadLong("/Theme/Type", static_cast<int>(ThemeType::System));
            m_themeType = static_cast<ThemeType>(themeInt);
        }
        ApplyTheme();
    }
    
    // Save theme to config
    void SaveThemePreference() {
        wxConfig* config = dynamic_cast<wxConfig*>(wxConfig::Get());
        if (config) {
            config->Write("/Theme/Type", static_cast<int>(m_themeType));
            config->Flush();
        }
    }
    
    // Callback for theme changes
    using ThemeChangedCallback = std::function<void()>;
    void SetThemeChangedCallback(ThemeChangedCallback callback) {
        m_themeChangedCallback = callback;
    }
    
private:
    ThemeManager() : m_themeType(ThemeType::System) {
        ApplyTheme();
    }
    
    // Check if system is in dark mode
    static bool IsSystemDarkMode() {
#ifdef __WXMSW__
        // Check Windows registry
        HKEY hKey;
        DWORD value = 1;
        DWORD size = sizeof(value);
        
        if (RegOpenKeyExW(HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
            0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            RegQueryValueExW(hKey, L"AppsUseLightTheme", nullptr, nullptr,
                reinterpret_cast<LPBYTE>(&value), &size);
            RegCloseKey(hKey);
        }
        return value == 0;
#else
        // On other platforms, check system color brightness
        wxColour bg = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW);
        int brightness = (bg.Red() + bg.Green() + bg.Blue()) / 3;
        return brightness < 128;
#endif
    }
    
    void ApplyTheme() {
        bool dark = IsDarkTheme();
        
        if (dark) {
            ApplyDarkTheme();
        } else {
            ApplyLightTheme();
        }
        
        if (m_themeChangedCallback) {
            m_themeChangedCallback();
        }
    }
    
    void ApplyLightTheme() {
        // Light theme - clean, readable colors
        m_colors.windowBg = wxColour(255, 255, 255);
        m_colors.windowFg = wxColour(0, 0, 0);
        
        m_colors.panelBg = wxColour(248, 248, 248);
        m_colors.controlBg = wxColour(255, 255, 255);
        m_colors.controlFg = wxColour(0, 0, 0);
        m_colors.controlBorder = wxColour(200, 200, 200);
        
        m_colors.chatBg = wxColour(255, 255, 255);
        m_colors.chatFg = wxColour(0, 0, 0);
        
        m_colors.listBg = wxColour(255, 255, 255);
        m_colors.listFg = wxColour(0, 0, 0);
        m_colors.listSelectionBg = wxColour(0, 120, 215);
        m_colors.listSelectionFg = wxColour(255, 255, 255);
        m_colors.listHoverBg = wxColour(229, 243, 255);
        
        m_colors.statusBarBg = wxColour(240, 240, 240);
        m_colors.statusBarFg = wxColour(0, 0, 0);
        
        // Accent colors - vibrant for light background
        m_colors.accentPrimary = wxColour(0, 102, 204);
        m_colors.accentSuccess = wxColour(0, 128, 0);
        m_colors.accentWarning = wxColour(200, 130, 0);
        m_colors.accentError = wxColour(200, 0, 0);
        m_colors.accentInfo = wxColour(0, 150, 180);
        
        // Message colors
        m_colors.timestampColor = wxColour(128, 128, 128);
        m_colors.senderColor = wxColour(0, 0, 170);
        m_colors.linkColor = wxColour(0, 102, 204);
        m_colors.mentionColor = wxColour(180, 0, 180);
        m_colors.readReceiptColor = wxColour(0, 150, 0);
        
        // User colors - distinct, readable on light background
        m_colors.userColors[0] = wxColour(0, 0, 170);       // Blue
        m_colors.userColors[1] = wxColour(0, 115, 0);       // Green
        m_colors.userColors[2] = wxColour(170, 0, 0);       // Red
        m_colors.userColors[3] = wxColour(170, 85, 0);      // Orange
        m_colors.userColors[4] = wxColour(85, 0, 85);       // Purple
        m_colors.userColors[5] = wxColour(0, 115, 115);     // Teal
        m_colors.userColors[6] = wxColour(170, 0, 85);      // Magenta
        m_colors.userColors[7] = wxColour(0, 85, 170);      // Steel blue
        m_colors.userColors[8] = wxColour(85, 85, 0);       // Olive
        m_colors.userColors[9] = wxColour(115, 60, 0);      // Brown
        m_colors.userColors[10] = wxColour(0, 85, 85);      // Dark cyan
        m_colors.userColors[11] = wxColour(85, 0, 170);     // Indigo
        m_colors.userColors[12] = wxColour(170, 0, 85);     // Deep pink
        m_colors.userColors[13] = wxColour(60, 115, 0);     // Dark lime
        m_colors.userColors[14] = wxColour(0, 60, 115);     // Navy
        m_colors.userColors[15] = wxColour(115, 0, 60);     // Maroon
        
        m_colors.dividerColor = wxColour(220, 220, 220);
        m_colors.borderColor = wxColour(200, 200, 200);
        m_colors.mutedText = wxColour(128, 128, 128);
        m_colors.placeholderText = wxColour(160, 160, 160);
    }
    
    void ApplyDarkTheme() {
        // Premium Dark Theme - rich, modern, easy on the eyes
        // Using a sophisticated dark palette with subtle blue undertones
        
        // Main backgrounds - deep charcoal with slight blue tint
        m_colors.windowBg = wxColour(18, 18, 24);          // Deep space blue-black
        m_colors.windowFg = wxColour(235, 235, 245);        // Soft white
        
        // Panel/control colors - layered depth
        m_colors.panelBg = wxColour(24, 24, 32);            // Slightly lighter layer
        m_colors.controlBg = wxColour(32, 32, 42);          // Input field background
        m_colors.controlFg = wxColour(235, 235, 245);       // Bright text
        m_colors.controlBorder = wxColour(55, 55, 75);      // Subtle border
        
        // Chat area - the main focus
        m_colors.chatBg = wxColour(18, 18, 24);             // Match window bg
        m_colors.chatFg = wxColour(230, 230, 240);          // Crisp white text
        
        // List/tree - sidebar styling
        m_colors.listBg = wxColour(22, 22, 30);             // Sidebar background
        m_colors.listFg = wxColour(200, 200, 215);          // Slightly muted text
        m_colors.listSelectionBg = wxColour(88, 101, 242);  // Discord-style purple-blue
        m_colors.listSelectionFg = wxColour(255, 255, 255); // Pure white on selection
        m_colors.listHoverBg = wxColour(40, 40, 55);        // Subtle hover highlight
        
        // Status bar - grounded at bottom
        m_colors.statusBarBg = wxColour(22, 22, 30);
        m_colors.statusBarFg = wxColour(180, 180, 195);
        
        // Accent colors - vibrant, modern palette
        m_colors.accentPrimary = wxColour(88, 166, 255);    // Bright sky blue
        m_colors.accentSuccess = wxColour(87, 242, 135);    // Mint green
        m_colors.accentWarning = wxColour(255, 184, 77);    // Warm amber
        m_colors.accentError = wxColour(255, 99, 99);       // Soft coral red
        m_colors.accentInfo = wxColour(99, 230, 255);       // Electric cyan
        
        // Message colors - readable and stylish
        m_colors.timestampColor = wxColour(115, 115, 140);  // Muted purple-gray
        m_colors.senderColor = wxColour(88, 166, 255);      // Match primary accent
        m_colors.linkColor = wxColour(99, 177, 255);        // Slightly lighter blue
        m_colors.mentionColor = wxColour(235, 130, 255);    // Vibrant magenta
        m_colors.readReceiptColor = wxColour(87, 242, 135); // Match success green
        
        // User colors - vibrant neon palette for sender names
        m_colors.userColors[0] = wxColour(99, 177, 255);    // Electric blue
        m_colors.userColors[1] = wxColour(87, 242, 135);    // Neon green
        m_colors.userColors[2] = wxColour(255, 121, 121);   // Coral red
        m_colors.userColors[3] = wxColour(255, 177, 66);    // Golden orange
        m_colors.userColors[4] = wxColour(199, 125, 255);   // Lavender purple
        m_colors.userColors[5] = wxColour(77, 238, 234);    // Turquoise
        m_colors.userColors[6] = wxColour(255, 121, 198);   // Hot pink
        m_colors.userColors[7] = wxColour(125, 177, 255);   // Soft blue
        m_colors.userColors[8] = wxColour(241, 250, 140);   // Pale yellow
        m_colors.userColors[9] = wxColour(255, 166, 121);   // Peach
        m_colors.userColors[10] = wxColour(121, 255, 209);  // Seafoam
        m_colors.userColors[11] = wxColour(166, 140, 255);  // Periwinkle
        m_colors.userColors[12] = wxColour(255, 140, 166);  // Rose
        m_colors.userColors[13] = wxColour(177, 255, 99);   // Lime
        m_colors.userColors[14] = wxColour(140, 200, 255);  // Sky
        m_colors.userColors[15] = wxColour(255, 166, 209);  // Blush pink
        
        // Dividers and borders - subtle but visible
        m_colors.dividerColor = wxColour(45, 45, 60);
        m_colors.borderColor = wxColour(55, 55, 75);
        m_colors.mutedText = wxColour(115, 115, 140);
        m_colors.placeholderText = wxColour(90, 90, 110);
    }
    
    ThemeType m_themeType;
    ThemeColors m_colors;
    ThemeChangedCallback m_themeChangedCallback;
};

#endif // THEME_H