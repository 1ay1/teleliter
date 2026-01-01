#ifndef CHATLISTWIDGET_H
#define CHATLISTWIDGET_H

#include <functional>
#include <map>
#include <vector>
#include <wx/srchctrl.h>
#include <wx/stattext.h>
#include <wx/timer.h>
#include <wx/treectrl.h>
#include <wx/wx.h>

// Forward declarations
class MainFrame;
class TelegramClient;
struct ChatInfo;
struct UserInfo;

class ChatListWidget : public wxPanel {
public:
  ChatListWidget(wxWindow *parent);
  virtual ~ChatListWidget();

  // Chat management
  void RefreshChatList(const std::vector<ChatInfo> &chats);
  void RefreshOnlineIndicators();  // Update online status for private chats
  void ClearAllChats();
  void SelectTeleliter();
  void SelectChat(int64_t chatId);

  // Chat item access
  int64_t GetSelectedChatId() const;
  bool IsTeleliterSelected() const;

  // Styling
  void SetTreeColors(const wxColour &bg, const wxColour &fg,
                     const wxColour &selBg);
  void SetTreeFont(const wxFont &font);

  // Tree item IDs
  wxTreeItemId GetTeleliterItem() const { return m_teleliterItem; }
  wxTreeItemId GetPinnedChats() const { return m_pinnedChats; }
  wxTreeItemId GetPrivateChats() const { return m_privateChats; }
  wxTreeItemId GetGroups() const { return m_groups; }
  wxTreeItemId GetChannels() const { return m_channels; }
  wxTreeItemId GetBots() const { return m_bots; }

  // Direct access to tree control (for event binding)
  wxTreeCtrl *GetTreeCtrl() { return m_chatTree; }

  // Chat ID mappings
  int64_t GetChatIdFromTreeItem(const wxTreeItemId &item) const;
  wxTreeItemId GetTreeItemFromChatId(int64_t chatId) const;

  // Set reference to TelegramClient for online status lookup
  void SetTelegramClient(TelegramClient *client) { m_telegramClient = client; }

  // Search/filter
  void SetSearchFilter(const wxString &filter);
  void ClearSearch();

  // Lazy loading
  void SetLoadMoreCallback(std::function<void()> callback) { m_loadMoreCallback = callback; }
  void SetHasMoreChats(bool hasMore);
  void SetIsLoadingChats(bool loading);
  bool IsNearBottom() const;
  
  // Loading indicator
  void ShowLoadingIndicator();
  void HideLoadingIndicator();
  bool IsLoadingVisible() const;

private:
  void CreateLayout();
  void CreateCategories();
  wxTreeItemId GetCategoryForChat(const ChatInfo &chat) const;
  wxTreeItemId AddChatToCategory(const ChatInfo &chat);
  wxTreeItemId InsertChatSorted(wxTreeItemId parent, const wxString &title, int64_t lastMessageDate);
  void UpdateChatItem(const wxTreeItemId &item, const ChatInfo &chat);
  wxString FormatChatTitle(const ChatInfo &chat) const;
  
  bool MatchesFilter(const ChatInfo &chat) const;
  void ApplyFilter();

  // Event handlers
  void OnSearchText(wxCommandEvent &event);
  void OnSearchCancel(wxCommandEvent &event);
  void OnSelectionChanged(wxTreeEvent &event);
  void OnTreeScrolled(wxScrollWinEvent &event);
  void OnTreeExpanded(wxTreeEvent &event);
  void OnMouseWheel(wxMouseEvent &event);
  void OnLoadingTimer(wxTimerEvent &event);
  void OnIdleCheck(wxIdleEvent &event);
  
  // Lazy loading helper
  void CheckAndTriggerLazyLoad();
  void ScheduleLazyLoadCheck();
  bool ShouldLoadMoreChats() const;

  TelegramClient *m_telegramClient;

  wxSearchCtrl *m_searchBox;
  wxTreeCtrl *m_chatTree;

  // Tree structure
  wxTreeItemId m_treeRoot;
  wxTreeItemId m_teleliterItem;
  wxTreeItemId m_pinnedChats;
  wxTreeItemId m_privateChats;
  wxTreeItemId m_groups;
  wxTreeItemId m_channels;
  wxTreeItemId m_bots;
  wxTreeItemId m_previousSelection;

  // Chat ID mappings
  std::map<wxTreeItemId, int64_t> m_treeItemToChatId;
  std::map<int64_t, wxTreeItemId> m_chatIdToTreeItem;

  // Store all chats for filtering
  std::vector<ChatInfo> m_allChats;
  wxString m_searchFilter;

  // Colors
  wxColour m_bgColor;
  wxColour m_fgColor;
  wxColour m_selBgColor;
  wxFont m_font;

  // Lazy loading state
  std::function<void()> m_loadMoreCallback;
  bool m_hasMoreChats = true;
  bool m_isLoadingChats = false;
  bool m_lazyLoadCheckPending = false;
  
  // Loading indicator panel
  wxPanel *m_loadingPanel = nullptr;
  wxStaticText *m_loadingText = nullptr;
  wxTimer m_loadingAnimTimer;
  int m_loadingDots = 0;
  
  // Debounce timer for scroll-triggered loads
  wxTimer m_scrollDebounceTimer;
  static constexpr int SCROLL_DEBOUNCE_MS = 100;
  static constexpr int LOADING_ANIM_MS = 400;

  // Online indicator
  static const wxString ONLINE_INDICATOR;
};

#endif // CHATLISTWIDGET_H