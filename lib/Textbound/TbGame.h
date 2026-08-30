#pragma once

#include <cstddef>
#include <cstdint>

#include "TbContent.h"
#include "TbRng.h"

// Pure run logic: no Arduino, no FreeRTOS, no renderer. The activity is a
// render/input shell over this, and the host tests drive it directly.
namespace tb {

class Game {
 public:
  // Injected so the engine stays free of Arduino headers: the firmware passes
  // random(), the host tests pass a fixed value.
  struct Hooks {
    uint32_t (*entropySeed)() = nullptr;
  };

  enum Phase : uint8_t {
    P_TITLE = 0,
    P_CLASS_PICK,
    P_ROOM,
    P_COMBAT,
    P_SKILL_MENU,
    P_ITEM_MENU,
    P_CHEST_PICK,
    P_DROP_PICK,
    P_REPLACE_PICK,
    P_UPGRADE_PICK,
    P_FORGET_PICK,
    P_PERK_PICK,
    P_SHOP,
    P_AFTERMATH,
    P_DEAD,
    P_WON,
  };

  struct State {
    uint32_t seed = 0;
    uint32_t rngState = 1;
    uint16_t turn = 0;
    uint16_t gold = 0;
    uint8_t classId = 0;
    uint8_t depth = 1;
    uint8_t hp = 0, maxHp = 0, atk = 0, def = 0, traits = 0;
    uint8_t inv[INV_SLOTS] = {NONE, NONE, NONE, NONE, NONE, NONE};
    uint8_t identified = 0;  // bit per unknown item (VIAL_A -> bit0, VIAL_B -> bit1)
    // High bit set means the index addresses kBossRooms instead of kRooms.
    uint8_t roomIdx = 0;
    uint8_t phase = P_TITLE;
    uint8_t monsterIdx = NONE, monsterHp = 0;
    uint8_t defending = 0, roomsThisDepth = 0, perksTaken = 0;
    uint8_t offer[3] = {NONE, NONE, NONE};
    uint8_t skills[SKILL_SLOTS] = {NONE, NONE, NONE};
    uint8_t cooldowns[SKILL_SLOTS] = {0, 0, 0};
    // bits0-1 upgrade level, bit2 cursed, bits3-5 fights the curse still holds.
    uint8_t skillMods[SKILL_SLOTS] = {0, 0, 0};
    uint8_t recent[4] = {NONE, NONE, NONE, NONE};
    uint8_t pendingSkill = NONE;  // waiting for P_REPLACE_PICK
    uint8_t pendingItem = NONE;   // waiting for P_DROP_PICK
    // What is still on the floor of a loot pile: handed over one at a time, so
    // a full pack can interrupt the pickup and resume after the drop screen.
    uint8_t loot[3] = {NONE, NONE, NONE};
    uint8_t pendingPerk = 0;  // a perk offer is owed once combat clears
    uint8_t bossPending = 0;  // the next room is this depth's boss landing
    uint8_t firstTurn = 0;    // Backstab only lands on the fight's opener
    uint8_t guard = 0, rally = 0, stunned = 0;
  };

  void reset();
  // Seeds the run and shows the class list; startRun() commits the choice.
  void prepareRun(uint32_t seed, const Hooks& hooks);
  void startRun(uint32_t seed, uint8_t classId, const Hooks& hooks);

  // Row model: every screen is a list, so the activity only ever asks for rows.
  int rowCount() const;
  void rowLabel(int index, char* out, size_t size) const;
  bool rowDimmed(int index) const;
  void pick(int index);
  // True when Back should step out of a sublist rather than open the pause menu.
  bool canGoBack() const;
  void goBack();

  const char* narrative() const { return body_; }
  const char* logLine(int index) const;
  int logCount() const { return logCount_; }
  bool showsLog() const {
    return s_.phase == P_COMBAT || s_.phase == P_SKILL_MENU || s_.phase == P_ITEM_MENU || s_.phase == P_AFTERMATH;
  }

  Phase phase() const { return static_cast<Phase>(s_.phase); }
  const State& state() const { return s_; }
  bool runActive() const {
    return s_.phase != P_TITLE && s_.phase != P_DEAD && s_.phase != P_WON && s_.phase != P_CLASS_PICK;
  }
  bool showsStatus() const { return runActive() || s_.phase == P_DEAD; }

