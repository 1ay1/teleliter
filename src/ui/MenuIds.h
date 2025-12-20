#ifndef MENUIDS_H
#define MENUIDS_H

#include <wx/wx.h>

// Menu IDs - Telegram client menu structure
enum {
    // Teleliter menu
    ID_LOGIN = wxID_HIGHEST + 1,
    ID_LOGOUT,
    ID_RAW_LOG,
    
    // Telegram menu
    ID_NEW_CHAT,
    ID_NEW_GROUP,
    ID_NEW_CHANNEL,
    ID_CONTACTS,
    ID_SEARCH,
    ID_SAVED_MESSAGES,
    ID_UPLOAD_FILE,
    
    // Edit menu
    ID_CLEAR_WINDOW,
    ID_PREFERENCES,
    
    // View menu
    ID_SHOW_CHAT_LIST,
    ID_SHOW_MEMBERS,
    ID_SHOW_CHAT_INFO,
    ID_FULLSCREEN,
    
    // Widget IDs
    ID_CHAT_TREE,
    ID_MEMBER_LIST,
    ID_CHAT_DISPLAY,
    ID_INPUT_BOX,
    ID_CHAT_INFO_BAR
};

// Telegram chat types
enum class TelegramChatType {
    Private,
    Group,
    Supergroup,
    Channel,
    Bot,
    SavedMessages
};

#endif // MENUIDS_H