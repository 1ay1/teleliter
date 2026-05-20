#pragma once

// Tiny header carrying just the AuthStage enum. Exists to break a
// cycle: td::event::Event needs to reference model VMs (UserVM,
// ChatListItemVM, MessageVM) and model::AppModel needs to reference
// td::event::AuthStage. By pulling the enum out into its own zero-
// dependency header, view_models.hpp can include this and event.hpp
// can still include view_models.hpp.

namespace tl::td::event {

enum class AuthStage : unsigned char {
    Connecting,       // authorizationStateWaitTdlibParameters / WaitEncryptionKey
    WaitPhone,        // authorizationStateWaitPhoneNumber
    WaitCode,         // authorizationStateWaitCode
    WaitPassword,     // authorizationStateWaitPassword
    LoggedIn,         // authorizationStateReady
    LoggedOut,        // authorizationStateClosed (after logout)
};

}  // namespace tl::td::event
