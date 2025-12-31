#include "ChatListWidget.h"
#include "../telegram/TelegramClient.h"
#include "../telegram/Types.h"
#include "MainFrame.h"
#include "MenuIds.h"
#include <wx/settings.h>

// Online indicator - green circle emoji
const wxString ChatListWidget::ONLINE_INDICATOR = wxString::FromUTF8("🟢 ");

ChatListWidget::ChatListWidget(wxWindow *parent, MainFrame *mainFrame)
    : wxPanel(parent, wxID_ANY), m_mainFrame(mainFrame),
      m_telegramClient(nullptr), m_searchBox(nullptr), m_chatTree(nullptr),
      m_bgColor(wxSystemSettings::GetColour(wxSYS_COLOUR_LISTBOX)),
      m_fgColor(wxSystemSettings::GetColour(wxSYS_COLOUR_LISTBOXTEXT)),
      m_selBgColor(wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHT)) {
  CreateLayout();
  CreateCategories();
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

  // Tree control for chat list - use native styling
  m_chatTree =
      new wxTreeCtrl(this, ID_CHAT_TREE, wxDefaultPosition, wxDefaultSize,
                     wxTR_DEFAULT_STYLE | wxTR_HIDE_ROOT | wxTR_NO_LINES |
                         wxTR_FULL_ROW_HIGHLIGHT | wxTR_SINGLE);

  // Explicitly set native window background
  m_chatTree->SetBackgroundColour(
      wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));

  sizer->Add(m_chatTree, 1, wxEXPAND);
  SetSizer(sizer);
}

void ChatListWidget::CreateCategories() {
  // Create root
  m_treeRoot = m_chatTree->AddRoot("Chats");

  // Add Teleliter at the top (like HexChat's network/status)
  m_teleliterItem = m_chatTree->AppendItem(m_treeRoot, "Teleliter");
  m_chatTree->SetItemBold(m_teleliterItem, true);

  // Create categories
  m_pinnedChats = m_chatTree->AppendItem(m_treeRoot, "📌 Pinned");
  m_privateChats = m_chatTree->AppendItem(m_treeRoot, "💬 Private Chats");
  m_groups = m_chatTree->AppendItem(m_treeRoot, "👥 Groups");
  m_channels = m_chatTree->AppendItem(m_treeRoot, "📢 Channels");
  m_bots = m_chatTree->AppendItem(m_treeRoot, "🤖 Bots");

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
      // Update existing item
      UpdateChatItem(it->second, chat);
    } else {
      // Add new item
      AddChatToCategory(chat);
    }
  }

  // Expand categories that have items, collapse empty ones
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
  if (m_chatTree->GetChildrenCount(m_groups) > 0) {
    m_chatTree->Expand(m_groups);
  } else {
    m_chatTree->Collapse(m_groups);
  }
  if (m_chatTree->GetChildrenCount(m_channels) > 0) {
    m_chatTree->Expand(m_channels);
  } else {
    m_chatTree->Collapse(m_channels);
  }
  if (m_chatTree->GetChildrenCount(m_bots) > 0) {
    m_chatTree->Expand(m_bots);
  } else {
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

wxTreeItemId ChatListWidget::AddChatToCategory(const ChatInfo &chat) {
  wxTreeItemId parent;

  // Determine which category this chat belongs to
  if (chat.isPinned) {
    parent = m_pinnedChats;
  } else if (chat.isBot) {
    parent = m_bots;
  } else if (chat.isChannel) {
    parent = m_channels;
  } else if (chat.isGroup || chat.isSupergroup) {
    parent = m_groups;
  } else if (chat.isPrivate) {
    parent = m_privateChats;
  } else {
    parent = m_privateChats; // Default
  }

  // Format title with unread count and online indicator
  wxString title = FormatChatTitle(chat);

  // Add the item
  wxTreeItemId item = m_chatTree->AppendItem(parent, title);

  // Set bold if unread
  if (chat.unreadCount > 0) {
    m_chatTree->SetItemBold(item, true);
    // Use highlight color for unread
    m_chatTree->SetItemTextColour(
        item, wxSystemSettings::GetColour(wxSYS_COLOUR_HOTLIGHT));
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
  m_chatTree->SetItemBold(item, chat.unreadCount > 0);

  // Update text color based on unread status
  if (chat.unreadCount > 0) {
    m_chatTree->SetItemTextColour(
        item, wxSystemSettings::GetColour(wxSYS_COLOUR_HOTLIGHT));
  } else {
    m_chatTree->SetItemTextColour(
        item, wxSystemSettings::GetColour(wxSYS_COLOUR_LISTBOXTEXT));
  }
}

wxString ChatListWidget::FormatChatTitle(const ChatInfo &chat) const {
  wxString title;

  // Add online indicator for private chats with online users
  if (chat.isPrivate && chat.userId != 0 && m_telegramClient) {
    bool found = false;
    UserInfo user = m_telegramClient->GetUser(chat.userId, &found);
    if (found && user.isOnline) {
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

void ChatListWidget::OnSearchText(wxCommandEvent &event) {
  m_searchFilter = m_searchBox->GetValue();
  ApplyFilter();
}

void ChatListWidget::OnSearchCancel(wxCommandEvent &event) { ClearSearch(); }