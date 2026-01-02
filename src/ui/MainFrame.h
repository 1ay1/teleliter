#ifndef MAINFRAME_H
#define MAINFRAME_H

#include <map>
#include <set>
#include <vector>
#include <wx/clipbrd.h>
#include <wx/fontenum.h>
#include <wx/gauge.h>
#include <wx/listctrl.h>
#include <wx/richtext/richtextctrl.h>
#include <wx/splitter.h>
#include <wx/stopwatch.h>
#include <wx/timer.h>
#include <wx/treectrl.h>
#include <wx/wx.h>

#include "../telegram/TransferManager.h"
#include "MediaTypes.h"
#include "MenuIds.h"
#include "ServiceMessageLog.h"
#include "StatusBarManager.h"

// Forward declarations
class WelcomeChat;
class TelegramClient;
class ChatListWidget;
class ChatViewWidget;

class InputBoxWidget;
class MediaPopup;
class MessageFormatter;
struct MessageInfo;
struct ChatInfo;
struct UserInfo;

class MainFrame : public wxFrame {
public:
  MainFrame(const wxString &title, const wxPoint &pos, const wxSize &size);
  virtual ~MainFrame();

  void OnFilesDropped(const wxArrayString &files);

  // TelegramClient callbacks
  void OnConnected();
  void OnLoginSuccess(const wxString &userName);
  void OnLoggedOut();
  void RefreshChatList();
  void OnMessagesLoaded(int64_t chatId,
                        const std::vector<MessageInfo> &messages);
  void OnOlderMessagesLoaded(int64_t chatId,
                             const std::vector<MessageInfo> &messages);
  void OnNewMessage(const MessageInfo &message);
  void OnMessageUpdated(int64_t chatId, const MessageInfo &message);
  void OnMessageEdited(int64_t chatId, int64_t messageId,
                       const wxString &newText, const wxString &senderName);
  void OnFileDownloaded(int32_t fileId, const wxString &localPath);
  void OnFileProgress(int32_t fileId, int64_t downloadedSize,
                      int64_t totalSize);
  void OnDownloadStarted(int32_t fileId, const wxString &fileName,
                         int64_t totalSize);
  void OnDownloadFailed(int32_t fileId, const wxString &error);
  void OnDownloadRetrying(int32_t fileId, int retryCount);
  void OnUserStatusChanged(int64_t userId, bool isOnline, int64_t lastSeenTime);
  void OnMembersLoaded(int64_t chatId, const std::vector<UserInfo> &members);
  void ShowStatusError(const wxString &error);

  // Reactive MVC - called when TelegramClient has dirty flags
  void ReactiveRefresh();
  void UpdateMemberList(int64_t chatId);

  // Unread message tracking
  void MarkMessageAsRead(int64_t chatId, int64_t messageId);
  void UpdateUnreadIndicator(int64_t chatId, int32_t unreadCount);
  int64_t GetLastReadMessageId(int64_t chatId) const;

  TelegramClient *GetTelegramClient() { return m_telegramClient; }
  int64_t GetCurrentChatId() const { return m_currentChatId; }
  wxString GetCurrentUser() const { return m_currentUser; }
  wxString GetCurrentChatTitle() const { return m_currentChatTitle; }

  // Widget access
  ChatListWidget *GetChatListWidget() { return m_chatListWidget; }
  ChatViewWidget *GetChatViewWidget() { return m_chatViewWidget; }
  InputBoxWidget *GetInputBoxWidget() { return m_inputBoxWidget; }
  wxListCtrl *GetMemberList() { return m_memberList; }
  StatusBarManager *GetStatusBarManager() { return m_statusBar; }
  ServiceMessageLog *GetServiceMessageLog() { return m_serviceLog; }

private:
  // UI Setup
  void CreateMenuBar();
  void CreateMainLayout();
  void CreateChatPanel(wxWindow *parent);
  void CreateMemberList(wxWindow *parent);
  void SetupColors();
  void SetupFonts();
  void ApplySavedFonts();
  void PopulateDummyData();

  // Transfer handling
  void UpdateTransferProgress(const TransferInfo &info);
  void OnTransferComplete(const TransferInfo &info);
  void OnTransferError(const TransferInfo &info);