  // Serialization. The log is not kept: it is cosmetic, and dropping it is what
  // holds a saved run inside MAX_SAVE_LENGTH.
  static constexpr size_t MAX_SAVE_LENGTH = 400;
  size_t toSaveString(char* out, size_t size) const;
  bool fromSaveString(const char* text);

  // Skill helpers, public so tests can assert on them directly.
  uint8_t skillLevel(int slot) const { return static_cast<uint8_t>(s_.skillMods[slot] & 0x03); }
  bool skillCursed(int slot) const { return (s_.skillMods[slot] & 0x04) != 0; }
  uint8_t curseFightsLeft(int slot) const { return static_cast<uint8_t>((s_.skillMods[slot] >> 3) & 0x07); }
  uint8_t effectiveCooldown(int slot) const;
  uint8_t effectiveArg(int slot) const;
  int knownSkillCount() const;
  bool skillKnown(uint8_t id) const;
  void skillLabel(int slot, char* out, size_t size) const;

  // Scaled monster figures, shared by combat and the activity's header line.
  static uint8_t monsterMaxHp(uint8_t id, uint8_t depth);
  static uint8_t monsterAtk(uint8_t id, uint8_t depth);

 private:
  enum RowAction : uint8_t {
    RA_NONE = 0,
    RA_CLASS,     // arg = class id
    RA_CHOICE,    // arg = index into the room template
    RA_COMBAT,    // arg = CA_*
    RA_SKILL,     // arg = skill slot
    RA_ITEM,      // arg = inventory slot
    RA_OFFER,     // arg = index into offer[]
    RA_SHOP_BUY,  // arg = index into offer[]
    RA_SLOT,      // arg = skill slot (replace / upgrade / forget)
    RA_KEEP,      // decline a replace / drop
    RA_BACK,      // leave a sublist
    RA_CONTINUE,  // aftermath
    RA_TITLE,     // dead / won
  };
  enum CombatAction : uint8_t { CA_ATTACK = 0, CA_SKILL, CA_DEFEND, CA_ITEM, CA_FLEE };

  struct Row {
    uint8_t action = RA_NONE;
    uint8_t arg = 0;
    bool dimmed = false;
  };

  int buildRows() const;
  const RoomTemplate& room() const;

  void setBody(const char* text);
  void appendBody(const char* text);
  void bodyPrintf(const char* fmt, ...);
  void appendPrintf(const char* fmt, ...);
  void rebuildBody();
  void pushLog(const char* fmt, ...);
  void clearLog();

  void applyEffect(uint8_t effect, uint8_t arg);
  void nextRoom(bool sameDepth);
  void generateRoom();
  void descend();
  void offerPerks();
  void applyPerk(uint8_t perkId);
  void learnSkill(uint8_t skillId);
  void forgetSkill(int slot);
  void upgradeSkill(int slot);
  void curseRandomSkill();
  void cleanseCurses();
  void tickCursesAfterFight();
  int upgradableSlots(int* out) const;

  void beginCombat(uint8_t monsterId);
  void combatAction(uint8_t action);
  void useSkill(int slot);
  void useItem(int slot);
  void afterPlayerAction(bool monsterActs);
  void monsterTurn();
  void winFight();
  void die();

  bool giveItem(uint8_t itemId);  // false when the pack is full (opens DropPick)
  void consumeItem(uint8_t itemId);
  uint8_t drawItem(uint8_t depth);
  // Draws up to `wanted` DIFFERENT items and returns how many it managed; a
  // chest never offers the same thing twice.
  uint8_t drawItems(uint8_t depth, uint8_t* out, uint8_t wanted);
  void openChest();
  void takeLoot();
  // Hands over the next item of a loot pile, or moves on when it is empty.
  void continueLoot();
  uint8_t drawMonster(uint8_t depth);
  int freeInvSlot() const;
  int invCount() const;

  // The generator state lives in State so a resumed run continues the very
  // same sequence; these are the only ways it advances.
  uint32_t roll(uint32_t bound);
  bool chance(uint32_t percent);
  uint32_t range(uint32_t lo, uint32_t hi);

  State s_;
  mutable Row rows_[8];
  mutable int rowCount_ = 0;
  char body_[900] = {0};
  char log_[3][64] = {{0}};
  int logCount_ = 0;
};

}  // namespace tb
