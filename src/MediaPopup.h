#ifndef MEDIAPOPUP_H
#define MEDIAPOPUP_H

#include <wx/wx.h>
#include <wx/popupwin.h>
#include <wx/dcbuffer.h>

// Media types for popup display
enum class MediaType {
    Photo,
    Video,
    Sticker,
    GIF,
    Voice,
    VideoNote,
    File,
    Reaction
};

// Media info structure
struct MediaInfo {
    MediaType type;
    wxString id;              // TDLib file ID
    wxString localPath;       // Local cached path (if downloaded)
    wxString remoteUrl;       // Remote URL (if available)
    wxString fileName;        // For files
    wxString fileSize;        // Human readable size
    wxString caption;         // Media caption
    wxString emoji;           // For stickers/reactions
    wxString reactedBy;       // For reactions - who reacted
    int width;
    int height;
    
    MediaInfo() : type(MediaType::Photo), width(0), height(0) {}
};

// HexChat-style popup for media preview
// Simple hover preview - no pinning, click opens file
class MediaPopup : public wxPopupWindow
{
public:
    MediaPopup(wxWindow* parent);
    virtual ~MediaPopup();
    
    // Show popup with media info at position
    void ShowMedia(const MediaInfo& info, const wxPoint& pos);
    
    // Load and display image
    void SetImage(const wxImage& image);
    void SetImage(const wxString& path);
    
    // For loading state
    void ShowLoading();
    void ShowError(const wxString& message);
    
    // Get current media info (for opening on click)
    const MediaInfo& GetMediaInfo() const { return m_mediaInfo; }
    
protected:
    void OnPaint(wxPaintEvent& event);
    
private:
    void UpdateSize();
    void ApplyHexChatStyle();
    wxString GetMediaLabel() const;
    
    // HexChat theme colors
    wxColour m_bgColor;
    wxColour m_borderColor;
    wxColour m_textColor;
    wxColour m_labelColor;
    
    // Content
    MediaInfo m_mediaInfo;
    wxBitmap m_bitmap;
    bool m_hasImage;
    bool m_isLoading;
    bool m_hasError;
    wxString m_errorMessage;
    
    // Size limits
    static constexpr int MAX_WIDTH = 400;
    static constexpr int MAX_HEIGHT = 300;
    static constexpr int MIN_WIDTH = 150;
    static constexpr int MIN_HEIGHT = 80;
    static constexpr int PADDING = 8;
    static constexpr int BORDER_WIDTH = 1;
    
    wxDECLARE_EVENT_TABLE();
};

// Tracks media spans in the chat display
struct MediaSpan {
    long startPos;            // Start position in text
    long endPos;              // End position in text
    MediaInfo info;           // Media information
    
    bool Contains(long pos) const {
        return pos >= startPos && pos <= endPos;
    }
};

#endif // MEDIAPOPUP_H