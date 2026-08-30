#include "TbGame.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace tb {

namespace {

// Crit and loot generosity; Lucky widens both.
constexpr uint32_t CRIT_PERCENT = 12;
constexpr uint32_t CRIT_PERCENT_LUCKY = 22;
constexpr uint32_t FLEE_PERCENT = 50;
constexpr uint32_t AMBUSH_BASE = 15;
constexpr uint32_t AMBUSH_PER_ROOM = 12;

uint8_t clampAdd(uint8_t value, int delta, uint8_t maxValue) {
  const int result = static_cast<int>(value) + delta;
  if (result < 0) return 0;
  if (result > static_cast<int>(maxValue)) return maxValue;
  return static_cast<uint8_t>(result);
}

}  // namespace

// ------------------------------------------------------------------ rng

uint32_t Game::roll(uint32_t bound) {
  Rng r;
  r.s = s_.rngState;
  const uint32_t value = r.below(bound);
  s_.rngState = r.s;
  return value;
}

bool Game::chance(uint32_t percent) { return roll(100) < percent; }

uint32_t Game::range(uint32_t lo, uint32_t hi) { return hi <= lo ? lo : lo + roll(hi - lo + 1); }

// ------------------------------------------------------------------ text

void Game::setBody(const char* text) { snprintf(body_, sizeof(body_), "%s", text == nullptr ? "" : text); }

void Game::appendBody(const char* text) {
  const size_t used = strlen(body_);
  if (used + 1 >= sizeof(body_)) return;
  snprintf(body_ + used, sizeof(body_) - used, "%s", text == nullptr ? "" : text);
}

void Game::bodyPrintf(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vsnprintf(body_, sizeof(body_), fmt, args);
  va_end(args);
}

void Game::appendPrintf(const char* fmt, ...) {
  const size_t used = strlen(body_);
  if (used + 1 >= sizeof(body_)) return;
  va_list args;
  va_start(args, fmt);
  vsnprintf(body_ + used, sizeof(body_) - used, fmt, args);
  va_end(args);
}

void Game::pushLog(const char* fmt, ...) {
  if (logCount_ == 3) {
    memmove(log_[0], log_[1], sizeof(log_[0]));
    memmove(log_[1], log_[2], sizeof(log_[0]));
    logCount_ = 2;
  }
  va_list args;
  va_start(args, fmt);
  vsnprintf(log_[logCount_], sizeof(log_[0]), fmt, args);
  va_end(args);
  logCount_++;
}

void Game::clearLog() {
  logCount_ = 0;
  for (auto& line : log_) line[0] = '\0';
}

const char* Game::logLine(int index) const {
  if (index < 0 || index >= logCount_) return "";
  return log_[index];
}

// ------------------------------------------------------------------ skills

uint8_t Game::effectiveCooldown(int slot) const {
  if (s_.skills[slot] == NONE) return 0;
  const uint8_t base = kSkills[s_.skills[slot]].cooldown;
  return skillLevel(slot) >= 1 ? static_cast<uint8_t>(base > 1 ? base - 1 : 1) : base;
}

uint8_t Game::effectiveArg(int slot) const {
  if (s_.skills[slot] == NONE) return 0;
  const uint8_t base = kSkills[s_.skills[slot]].arg;
  return skillLevel(slot) >= 2 ? static_cast<uint8_t>((base * 3 + 1) / 2) : base;
}

int Game::knownSkillCount() const {
  int count = 0;
  for (uint8_t id : s_.skills)
    if (id != NONE) count++;
  return count;
}

bool Game::skillKnown(uint8_t id) const {
  for (uint8_t known : s_.skills)
    if (known == id) return true;
  return false;
}

void Game::skillLabel(int slot, char* out, size_t size) const {
  if (slot < 0 || slot >= SKILL_SLOTS || s_.skills[slot] == NONE) {
    snprintf(out, size, "-");
    return;
  }
  const SkillDef& def = kSkills[s_.skills[slot]];
  const char* mark = skillLevel(slot) == 2 ? "++" : (skillLevel(slot) == 1 ? "+" : "");
  if (skillCursed(slot)) {
    snprintf(out, size, "%s%s (cursed %u)", def.name, mark, curseFightsLeft(slot));
  } else if (s_.cooldowns[slot] > 0) {
    snprintf(out, size, "%s%s (%u)", def.name, mark, s_.cooldowns[slot]);
  } else {
    snprintf(out, size, "%s%s", def.name, mark);
  }
}

void Game::learnSkill(uint8_t skillId) {
  if (skillKnown(skillId)) {
    appendPrintf("\n\nYou already know %s.", kSkills[skillId].name);
    return;
  }
  for (int i = 0; i < SKILL_SLOTS; i++) {
    if (s_.skills[i] == NONE) {
      s_.skills[i] = skillId;
      s_.cooldowns[i] = 0;
      s_.skillMods[i] = 0;
      appendPrintf("\n\nYou learn %s.", kSkills[skillId].name);
      return;
    }
  }
  // No room: the player decides what goes, on its own screen.
  s_.pendingSkill = skillId;
  s_.phase = P_REPLACE_PICK;
  appendPrintf("\n\nTo hold %s you must let something else go.", kSkills[skillId].name);
}

void Game::forgetSkill(int slot) {
  s_.skills[slot] = NONE;
  s_.cooldowns[slot] = 0;
  s_.skillMods[slot] = 0;
}

void Game::upgradeSkill(int slot) {
  const uint8_t level = skillLevel(slot);
  if (level >= 2) return;
  s_.skillMods[slot] = static_cast<uint8_t>((s_.skillMods[slot] & ~0x03) | (level + 1));
}

int Game::upgradableSlots(int* out) const {
  int count = 0;
  for (int i = 0; i < SKILL_SLOTS; i++) {
    if (s_.skills[i] != NONE && skillLevel(i) < 2) out[count++] = i;
  }
  return count;
}

void Game::curseRandomSkill() {
  int slots[SKILL_SLOTS];
  int count = 0;
  for (int i = 0; i < SKILL_SLOTS; i++) {
    if (s_.skills[i] != NONE && !skillCursed(i)) slots[count++] = i;
  }
  if (count == 0) {
    appendBody("\n\nSomething reaches for what you know and finds nothing it can hold.");
    return;
  }
  const int slot = slots[roll(static_cast<uint32_t>(count))];
  const uint8_t fights = static_cast<uint8_t>(range(1, 3));
  s_.skillMods[slot] = static_cast<uint8_t>((s_.skillMods[slot] & 0x03) | 0x04 | (fights << 3));
  appendPrintf("\n\n%s goes out of you like a word you have forgotten. (%u fights)", kSkills[s_.skills[slot]].name,
               fights);
}

