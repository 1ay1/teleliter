#ifndef CHATLISTWIDGET_H
#define CHATLISTWIDGET_H

#include <wx/wx.h>
#include <wx/treectrl.h>
#include <map>
#include <vector>

// Forward declarations
class MainFrame;
struct ChatInfo;

class ChatListWidget : public wxPanel
{
public:
    ChatListWidget(wxWindow* parent, MainFrame* mainFrame);
    virtual ~ChatListWidget();
    
    // Chat management
    void RefreshChatList(const std::vector<ChatInfo>& chats);
    void ClearAllChats();
    void SelectTeleliter();
    void SelectChat(int64_t chatId);
    
    // Chat item access
    int64_t GetSelectedChatId() const;
    bool IsTeleliterSelected() const;
    
    // Styling
    void SetTreeColors(const wxColour& bg, const wxColour& fg, const wxColour& selBg);
    void SetTreeFont(const wxFont& font);
    
    // Tree item IDs
    wxTreeItemId GetTeleliterItem() const { return m_teleliterItem; }
    wxTreeItemId GetPinnedChats() const { return m_pinnedChats; }
    wxTreeItemId GetPrivateChats() const { return m_privateChats; }
    wxTreeItemId GetGroups() const { return m_groups; }
    wxTreeItemId GetChannels() const { return m_channels; }
    wxTreeItemId GetBots() const { return m_bots; }
    
    // Direct access to tree control (for event binding)
    wxTreeCtrl* GetTreeCtrl() { return m_chatTree; }
    
    // Chat ID mappings
    int64_t GetChatIdFromTreeItem(const wxTreeItemId& item) const;
    wxTreeItemId GetTreeItemFromChatId(int64_t chatId) const;
    
private:
    void CreateLayout();
    void CreateCategories();
    wxTreeItemId AddChatToCategory(const ChatInfo& chat);
    void UpdateChatItem(const wxTreeItemId& item, const ChatInfo& chat);
    wxString FormatChatTitle(const ChatInfo& chat) const;
    
    wxTreeCtrl* m_chatTree;
    
    // Tree structure
    wxTreeItemId m_treeRoot;
    wxTreeItemId m_teleliterItem;
    wxTreeItemId m_pinnedChats;
    wxTreeItemId m_privateChats;
    wxTreeItemId m_groups;
    wxTreeItemId m_channels;
    wxTreeItemId m_bots;
    
    // Chat ID mappings
    std::map<wxTreeItemId, int64_t> m_treeItemToChatId;
    std::map<int64_t, wxTreeItemId> m_chatIdToTreeItem;
    
    // Colors
    wxColour m_bgColor;
    wxColour m_fgColor;
    wxColour m_selBgColor;
    wxFont m_font;
};

#endif // CHATLISTWIDGET_H