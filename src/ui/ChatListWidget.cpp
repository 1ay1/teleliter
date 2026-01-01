#include "ChatListWidget.h"
#include "../telegram/TelegramClient.h"
#include "../telegram/Types.h"
#include "MainFrame.h"
#include "MenuIds.h"
#include <wx/settings.h>
#include <wx/sizer.h>
#include <iostream>

// #define CLWLOG(msg) std::cerr << "[ChatListWidget] " << msg << std::endl
#define CLWLOG(msg) do {} while(0)

// Online indicator - green circle emoji (requires emoji font on system)
const wxString ChatListWidget::ONLINE_INDICATOR =
    wxString::FromUTF8("\xF0\x9F\x9F\xA2 ");

ChatListWidget::ChatListWidget(wxWindow *parent)
    : wxPanel(parent, wxID_ANY), m_telegramClient(nullptr),
      m_searchBox(nullptr), m_chatTree(nullptr),
      m_bgColor(wxSystemSettings::GetColour(wxSYS_COLOUR_LISTBOX)),
      m_fgColor(wxSystemSettings::GetColour(wxSYS_COLOUR_LISTBOXTEXT)),
      m_selBgColor(wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHT)),
      m_loadingAnimTimer(this),
      m_scrollDebounceTimer(this) {
  CreateLayout();
  CreateCategories();
  
  // Bind timers
  Bind(wxEVT_TIMER, &ChatListWidget::OnLoadingTimer, this, m_loadingAnimTimer.GetId());
  Bind(wxEVT_TIMER, [this](wxTimerEvent&) {
    CheckAndTriggerLazyLoad();
  }, m_scrollDebounceTimer.GetId());
  
  // Bind idle event for deferred lazy load checks
  Bind(wxEVT_IDLE, &ChatListWidget::OnIdleCheck, this);
}

ChatListWidget::~ChatListWidget() {}

void ChatListWidget::CreateLayout() {
  wxBoxSizer *sizer = new wxBoxSizer(wxVERTICAL);

  // Search box at the top
  m_searchBox =
      new wxSearchCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition,
                       wxDefaultSize, wxTE_PROCESS_ENTER);
  m_searchBox->SetDescriptiveText("Search chats...");
  m_searchBox->ShowCancelButton(true);

  // Bind search events
  m_searchBox->Bind(wxEVT_TEXT, &ChatListWidget::OnSearchText, this);
  m_searchBox->Bind(wxEVT_SEARCHCTRL_CANCEL_BTN,
                    &ChatListWidget::OnSearchCancel, this);

  sizer->Add(m_searchBox, 0, wxEXPAND | wxALL, 2);

  // Tree control for chat list - use default selection style for better text
  // contrast
  m_chatTree =
      new wxTreeCtrl(this, ID_CHAT_TREE, wxDefaultPosition, wxDefaultSize,
                     wxTR_HIDE_ROOT | wxTR_NO_LINES | wxTR_SINGLE |
                         wxTR_HAS_BUTTONS | wxTR_FULL_ROW_HIGHLIGHT);

  // Bind selection change to update text colors for proper contrast
  m_chatTree->Bind(wxEVT_TREE_SEL_CHANGED, &ChatListWidget::OnSelectionChanged,
                   this);
  
  // Bind scroll events for lazy loading
  m_chatTree->Bind(wxEVT_SCROLLWIN_THUMBRELEASE, &ChatListWidget::OnTreeScrolled, this);
  m_chatTree->Bind(wxEVT_SCROLLWIN_LINEDOWN, &ChatListWidget::OnTreeScrolled, this);
  m_chatTree->Bind(wxEVT_SCROLLWIN_PAGEDOWN, &ChatListWidget::OnTreeScrolled, this);
  m_chatTree->Bind(wxEVT_SCROLLWIN_LINEUP, &ChatListWidget::OnTreeScrolled, this);
  m_chatTree->Bind(wxEVT_SCROLLWIN_PAGEUP, &ChatListWidget::OnTreeScrolled, this);
  m_chatTree->Bind(wxEVT_TREE_ITEM_EXPANDED, &ChatListWidget::OnTreeExpanded, this);
  m_chatTree->Bind(wxEVT_MOUSEWHEEL, &ChatListWidget::OnMouseWheel, this);

  sizer->Add(m_chatTree, 1, wxEXPAND);
  
  // Create loading indicator panel (hidden initially)
  m_loadingPanel = new wxPanel(this, wxID_ANY);
  wxBoxSizer *loadingSizer = new wxBoxSizer(wxHORIZONTAL);
  m_loadingText = new wxStaticText(m_loadingPanel, wxID_ANY, "Loading chats...");
  m_loadingText->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
  loadingSizer->AddStretchSpacer();
  loadingSizer->Add(m_loadingText, 0, wxALIGN_CENTER_VERTICAL | wxALL, 4);
  loadingSizer->AddStretchSpacer();
  m_loadingPanel->SetSizer(loadingSizer);
  m_loadingPanel->Hide();
  
  sizer->Add(m_loadingPanel, 0, wxEXPAND);
  SetSizer(sizer);
}