void Game::cleanseCurses() {
  bool any = false;
  for (int i = 0; i < SKILL_SLOTS; i++) {
    if (skillCursed(i)) {
      s_.skillMods[i] = static_cast<uint8_t>(s_.skillMods[i] & 0x03);
      any = true;
    }
  }
  appendBody(any ? "\n\nWhat was taken from you comes back."
                 : "\n\nNothing in you needs lifting. The water is only water.");
}

void Game::tickCursesAfterFight() {
  for (int i = 0; i < SKILL_SLOTS; i++) {
    if (!skillCursed(i)) continue;
    const uint8_t left = curseFightsLeft(i);
    if (left <= 1) {
      s_.skillMods[i] = static_cast<uint8_t>(s_.skillMods[i] & 0x03);
    } else {
      s_.skillMods[i] = static_cast<uint8_t>((s_.skillMods[i] & 0x07) | ((left - 1) << 3));
    }
  }
}

// ------------------------------------------------------------------ items

int Game::freeInvSlot() const {
  for (int i = 0; i < INV_SLOTS; i++)
    if (s_.inv[i] == NONE) return i;
  return -1;
}

int Game::invCount() const {
  int count = 0;
  for (uint8_t id : s_.inv)
    if (id != NONE) count++;
  return count;
}

uint8_t Game::drawItem(uint8_t depth) {
  uint8_t pool[ITEM_COUNT];
  uint8_t count = 0;
  for (uint8_t i = 0; i < ITEM_COUNT; i++) {
    if (kItems[i].minDepth <= depth) pool[count++] = i;
  }
  if (count == 0) return 0;
  return pool[roll(count)];
}

bool Game::giveItem(uint8_t itemId) {
  const ItemDef& def = kItems[itemId];
  if (!itemGoesInPack(def.kind)) {
    // Tools are used the moment they are found; they never take up a slot.
    consumeItem(itemId);
    return true;
  }
  const int slot = freeInvSlot();
  if (slot < 0) {
    s_.pendingItem = itemId;
    s_.phase = P_DROP_PICK;
    appendPrintf("\n\nYour pack is full. Something has to go for the %s.", def.name);
    return false;
  }
  s_.inv[slot] = itemId;
  appendPrintf("\n\nYou take the %s.", def.name);
  return true;
}

void Game::consumeItem(uint8_t itemId) {
  const ItemDef& def = kItems[itemId];
  switch (def.kind) {
    case IT_ATK_UP:
      s_.atk = clampAdd(s_.atk, def.power, 99);
      appendPrintf("\n\n%s: +%u ATK.", def.name, def.power);
      break;
    case IT_DEF_UP:
      s_.def = clampAdd(s_.def, def.power, 99);
      appendPrintf("\n\n%s: +%u DEF.", def.name, def.power);
      break;
    case IT_MAXHP_UP:
      s_.maxHp = clampAdd(s_.maxHp, def.power, 99);
      s_.hp = clampAdd(s_.hp, def.power, s_.maxHp);
      appendPrintf("\n\n%s: +%u max HP.", def.name, def.power);
      break;
    default:
      break;
  }
}

// ------------------------------------------------------------------ setup

void Game::prepareRun(uint32_t seed, const Hooks& hooks) {
  const uint32_t entropy = seed != 0 ? seed : (hooks.entropySeed ? hooks.entropySeed() : 0x9E3779B9u);
  s_ = State{};
  s_.seed = entropy != 0 ? entropy : 0x9E3779B9u;
  s_.rngState = s_.seed;
  s_.phase = P_CLASS_PICK;
  clearLog();
  setBody(kIntro);
}

void Game::reset() {
  s_ = State{};
  s_.phase = P_TITLE;
  clearLog();
  setBody(kIntro);
}

void Game::startRun(uint32_t seed, uint8_t classId, const Hooks& hooks) {
  s_ = State{};
  if (seed == 0) seed = hooks.entropySeed ? hooks.entropySeed() : 0x9E3779B9u;
  if (seed == 0) seed = 0x9E3779B9u;
  s_.seed = seed;
  s_.rngState = seed;
  s_.classId = classId < CLASS_COUNT ? classId : 0;

  const ClassDef& cls = kClasses[s_.classId];
  s_.hp = cls.hp;
  s_.maxHp = cls.hp;
  s_.atk = cls.atk;
  s_.def = cls.def;
  s_.traits = cls.traits;
  s_.skills[0] = cls.skillId;
  for (uint8_t i = 0; i < cls.potions && i < INV_SLOTS; i++) s_.inv[i] = 0;  // Potion
  s_.depth = 1;
  s_.turn = 1;

  clearLog();
  setBody("");
  nextRoom(true);
}

// ------------------------------------------------------------------ rooms

const RoomTemplate& Game::room() const {
  if (s_.roomIdx & 0x80) {
    const uint8_t index = static_cast<uint8_t>(s_.roomIdx & 0x7F);
    return kBossRooms[index > 1 ? 1 : index];
  }
  return kRooms[s_.roomIdx >= ROOM_COUNT ? 0 : s_.roomIdx];
}

void Game::generateRoom() {
  if (s_.bossPending) {
    s_.bossPending = 0;
    s_.roomIdx = static_cast<uint8_t>(0x80 | (s_.depth >= MAX_DEPTH ? 1 : 0));
    return;
  }

  uint8_t pool[ROOM_COUNT];
  uint16_t weights[ROOM_COUNT];
  uint8_t count = 0;
  uint32_t total = 0;
  for (uint8_t pass = 0; pass < 2 && count == 0; pass++) {
    total = 0;
    count = 0;
    for (uint8_t i = 0; i < ROOM_COUNT; i++) {
      const RoomTemplate& r = kRooms[i];
      if (r.weight == 0 || r.minDepth > s_.depth || r.maxDepth < s_.depth) continue;
      if (pass == 0) {
        bool seen = false;
        for (uint8_t recent : s_.recent)
          if (recent == i) seen = true;
        if (seen) continue;
      }
      pool[count] = i;
      weights[count] = r.weight;
      total += r.weight;
      count++;
    }
  }
  if (count == 0) {
    s_.roomIdx = 0;
    return;
  }

  uint32_t pickValue = roll(total);
  uint8_t chosen = pool[0];
  for (uint8_t i = 0; i < count; i++) {
    if (pickValue < weights[i]) {
      chosen = pool[i];
      break;
    }
    pickValue -= weights[i];
  }
  s_.roomIdx = chosen;
  s_.recent[3] = s_.recent[2];
  s_.recent[2] = s_.recent[1];
  s_.recent[1] = s_.recent[0];
  s_.recent[0] = chosen;
}