  // Message display
  void DisplayMessage(const MessageInfo &msg);
  wxString FormatTimestamp(int64_t unixTime);

  // Menu event handlers
  void OnExit(wxCommandEvent &event);
  void OnAbout(wxCommandEvent &event);
  void OnLogin(wxCommandEvent &event);
  void OnLogout(wxCommandEvent &event);
  void OnNewChat(wxCommandEvent &event);
  void OnNewGroup(wxCommandEvent &event);
  void OnNewChannel(wxCommandEvent &event);
  void OnContacts(wxCommandEvent &event);
  void OnSearch(wxCommandEvent &event);
  void OnSavedMessages(wxCommandEvent &event);
  void OnUploadFile(wxCommandEvent &event);
  void OnPreferences(wxCommandEvent &event);
  void OnClearWindow(wxCommandEvent &event);
  void OnToggleChatList(wxCommandEvent &event);
  void OnToggleMembers(wxCommandEvent &event);
  void OnToggleChatInfo(wxCommandEvent &event);
  void OnFullscreen(wxCommandEvent &event);
  void OnToggleUnreadFirst(wxCommandEvent &event);
  void OnToggleReadReceipts(wxCommandEvent &event);
  void OnRawLog(wxCommandEvent &event);
  void OnPrevChat(wxCommandEvent &event);
  void OnNextChat(wxCommandEvent &event);
  void OnCloseChat(wxCommandEvent &event);
  void OnDocumentation(wxCommandEvent &event);

  // Timer event handlers
  void OnRefreshTimer(wxTimerEvent &event);
  void OnStatusTimer(wxTimerEvent &event);

  // UI event handlers
  void OnChatTreeSelectionChanged(wxTreeEvent &event);
  void OnChatTreeItemActivated(wxTreeEvent &event);
  void OnMemberListItemActivated(wxListEvent &event);
  void OnMemberListRightClick(wxListEvent &event);
  void OnCharHook(wxKeyEvent &event);

  // Welcome chat
  void ForwardInputToWelcomeChat(const wxString &input);
  bool IsWelcomeChatActive() const;

  // Core components
  TelegramClient *m_telegramClient;
  TransferManager m_transferManager;
  wxTimer *m_refreshTimer;
  wxTimer *m_statusTimer;
  wxStopWatch m_sessionTimer;

  // Mapping from TDLib fileId to TransferManager transferId
  std::map<int32_t, int> m_fileToTransferId;

  // Unread message tracking (chatId -> last read message ID)
  std::map<int64_t, int64_t> m_lastReadMessages;
  std::set<int64_t> m_chatsWithUnread;

  // Splitters
  wxSplitterWindow *m_mainSplitter;
  wxSplitterWindow *m_rightSplitter;

  // Left panel - Chat list widget
  wxPanel *m_leftPanel;
  ChatListWidget *m_chatListWidget;

  // Center panel - Chat area
  wxPanel *m_chatPanel;
  WelcomeChat *m_welcomeChat;
  ChatViewWidget *m_chatViewWidget;
  InputBoxWidget *m_inputBoxWidget;

  // Right panel - Member list
  wxPanel *m_rightPanel;
  wxListCtrl *m_memberList;
  wxStaticText *m_memberCountLabel;

  // Status bar manager
  StatusBarManager *m_statusBar;
  
  // Service message log - tracks and displays Telegram events
  ServiceMessageLog *m_serviceLog;

  // User colors for sender names (only colors that need to be stored)
  wxColour m_userColors[16];

  // Fonts - only two types, both configurable from settings
  wxFont m_chatFont; // For chat display and input box
  wxFont
      m_uiFont; // For everything else (tree, member list, status bar, labels)

  // State
  bool m_showChatList;
  bool m_showMembers;
  bool m_showChatInfo;
  bool m_showUnreadFirst;
  bool m_isLoggedIn;
  wxString m_currentUser;
  int64_t m_currentChatId;
  wxString m_currentChatTitle;
  TelegramChatType m_currentChatType;

  // Timer IDs
  static const int ID_REFRESH_TIMER = wxID_HIGHEST + 200;
  static const int ID_STATUS_TIMER = wxID_HIGHEST + 201;

  wxDECLARE_EVENT_TABLE();
};

#endif // MAINFRAME_H