void ChatListWidget::CreateCategories() {
  // Create root
  m_treeRoot = m_chatTree->AddRoot("Chats");

  // Add Teleliter at the top (like HexChat's network/status)
  m_teleliterItem = m_chatTree->AppendItem(m_treeRoot, "Teleliter");
  m_chatTree->SetItemBold(m_teleliterItem, true);

  // Create categories with emoji icons
  m_pinnedChats = m_chatTree->AppendItem(
      m_treeRoot, wxString::FromUTF8("\xF0\x9F\x93\x8C Pinned")); // 📌
  m_privateChats = m_chatTree->AppendItem(
      m_treeRoot, wxString::FromUTF8("\xF0\x9F\x92\xAC Private Chats")); // 💬
  m_groups = m_chatTree->AppendItem(
      m_treeRoot, wxString::FromUTF8("\xF0\x9F\x91\xA5 Groups")); // 👥
  m_channels = m_chatTree->AppendItem(
      m_treeRoot, wxString::FromUTF8("\xF0\x9F\x93\xA2 Channels")); // 📢
  m_bots = m_chatTree->AppendItem(
      m_treeRoot, wxString::FromUTF8("\xF0\x9F\xA4\x96 Bots")); // 🤖

  // Make categories bold
  m_chatTree->SetItemBold(m_pinnedChats, true);
  m_chatTree->SetItemBold(m_privateChats, true);
  m_chatTree->SetItemBold(m_groups, true);
  m_chatTree->SetItemBold(m_channels, true);
  m_chatTree->SetItemBold(m_bots, true);

  // Add Test Chat under Groups for testing (uses special ID -1)
  wxTreeItemId testChat =
      m_chatTree->AppendItem(m_groups, "Test Chat - Media Demo");
  m_treeItemToChatId[testChat] = -1; // Special ID for test chat
  m_chatIdToTreeItem[-1] = testChat;
  m_chatTree->Expand(m_groups);

  // Select Teleliter by default
  m_chatTree->SelectItem(m_teleliterItem);
}

