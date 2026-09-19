#pragma once

// Delivers a KOReader Sync progress upload queued by EpubReaderActivity::onExit()
// (see PendingKOReaderSync in KOReaderCredentialStore.h). Reading never keeps
// WiFi connected, so the queue is flushed wherever WiFi comes up. Uses
// KOReaderCredentialStore's own credentials against its own server.
namespace KOReaderPendingSync {

// No-op when nothing is queued or WiFi is down. A queue entry whose
// credentials were cleared since queueing is dropped rather than retried forever.
void flush();

}  // namespace KOReaderPendingSync