void Game::nextRoom(bool sameDepth) {
  if (!sameDepth) s_.roomsThisDepth = 0;
  generateRoom();
  s_.phase = P_ROOM;
  if (body_[0] != '\0') appendBody("\n\n");
  appendBody(room().text);
}

void Game::descend() {
  if (s_.depth < MAX_DEPTH) s_.depth++;
  s_.roomsThisDepth = 0;
  appendPrintf(" Depth %u.", s_.depth);
  if (s_.depth == MID_BOSS_DEPTH || s_.depth == MAX_DEPTH) {
    s_.bossPending = 1;
    nextRoom(false);
    return;
  }
  if (s_.depth % 3 == 0) {
    offerPerks();
    return;
  }
  nextRoom(false);
}

// ------------------------------------------------------------------ perks

void Game::offerPerks() {
  uint8_t pool[PERK_COUNT];
  uint8_t count = 0;
  int slots[SKILL_SLOTS];
  const bool canUpgrade = upgradableSlots(slots) > 0;
  for (uint8_t i = 0; i < PERK_COUNT; i++) {
    const PerkDef& p = kPerks[i];
    if (p.kind == PK_TRAIT && (s_.traits & p.arg) != 0) continue;
    if (p.kind == PK_LEARN && skillKnown(p.arg)) continue;
    if (p.kind == PK_UPGRADE && !canUpgrade) continue;
    pool[count++] = i;
  }
  for (int i = 0; i < 3; i++) s_.offer[i] = NONE;
  for (int i = 0; i < 3 && count > 0; i++) {
    const uint32_t index = roll(count);
    s_.offer[i] = pool[index];
    pool[index] = pool[--count];
  }
  s_.pendingPerk = 0;
  s_.phase = P_PERK_PICK;
  setBody("You stop where you are, and something you have been carrying settles into place.");
}

void Game::applyPerk(uint8_t perkId) {
  const PerkDef& p = kPerks[perkId];
  setBody("");
  switch (p.kind) {
    case PK_MAXHP:
      s_.maxHp = clampAdd(s_.maxHp, p.arg, 99);
      s_.hp = clampAdd(s_.hp, p.arg, s_.maxHp);
      bodyPrintf("%s: +%u max HP.", p.name, p.arg);
      break;
    case PK_ATK:
      s_.atk = clampAdd(s_.atk, p.arg, 99);
      bodyPrintf("%s: +%u ATK.", p.name, p.arg);
      break;
    case PK_DEF:
      s_.def = clampAdd(s_.def, p.arg, 99);
      bodyPrintf("%s: +%u DEF.", p.name, p.arg);
      break;
    case PK_TRAIT:
      s_.traits = static_cast<uint8_t>(s_.traits | p.arg);
      bodyPrintf("%s. %s", p.name, p.blurb);
      break;
    case PK_LEARN:
      bodyPrintf("%s.", p.blurb);
      learnSkill(p.arg);
      if (s_.phase == P_REPLACE_PICK) return;  // waits on the player
      break;
    case PK_UPGRADE: {
      int slots[SKILL_SLOTS];
      const int count = upgradableSlots(slots);
      if (count == 0) {
        setBody("Nothing you carry can be sharpened further.");
        break;
      }
      if (count == 1) {
        upgradeSkill(slots[0]);
        char label[48];
        skillLabel(slots[0], label, sizeof(label));
        bodyPrintf("You practise until it is second nature: %s.", label);
        break;
      }
      s_.phase = P_UPGRADE_PICK;
      setBody("Which do you practise?");
      return;
    }
    default:
      break;
  }
  s_.perksTaken++;
  nextRoom(true);
}

// ------------------------------------------------------------------ effects

uint8_t Game::drawMonster(uint8_t depth) {
  uint8_t pool[MONSTER_COUNT];
  uint8_t count = 0;
  for (uint8_t i = 0; i < MONSTER_COUNT; i++) {
    const MonsterDef& m = kMonsters[i];
    if (m.boss) continue;
    if (m.minDepth > depth || m.maxDepth < depth) continue;
    pool[count++] = i;
  }
  if (count == 0) return 0;
  return pool[roll(count)];
}

uint8_t Game::monsterMaxHp(uint8_t id, uint8_t depth) {
  const MonsterDef& m = kMonsters[id];
  if (m.boss) return m.hp;
  const int value = m.hp + depth * 2;
  return static_cast<uint8_t>(value > 250 ? 250 : value);
}

uint8_t Game::monsterAtk(uint8_t id, uint8_t depth) {
  const MonsterDef& m = kMonsters[id];
  if (m.boss) return m.atk;
  return static_cast<uint8_t>(m.atk + depth / 2);
}

