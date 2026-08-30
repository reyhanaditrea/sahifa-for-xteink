#pragma once
#include <PersistableStore.h>

#include <cstdint>
#include <string>

#include "TbGame.h"

// Saved Textbound run plus the tally across runs. Its own file rather than a
// field in CrossPointSettings for the same reason ChessStore is: this is game
// state, not a user-configurable setting.
//
// Nothing here carries into a new run -- the game is a roguelike, so runs,
// escapes and best depth are a record, never a head start.
class TextboundStore : public PersistableStore<TextboundStore> {
 private:
  std::string savedRun;
  uint8_t runDepth = 0;  // the depth of the run in savedRun, for the title row
  uint16_t runs = 0;
  uint16_t escaped = 0;
  uint8_t bestDepth = 0;

  TextboundStore() = default;
  ~TextboundStore() = default;

  friend class PersistableStore<TextboundStore>;

 public:
  static const char* getFilePath() { return "/.crosspoint/textbound.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  bool hasSavedRun() const { return !savedRun.empty(); }
  const std::string& getSavedRun() const { return savedRun; }
  uint8_t getSavedDepth() const { return runDepth; }
  uint16_t getRuns() const { return runs; }
  uint16_t getEscaped() const { return escaped; }
  uint8_t getBestDepth() const { return bestDepth; }

  // Called after every action, so it writes only when the string actually moved.
  void saveRun(const char* serialized, uint8_t depth);
  // A finished run is not resumable: the record is updated and the save dropped
  // in one write.
  void reportRun(uint8_t depth, bool won);
  void clearSavedRun();
};

#define TEXTBOUND_STORE TextboundStore::getInstance()