void ChatListWidget::RefreshChatList(const std::vector<ChatInfo> &chats) {
  // Store chats for filtering
  m_allChats = chats;

  // Remember current selection
  int64_t selectedChatId = GetSelectedChatId();
  bool wasOnTeleliter = IsTeleliterSelected();

  // Freeze the tree to prevent UI updates during batch modifications
  // This significantly improves performance for large chat lists
  m_chatTree->Freeze();

  // Build a set of chat IDs we're about to display (after filtering)
  std::set<int64_t> newChatIds;
  for (const auto &chat : m_allChats) {
    if (MatchesFilter(chat)) {
      newChatIds.insert(chat.id);
    }
  }

  // Remove chats that no longer exist (keeping Test Chat -1)
  std::vector<int64_t> toRemove;
  for (const auto &[chatId, treeItem] : m_chatIdToTreeItem) {
    if (chatId != -1 && newChatIds.find(chatId) == newChatIds.end()) {
      toRemove.push_back(chatId);
    }
  }
  for (int64_t chatId : toRemove) {
    auto it = m_chatIdToTreeItem.find(chatId);
    if (it != m_chatIdToTreeItem.end()) {
      wxTreeItemId item = it->second;
      m_treeItemToChatId.erase(item);
      m_chatTree->Delete(item);
      m_chatIdToTreeItem.erase(it);
    }
  }

  // Update existing chats or add new ones
  for (const auto &chat : m_allChats) {
    if (!MatchesFilter(chat)) {
      // Remove if it exists but doesn't match filter
      auto it = m_chatIdToTreeItem.find(chat.id);
      if (it != m_chatIdToTreeItem.end()) {
        wxTreeItemId item = it->second;
        m_treeItemToChatId.erase(item);
        m_chatTree->Delete(item);
        m_chatIdToTreeItem.erase(it);
      }
      continue;
    }

    auto it = m_chatIdToTreeItem.find(chat.id);
    if (it != m_chatIdToTreeItem.end()) {
      // Check if item needs to move to a different category
      wxTreeItemId currentItem = it->second;
      wxTreeItemId currentParent = m_chatTree->GetItemParent(currentItem);
      wxTreeItemId correctParent = GetCategoryForChat(chat);
      
      if (currentParent != correctParent) {
        // Category changed - remove from old location and add to new
        m_treeItemToChatId.erase(currentItem);
        m_chatTree->Delete(currentItem);
        m_chatIdToTreeItem.erase(it);
        AddChatToCategory(chat);
      } else {
        // Same category - just update the item
        UpdateChatItem(currentItem, chat);
      }
    } else {
      // Add new item
      AddChatToCategory(chat);
    }
  }

  // Expand Pinned and Private Chats if they have items (these are primary
  // categories) Keep Groups, Channels, and Bots collapsed by default (user can
  // expand manually)
  if (m_chatTree->GetChildrenCount(m_pinnedChats) > 0) {
    m_chatTree->Expand(m_pinnedChats);
  } else {
    m_chatTree->Collapse(m_pinnedChats);
  }
  if (m_chatTree->GetChildrenCount(m_privateChats) > 0) {
    m_chatTree->Expand(m_privateChats);
  } else {
    m_chatTree->Collapse(m_privateChats);
  }
  // Groups, Channels, Bots - collapse (don't auto-expand) unless already
  // expanded by user Just ensure empty ones are collapsed
  if (m_chatTree->GetChildrenCount(m_groups) == 0) {
    m_chatTree->Collapse(m_groups);
  }
  if (m_chatTree->GetChildrenCount(m_channels) == 0) {
    m_chatTree->Collapse(m_channels);
  }
  if (m_chatTree->GetChildrenCount(m_bots) == 0) {
    m_chatTree->Collapse(m_bots);
  }

  // Restore selection
  if (wasOnTeleliter) {
    m_chatTree->SelectItem(m_teleliterItem);
  } else if (selectedChatId != 0) {
    auto it = m_chatIdToTreeItem.find(selectedChatId);
    if (it != m_chatIdToTreeItem.end()) {
      m_chatTree->SelectItem(it->second);
    }
  }

  // Thaw the tree to allow UI updates again
  m_chatTree->Thaw();
}

void ChatListWidget::ClearAllChats() {
  // Clear all children of category items (but keep the categories)
  m_chatTree->DeleteChildren(m_pinnedChats);
  m_chatTree->DeleteChildren(m_privateChats);
  m_chatTree->DeleteChildren(m_groups);
  m_chatTree->DeleteChildren(m_channels);
  m_chatTree->DeleteChildren(m_bots);

  // Clear mappings
  m_treeItemToChatId.clear();
  m_chatIdToTreeItem.clear();
  m_allChats.clear();
}

void ChatListWidget::SelectTeleliter() {
  m_chatTree->SelectItem(m_teleliterItem);
}

void ChatListWidget::SelectChat(int64_t chatId) {
  auto it = m_chatIdToTreeItem.find(chatId);
  if (it != m_chatIdToTreeItem.end()) {
    m_chatTree->SelectItem(it->second);
  }
}