void Game::applyEffect(uint8_t effect, uint8_t arg) {
  switch (effect) {
    case EF_DESCEND:
      setBody("You take the stair down.");
      descend();
      return;

    case EF_EXPLORE: {
      s_.roomsThisDepth++;
      const uint32_t risk = AMBUSH_BASE + AMBUSH_PER_ROOM * s_.roomsThisDepth;
      if (chance(risk > 80 ? 80 : risk)) {
        setBody("You have been here long enough to be noticed.");
        beginCombat(drawMonster(s_.depth));
        return;
      }
      setBody("");
      nextRoom(true);
      return;
    }

    case EF_FIGHT:
      setBody("");
      beginCombat(arg < MONSTER_COUNT ? arg : drawMonster(s_.depth));
      return;

    case EF_FIGHT_RANDOM:
      setBody("");
      beginCombat(drawMonster(s_.depth));
      return;

    case EF_BOSS:
      setBody("");
      beginCombat(s_.depth >= MAX_DEPTH ? BOSS_FINAL : BOSS_MID);
      return;

    case EF_CHEST: {
      s_.offer[0] = drawItem(s_.depth);
      s_.offer[1] = drawItem(s_.depth);
      s_.offer[2] = NONE;
      s_.phase = P_CHEST_PICK;
      setBody("Two things are worth carrying. You cannot manage both.");
      return;
    }

    case EF_GOLD_GAIN: {
      const uint16_t amount = static_cast<uint16_t>(arg + roll(6));
      s_.gold = static_cast<uint16_t>(s_.gold + amount);
      bodyPrintf("You come away %u gold heavier.", amount);
      break;
    }

    case EF_GOLD_LOSS: {
      const uint16_t amount = s_.gold < arg ? s_.gold : arg;
      s_.gold = static_cast<uint16_t>(s_.gold - amount);
      bodyPrintf("You are lighter by %u gold, and you did not feel it go.", amount);
      break;
    }

    case EF_HEAL: {
      const uint8_t before = s_.hp;
      s_.hp = clampAdd(s_.hp, arg, s_.maxHp);
      bodyPrintf("Something in you closes over. (%u \xE2\x86\x92 %u HP)", before, s_.hp);
      break;
    }

    case EF_DAMAGE: {
      const uint8_t before = s_.hp;
      s_.hp = s_.hp > arg ? static_cast<uint8_t>(s_.hp - arg) : 0;
      bodyPrintf("That cost you. (%u \xE2\x86\x92 %u HP)", before, s_.hp);
      if (s_.hp == 0) {
        die();
        return;
      }
      break;
    }

    case EF_GAMBLE: {
      if (s_.gold < arg) {
        setBody("He looks at your purse, and then at you, and shuffles on.");
        break;
      }
      if (chance(45)) {
        s_.gold = static_cast<uint16_t>(s_.gold + arg);
        bodyPrintf("The cards come up yours. +%u gold.", arg);
      } else {
        s_.gold = static_cast<uint16_t>(s_.gold - arg);
        bodyPrintf("They do not. -%u gold.", arg);
      }
      break;
    }

    case EF_SHOP: {
      for (int i = 0; i < 3; i++) s_.offer[i] = drawItem(s_.depth);
      s_.phase = P_SHOP;
      setBody("He turns the lamp so you can see the cloth properly, and waits.");
      return;
    }

    case EF_REST: {
      const uint8_t before = s_.hp;
      s_.hp = clampAdd(s_.hp, arg, s_.maxHp);
      bodyPrintf("You sit a while. (%u \xE2\x86\x92 %u HP)", before, s_.hp);
      if (chance(35)) {
        appendBody("\n\nAnd then you are not alone.");
        beginCombat(drawMonster(s_.depth));
        return;
      }
      break;
    }

    case EF_CURSE:
      setBody("You should not have. You know it while you are still doing it.");
      curseRandomSkill();
      break;

    case EF_CLEANSE:
      setBody("You give what the place asks for.");
      cleanseCurses();
      break;

    case EF_UNBURDEN: {
      if (knownSkillCount() == 0) {
        setBody("You have nothing left to put down.");
        break;
      }
      s_.phase = P_FORGET_PICK;
      setBody("Leave something of yourself here, and walk lighter for it. (+2 max HP)");
      return;
    }

    case EF_TEACH: {
      const uint16_t price = arg;
      if (s_.gold < price) {
        bodyPrintf("He looks at what you have and shakes his head. (%u gold)", price);
        break;
      }
      uint8_t pool[SKILL_COUNT];
      uint8_t count = 0;
      for (uint8_t i = 0; i < SKILL_COUNT; i++)
        if (!skillKnown(i)) pool[count++] = i;
      if (count == 0) {
        setBody("\"You already have everything I could give you,\" he says, and looks away.");
        break;
      }
      s_.gold = static_cast<uint16_t>(s_.gold - price);
      setBody("He shows you, slowly, twice.");
      learnSkill(pool[roll(count)]);
      if (s_.phase == P_REPLACE_PICK) return;
      break;
    }

    case EF_NOTHING:
    default:
      setBody("Nothing here answers you.");
      break;
  }

  nextRoom(true);
}

// ------------------------------------------------------------------ combat

void Game::beginCombat(uint8_t monsterId) {
  if (monsterId >= MONSTER_COUNT) monsterId = 0;
  s_.monsterIdx = monsterId;
  s_.monsterHp = monsterMaxHp(monsterId, s_.depth);
  s_.defending = 0;
  s_.firstTurn = 1;
  s_.guard = 0;
  s_.rally = 0;
  s_.stunned = 0;
  for (int i = 0; i < SKILL_SLOTS; i++) s_.cooldowns[i] = 0;
  s_.phase = P_COMBAT;
  clearLog();

  const MonsterDef& m = kMonsters[monsterId];
  if (body_[0] != '\0') appendBody("\n\n");
  appendBody(m.intro);
  appendPrintf("\n\n%s \xE2\x80\x94 HP %u \xC2\xB7 ATK %u \xC2\xB7 DEF %u", m.name, s_.monsterHp,
               monsterAtk(monsterId, s_.depth), m.def);
}

void Game::rebuildBody() {
  switch (s_.phase) {
    case P_COMBAT:
    case P_SKILL_MENU:
    case P_ITEM_MENU: {
      const MonsterDef& m = kMonsters[s_.monsterIdx < MONSTER_COUNT ? s_.monsterIdx : 0];
      setBody(m.intro);
      appendPrintf("\n\n%s \xE2\x80\x94 HP %u \xC2\xB7 ATK %u \xC2\xB7 DEF %u", m.name, s_.monsterHp,
                   monsterAtk(s_.monsterIdx, s_.depth), m.def);
      break;
    }
    case P_ROOM:
      setBody(room().text);
      break;
    case P_DEAD:
      setBody(kDeathLine);
      break;
    case P_WON:
      setBody(kEpilogue[0]);
      for (uint8_t i = 1; i < EPILOGUE_PAGES; i++) {
        appendBody("\n\n");
        appendBody(kEpilogue[i]);
      }
      break;
    default:
      break;
  }
}

void Game::combatAction(uint8_t action) {
  const MonsterDef& m = kMonsters[s_.monsterIdx];
  s_.turn++;

  switch (action) {
    case CA_ATTACK: {
      int damage = static_cast<int>(s_.atk) + s_.rally - static_cast<int>(m.def);
      if (damage < 1) damage = 1;
      const bool crit = chance((s_.traits & TRAIT_LUCKY) ? CRIT_PERCENT_LUCKY : CRIT_PERCENT);
      if (crit) damage *= 2;
      const uint8_t before = s_.monsterHp;
      s_.monsterHp = s_.monsterHp > damage ? static_cast<uint8_t>(s_.monsterHp - damage) : 0;
      pushLog("%sYou hit for %d. (%u \xE2\x86\x92 %u)", crit ? "Clean strike. " : "", damage, before, s_.monsterHp);
      break;
    }

    case CA_DEFEND:
      s_.defending = 1;
      pushLog("You set your feet and raise your guard.");
      break;

    case CA_FLEE: {
      if (kMonsters[s_.monsterIdx].boss) {
        pushLog("There is nowhere behind you to run to.");
        break;
      }
      if ((s_.traits & TRAIT_QUICK) || chance(FLEE_PERCENT)) {
        tickCursesAfterFight();
        s_.monsterIdx = NONE;
        setBody("You run until the sound of it fades, and whatever it was does not follow.");
        nextRoom(true);
        return;
      }
      pushLog("You turn to run, and it is faster.");
      break;
    }

    default:
      break;
  }

  afterPlayerAction(true);
}

