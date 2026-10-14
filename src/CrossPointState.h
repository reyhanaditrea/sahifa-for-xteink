#pragma once
#include <ArduinoJson.h>
#include <PersistableStore.h>

#include <cstdint>
#include <string>

class CrossPointState : public PersistableStore<CrossPointState> {
  // Private constructor for singleton (see PersistableStore.h)
  CrossPointState() = default;
  ~CrossPointState() = default;

  friend class PersistableStore<CrossPointState>;

 public:
  static constexpr uint8_t SLEEP_RECENT_COUNT = 16;

  std::string openEpubPath;
  // Digest (hex app_elf_sha256) of the last OTA-rollback-invalidated image the user
  // explicitly acknowledged via OtaRollbackRecoveryActivity's "continue anyway" --
  // empty if never acknowledged. Keyed to image identity, not partition label, so a
  // brand new rollback event (different image) is never silently treated as
  // already-acknowledged. See OtaRollbackRecoveryPlan.h.
  std::string acknowledgedOtaRollbackDigestHex;
  uint16_t recentSleepImages[SLEEP_RECENT_COUNT] = {};
  uint8_t recentSleepPos = 0;
  uint8_t recentSleepFill = 0;
  uint16_t recentOverlaySleepImages[SLEEP_RECENT_COUNT] = {};
  uint8_t recentOverlaySleepPos = 0;
  uint8_t recentOverlaySleepFill = 0;
  uint8_t readerActivityLoadCount = 0;
  bool lastSleepFromReader = false;
  // Image the viewer was showing when the device went to sleep, reopened on wake.
  // Kept apart from openEpubPath: that one is the book the sleep screen draws its
  // cover and stats from, and looking at an image must not replace it.
  std::string lastSleepImagePath;
  bool showBootScreen = true;

  static const char* getFilePath() { return "/.crosspoint/state.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  bool isRecentSleep(uint16_t idx, uint8_t checkCount) const;
  bool isRecentOverlaySleep(uint16_t idx, uint8_t checkCount) const;

  void pushRecentSleep(uint16_t idx);
  void pushRecentOverlaySleep(uint16_t idx);

  bool loadFromFile();

 private:
  bool loadFromBinaryFile();
};

#define APP_STATE CrossPointState::getInstance()
