#ifndef CHATVIEWWIDGET_H
#define CHATVIEWWIDGET_H

#include <wx/wx.h>
#include <wx/richtext/richtextctrl.h>
#include <wx/popupwin.h>
#include <vector>
#include <map>

#include "MediaTypes.h"

// Forward declarations
class MainFrame;
class MessageFormatter;
class MediaPopup;
struct MessageInfo;

class ChatViewWidget : public wxPanel
{
public:
    ChatViewWidget(wxWindow* parent, MainFrame* mainFrame);
    virtual ~ChatViewWidget();
    
    // Message display
    void DisplayMessage(const MessageInfo& msg);
    void DisplayMessages(const std::vector<MessageInfo>& messages);
    void ClearMessages();
    void ScrollToBottom();
    
    // Media span tracking
    void AddMediaSpan(long startPos, long endPos, const MediaInfo& info);
    MediaSpan* GetMediaSpanAtPosition(long pos);
    void ClearMediaSpans();
    
    // Edit span tracking (for showing original text on hover)
    void AddEditSpan(long startPos, long endPos, int64_t messageId, 
                     const wxString& originalText, int64_t editDate);
    EditSpan* GetEditSpanAtPosition(long pos);
    void ClearEditSpans();
    
    // Link span tracking (for clickable URLs)
    void AddLinkSpan(long startPos, long endPos, const wxString& url);
    LinkSpan* GetLinkSpanAtPosition(long pos);
    void ClearLinkSpans();
    
    // Access to display control
    wxRichTextCtrl* GetDisplayCtrl() { return m_chatDisplay; }
    MessageFormatter* GetMessageFormatter() { return m_messageFormatter; }
    
    // Styling
    void SetColors(const wxColour& bg, const wxColour& fg,
                   const wxColour& timestamp, const wxColour& text,
                   const wxColour& service, const wxColour& action,
                   const wxColour& media, const wxColour& edited,
                   const wxColour& forward, const wxColour& reply,
                   const wxColour& highlight, const wxColour& notice);
    void SetUserColors(const wxColour* colors); // Array of 16 colors
    void SetChatFont(const wxFont& font);
    
    // Pending downloads
    void AddPendingDownload(int32_t fileId, const MediaInfo& info);
    bool HasPendingDownload(int32_t fileId) const;
    MediaInfo GetPendingDownload(int32_t fileId) const;
    void RemovePendingDownload(int32_t fileId);
    
    // Media popup
    void ShowMediaPopup(const MediaInfo& info, const wxPoint& position);
    void HideMediaPopup();
    void UpdateMediaPopup(int32_t fileId, const wxString& localPath);
    
    // Edit history popup
    void ShowEditHistoryPopup(const EditSpan& span, const wxPoint& position);
    void HideEditHistoryPopup();
    
    // Open media (external viewer or download)
    void OpenMedia(const MediaInfo& info);
    
private:
    void CreateLayout();
    void SetupDisplayControl();
    wxString FormatTimestamp(int64_t unixTime);
    
    // Event handlers
    void OnMouseMove(wxMouseEvent& event);
    void OnMouseLeave(wxMouseEvent& event);
    void OnLeftDown(wxMouseEvent& event);
    
    MainFrame* m_mainFrame;
    wxRichTextCtrl* m_chatDisplay;
    MessageFormatter* m_messageFormatter;
    MediaPopup* m_mediaPopup;
    wxPopupWindow* m_editHistoryPopup;
    
    // Media spans for clickable media
    std::vector<MediaSpan> m_mediaSpans;
    
    // Edit spans for showing original text
    std::vector<EditSpan> m_editSpans;
    
    // Link spans for clickable URLs
    std::vector<LinkSpan> m_linkSpans;
    
    // Pending downloads (file ID -> media info)
    std::map<int32_t, MediaInfo> m_pendingDownloads;
    
    // Colors
    wxColour m_bgColor;
    wxColour m_fgColor;
    wxColour m_timestampColor;
    wxColour m_textColor;
    wxColour m_serviceColor;
    wxColour m_actionColor;
    wxColour m_mediaColor;
    wxColour m_editedColor;
    wxColour m_forwardColor;
    wxColour m_replyColor;
    wxColour m_highlightColor;
    wxColour m_noticeColor;
    wxColour m_userColors[16];
    
    wxFont m_font;
};

#endif // CHATVIEWWIDGET_H