void Game::useSkill(int slot) {
  if (slot < 0 || slot >= SKILL_SLOTS || s_.skills[slot] == NONE) return;
  if (s_.cooldowns[slot] > 0 || skillCursed(slot)) return;

  const SkillDef& def = kSkills[s_.skills[slot]];
  const MonsterDef& m = kMonsters[s_.monsterIdx];
  const uint8_t arg = effectiveArg(slot);
  s_.turn++;
  s_.cooldowns[slot] = static_cast<uint8_t>(effectiveCooldown(slot) + 1);  // ticks down once this same turn

  switch (def.kind) {
    case SK_CLEAVE:
    case SK_BACKSTAB: {
      const bool full = def.kind == SK_CLEAVE || s_.firstTurn;
      int base = static_cast<int>(s_.atk) + s_.rally - static_cast<int>(m.def);
      if (base < 1) base = 1;
      const int damage = full ? base * arg : base;
      const uint8_t before = s_.monsterHp;
      s_.monsterHp = s_.monsterHp > damage ? static_cast<uint8_t>(s_.monsterHp - damage) : 0;
      pushLog("%s for %d. (%u \xE2\x86\x92 %u)", full ? def.name : "The moment is gone; you hit", damage, before,
              s_.monsterHp);
      break;
    }
    case SK_PRAYER: {
      const uint8_t before = s_.hp;
      s_.hp = clampAdd(s_.hp, arg, s_.maxHp);
      pushLog("%s. (%u \xE2\x86\x92 %u HP)", def.name, before, s_.hp);
      break;
    }
    case SK_STUN:
      s_.stunned = 1;
      pushLog("%s. It loses its footing.", def.name);
      break;
    case SK_GUARD:
      s_.guard = arg;
      pushLog("%s. +%u DEF for this fight.", def.name, arg);
      break;
    case SK_RALLY:
      s_.rally = arg;
      pushLog("%s. +%u ATK for this fight.", def.name, arg);
      break;
    default:
      break;
  }

  s_.phase = P_COMBAT;
  afterPlayerAction(true);
}

void Game::useItem(int slot) {
  if (slot < 0 || slot >= INV_SLOTS || s_.inv[slot] == NONE) return;
  const uint8_t itemId = s_.inv[slot];
  const ItemDef& def = kItems[itemId];
  const bool inCombat = s_.monsterIdx != NONE && (s_.phase == P_ITEM_MENU || s_.phase == P_COMBAT);
  s_.inv[slot] = NONE;
  s_.turn++;

  bool fled = false;
  switch (def.kind) {
    case IT_POTION: {
      const uint8_t before = s_.hp;
      s_.hp = clampAdd(s_.hp, def.power, s_.maxHp);
      pushLog("%s. (%u \xE2\x86\x92 %u HP)", def.name, before, s_.hp);
      break;
    }
    case IT_BOMB: {
      if (!inCombat) {
        pushLog("Nothing here to throw it at.");
        s_.inv[slot] = itemId;
        break;
      }
      const uint8_t before = s_.monsterHp;
      s_.monsterHp = s_.monsterHp > def.power ? static_cast<uint8_t>(s_.monsterHp - def.power) : 0;
      pushLog("The bomb goes off. (%u \xE2\x86\x92 %u)", before, s_.monsterHp);
      break;
    }
    case IT_SMOKE:
      if (!inCombat || kMonsters[s_.monsterIdx].boss) {
        pushLog("The smoke drifts, and changes nothing.");
        break;
      }
      fled = true;
      break;
    case IT_HOLY: {
      bool any = false;
      for (int i = 0; i < SKILL_SLOTS; i++) {
        if (skillCursed(i)) {
          s_.skillMods[i] = static_cast<uint8_t>(s_.skillMods[i] & 0x03);
          any = true;
        }
      }
      pushLog(any ? "The water burns, and then it does not." : "The water does nothing. There was nothing to undo.");
      break;
    }
    case IT_VIAL: {
      // Which vial is which is fixed for the whole run by its seed, so a
      // player who learns it on depth 2 still knows it on depth 9.
      const bool goodIsA = ((s_.seed >> 7) & 1u) != 0;
      const bool good = (itemId == VIAL_A) == goodIsA;
      s_.identified = static_cast<uint8_t>(s_.identified | (itemId == VIAL_A ? 0x01 : 0x02));
      if (good) {
        const uint8_t before = s_.hp;
        s_.hp = clampAdd(s_.hp, 12, s_.maxHp);
        pushLog("%s: it was medicine. (%u \xE2\x86\x92 %u HP)", def.name, before, s_.hp);
      } else {
        const uint8_t before = s_.hp;
        s_.hp = s_.hp > 6 ? static_cast<uint8_t>(s_.hp - 6) : 0;
        pushLog("%s: it was not. (%u \xE2\x86\x92 %u HP)", def.name, before, s_.hp);
      }
      break;
    }
    default:
      consumeItem(itemId);
      pushLog("%s.", def.name);
      break;
  }

  if (s_.hp == 0) {
    die();
    return;
  }
  if (fled) {
    tickCursesAfterFight();
    s_.monsterIdx = NONE;
    setBody("You drop the bead and walk out of the smoke somewhere else entirely.");
    nextRoom(true);
    return;
  }

  if (!inCombat) {
    s_.phase = P_ROOM;
    rebuildBody();
    return;
  }
  s_.phase = P_COMBAT;
  afterPlayerAction(true);
}

void Game::afterPlayerAction(bool monsterActs) {
  s_.firstTurn = 0;
  for (int i = 0; i < SKILL_SLOTS; i++) {
    if (s_.cooldowns[i] > 0) s_.cooldowns[i]--;
  }

  if (s_.monsterHp == 0) {
    winFight();
    return;
  }
  if (monsterActs) monsterTurn();
  if (s_.hp == 0) {
    die();
    return;
  }
  rebuildBody();
}

void Game::monsterTurn() {
  if (s_.stunned) {
    s_.stunned = 0;
    s_.defending = 0;
    pushLog("It is still finding its feet.");
    return;
  }

  const MonsterDef& m = kMonsters[s_.monsterIdx];
  int damage = static_cast<int>(monsterAtk(s_.monsterIdx, s_.depth)) - static_cast<int>(s_.def) - s_.guard;
  if (damage < 1) damage = 1;
  if (s_.defending) {
    damage = (s_.traits & TRAIT_STURDY) ? 0 : (damage + 1) / 2;
    s_.defending = 0;
  }
  const uint8_t before = s_.hp;
  s_.hp = s_.hp > damage ? static_cast<uint8_t>(s_.hp - damage) : 0;
  if (damage == 0) {
    pushLog("It swings; you take none of it.");
  } else {
    pushLog("The %s hits you for %d. (%u \xE2\x86\x92 %u)", m.name, damage, before, s_.hp);
  }
}

