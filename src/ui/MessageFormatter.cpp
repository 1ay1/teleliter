#include "MessageFormatter.h"
#include "../telegram/Types.h"
#include <wx/datetime.h>
#include <wx/regex.h>
#include <wx/settings.h>

// Define static constants
const int MessageFormatter::GROUP_TIME_WINDOW_SECONDS;
const int MessageFormatter::DEFAULT_USERNAME_WIDTH;
const int MessageFormatter::MIN_USERNAME_WIDTH;
const int MessageFormatter::MAX_USERNAME_WIDTH;

MessageFormatter::MessageFormatter(ChatArea *chatArea)
    : m_chatArea(chatArea ? chatArea : nullptr), m_lastMediaSpanStart(0),
      m_lastMediaSpanEnd(0), m_lastStatusMarkerStart(-1),
      m_lastStatusMarkerEnd(-1), m_unreadMarkerStart(-1), m_unreadMarkerEnd(-1),
      m_lastTimestamp(0), m_lastDateDay(0),
      m_usernameWidth(DEFAULT_USERNAME_WIDTH), m_typingIndicatorStart(-1),
      m_typingIndicatorEnd(-1) {
  // Additional colors not provided by ChatArea - use system colors
  m_mediaColor = wxSystemSettings::GetColour(wxSYS_COLOUR_HOTLIGHT);
  m_editedColor = wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT);
  m_forwardColor = wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT);
  m_replyColor = wxSystemSettings::GetColour(wxSYS_COLOUR_HOTLIGHT);
  m_highlightColor = wxSystemSettings::GetColour(wxSYS_COLOUR_HOTLIGHT);
}

void MessageFormatter::ResetGroupingState() {
  m_lastSender.Clear();
  m_lastTimestamp = 0;
  m_lastDateDay = 0;
  m_typingIndicatorStart = -1;
  m_typingIndicatorEnd = -1;
}

wxString MessageFormatter::GetMediaEmoji(MediaType type) {
  switch (type) {
  case MediaType::Photo:
    return wxString::FromUTF8("📷");
  case MediaType::Video:
    return wxString::FromUTF8("🎬");
  case MediaType::Sticker:
    return wxString::FromUTF8("🏷️");
  case MediaType::GIF:
    return wxString::FromUTF8("🎞️");
  case MediaType::Voice:
    return wxString::FromUTF8("🎤");
  case MediaType::VideoNote:
    return wxString::FromUTF8("🎥");
  case MediaType::File:
    return wxString::FromUTF8("📎");
  default:
    return wxString::FromUTF8("📁");
  }
}

wxString MessageFormatter::GenerateAsciiWaveform(
    const std::vector<uint8_t> &waveformData, int targetLength) {
  if (waveformData.empty() || targetLength <= 0) {
    return wxString();
  }

  // TDLib waveform: 5-bit values (0-31) packed into bytes
  // Each byte contains bits for multiple samples
  // We need to unpack them first
  std::vector<int> samples;

  int bitPos = 0;
  size_t byteIdx = 0;

  while (byteIdx < waveformData.size()) {
    // Extract 5-bit value starting at bitPos
    int value = 0;
    int bitsRemaining = 5;
    int shift = 0;

    while (bitsRemaining > 0 && byteIdx < waveformData.size()) {
      int bitsInCurrentByte = 8 - bitPos;
      int bitsToTake = std::min(bitsRemaining, bitsInCurrentByte);

      int mask = (1 << bitsToTake) - 1;
      int extracted = (waveformData[byteIdx] >> bitPos) & mask;
      value |= (extracted << shift);

      shift += bitsToTake;
      bitsRemaining -= bitsToTake;
      bitPos += bitsToTake;

      if (bitPos >= 8) {
        bitPos = 0;
        byteIdx++;
      }
    }

    samples.push_back(value);
  }

  if (samples.empty()) {
    return wxString();
  }

  // Resample to target length
  std::vector<int> resampled(targetLength);
  for (int i = 0; i < targetLength; i++) {
    // Map target index to source index
    size_t srcIdx = (i * samples.size()) / targetLength;
    if (srcIdx >= samples.size())
      srcIdx = samples.size() - 1;
    resampled[i] = samples[srcIdx];
  }

  // Unicode block characters for waveform visualization
  // These represent different heights: ▁▂▃▄▅▆▇█
  static const wchar_t *blocks[] = {L"▁", L"▂", L"▃", L"▄",
                                    L"▅", L"▆", L"▇", L"█"};

  wxString result;
  for (int val : resampled) {
    // Map 0-31 to 0-7 (8 levels)
    int level = (val * 7) / 31;
    if (level < 0)
      level = 0;
    if (level > 7)
      level = 7;
    result += wxString(blocks[level]);
  }

  return result;
}

