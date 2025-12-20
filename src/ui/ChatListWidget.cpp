#include "ChatListWidget.h"
#include "MainFrame.h"
#include "MenuIds.h"
#include "../telegram/Types.h"

ChatListWidget::ChatListWidget(wxWindow* parent, MainFrame* mainFrame)
    : wxPanel(parent, wxID_ANY),
      m_mainFrame(mainFrame),
      m_chatTree(nullptr),
      m_bgColor(0x23, 0x23, 0x23),
      m_fgColor(0xD0, 0xD0, 0xD0),
      m_selBgColor(0x3A, 0x3A, 0x3A)
{
    CreateLayout();
    CreateCategories();
}

ChatListWidget::~ChatListWidget()
{
}

void ChatListWidget::CreateLayout()
{
    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
    
    // Tree control for chat list
    m_chatTree = new wxTreeCtrl(this, ID_CHAT_TREE,
        wxDefaultPosition, wxDefaultSize,
        wxTR_DEFAULT_STYLE | wxTR_HIDE_ROOT | wxTR_NO_LINES | 
        wxTR_FULL_ROW_HIGHLIGHT | wxBORDER_NONE | wxTR_SINGLE);
    
    m_chatTree->SetBackgroundColour(m_bgColor);
    m_chatTree->SetForegroundColour(m_fgColor);
    
    sizer->Add(m_chatTree, 1, wxEXPAND);
    SetSizer(sizer);
}

void ChatListWidget::CreateCategories()
{
    // Create root
    m_treeRoot = m_chatTree->AddRoot("Chats");
    
    // Add Teleliter at the top (like HexChat's network/status)
    m_teleliterItem = m_chatTree->AppendItem(m_treeRoot, "Teleliter");
    m_chatTree->SetItemBold(m_teleliterItem, true);
    
    // Create categories
    m_pinnedChats = m_chatTree->AppendItem(m_treeRoot, "Pinned");
    m_privateChats = m_chatTree->AppendItem(m_treeRoot, "Private Chats");
    m_groups = m_chatTree->AppendItem(m_treeRoot, "Groups");
    m_channels = m_chatTree->AppendItem(m_treeRoot, "Channels");
    m_bots = m_chatTree->AppendItem(m_treeRoot, "Bots");
    
    // Make categories bold
    m_chatTree->SetItemBold(m_pinnedChats, true);
    m_chatTree->SetItemBold(m_privateChats, true);
    m_chatTree->SetItemBold(m_groups, true);
    m_chatTree->SetItemBold(m_channels, true);
    m_chatTree->SetItemBold(m_bots, true);
    
    // Add Test Chat under Groups for testing (uses special ID -1)
    wxTreeItemId testChat = m_chatTree->AppendItem(m_groups, "Test Chat - Media Demo");
    m_treeItemToChatId[testChat] = -1;  // Special ID for test chat
    m_chatIdToTreeItem[-1] = testChat;
    m_chatTree->Expand(m_groups);
    
    // Select Teleliter by default
    m_chatTree->SelectItem(m_teleliterItem);
}

void ChatListWidget::RefreshChatList(const std::vector<ChatInfo>& chats)
{
    // Clear existing chat items (keep categories)
    ClearAllChats();
    
    // Add chats to appropriate categories
    for (const auto& chat : chats) {
        AddChatToCategory(chat);
    }
    
    // Expand all categories
    m_chatTree->Expand(m_pinnedChats);
    m_chatTree->Expand(m_privateChats);
    m_chatTree->Expand(m_groups);
    m_chatTree->Expand(m_channels);
    m_chatTree->Expand(m_bots);
}

void ChatListWidget::ClearAllChats()
{
    // Clear all children of category items (but keep the categories)
    m_chatTree->DeleteChildren(m_pinnedChats);
    m_chatTree->DeleteChildren(m_privateChats);
    m_chatTree->DeleteChildren(m_groups);
    m_chatTree->DeleteChildren(m_channels);
    m_chatTree->DeleteChildren(m_bots);
    
    // Clear mappings
    m_treeItemToChatId.clear();
    m_chatIdToTreeItem.clear();
}

void ChatListWidget::SelectTeleliter()
{
    m_chatTree->SelectItem(m_teleliterItem);
}

void ChatListWidget::SelectChat(int64_t chatId)
{
    auto it = m_chatIdToTreeItem.find(chatId);
    if (it != m_chatIdToTreeItem.end()) {
        m_chatTree->SelectItem(it->second);
    }
}

int64_t ChatListWidget::GetSelectedChatId() const
{
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

bool ChatListWidget::IsTeleliterSelected() const
{
    wxTreeItemId selection = m_chatTree->GetSelection();
    return selection.IsOk() && selection == m_teleliterItem;
}

void ChatListWidget::SetTreeColors(const wxColour& bg, const wxColour& fg, const wxColour& selBg)
{
    m_bgColor = bg;
    m_fgColor = fg;
    m_selBgColor = selBg;
    
    if (m_chatTree) {
        m_chatTree->SetBackgroundColour(m_bgColor);
        m_chatTree->SetForegroundColour(m_fgColor);
        m_chatTree->Refresh();
    }
}

void ChatListWidget::SetTreeFont(const wxFont& font)
{
    m_font = font;
    
    if (m_chatTree) {
        m_chatTree->SetFont(m_font);
        m_chatTree->Refresh();
    }
}

int64_t ChatListWidget::GetChatIdFromTreeItem(const wxTreeItemId& item) const
{
    auto it = m_treeItemToChatId.find(item);
    if (it != m_treeItemToChatId.end()) {
        return it->second;
    }
    return 0;
}

wxTreeItemId ChatListWidget::GetTreeItemFromChatId(int64_t chatId) const
{
    auto it = m_chatIdToTreeItem.find(chatId);
    if (it != m_chatIdToTreeItem.end()) {
        return it->second;
    }
    return wxTreeItemId();
}

wxTreeItemId ChatListWidget::AddChatToCategory(const ChatInfo& chat)
{
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
    
    // Format title with unread count
    wxString title = FormatChatTitle(chat);
    
    // Add the item
    wxTreeItemId item = m_chatTree->AppendItem(parent, title);
    
    // Set bold if unread
    if (chat.unreadCount > 0) {
        m_chatTree->SetItemBold(item, true);
    }
    
    // Store mappings
    m_treeItemToChatId[item] = chat.id;
    m_chatIdToTreeItem[chat.id] = item;
    
    return item;
}

void ChatListWidget::UpdateChatItem(const wxTreeItemId& item, const ChatInfo& chat)
{
    if (!item.IsOk()) {
        return;
    }
    
    wxString title = FormatChatTitle(chat);
    m_chatTree->SetItemText(item, title);
    m_chatTree->SetItemBold(item, chat.unreadCount > 0);
}

wxString ChatListWidget::FormatChatTitle(const ChatInfo& chat) const
{
    wxString title = chat.title;
    
    // Append unread count if present
    if (chat.unreadCount > 0) {
        title += wxString::Format(" (%d)", chat.unreadCount);
    }
    
    return title;
}