void Game::winFight() {
  const MonsterDef& m = kMonsters[s_.monsterIdx];
  uint16_t gold = m.gold;
  if (s_.traits & TRAIT_LUCKY) gold = static_cast<uint16_t>(gold + gold / 4);
  s_.gold = static_cast<uint16_t>(s_.gold + gold);
  tickCursesAfterFight();
  pushLog("It goes down and stays down. +%u gold.", gold);

  const bool wasFinalBoss = m.boss && s_.depth >= MAX_DEPTH;
  const bool wasMidBoss = m.boss && !wasFinalBoss;
  s_.monsterIdx = NONE;

  if (wasFinalBoss) {
    s_.phase = P_WON;
    rebuildBody();
    return;
  }
  s_.pendingPerk = wasMidBoss ? 1 : 0;
  s_.phase = P_AFTERMATH;
  setBody("You wipe the blade and give yourself a moment you cannot really afford.");
  if (gold > 0) appendPrintf("\n\n+%u gold.", gold);
}

void Game::die() {
  s_.hp = 0;
  s_.phase = P_DEAD;
  s_.monsterIdx = NONE;
  setBody(kDeathLine);
  appendPrintf("\n\nDepth %u, turn %u, %u gold left on the stones.", s_.depth, s_.turn, s_.gold);
}

// ------------------------------------------------------------------ rows

int Game::buildRows() const {
  int count = 0;
  auto add = [&](uint8_t action, uint8_t arg, bool dimmed) {
    if (count >= 8) return;
    rows_[count].action = action;
    rows_[count].arg = arg;
    rows_[count].dimmed = dimmed;
    count++;
  };

  switch (s_.phase) {
    case P_CLASS_PICK:
      for (uint8_t i = 0; i < CLASS_COUNT; i++) add(RA_CLASS, i, false);
      break;

    case P_ROOM: {
      const RoomTemplate& r = room();
      for (uint8_t i = 0; i < r.choiceCount && i < 4; i++) add(RA_CHOICE, i, false);
      break;
    }

    case P_COMBAT:
      add(RA_COMBAT, CA_ATTACK, false);
      if (knownSkillCount() > 0) {
        // With one skill the row IS the skill, so it carries that skill's
        // state; with several it only opens the sublist, and is dead only when
        // nothing in there can be used.
        bool anyUsable = false;
        for (int i = 0; i < SKILL_SLOTS; i++) {
          if (s_.skills[i] != NONE && s_.cooldowns[i] == 0 && !skillCursed(i)) anyUsable = true;
        }
        add(RA_COMBAT, CA_SKILL, !anyUsable);
      }
      add(RA_COMBAT, CA_DEFEND, false);
      if (invCount() > 0) add(RA_COMBAT, CA_ITEM, false);
      add(RA_COMBAT, CA_FLEE, kMonsters[s_.monsterIdx < MONSTER_COUNT ? s_.monsterIdx : 0].boss);
      break;

    case P_SKILL_MENU:
      for (uint8_t i = 0; i < SKILL_SLOTS; i++) {
        if (s_.skills[i] == NONE) continue;
        add(RA_SKILL, i, s_.cooldowns[i] > 0 || skillCursed(i));
      }
      add(RA_BACK, 0, false);
      break;

    case P_ITEM_MENU:
      for (uint8_t i = 0; i < INV_SLOTS; i++) {
        if (s_.inv[i] != NONE) add(RA_ITEM, i, false);
      }
      add(RA_BACK, 0, false);
      break;

    case P_CHEST_PICK:
      for (uint8_t i = 0; i < 2; i++) {
        if (s_.offer[i] != NONE) add(RA_OFFER, i, false);
      }
      add(RA_KEEP, 0, false);
      break;

    case P_SHOP:
      for (uint8_t i = 0; i < 3; i++) {
        if (s_.offer[i] == NONE) continue;
        const uint16_t price = static_cast<uint16_t>(kItems[s_.offer[i]].price * (100 + 10 * s_.depth) / 100);
        add(RA_SHOP_BUY, i, s_.gold < price);
      }
      add(RA_KEEP, 0, false);
      break;

    case P_DROP_PICK:
      for (uint8_t i = 0; i < INV_SLOTS; i++) {
        if (s_.inv[i] != NONE) add(RA_SLOT, i, false);
      }
      add(RA_KEEP, 0, false);
      break;

    case P_REPLACE_PICK:
    case P_FORGET_PICK:
      for (uint8_t i = 0; i < SKILL_SLOTS; i++) {
        if (s_.skills[i] != NONE) add(RA_SLOT, i, false);
      }
      add(RA_KEEP, 0, false);
      break;

    case P_UPGRADE_PICK:
      for (uint8_t i = 0; i < SKILL_SLOTS; i++) {
        if (s_.skills[i] != NONE && skillLevel(i) < 2) add(RA_SLOT, i, false);
      }
      break;

    case P_PERK_PICK:
      for (uint8_t i = 0; i < 3; i++) {
        if (s_.offer[i] != NONE) add(RA_OFFER, i, false);
      }
      break;

    case P_AFTERMATH:
      add(RA_CONTINUE, 0, false);
      break;

    case P_DEAD:
    case P_WON:
      add(RA_TITLE, 0, false);
      break;

    default:
      break;
  }

  rowCount_ = count;
  return count;
}

int Game::rowCount() const { return buildRows(); }

bool Game::rowDimmed(int index) const {
  const int count = buildRows();
  if (index < 0 || index >= count) return false;
  return rows_[index].dimmed;
}

