#include "KOReaderPendingSync.h"

#include <WiFi.h>

#include "KOReaderCredentialStore.h"
#include "KOReaderSyncClient.h"
#include "util/DebugLog.h"
#include "util/RollingSdLog.h"

namespace KOReaderPendingSync {

void flush() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (!KOREADER_STORE.hasPendingSync()) return;
  // Credentials may have been cleared (sign-out, re-pairing) since the item was
  // queued; a stale queue entry would otherwise retry forever with NO_CREDENTIALS.
  if (!KOREADER_STORE.hasCredentials()) {
    KOREADER_STORE.clearPendingSync();
    return;
  }

  const PendingKOReaderSync& pending = *KOREADER_STORE.getPendingSync();

  KOReaderProgress progress;
  progress.document = pending.documentHash;
  progress.progress = pending.xpath;
  progress.percentage = pending.percentage;

  // Rich CrossPoint position, same as the manual sync's live upload -- see
  // KOReaderSyncActivity::performUpload().
  KOReaderRichPosition pos;
  const float pct = pending.percentage < 0.0f ? 0.0f : pending.percentage > 1.0f ? 1.0f : pending.percentage;
  pos.pctQ = static_cast<uint32_t>(pct * 1000000.0f + 0.5f);
  pos.spineIndex = pending.spineIndex;
  pos.pageNumber = pending.pageNumber;
  pos.totalPages = pending.totalPages;
  pos.paragraphIndex = pending.paragraphIndex;
  pos.xpath = pending.xpath;
  progress.position = std::move(pos);

  const auto result = KOReaderSyncClient::updateProgress(progress);
  if (result == KOReaderSyncClient::OK) {
    // Copied first: `pending` refers into the store, which clearPendingSync() resets.
    const std::string documentHash = pending.documentHash;
    KOREADER_STORE.clearPendingSync();
    RollingSdLog::append(DebugLog::PATH, "[KOSYNC] flushed queued progress (doc=" + documentHash + ")",
                         DebugLog::MAX_LINES);
  }
  // Failure (offline mid-request, server error): leave it queued for the next
  // reconnect.
}

}  // namespace KOReaderPendingSync