int64_t ChatListWidget::GetSelectedChatId() const {
  wxTreeItemId selection = m_chatTree->GetSelection();
  if (!selection.IsOk()) {
    return 0;
  }

  auto it = m_treeItemToChatId.find(selection);
  if (it != m_treeItemToChatId.end()) {
    return it->second;
  }

  return 0;
}

bool ChatListWidget::IsTeleliterSelected() const {
  wxTreeItemId selection = m_chatTree->GetSelection();
  return selection.IsOk() && selection == m_teleliterItem;
}

void ChatListWidget::SetTreeColors(const wxColour &bg, const wxColour &fg,
                                   const wxColour &selBg) {
  m_bgColor = bg;
  m_fgColor = fg;
  m_selBgColor = selBg;

  // Let tree control use native colors - don't override
  if (m_chatTree) {
    m_chatTree->Refresh();
  }
}

void ChatListWidget::SetTreeFont(const wxFont &font) {
  m_font = font;

  // Apply font to tree control
  if (m_chatTree && font.IsOk()) {
    m_chatTree->SetFont(font);
    m_chatTree->Refresh();
  }

  // Also apply to search box
  if (m_searchBox && font.IsOk()) {
    m_searchBox->SetFont(font);
  }
}

int64_t ChatListWidget::GetChatIdFromTreeItem(const wxTreeItemId &item) const {
  auto it = m_treeItemToChatId.find(item);
  if (it != m_treeItemToChatId.end()) {
    return it->second;
  }
  return 0;
}

wxTreeItemId ChatListWidget::GetTreeItemFromChatId(int64_t chatId) const {
  auto it = m_chatIdToTreeItem.find(chatId);
  if (it != m_chatIdToTreeItem.end()) {
    return it->second;
  }
  return wxTreeItemId();
}

wxTreeItemId ChatListWidget::GetCategoryForChat(const ChatInfo &chat) const {
  // Determine which category this chat belongs to
  if (chat.isPinned) {
    return m_pinnedChats;
  } else if (chat.isBot) {
    return m_bots;
  } else if (chat.isChannel) {
    return m_channels;
  } else if (chat.isGroup || chat.isSupergroup) {
    return m_groups;
  } else if (chat.isPrivate) {
    return m_privateChats;
  } else {
    return m_privateChats; // Default
  }
}

wxTreeItemId ChatListWidget::AddChatToCategory(const ChatInfo &chat) {
  wxTreeItemId parent = GetCategoryForChat(chat);

  // Format title with unread count and online indicator
  wxString title = FormatChatTitle(chat);

  // Add the item
  wxTreeItemId item = m_chatTree->AppendItem(parent, title);

  // Set bold if unread (don't set custom colors - breaks selection contrast)
  if (chat.unreadCount > 0) {
    m_chatTree->SetItemBold(item, true);
  }

  // Store mappings
  m_treeItemToChatId[item] = chat.id;
  m_chatIdToTreeItem[chat.id] = item;

  return item;
}

void ChatListWidget::UpdateChatItem(const wxTreeItemId &item,
                                    const ChatInfo &chat) {
  if (!item.IsOk()) {
    return;
  }

  wxString title = FormatChatTitle(chat);
  m_chatTree->SetItemText(item, title);
  // Only use bold for unread (don't set custom colors - breaks selection
  // contrast)
  m_chatTree->SetItemBold(item, chat.unreadCount > 0);
}

wxString ChatListWidget::FormatChatTitle(const ChatInfo &chat) const {
  wxString title;

  // Add online indicator for private chats with online users
  if (chat.isPrivate && chat.userId != 0 && m_telegramClient) {
    bool found = false;
    UserInfo user = m_telegramClient->GetUser(chat.userId, &found);
    // Use IsCurrentlyOnline() which checks expiry time
    if (found && user.IsCurrentlyOnline()) {
      title = ONLINE_INDICATOR;
    }
  }

  title += chat.title;

  // Append unread count with badge style
  if (chat.unreadCount > 0) {
    if (chat.unreadCount > 99) {
      title += " [99+]";
    } else {
      title += wxString::Format(" [%d]", chat.unreadCount);
    }
  }

  // Show muted indicator
  if (chat.isMuted) {
    title += wxString::FromUTF8(" 🔇");
  }

  return title;
}