void MessageFormatter::CalculateUsernameWidth(
    const std::vector<wxString> &usernames) {
  int maxLen = MIN_USERNAME_WIDTH;
  for (const auto &name : usernames) {
    int len = static_cast<int>(name.length());
    if (len > maxLen)
      maxLen = len;
  }
  m_usernameWidth = std::min(maxLen, MAX_USERNAME_WIDTH);
}

bool MessageFormatter::NeedsDateSeparator(int64_t timestamp) const {
  if (m_lastDateDay == 0)
    return false;
  if (timestamp <= 0)
    return false;

  time_t t = static_cast<time_t>(timestamp);
  wxDateTime dt(t);
  // Use local calendar day (YYYYMMDD) instead of Julian Day (which switches at
  // noon UTC)
  long newDay = dt.GetYear() * 10000 + (dt.GetMonth() + 1) * 100 + dt.GetDay();

  return newDay != m_lastDateDay;
}

void MessageFormatter::WriteAlignedUsername(const wxString &sender) {
  // Right-align username within fixed width column
  wxString displayName = sender;
  if (displayName.length() > static_cast<size_t>(m_usernameWidth)) {
    displayName =
        displayName.Left(m_usernameWidth - 1) + wxString::FromUTF8("…");
  }

  // Pad on the left to right-align
  int padding = m_usernameWidth - static_cast<int>(displayName.length());
  if (padding > 0) {
    m_chatArea->WriteText(wxString(' ', padding));
  }

  wxColour userColor = m_chatArea->GetUserColor(sender);
  m_chatArea->BeginTextColour(userColor);
  m_chatArea->WriteText("<");
  
  // Track sender span start position (after the "<")
  long senderSpanStart = m_chatArea->GetLastPosition();
  
  m_chatArea->BeginBold();
  m_chatArea->WriteText(displayName);
  m_chatArea->EndBold();
  
  // Track sender span end position (before the ">")
  long senderSpanEnd = m_chatArea->GetLastPosition();
  
  // Notify callback about sender span if set
  if (m_senderSpanCallback && m_currentSenderId != 0) {
    m_senderSpanCallback(senderSpanStart, senderSpanEnd, m_currentSenderId, sender);
  }
  
  m_chatArea->WriteText("> ");
  m_chatArea->EndTextColour();
}

void MessageFormatter::WriteStatusSuffix(MessageStatus status,
                                         bool statusHighlight) {
  // Reset marker positions
  m_lastStatusMarkerStart = -1;
  m_lastStatusMarkerEnd = -1;

  if (status == MessageStatus::None)
    return;

  m_chatArea->WriteText(" ");

  // Record position before writing the marker
  long markerStart = m_chatArea->GetLastPosition();

  switch (status) {
  case MessageStatus::Sending:
    m_chatArea->BeginTextColour(m_chatArea->GetTimestampColor());
    m_chatArea->WriteText("..");
    m_chatArea->EndTextColour();
    break;
  case MessageStatus::Sent:
    m_chatArea->BeginTextColour(m_chatArea->GetSentColor());
    m_chatArea->WriteText(wxString::FromUTF8("✓"));
    m_chatArea->EndTextColour();
    break;
  case MessageStatus::Read:
    if (statusHighlight) {
      m_chatArea->BeginTextColour(m_chatArea->GetReadHighlightColor());
    } else {
      m_chatArea->BeginTextColour(m_chatArea->GetReadColor());
    }
    m_chatArea->WriteText(wxString::FromUTF8("✓✓"));
    m_chatArea->EndTextColour();
    // Record read marker position for tooltip
    m_lastStatusMarkerStart = markerStart;
    m_lastStatusMarkerEnd = m_chatArea->GetLastPosition();
    break;
  default:
    break;
  }
}

void MessageFormatter::WriteContinuationPrefix() {
  // Indent to align with message text (after timestamp and username)
  // Format: [HH:MM:SS] <username> message
  // Timestamp: 11 chars, username area: m_usernameWidth + 3 for "< >"
  int totalIndent = 11 + m_usernameWidth + 3;
  m_chatArea->WriteText(wxString(' ', totalIndent));
}

void MessageFormatter::WriteTextWithLineHandling(const wxString &text,
                                                 MessageStatus status,
                                                 bool statusHighlight) {
  // Simply write text (including any newlines) and add ticks at the end
  // No special multi-line handling - just show message as-is with ticks at end

  if (text.IsEmpty())
    return;

  WriteTextWithLinks(text);
  m_chatArea->EndTextColour();
  WriteStatusSuffix(status, statusHighlight);
  m_chatArea->BeginTextColour(m_chatArea->GetFgColor());
}

