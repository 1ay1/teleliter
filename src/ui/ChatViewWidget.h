#ifndef CHATVIEWWIDGET_H
#define CHATVIEWWIDGET_H

#include <wx/wx.h>
#include <wx/richtext/richtextctrl.h>
#include <wx/popupwin.h>
#include <wx/timer.h>
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
    
    // Smart scrolling - only auto-scroll if at bottom
    void ScrollToBottomIfAtBottom();
    bool IsAtBottom() const;
    void ShowNewMessageIndicator();
    void HideNewMessageIndicator();
    
    // Batch updates (freeze/thaw for performance)
    void BeginBatchUpdate();
    void EndBatchUpdate();
    
    // Topic bar (HexChat-style header showing chat name and info)
    void SetTopicText(const wxString& chatName, const wxString& info = "");
    void ClearTopicText();
    
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
    
    // Current user for mention/highlight detection (HexChat-style)
    void SetCurrentUsername(const wxString& username) { m_currentUsername = username; }
    wxString GetCurrentUsername() const { return m_currentUsername; }
    
    // Pending downloads
    void AddPendingDownload(int32_t fileId, const MediaInfo& info);
    bool HasPendingDownload(int32_t fileId) const;
    MediaInfo GetPendingDownload(int32_t fileId) const;
    void RemovePendingDownload(int32_t fileId);
    
    // Media popup
    void ShowMediaPopup(const MediaInfo& info, const wxPoint& position);
    void HideMediaPopup();
    void ScheduleHideMediaPopup();
    void UpdateMediaPopup(int32_t fileId, const wxString& localPath);
    
    // Edit history popup
    void ShowEditHistoryPopup(const EditSpan& span, const wxPoint& position);
    void HideEditHistoryPopup();
    
    // Open media (external viewer or download)
    void OpenMedia(const MediaInfo& info);
    
    // Called when a media download completes
    void OnMediaDownloadComplete(int32_t fileId, const wxString& localPath);
    
    // Loading state
    void SetLoading(bool loading);
    bool IsLoading() const { return m_isLoading; }
    
private:
    void CreateLayout();
    void SetupDisplayControl();
    void CreateNewMessageButton();
    wxString FormatTimestamp(int64_t unixTime);
    wxString FormatSmartTimestamp(int64_t unixTime);
    
    // Event handlers
    void OnMouseMove(wxMouseEvent& event);
    void OnMouseLeave(wxMouseEvent& event);
    void OnLeftDown(wxMouseEvent& event);
    void OnRightDown(wxMouseEvent& event);
    void OnKeyDown(wxKeyEvent& event);
    void OnHoverTimer(wxTimerEvent& event);
    void OnHideTimer(wxTimerEvent& event);
    void OnScroll(wxScrollWinEvent& event);
    void OnNewMessageButtonClick(wxCommandEvent& event);
    void OnSize(wxSizeEvent& event);
    
    // Context menu handlers
    void OnCopyText(wxCommandEvent& event);
    void OnCopyLink(wxCommandEvent& event);
    void OnOpenLink(wxCommandEvent& event);
    void OnSaveMedia(wxCommandEvent& event);
    void OnOpenMedia(wxCommandEvent& event);
    
    // Context menu helpers
    void ShowContextMenu(const wxPoint& pos);
    wxString GetSelectedText() const;
    wxString GetLinkAtPosition(long pos) const;
    
    MainFrame* m_mainFrame;
    wxRichTextCtrl* m_chatDisplay;
    MessageFormatter* m_messageFormatter;
    MediaPopup* m_mediaPopup;
    wxPopupWindow* m_editHistoryPopup;
    wxButton* m_newMessageButton;  // "↓ New Messages" button
    
    // Topic bar (HexChat-style)
    wxPanel* m_topicBar;
    wxStaticText* m_topicText;
    
    // Media spans for clickable media
    std::vector<MediaSpan> m_mediaSpans;
    
    // Edit spans for showing original text
    std::vector<EditSpan> m_editSpans;
    
    // Link spans for clickable URLs
    std::vector<LinkSpan> m_linkSpans;
    
    // Pending downloads (file ID -> media info)
    std::map<int32_t, MediaInfo> m_pendingDownloads;
    
    // Hover debouncing and popup management
    wxTimer m_hoverTimer;
    wxTimer m_hideTimer;  // Delayed hide for smoother UX
    MediaInfo m_pendingHoverMedia;
    wxPoint m_pendingHoverPos;
    MediaInfo m_currentlyShowingMedia;  // Track what's currently displayed
    long m_lastHoveredTextPos;
    bool m_isOverMediaSpan;  // Track if mouse is currently over any media span
    static const int HOVER_DELAY_MS = 200;  // Delay before showing popup
    static const int HIDE_DELAY_MS = 300;   // Delay before hiding popup
    
    // Smart scrolling state
    bool m_wasAtBottom;  // Track if we were at bottom before new messages
    int m_newMessageCount;  // Count of unread messages when scrolled up
    bool m_isLoading;  // Loading state
    int m_batchUpdateDepth;  // Nested batch update counter
    
    // Message grouping state (HexChat-style)
    wxString m_lastDisplayedSender;
    int64_t m_lastDisplayedTimestamp;
    
    // Current username for highlight detection
    wxString m_currentUsername;
    
    // Context menu state
    long m_contextMenuPos;  // Text position where context menu was opened
    wxString m_contextMenuLink;  // Link URL if right-clicked on a link
    MediaInfo m_contextMenuMedia;  // Media info if right-clicked on media
    
    // Helper to check if two MediaInfo refer to the same media
    bool IsSameMedia(const MediaInfo& a, const MediaInfo& b) const;
    
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
    
    // Menu IDs
    enum {
        ID_COPY_TEXT = wxID_HIGHEST + 1000,
        ID_COPY_LINK,
        ID_OPEN_LINK,
        ID_SAVE_MEDIA,
        ID_OPEN_MEDIA,
        ID_NEW_MESSAGE_BUTTON
    };
};

#endif // CHATVIEWWIDGET_H