bool ChatListWidget::MatchesFilter(const ChatInfo &chat) const {
  if (m_searchFilter.IsEmpty()) {
    return true;
  }

  // Case-insensitive search on title
  wxString lowerTitle = chat.title.Lower();
  wxString lowerFilter = m_searchFilter.Lower();

  return lowerTitle.Contains(lowerFilter);
}

void ChatListWidget::SetSearchFilter(const wxString &filter) {
  m_searchFilter = filter;
  ApplyFilter();
}

void ChatListWidget::ClearSearch() {
  m_searchFilter.Clear();
  if (m_searchBox) {
    m_searchBox->Clear();
  }
  ApplyFilter();
}

void ChatListWidget::ApplyFilter() {
  // Re-render the chat list with current filter
  RefreshChatList(m_allChats);
}

void ChatListWidget::RefreshOnlineIndicators() {
  // Update online indicators for all private chats without full rebuild
  if (!m_telegramClient || !m_chatTree) {
    return;
  }

  for (const auto &chat : m_allChats) {
    // Only update private chats (they have online indicators)
    if (!chat.isPrivate || chat.userId == 0) {
      continue;
    }

    auto it = m_chatIdToTreeItem.find(chat.id);
    if (it == m_chatIdToTreeItem.end() || !it->second.IsOk()) {
      continue;
    }

    // Re-format the title (which includes online indicator check)
    wxString newTitle = FormatChatTitle(chat);
    wxString currentTitle = m_chatTree->GetItemText(it->second);

    // Only update if changed to avoid flicker
    if (newTitle != currentTitle) {
      m_chatTree->SetItemText(it->second, newTitle);
    }
  }
}

void ChatListWidget::OnSearchText(wxCommandEvent &event) {
  m_searchFilter = m_searchBox->GetValue();
  ApplyFilter();
}

void ChatListWidget::OnSearchCancel(wxCommandEvent &event) { ClearSearch(); }

void ChatListWidget::OnSelectionChanged(wxTreeEvent &event) {
  // Restore previous selection's text color to normal
  if (m_previousSelection.IsOk() && m_chatTree) {
    m_chatTree->SetItemTextColour(
        m_previousSelection,
        wxSystemSettings::GetColour(wxSYS_COLOUR_LISTBOXTEXT));
  }

  // Set new selection's text color to white for contrast
  wxTreeItemId newSelection = event.GetItem();
  if (newSelection.IsOk() && m_chatTree) {
    m_chatTree->SetItemTextColour(newSelection, *wxWHITE);
    m_previousSelection = newSelection;
  }

  event.Skip(); // Allow event to propagate to MainFrame
}

void ChatListWidget::OnTreeScrolled(wxScrollWinEvent &event) {
  event.Skip();
  ScheduleLazyLoadCheck();
}

void ChatListWidget::OnTreeExpanded(wxTreeEvent &event) {
  event.Skip();
  ScheduleLazyLoadCheck();
}

void ChatListWidget::OnMouseWheel(wxMouseEvent &event) {
  event.Skip();
  ScheduleLazyLoadCheck();
}

void ChatListWidget::OnLoadingTimer(wxTimerEvent &event) {
  // Animate the loading text with dots
  m_loadingDots = (m_loadingDots + 1) % 4;
  wxString dots;
  for (int i = 0; i < m_loadingDots; ++i) {
    dots += ".";
  }
  if (m_loadingText) {
    m_loadingText->SetLabel("Loading chats" + dots);
  }
}

void ChatListWidget::OnIdleCheck(wxIdleEvent &event) {
  event.Skip();
  
  if (m_lazyLoadCheckPending) {
    m_lazyLoadCheckPending = false;
    CheckAndTriggerLazyLoad();
  }
}

void ChatListWidget::ScheduleLazyLoadCheck() {
  // Use debounce timer to coalesce rapid scroll events
  if (m_scrollDebounceTimer.IsRunning()) {
    m_scrollDebounceTimer.Stop();
  }
  m_scrollDebounceTimer.StartOnce(SCROLL_DEBOUNCE_MS);
}