void MessageFormatter::SetLastMessage(const wxString &sender,
                                      int64_t timestamp) {
  m_lastSender = sender;
  m_lastTimestamp = timestamp;
  if (timestamp > 0) {
    time_t t = static_cast<time_t>(timestamp);
    wxDateTime dt(t);
    m_lastDateDay =
        dt.GetYear() * 10000 + (dt.GetMonth() + 1) * 100 + dt.GetDay();
  }
}

bool MessageFormatter::ShouldGroupWithPrevious(const wxString &sender,
                                               int64_t timestamp) const {
  if (m_lastSender.IsEmpty() || sender.IsEmpty()) {
    return false;
  }
  if (m_lastSender != sender) {
    return false;
  }
  if (m_lastTimestamp == 0 || timestamp == 0) {
    return false;
  }
  // Don't group across date boundaries
  if (!IsSameDay(m_lastTimestamp, timestamp)) {
    return false;
  }
  // Group if within time window
  int64_t diff = timestamp - m_lastTimestamp;
  return diff >= 0 && diff <= GROUP_TIME_WINDOW_SECONDS;
}

bool MessageFormatter::IsSameDay(int64_t time1, int64_t time2) {
  if (time1 <= 0 || time2 <= 0)
    return false;
  wxDateTime dt1(static_cast<time_t>(time1));
  wxDateTime dt2(static_cast<time_t>(time2));
  long day1 = dt1.GetYear() * 10000 + (dt1.GetMonth() + 1) * 100 + dt1.GetDay();
  long day2 = dt2.GetYear() * 10000 + (dt2.GetMonth() + 1) * 100 + dt2.GetDay();
  return day1 == day2;
}

wxString MessageFormatter::GetDateString(int64_t unixTime) {
  if (unixTime <= 0)
    return wxString();

  time_t t = static_cast<time_t>(unixTime);
  wxDateTime dt(t);
  wxDateTime now = wxDateTime::Now();
  wxDateTime today = now.GetDateOnly();
  wxDateTime yesterday = today - wxDateSpan::Day();
  wxDateTime msgDate = dt.GetDateOnly();

  if (msgDate == today) {
    return "Today";
  } else if (msgDate == yesterday) {
    return "Yesterday";
  } else if (msgDate > today - wxDateSpan::Week()) {
    return dt.Format("%A"); // Day name (Monday, Tuesday, etc.)
  } else if (dt.GetYear() == now.GetYear()) {
    return dt.Format("%A, %B %d"); // "Monday, January 15"
  } else {
    return dt.Format("%A, %B %d, %Y"); // "Monday, January 15, 2024"
  }
}

