#include "TextboundStore.h"

void TextboundStore::toJson(JsonDocument& doc) const {
  doc["run"] = savedRun;
  doc["runs"] = runs;
  doc["escaped"] = escaped;
  doc["bestDepth"] = bestDepth;
}

bool TextboundStore::fromJson(JsonVariantConst doc) {
  // const char* rather than std::string: ArduinoJson's std::string converter
  // pulls a copy of the serializer into this TU (see PersistableStore's note).
  savedRun = doc["run"] | "";
  if (savedRun.size() > tb::Game::MAX_SAVE_LENGTH) savedRun.clear();
  runs = doc["runs"] | 0;
  escaped = doc["escaped"] | 0;
  bestDepth = doc["bestDepth"] | 0;
  if (bestDepth > tb::MAX_DEPTH) bestDepth = tb::MAX_DEPTH;
  return true;
}

void TextboundStore::saveRun(const char* serialized) {
  const char* text = (serialized == nullptr) ? "" : serialized;
  if (savedRun == text) return;
  savedRun = text;
  if (savedRun.size() > tb::Game::MAX_SAVE_LENGTH) savedRun.clear();
  saveToFile();
}

void TextboundStore::reportRun(uint8_t depth, bool won) {
  savedRun.clear();
  if (runs < 0xFFFF) runs++;
  if (won && escaped < 0xFFFF) escaped++;
  if (depth > bestDepth) bestDepth = depth > tb::MAX_DEPTH ? tb::MAX_DEPTH : depth;
  saveToFile();
}

void TextboundStore::clearSavedRun() {
  if (savedRun.empty()) return;
  savedRun.clear();
  saveToFile();
}