void ChatListWidget::CheckAndTriggerLazyLoad() {
  if (!m_loadMoreCallback || !m_hasMoreChats || m_isLoadingChats) {
    CLWLOG("CheckAndTriggerLazyLoad: skip (callback=" << (m_loadMoreCallback ? "yes" : "no")
           << " hasMore=" << m_hasMoreChats << " loading=" << m_isLoadingChats << ")");
    return;
  }
  
  if (ShouldLoadMoreChats()) {
    CLWLOG("CheckAndTriggerLazyLoad: TRIGGERING load more chats!");
    m_isLoadingChats = true;
    ShowLoadingIndicator();
    m_loadMoreCallback();
  }
}

bool ChatListWidget::ShouldLoadMoreChats() const {
  if (!m_chatTree) {
    return false;
  }
  
  // Count total visible chat items
  size_t totalItems = 0;
  wxTreeItemIdValue cookie;
  for (wxTreeItemId cat = m_chatTree->GetFirstChild(m_treeRoot, cookie);
       cat.IsOk();
       cat = m_chatTree->GetNextChild(m_treeRoot, cookie)) {
    totalItems += m_chatTree->GetChildrenCount(cat, false);
  }
  
  CLWLOG("ShouldLoadMoreChats: totalItems=" << totalItems << " hasMore=" << m_hasMoreChats);
  
  // Always load more if we have fewer than minimum visible chats
  if (totalItems < 50 && m_hasMoreChats) {
    CLWLOG("ShouldLoadMoreChats: YES (need more items)");
    return true;
  }
  
  // Check scroll position
  int scrollPos = m_chatTree->GetScrollPos(wxVERTICAL);
  int scrollRange = m_chatTree->GetScrollRange(wxVERTICAL);
  int thumbSize = m_chatTree->GetScrollThumb(wxVERTICAL);
  
  CLWLOG("ShouldLoadMoreChats: scrollPos=" << scrollPos << " scrollRange=" << scrollRange << " thumbSize=" << thumbSize);
  
  // If there's no scrollbar, we might need more content
  if (scrollRange <= thumbSize) {
    CLWLOG("ShouldLoadMoreChats: no scrollbar, hasMore=" << m_hasMoreChats);
    return m_hasMoreChats;
  }
  
  // Check if we're within 30% of the bottom
  int maxScroll = scrollRange - thumbSize;
  if (maxScroll <= 0) {
    CLWLOG("ShouldLoadMoreChats: maxScroll<=0");
    return false;
  }
  
  float scrollPercent = (float)scrollPos / (float)maxScroll;
  CLWLOG("ShouldLoadMoreChats: scrollPercent=" << scrollPercent << " (threshold=0.70)");
  return scrollPercent > 0.70f;
}

void ChatListWidget::SetHasMoreChats(bool hasMore) {
  m_hasMoreChats = hasMore;
  if (!hasMore) {
    HideLoadingIndicator();
  }
}

void ChatListWidget::SetIsLoadingChats(bool loading) {
  bool wasLoading = m_isLoadingChats;
  m_isLoadingChats = loading;
  
  if (loading && !wasLoading) {
    ShowLoadingIndicator();
  } else if (!loading && wasLoading) {
    HideLoadingIndicator();
    // After loading completes, check if we need to load more
    // (user might have scrolled while loading)
    m_lazyLoadCheckPending = true;
  }
}

void ChatListWidget::ShowLoadingIndicator() {
  if (m_loadingPanel && !m_loadingPanel->IsShown()) {
    m_loadingDots = 0;
    m_loadingText->SetLabel("Loading chats...");
    m_loadingPanel->Show();
    m_loadingAnimTimer.Start(LOADING_ANIM_MS);
    Layout();
  }
}

void ChatListWidget::HideLoadingIndicator() {
  if (m_loadingPanel && m_loadingPanel->IsShown()) {
    m_loadingAnimTimer.Stop();
    m_loadingPanel->Hide();
    Layout();
  }
}

bool ChatListWidget::IsLoadingVisible() const {
  return m_loadingPanel && m_loadingPanel->IsShown();
}

bool ChatListWidget::IsNearBottom() const {
  return ShouldLoadMoreChats();
}