void MessageFormatter::AppendDateSeparator(const wxString &dateText) {
  if (!m_chatArea)
    return;

  m_chatArea->ResetStyles();

  // Date separator with box-drawing characters for HexChat style
  // ─────────────────── January 15, 2025 ───────────────────
  wxString separator = wxString::FromUTF8("───────────────────");

  m_chatArea->BeginTextColour(
      wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
  m_chatArea->WriteText(separator + " " + dateText + " " + separator + "\n");
  m_chatArea->EndTextColour();

  m_chatArea->ResetStyles();
}

void MessageFormatter::AppendDateSeparatorForTime(int64_t unixTime) {
  wxString dateStr = GetDateString(unixTime);
  if (!dateStr.IsEmpty()) {
    AppendDateSeparator(dateStr);
  }

  // Update last date day
  if (unixTime > 0) {
    time_t t = static_cast<time_t>(unixTime);
    wxDateTime dt(t);
    m_lastDateDay =
        dt.GetYear() * 10000 + (dt.GetMonth() + 1) * 100 + dt.GetDay();
  }
}

void MessageFormatter::AppendContinuationMessage(const wxString &timestamp,
                                                 const wxString &message,
                                                 MessageStatus status,
                                                 bool statusHighlight) {
  if (!m_chatArea)
    return;

  m_chatArea->ResetStyles();

  // Write continuation prefix with proper alignment
  WriteContinuationPrefix();

  m_chatArea->BeginTextColour(m_chatArea->GetFgColor());
  WriteTextWithLinks(message);
  m_chatArea->EndTextColour();

  // Append status ticks at the end
  WriteStatusSuffix(status, statusHighlight);

  m_chatArea->ResetStyles();
  m_chatArea->WriteText("\n");
}

void MessageFormatter::AppendContinuationMediaMessage(const wxString &timestamp,
                                                      const MediaInfo &media,
                                                      const wxString &caption,
                                                      MessageStatus status,
                                                      bool statusHighlight) {
  if (!m_chatArea)
    return;

  m_chatArea->ResetStyles();

  // Write continuation prefix
  WriteContinuationPrefix();

  m_lastMediaSpanStart = m_chatArea->GetLastPosition();

  // Media indicator with emoji
  wxString emoji = GetMediaEmoji(media.type);
  m_chatArea->WriteText(emoji + " ");

  m_chatArea->BeginTextColour(m_mediaColor);
  m_chatArea->BeginUnderline();

  wxString mediaLabel;
  switch (media.type) {
  case MediaType::Photo:
    mediaLabel = "[Photo]";
    break;
  case MediaType::Video:
    mediaLabel = "[Video]";
    break;
  case MediaType::Sticker:
    mediaLabel = "[Sticker]";
    break;
  case MediaType::GIF:
    mediaLabel = "[GIF]";
    break;
  case MediaType::Voice:
    mediaLabel = "[Voice]";
    break;
  case MediaType::VideoNote:
    mediaLabel = "[Video Message]";
    break;
  case MediaType::File:
    mediaLabel = "[File: " + media.fileName + "]";
    break;
  default:
    mediaLabel = "[Media]";
    break;
  }

  m_chatArea->WriteText(mediaLabel);
  m_chatArea->EndUnderline();
  m_chatArea->EndTextColour();

  if (media.type == MediaType::Sticker && !media.emoji.IsEmpty()) {
    m_chatArea->WriteText(" " + media.emoji);
  }

  m_lastMediaSpanEnd = m_chatArea->GetLastPosition();

  if (!caption.IsEmpty() && media.type != MediaType::Sticker) {
    m_chatArea->BeginTextColour(m_chatArea->GetFgColor());
    m_chatArea->WriteText(" " + caption);
    m_chatArea->EndTextColour();
  }

  m_chatArea->ResetStyles();
  m_chatArea->WriteText("\n");
}

void MessageFormatter::AppendTypingIndicator(const wxString &username) {
  if (!m_chatArea)
    return;

  // Remove existing typing indicator first
  RemoveTypingIndicator();

  m_typingIndicatorStart = m_chatArea->GetLastPosition();

  m_chatArea->ResetStyles();
  m_chatArea->BeginTextColour(m_chatArea->GetTimestampColor());
  m_chatArea->BeginItalic();
  m_chatArea->WriteText("           * " + username + " is typing...\n");
  m_chatArea->EndItalic();
  m_chatArea->EndTextColour();

  m_typingIndicatorEnd = m_chatArea->GetLastPosition();
}

void MessageFormatter::RemoveTypingIndicator() {
  if (!m_chatArea || m_typingIndicatorStart < 0 || m_typingIndicatorEnd < 0)
    return;

  wxRichTextCtrl *display = m_chatArea->GetDisplay();
  if (!display)
    return;

  display->Remove(m_typingIndicatorStart, m_typingIndicatorEnd);

  m_typingIndicatorStart = -1;
  m_typingIndicatorEnd = -1;
}

void MessageFormatter::AppendReactions(
    const std::map<wxString, std::vector<wxString>> &reactions) {
  if (!m_chatArea || reactions.empty())
    return;

  m_chatArea->ResetStyles();

  // Indent to align with message content
  // Timestamp is [HH:MM:SS] = 11 chars, username column = m_usernameWidth + 3
  wxString indent = "           ";              // Timestamp width (11 chars)
  indent += wxString(' ', m_usernameWidth + 3); // Username column

  m_chatArea->BeginTextColour(m_chatArea->GetTimestampColor());
  m_chatArea->WriteText(indent);
  m_chatArea->WriteText(wxString::FromUTF8("╰ "));
  m_chatArea->EndTextColour();

  bool first = true;
  for (const auto &[emoji, users] : reactions) {
    if (!first) {
      m_chatArea->BeginTextColour(m_chatArea->GetTimestampColor());
      m_chatArea->WriteText(" · ");
      m_chatArea->EndTextColour();
    }
    first = false;

    m_chatArea->WriteText(emoji);

    // Show count or usernames
    m_chatArea->BeginTextColour(m_chatArea->GetTimestampColor());
    if (users.size() == 1) {
      m_chatArea->WriteText(" " + users[0]);
    } else {
      m_chatArea->WriteText(wxString::Format(" %zu", users.size()));
    }
    m_chatArea->EndTextColour();
  }

  m_chatArea->ResetStyles();
  m_chatArea->WriteText("\n");
}

void MessageFormatter::WriteTextWithLinks(const wxString &text) {
  if (!m_chatArea)
    return;

  // Handle empty text
  if (text.IsEmpty())
    return;

  // Calculate continuation indent for multiline messages
  // Format: [HH:MM:SS] <username> message
  // Timestamp: 11 chars, username area: m_usernameWidth + 3 for "< >"
  int continuationIndent = 11 + m_usernameWidth + 3;
  wxString indentStr = wxString(' ', continuationIndent);

  // Helper lambda to write plain text with continuation line indentation
  auto writeTextWithIndent = [this, &indentStr](const wxString &chunk) {
    if (chunk.IsEmpty())
      return;

    // Split by newlines and add indentation for continuation lines
    wxArrayString lines = wxSplit(chunk, '\n');
    for (size_t i = 0; i < lines.size(); ++i) {
      if (i > 0) {
        // This is a continuation line - add newline and indent
        m_chatArea->WriteText("\n");
        m_chatArea->WriteText(indentStr);
      }
      m_chatArea->WriteText(lines[i]);
    }
  };

  // FAST MODE: Skip expensive URL detection during bulk loading
  if (m_fastMode) {
    writeTextWithIndent(text);
    return;
  }

  // URL regex pattern - matches http://, https://, and www. URLs
  static wxRegEx urlRegex(
      "(https?://[^\\s<>\"'\\)\\]]+|www\\.[^\\s<>\"'\\)\\]]+)",
      wxRE_EXTENDED | wxRE_ICASE);

  if (!urlRegex.IsValid()) {
    // Fallback - just write plain text with indent handling
    writeTextWithIndent(text);
    return;
  }

  wxString remaining = text;

  while (urlRegex.Matches(remaining)) {
    size_t matchStart, matchLen;
    if (!urlRegex.GetMatch(&matchStart, &matchLen, 0)) {
      break;
    }

    // Write text before the link (with indent handling)
    if (matchStart > 0) {
      writeTextWithIndent(remaining.Left(matchStart));
    }

    // Extract the URL
    wxString url = remaining.Mid(matchStart, matchLen);

    // Track link span start
    long linkStart = m_chatArea->GetLastPosition();

    // Write the link with special formatting
    m_chatArea->BeginTextColour(m_chatArea->GetLinkColor());
    m_chatArea->BeginUnderline();
    m_chatArea->WriteText(url);
    m_chatArea->EndUnderline();
    m_chatArea->EndTextColour();

    // Track link span end
    long linkEnd = m_chatArea->GetLastPosition();

    // Notify callback about the link span
    if (m_linkSpanCallback) {
      // Prepend https:// to www. URLs
      wxString fullUrl = url;
      if (url.Lower().StartsWith("www.")) {
        fullUrl = "https://" + url;
      }
      m_linkSpanCallback(linkStart, linkEnd, fullUrl);
    }

    // Continue with remaining text
    remaining = remaining.Mid(matchStart + matchLen);
  }

  // Write any remaining text (with indent handling)
  if (!remaining.IsEmpty()) {
    writeTextWithIndent(remaining);
  }
}

void MessageFormatter::AppendMessage(const wxString &timestamp,
                                     const wxString &sender,
                                     const wxString &message,
                                     MessageStatus status,
                                     bool statusHighlight) {
  if (!m_chatArea)
    return;

  // Reset styles at start to prevent any leaking from previous operations
  m_chatArea->ResetStyles();

  // [HH:MM:SS] <username> message ✓✓
  m_chatArea->WriteTimestamp(timestamp);

  // Write username
  WriteAlignedUsername(sender);

  // Write message text (may contain newlines) then ticks at end
  m_chatArea->BeginTextColour(m_chatArea->GetFgColor());
  WriteTextWithLinks(message);
  m_chatArea->EndTextColour();
  WriteStatusSuffix(status, statusHighlight);

  // Always write newline with reset styles to ensure it's not swallowed
  m_chatArea->ResetStyles();
  m_chatArea->WriteText("\n");
}

void MessageFormatter::AppendServiceMessage(const wxString &timestamp,
                                            const wxString &message) {
  if (!m_chatArea)
    return;

  // Handle multiline messages - first line gets timestamp and arrow,
  // continuation lines get aligned padding
  wxArrayString lines = wxSplit(message, '\n');

  for (size_t i = 0; i < lines.size(); ++i) {
    m_chatArea->ResetStyles();

    if (i == 0) {
      // First line: timestamp + padding + arrow
      m_chatArea->WriteTimestamp(timestamp);

      int padding = m_usernameWidth - 3; // Account for arrow width
      if (padding > 0) {
        m_chatArea->WriteText(wxString(' ', padding));
      }

      m_chatArea->BeginTextColour(m_chatArea->GetServiceColor());
      m_chatArea->WriteText(wxString::FromUTF8("——▶ ") + lines[i]);
      m_chatArea->EndTextColour();
    } else {
      // Continuation lines: aligned padding (timestamp width + username width)
      // Timestamp is typically 10 chars "[HH:MM:SS]" + space
      int timestampWidth = 11;
      int totalPadding =
          timestampWidth + m_usernameWidth + 1; // +1 for arrow spacing
      m_chatArea->WriteText(wxString(' ', totalPadding));

      m_chatArea->BeginTextColour(m_chatArea->GetServiceColor());
      m_chatArea->WriteText(lines[i]);
      m_chatArea->EndTextColour();
    }

    m_chatArea->ResetStyles();
    m_chatArea->WriteText("\n");
  }
}

void MessageFormatter::AppendActionMessage(const wxString &timestamp,
                                           const wxString &sender,
                                           const wxString &action,
                                           MessageStatus status,
                                           bool statusHighlight) {
  if (!m_chatArea)
    return;

  m_chatArea->ResetStyles();
  m_chatArea->WriteTimestamp(timestamp, status, statusHighlight);
  m_chatArea->BeginTextColour(m_chatArea->GetActionColor());
  m_chatArea->WriteText("* ");
  m_chatArea->BeginBold();
  m_chatArea->WriteText(sender);
  m_chatArea->EndBold();
  m_chatArea->WriteText(" ");
  WriteTextWithLinks(action);
  m_chatArea->EndTextColour();
  m_chatArea->ResetStyles();
  m_chatArea->WriteText("\n");
}

void MessageFormatter::AppendNoticeMessage(const wxString &timestamp,
                                           const wxString &source,
                                           const wxString &message) {
  if (!m_chatArea)
    return;

  // HexChat-style notice: [HH:MM] -source- message
  m_chatArea->WriteTimestamp(timestamp);

  m_chatArea->BeginTextColour(m_chatArea->GetServiceColor());
  m_chatArea->WriteText("-" + source + "- " + message + "\n");
  m_chatArea->EndTextColour();
}

void MessageFormatter::AppendHighlightMessage(const wxString &timestamp,
                                              const wxString &sender,
                                              const wxString &message,
                                              MessageStatus status,
                                              bool statusHighlight) {
  if (!m_chatArea)
    return;

  m_chatArea->ResetStyles();

  // Highlighted message (when you are mentioned)
  m_chatArea->WriteTimestamp(timestamp);

  // Write username
  WriteAlignedUsername(sender);

  m_chatArea->BeginTextColour(m_highlightColor);
  m_chatArea->BeginBold();
  WriteTextWithLinks(message);
  m_chatArea->EndBold();
  m_chatArea->EndTextColour();
  WriteStatusSuffix(status, statusHighlight);

  m_chatArea->ResetStyles();
  m_chatArea->WriteText("\n");
}

void MessageFormatter::AppendUserJoinedMessage(const wxString &timestamp,
                                               const wxString &user) {
  if (!m_chatArea)
    return;

  m_chatArea->ResetStyles();
  m_chatArea->WriteTimestamp(timestamp);

  // Align with username column
  int padding = m_usernameWidth - 3;
  if (padding > 0) {
    m_chatArea->WriteText(wxString(' ', padding));
  }

  m_chatArea->BeginTextColour(m_chatArea->GetSuccessColor());
  m_chatArea->WriteText(wxString::FromUTF8("——▶ "));
  m_chatArea->EndTextColour();
  m_chatArea->BeginTextColour(m_chatArea->GetFgColor());
  m_chatArea->BeginBold();
  m_chatArea->WriteText(user);
  m_chatArea->EndBold();
  m_chatArea->WriteText(" joined");
  m_chatArea->EndTextColour();
  m_chatArea->ResetStyles();
  m_chatArea->WriteText("\n");
}

void MessageFormatter::AppendUserLeftMessage(const wxString &timestamp,
                                             const wxString &user) {
  if (!m_chatArea)
    return;

  m_chatArea->ResetStyles();
  m_chatArea->WriteTimestamp(timestamp);

  // Align with username column
  int padding = m_usernameWidth - 3;
  if (padding > 0) {
    m_chatArea->WriteText(wxString(' ', padding));
  }

  m_chatArea->BeginTextColour(m_chatArea->GetServiceColor());
  m_chatArea->WriteText(wxString::FromUTF8("◀—— "));
  m_chatArea->EndTextColour();
  m_chatArea->BeginTextColour(m_chatArea->GetFgColor());
  m_chatArea->BeginBold();
  m_chatArea->WriteText(user);
  m_chatArea->EndBold();
  m_chatArea->WriteText(" left");
  m_chatArea->EndTextColour();
  m_chatArea->ResetStyles();
  m_chatArea->WriteText("\n");
}

void MessageFormatter::AppendMediaMessage(
    const wxString &timestamp, const wxString &sender, const MediaInfo &media,
    const wxString &caption, MessageStatus status, bool statusHighlight) {
  if (!m_chatArea)
    return;

  m_chatArea->ResetStyles();
  m_chatArea->WriteTimestamp(timestamp);

  // Write username
  WriteAlignedUsername(sender);

  // Media emoji indicator
  wxString emoji = GetMediaEmoji(media.type);
  m_chatArea->WriteText(emoji + " ");

  m_lastMediaSpanStart = m_chatArea->GetLastPosition();

  m_chatArea->BeginTextColour(m_mediaColor);
  m_chatArea->BeginUnderline();

  wxString mediaLabel;
  switch (media.type) {
  case MediaType::Photo:
    mediaLabel = "[Photo]";
    break;
  case MediaType::Video:
    if (media.duration > 0) {
      int mins = media.duration / 60;
      int secs = media.duration % 60;
      mediaLabel = wxString::Format("[Video %d:%02d]", mins, secs);
    } else {
      mediaLabel = "[Video]";
    }
    break;
  case MediaType::Sticker:
    mediaLabel = "[Sticker]";
    break;
  case MediaType::GIF:
    mediaLabel = "[GIF]";
    break;
  case MediaType::Voice: {
    // Format duration as M:SS
    int mins = media.duration / 60;
    int secs = media.duration % 60;
    wxString durationStr = wxString::Format("%d:%02d", mins, secs);

    // Generate ASCII waveform from waveform data
    wxString waveformStr = GenerateAsciiWaveform(media.waveform, 20);

    if (!waveformStr.IsEmpty()) {
      mediaLabel = wxString::Format("[Voice %s]  %s", durationStr, waveformStr);
    } else {
      mediaLabel = wxString::Format("[Voice %s]", durationStr);
    }
  } break;
  case MediaType::VideoNote:
    if (media.duration > 0) {
      int mins = media.duration / 60;
      int secs = media.duration % 60;
      mediaLabel = wxString::Format("[Video Message %d:%02d]", mins, secs);
    } else {
      mediaLabel = "[Video Message]";
    }
    break;
  case MediaType::File:
    mediaLabel = "[File: " + media.fileName + "]";
    break;
  default:
    mediaLabel = "[Media]";
    break;
  }

  m_chatArea->WriteText(mediaLabel);
  m_chatArea->EndUnderline();
  m_chatArea->EndTextColour();

  // For stickers, show emoji after the underlined text (emoji doesn't underline
  // well)
  if (media.type == MediaType::Sticker && !media.emoji.IsEmpty()) {
    m_chatArea->WriteText(" " + media.emoji);
  }

  m_lastMediaSpanEnd = m_chatArea->GetLastPosition();

  if (!caption.IsEmpty() && media.type != MediaType::Sticker) {
    // Don't repeat caption for stickers since emoji is already shown
    m_chatArea->WriteText(" ");
    m_chatArea->BeginTextColour(m_chatArea->GetFgColor());
    WriteTextWithLinks(caption);
    m_chatArea->EndTextColour();
  }

  // Add status ticks for outgoing messages
  WriteStatusSuffix(status, statusHighlight);

  m_chatArea->ResetStyles();
  m_chatArea->WriteText("\n");
}

void MessageFormatter::AppendReplyMessage(
    const wxString &timestamp, const wxString &sender, const wxString &replyTo,
    const wxString &message, MessageStatus status, bool statusHighlight) {
  if (!m_chatArea)
    return;

  m_chatArea->ResetStyles();
  m_chatArea->WriteTimestamp(timestamp);

  // Write username
  WriteAlignedUsername(sender);

  // Write the message first
  m_chatArea->BeginTextColour(m_chatArea->GetFgColor());
  WriteTextWithLinks(message);
  m_chatArea->EndTextColour();
  WriteStatusSuffix(status, statusHighlight);

  m_chatArea->ResetStyles();
  m_chatArea->WriteText("\n");

  // Reply context on next line with arrow indicator
  wxString indent = "           ";              // Timestamp width
  indent += wxString(' ', m_usernameWidth + 3); // Username column

  m_chatArea->WriteText(indent);
  m_chatArea->BeginTextColour(m_replyColor);
  m_chatArea->WriteText(wxString::FromUTF8("↳ re: \""));

  // Truncate long reply text
  wxString truncatedReply = replyTo;
  if (truncatedReply.length() > 40) {
    truncatedReply = truncatedReply.Left(37) + "...";
  }
  m_chatArea->BeginItalic();
  m_chatArea->WriteText(truncatedReply);
  m_chatArea->EndItalic();
  m_chatArea->WriteText("\"");
  m_chatArea->EndTextColour();

  m_chatArea->ResetStyles();
  m_chatArea->WriteText("\n");
}

void MessageFormatter::AppendForwardMessage(const wxString &timestamp,
                                            const wxString &sender,
                                            const wxString &forwardFrom,
                                            const wxString &message,
                                            MessageStatus status,
                                            bool statusHighlight) {
  if (!m_chatArea)
    return;

  m_chatArea->ResetStyles();
  m_chatArea->WriteTimestamp(timestamp);

  // Write username
  WriteAlignedUsername(sender);

  // Forward indicator
  m_chatArea->BeginTextColour(m_forwardColor);
  m_chatArea->BeginItalic();
  m_chatArea->WriteText(wxString::FromUTF8("⤴ Fwd from "));
  m_chatArea->BeginBold();
  m_chatArea->WriteText(forwardFrom);
  m_chatArea->EndBold();
  m_chatArea->WriteText(": ");
  m_chatArea->EndItalic();
  m_chatArea->EndTextColour();

  // Message on same line
  m_chatArea->BeginTextColour(m_chatArea->GetFgColor());
  WriteTextWithLinks(message);
  m_chatArea->EndTextColour();
  WriteStatusSuffix(status, statusHighlight);

  m_chatArea->ResetStyles();
  m_chatArea->WriteText("\n");
}

void MessageFormatter::AppendUnreadMarker() {
  if (!m_chatArea)
    return;

  // Track start position of marker
  m_unreadMarkerStart = m_chatArea->GetLastPosition();

  // Ensure we start on a new line
  m_chatArea->WriteText("\n");

  // Visual marker showing where messages have been read up to
  // Use system highlight color for the marker
  wxColour markerColor = wxSystemSettings::GetColour(wxSYS_COLOUR_HOTLIGHT);
  m_chatArea->BeginTextColour(markerColor);
  m_chatArea->WriteText("----------------------- ");
  m_chatArea->EndTextColour();

  m_chatArea->BeginTextColour(markerColor);
  m_chatArea->BeginBold();
  m_chatArea->WriteText("<- read up to here");
  m_chatArea->EndBold();
  m_chatArea->EndTextColour();

  m_chatArea->BeginTextColour(markerColor);
  m_chatArea->WriteText(" -----------------------\n");
  m_chatArea->EndTextColour();

  // Track end position of marker
  m_unreadMarkerEnd = m_chatArea->GetLastPosition();
}

void MessageFormatter::RemoveUnreadMarker() {
  if (!m_chatArea || m_unreadMarkerStart < 0 || m_unreadMarkerEnd < 0)
    return;

  wxRichTextCtrl *display = m_chatArea->GetDisplay();
  if (!display)
    return;

  // Delete the marker text range
  display->Remove(m_unreadMarkerStart, m_unreadMarkerEnd);

  // Reset marker tracking
  m_unreadMarkerStart = -1;
  m_unreadMarkerEnd = -1;
}

void MessageFormatter::AppendEditedMessage(
    const wxString &timestamp, const wxString &sender, const wxString &message,
    long *editSpanStart, long *editSpanEnd, MessageStatus status,
    bool statusHighlight) {
  if (!m_chatArea)
    return;

  m_chatArea->ResetStyles();
  m_chatArea->WriteTimestamp(timestamp);

  // Write username
  WriteAlignedUsername(sender);

  m_chatArea->BeginTextColour(m_chatArea->GetFgColor());
  WriteTextWithLinks(message);
  m_chatArea->EndTextColour();

  // Subtle (edited) marker in gray, before status ticks
  m_chatArea->BeginTextColour(m_chatArea->GetTimestampColor());
  m_chatArea->WriteText(" (edited)");
  m_chatArea->EndTextColour();

  WriteStatusSuffix(status, statusHighlight);

  m_chatArea->ResetStyles();
  m_chatArea->WriteText("\n");

  // No span tracking - TDLib doesn't provide original message text
  if (editSpanStart)
    *editSpanStart = 0;
  if (editSpanEnd)
    *editSpanEnd = 0;
}

void MessageFormatter::DisplayMessage(const MessageInfo &msg,
                                      const wxString &timestamp) {
  if (!m_chatArea)
    return;

  if (msg.isForwarded && !msg.forwardedFrom.IsEmpty()) {
    AppendForwardMessage(timestamp, msg.senderName, msg.forwardedFrom,
                         msg.text);
  } else if (msg.replyToMessageId != 0 && !msg.replyToText.IsEmpty()) {
    AppendReplyMessage(timestamp, msg.senderName, msg.replyToText, msg.text);
  } else if (msg.isEdited) {
    AppendEditedMessage(timestamp, msg.senderName, msg.text);
  } else if (msg.hasPhoto || msg.hasVideo || msg.hasDocument || msg.hasVoice ||
             msg.hasVideoNote || msg.hasSticker || msg.hasAnimation) {
    MediaInfo media;
    if (msg.hasPhoto) {
      media.type = MediaType::Photo;
    } else if (msg.hasVideo) {
      media.type = MediaType::Video;
    } else if (msg.hasDocument) {
      media.type = MediaType::File;
      media.fileName = msg.mediaFileName;
    } else if (msg.hasVoice) {
      media.type = MediaType::Voice;
    } else if (msg.hasVideoNote) {
      media.type = MediaType::VideoNote;
    } else if (msg.hasSticker) {
      media.type = MediaType::Sticker;
    } else if (msg.hasAnimation) {
      media.type = MediaType::GIF;
    }
    media.localPath = msg.mediaLocalPath;
    AppendMediaMessage(timestamp, msg.senderName, media, msg.mediaCaption);
  } else {
    AppendMessage(timestamp, msg.senderName, msg.text);
  }
}