void Game::rowLabel(int index, char* out, size_t size) const {
  if (out == nullptr || size == 0) return;
  out[0] = '\0';
  const int count = buildRows();
  if (index < 0 || index >= count) return;
  const Row& row = rows_[index];

  switch (row.action) {
    case RA_CLASS: {
      const ClassDef& c = kClasses[row.arg];
      snprintf(out, size, "%s  \xC2\xB7  %u HP / %u ATK / %u DEF", c.name, c.hp, c.atk, c.def);
      break;
    }
    case RA_CHOICE:
      snprintf(out, size, "%s", room().choices[row.arg].label);
      break;
    case RA_COMBAT:
      switch (row.arg) {
        case CA_ATTACK:
          snprintf(out, size, "Attack");
          break;
        case CA_SKILL:
          if (knownSkillCount() == 1) {
            for (int i = 0; i < SKILL_SLOTS; i++) {
              if (s_.skills[i] != NONE) {
                skillLabel(i, out, size);
                break;
              }
            }
          } else {
            snprintf(out, size, "Skill");
          }
          break;
        case CA_DEFEND:
          snprintf(out, size, "Defend");
          break;
        case CA_ITEM:
          snprintf(out, size, "Item (%d)", invCount());
          break;
        default:
          snprintf(out, size, "Flee");
          break;
      }
      break;
    case RA_SKILL:
      skillLabel(row.arg, out, size);
      break;
    case RA_ITEM:
      snprintf(out, size, "%s", kItems[s_.inv[row.arg]].name);
      break;
    case RA_OFFER: {
      const uint8_t id = s_.offer[row.arg];
      if (s_.phase == P_PERK_PICK) {
        snprintf(out, size, "%s \xE2\x80\x94 %s", kPerks[id].name, kPerks[id].blurb);
      } else {
        snprintf(out, size, "%s", kItems[id].name);
      }
      break;
    }
    case RA_SHOP_BUY: {
      const uint8_t id = s_.offer[row.arg];
      const uint16_t price = static_cast<uint16_t>(kItems[id].price * (100 + 10 * s_.depth) / 100);
      snprintf(out, size, "%s \xE2\x80\x94 %u gold", kItems[id].name, price);
      break;
    }
    case RA_SLOT:
      if (s_.phase == P_DROP_PICK) {
        snprintf(out, size, "Drop %s", kItems[s_.inv[row.arg]].name);
      } else if (s_.phase == P_FORGET_PICK) {
        char label[48];
        skillLabel(row.arg, label, sizeof(label));
        snprintf(out, size, "Forget %s", label);
      } else {
        skillLabel(row.arg, out, size);
      }
      break;
    case RA_KEEP:
      if (s_.phase == P_SHOP) {
        snprintf(out, size, "Leave the cloth");
      } else if (s_.phase == P_CHEST_PICK) {
        snprintf(out, size, "Take neither");
      } else {
        snprintf(out, size, "Keep what I have");
      }
      break;
    case RA_BACK:
      snprintf(out, size, "Back");
      break;
    case RA_CONTINUE:
      snprintf(out, size, "Continue");
      break;
    case RA_TITLE:
      snprintf(out, size, "Return to the surface");
      break;
    default:
      break;
  }
}

bool Game::canGoBack() const { return s_.phase == P_SKILL_MENU || s_.phase == P_ITEM_MENU; }

void Game::goBack() {
  if (canGoBack()) {
    s_.phase = P_COMBAT;
    rebuildBody();
  }
}

// ------------------------------------------------------------------ pick

void Game::pick(int index) {
  const int count = buildRows();
  if (index < 0 || index >= count) return;
  const Row row = rows_[index];
  if (row.dimmed && row.action != RA_COMBAT) return;

  switch (row.action) {
    case RA_CLASS: {
      Hooks none;
      startRun(s_.seed, row.arg, none);
      return;
    }

    case RA_CHOICE: {
      const Choice& choice = room().choices[row.arg];
      s_.turn++;
      applyEffect(choice.effect, choice.arg);
      return;
    }

    case RA_COMBAT:
      if (row.arg == CA_SKILL) {
        if (row.dimmed) return;
        if (knownSkillCount() == 1) {
          for (int i = 0; i < SKILL_SLOTS; i++) {
            if (s_.skills[i] == NONE) continue;
            if (s_.cooldowns[i] > 0 || skillCursed(i)) return;
            useSkill(i);
            return;
          }
        }
        s_.phase = P_SKILL_MENU;
        return;
      }
      if (row.arg == CA_ITEM) {
        s_.phase = P_ITEM_MENU;
        return;
      }
      if (row.dimmed) return;  // fleeing a boss
      combatAction(row.arg);
      return;

    case RA_SKILL:
      useSkill(row.arg);
      return;

    case RA_ITEM:
      useItem(row.arg);
      return;

    case RA_OFFER:
      if (s_.phase == P_PERK_PICK) {
        applyPerk(s_.offer[row.arg]);
        return;
      }
      {
        const uint8_t itemId = s_.offer[row.arg];
        setBody("");
        for (int i = 0; i < 3; i++) s_.offer[i] = NONE;
        if (!giveItem(itemId)) return;  // waits on P_DROP_PICK
        nextRoom(true);
      }
      return;

    case RA_SHOP_BUY: {
      const uint8_t itemId = s_.offer[row.arg];
      const uint16_t price = static_cast<uint16_t>(kItems[itemId].price * (100 + 10 * s_.depth) / 100);
      if (s_.gold < price) return;
      s_.gold = static_cast<uint16_t>(s_.gold - price);
      s_.offer[row.arg] = NONE;
      const uint8_t remaining[3] = {s_.offer[0], s_.offer[1], s_.offer[2]};
      setBody("");
      if (!giveItem(itemId)) return;  // pack full: the drop screen takes over
      // Stay at the cloth so the rest of it can be bought too.
      s_.offer[0] = remaining[0];
      s_.offer[1] = remaining[1];
      s_.offer[2] = remaining[2];
      s_.phase = P_SHOP;
      appendBody("\n\nHe waits to see whether you want anything else.");
      return;
    }

    case RA_SLOT:
      if (s_.phase == P_DROP_PICK) {
        const uint8_t dropped = s_.inv[row.arg];
        s_.inv[row.arg] = s_.pendingItem;
        s_.pendingItem = NONE;
        setBody("");
        bodyPrintf("You leave the %s behind and take the %s.", kItems[dropped].name, kItems[s_.inv[row.arg]].name);
        nextRoom(true);
        return;
      }
      if (s_.phase == P_REPLACE_PICK) {
        const uint8_t forgotten = s_.skills[row.arg];
        forgetSkill(row.arg);
        s_.skills[row.arg] = s_.pendingSkill;
        s_.pendingSkill = NONE;
        setBody("");
        bodyPrintf("%s goes, and %s takes its place.", kSkills[forgotten].name, kSkills[s_.skills[row.arg]].name);
        s_.perksTaken++;
        nextRoom(true);
        return;
      }
      if (s_.phase == P_UPGRADE_PICK) {
        upgradeSkill(row.arg);
        char label[48];
        skillLabel(row.arg, label, sizeof(label));
        setBody("");
        bodyPrintf("You practise until it is second nature: %s.", label);
        s_.perksTaken++;
        nextRoom(true);
        return;
      }
      if (s_.phase == P_FORGET_PICK) {
        const uint8_t forgotten = s_.skills[row.arg];
        forgetSkill(row.arg);
        s_.maxHp = clampAdd(s_.maxHp, 2, 99);
        s_.hp = clampAdd(s_.hp, 2, s_.maxHp);
        setBody("");
        bodyPrintf("You put %s down here, and it stays put down. +2 max HP.", kSkills[forgotten].name);
        nextRoom(true);
        return;
      }
      return;

    case RA_KEEP:
      if (s_.phase == P_REPLACE_PICK) {
        setBody("");
        bodyPrintf("You keep what you know. %s stays where you found it.", kSkills[s_.pendingSkill].name);
        s_.pendingSkill = NONE;
        nextRoom(true);
        return;
      }
      if (s_.phase == P_DROP_PICK) {
        setBody("");
        bodyPrintf("The %s stays where it is.", kItems[s_.pendingItem].name);
        s_.pendingItem = NONE;
        nextRoom(true);
        return;
      }
      setBody("");
      for (int i = 0; i < 3; i++) s_.offer[i] = NONE;
      nextRoom(true);
      return;

    case RA_BACK:
      goBack();
      return;

    case RA_CONTINUE:
      if (s_.pendingPerk) {
        offerPerks();
        return;
      }
      setBody("");
      nextRoom(true);
      return;

    case RA_TITLE:
      reset();
      return;

    default:
      return;
  }
}

// ------------------------------------------------------------------ save

size_t Game::toSaveString(char* out, size_t size) const {
  if (out == nullptr || size == 0) return 0;
  int written =
      snprintf(out, size,
               "1;%u;%u;%u;%u;%u;%u;%u;%u;%u;%u;%u;%u;%u;%u;%u;%u;%u;%u;%u;"
               "%u;%u;%u;%u;%u;%u;"
               "%u;%u;%u;"
               "%u;%u;%u;"
               "%u;%u;%u;"
               "%u;%u;%u;"
               "%u;%u;%u;%u;"
               "%u;%u;%u;%u;%u;%u;%u;%u",
               static_cast<unsigned>(s_.seed), static_cast<unsigned>(s_.rngState), s_.turn, s_.gold, s_.classId,
               s_.depth, s_.hp, s_.maxHp, s_.atk, s_.def, s_.traits, s_.identified, s_.roomIdx, s_.phase, s_.monsterIdx,
               s_.monsterHp, s_.defending, s_.roomsThisDepth, s_.perksTaken, s_.bossPending, s_.inv[0], s_.inv[1],
               s_.inv[2], s_.inv[3], s_.inv[4], s_.inv[5], s_.offer[0], s_.offer[1], s_.offer[2], s_.skills[0],
               s_.skills[1], s_.skills[2], s_.cooldowns[0], s_.cooldowns[1], s_.cooldowns[2], s_.skillMods[0],
               s_.skillMods[1], s_.skillMods[2], s_.recent[0], s_.recent[1], s_.recent[2], s_.recent[3],
               s_.pendingSkill, s_.pendingItem, s_.pendingPerk, s_.firstTurn, s_.guard, s_.rally, s_.stunned);
  if (written < 0) return 0;
  if (static_cast<size_t>(written) >= size) {
    out[0] = '\0';
    return 0;
  }
  return static_cast<size_t>(written);
}

bool Game::fromSaveString(const char* text) {
  if (text == nullptr || *text == '\0') return false;

  unsigned long values[50] = {0};
  const char* cursor = text;
  for (size_t i = 0; i < 50; i++) {
    char* end = nullptr;
    const unsigned long value = strtoul(cursor, &end, 10);
    if (end == cursor) return false;
    values[i] = value;
    cursor = end;
    if (i + 1 < 50) {
      if (*cursor != ';') return false;
      cursor++;
    }
  }
  if (values[0] != 1) return false;

  State next;
  size_t i = 1;
  next.seed = static_cast<uint32_t>(values[i++]);
  next.rngState = static_cast<uint32_t>(values[i++]);
  next.turn = static_cast<uint16_t>(values[i++]);
  next.gold = static_cast<uint16_t>(values[i++]);
  next.classId = static_cast<uint8_t>(values[i++]);
  next.depth = static_cast<uint8_t>(values[i++]);
  next.hp = static_cast<uint8_t>(values[i++]);
  next.maxHp = static_cast<uint8_t>(values[i++]);
  next.atk = static_cast<uint8_t>(values[i++]);
  next.def = static_cast<uint8_t>(values[i++]);
  next.traits = static_cast<uint8_t>(values[i++]);
  next.identified = static_cast<uint8_t>(values[i++]);
  next.roomIdx = static_cast<uint8_t>(values[i++]);
  next.phase = static_cast<uint8_t>(values[i++]);
  next.monsterIdx = static_cast<uint8_t>(values[i++]);
  next.monsterHp = static_cast<uint8_t>(values[i++]);
  next.defending = static_cast<uint8_t>(values[i++]);
  next.roomsThisDepth = static_cast<uint8_t>(values[i++]);
  next.perksTaken = static_cast<uint8_t>(values[i++]);
  next.bossPending = static_cast<uint8_t>(values[i++]);
  for (uint8_t& slot : next.inv) slot = static_cast<uint8_t>(values[i++]);
  for (uint8_t& slot : next.offer) slot = static_cast<uint8_t>(values[i++]);
  for (uint8_t& slot : next.skills) slot = static_cast<uint8_t>(values[i++]);
  for (uint8_t& slot : next.cooldowns) slot = static_cast<uint8_t>(values[i++]);
  for (uint8_t& slot : next.skillMods) slot = static_cast<uint8_t>(values[i++]);
  for (uint8_t& slot : next.recent) slot = static_cast<uint8_t>(values[i++]);
  next.pendingSkill = static_cast<uint8_t>(values[i++]);
  next.pendingItem = static_cast<uint8_t>(values[i++]);
  next.pendingPerk = static_cast<uint8_t>(values[i++]);
  next.firstTurn = static_cast<uint8_t>(values[i++]);
  next.guard = static_cast<uint8_t>(values[i++]);
  next.rally = static_cast<uint8_t>(values[i++]);
  next.stunned = static_cast<uint8_t>(values[i++]);

  // Anything that would index a table out of range means a file from another
  // build, or a corrupted one; either way the run is not resumable.
  if (next.phase > P_WON) return false;
  if (next.classId >= CLASS_COUNT) return false;
  if (next.depth == 0 || next.depth > MAX_DEPTH) return false;
  if (next.monsterIdx != NONE && next.monsterIdx >= MONSTER_COUNT) return false;
  if ((next.roomIdx & 0x80) == 0 && next.roomIdx >= ROOM_COUNT) return false;
  for (uint8_t slot : next.inv)
    if (slot != NONE && slot >= ITEM_COUNT) return false;
  for (uint8_t slot : next.skills)
    if (slot != NONE && slot >= SKILL_COUNT) return false;
  if (next.pendingSkill != NONE && next.pendingSkill >= SKILL_COUNT) return false;
  if (next.pendingItem != NONE && next.pendingItem >= ITEM_COUNT) return false;
  if (next.rngState == 0) return false;

  s_ = next;
  clearLog();
  rebuildBody();
  return true;
}

}  // namespace tb
