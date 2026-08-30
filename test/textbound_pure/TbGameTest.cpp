#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

#include "TbGame.h"

using tb::Game;

namespace {

// Field indices into the save string, in toSaveString()'s order. Crafting a
// save is how these tests reach states (depth 9, a cursed skill, a full pack)
// that would otherwise take hundreds of lucky picks to reach.
enum Field {
  F_VERSION = 0,
  F_SEED,
  F_RNG,
  F_TURN,
  F_GOLD,
  F_CLASS,
  F_DEPTH,
  F_HP,
  F_MAXHP,
  F_ATK,
  F_DEF,
  F_TRAITS,
  F_IDENT,
  F_ROOM,
  F_PHASE,
  F_MON,
  F_MONHP,
  F_DEFENDING,
  F_ROOMS_THIS_DEPTH,
  F_PERKS,
  F_BOSS_PENDING,
  F_INV0,
  F_OFF0 = F_INV0 + 6,
  F_SK0 = F_OFF0 + 3,
  F_CD0 = F_SK0 + 3,
  F_SM0 = F_CD0 + 3,
  F_REC0 = F_SM0 + 3,
  F_PENDING_SKILL = F_REC0 + 4,
  F_PENDING_ITEM,
  F_PENDING_PERK,
  F_FIRST_TURN,
  F_GUARD,
  F_RALLY,
  F_STUNNED,
  F_COUNT,
};

std::vector<std::string> split(const std::string& text) {
  std::vector<std::string> out;
  size_t start = 0;
  while (start <= text.size()) {
    const size_t end = text.find(';', start);
    if (end == std::string::npos) {
      out.push_back(text.substr(start));
      break;
    }
    out.push_back(text.substr(start, end - start));
    start = end + 1;
  }
  return out;
}

std::string join(const std::vector<std::string>& parts) {
  std::string out;
  for (size_t i = 0; i < parts.size(); i++) {
    if (i) out += ';';
    out += parts[i];
  }
  return out;
}

std::string saveOf(const Game& game) {
  char buffer[Game::MAX_SAVE_LENGTH + 64];
  const size_t written = game.toSaveString(buffer, sizeof(buffer));
  EXPECT_GT(written, 0u);
  return std::string(buffer);
}

Game freshRun(uint8_t classId = 0, uint32_t seed = 12345) {
  Game game;
  Game::Hooks hooks;
  game.startRun(seed, classId, hooks);
  return game;
}

// Builds a run, then rewrites the named fields and loads it back.
Game crafted(const std::vector<std::pair<int, int>>& overrides, uint8_t classId = 0, uint32_t seed = 12345) {
  Game game = freshRun(classId, seed);
  std::vector<std::string> parts = split(saveOf(game));
  EXPECT_EQ(parts.size(), static_cast<size_t>(F_COUNT));
  for (const auto& item : overrides) parts[item.first] = std::to_string(item.second);
  Game loaded;
  EXPECT_TRUE(loaded.fromSaveString(join(parts).c_str()));
  return loaded;
}

int rowWith(const Game& game, const char* prefix) {
  char label[96];
  for (int i = 0; i < game.rowCount(); i++) {
    game.rowLabel(i, label, sizeof(label));
    if (strncmp(label, prefix, strlen(prefix)) == 0) return i;
  }
  return -1;
}

std::string labelOf(const Game& game, int row) {
  char label[96];
  game.rowLabel(row, label, sizeof(label));
  return std::string(label);
}

}  // namespace

// ------------------------------------------------------------------ setup

TEST(TextboundRun, StartRunAppliesTheClassLoadout) {
  const Game game = freshRun(0);
  const tb::ClassDef& warden = tb::kClasses[0];
  EXPECT_EQ(game.state().hp, warden.hp);
  EXPECT_EQ(game.state().maxHp, warden.hp);
  EXPECT_EQ(game.state().atk, warden.atk);
  EXPECT_EQ(game.state().traits, warden.traits);
  EXPECT_EQ(game.state().skills[0], warden.skillId);
  EXPECT_EQ(game.knownSkillCount(), 1);
  EXPECT_EQ(game.phase(), Game::P_ROOM);
  EXPECT_GT(game.rowCount(), 1);
  EXPECT_FALSE(std::string(game.narrative()).empty());
}

TEST(TextboundRun, PrepareRunOffersEveryClass) {
  Game game;
  Game::Hooks hooks;
  game.prepareRun(777, hooks);
  EXPECT_EQ(game.phase(), Game::P_CLASS_PICK);
  ASSERT_EQ(game.rowCount(), static_cast<int>(tb::CLASS_COUNT));
  game.pick(1);
  EXPECT_EQ(game.phase(), Game::P_ROOM);
  EXPECT_EQ(game.state().classId, 1u);
  EXPECT_EQ(game.state().seed, 777u);
}

TEST(TextboundRun, SameSeedAndPicksReplayIdentically) {
  Game a = freshRun(1, 0xABCDEF01);
  Game b = freshRun(1, 0xABCDEF01);
  for (int step = 0; step < 200; step++) {
    const int countA = a.rowCount();
    const int countB = b.rowCount();
    ASSERT_EQ(countA, countB) << "diverged at step " << step;
    ASSERT_EQ(std::string(a.narrative()), std::string(b.narrative())) << "diverged at step " << step;
    if (countA == 0) break;
    const int row = step % countA;
    a.pick(row);
    b.pick(row);
  }
  EXPECT_EQ(saveOf(a), saveOf(b));
}

TEST(TextboundRun, DifferentSeedsDiverge) {
  Game a = freshRun(0, 11111);
  Game b = freshRun(0, 22222);
  bool differs = false;
  for (int step = 0; step < 60 && !differs; step++) {
    if (a.rowCount() == 0 || b.rowCount() == 0) break;
    a.pick(0);
    b.pick(0);
    differs = std::string(a.narrative()) != std::string(b.narrative());
  }
  EXPECT_TRUE(differs);
}

// ------------------------------------------------------------------ saving

TEST(TextboundSave, RoundTripsExactly) {
  Game game = freshRun(2, 4242);
  for (int i = 0; i < 25; i++) {
    if (game.rowCount() == 0) break;
    game.pick(i % game.rowCount());
  }
  const std::string first = saveOf(game);
  Game loaded;
  ASSERT_TRUE(loaded.fromSaveString(first.c_str()));
  EXPECT_EQ(saveOf(loaded), first);
  EXPECT_EQ(loaded.phase(), game.phase());
  EXPECT_EQ(loaded.rowCount(), game.rowCount());
}

TEST(TextboundSave, ResumedRunContinuesTheSameSequence) {
  Game game = freshRun(0, 999);
  for (int i = 0; i < 10; i++) game.pick(0);
  Game resumed;
  ASSERT_TRUE(resumed.fromSaveString(saveOf(game).c_str()));
  for (int i = 0; i < 20; i++) {
    if (game.rowCount() == 0) break;
    game.pick(0);
    resumed.pick(0);
  }
  EXPECT_EQ(saveOf(game), saveOf(resumed));
}

TEST(TextboundSave, StaysWithinTheStoreCap) {
  Game game = freshRun(0, 31337);
  for (int i = 0; i < 400; i++) {
    if (game.rowCount() == 0) break;
    game.pick(i % game.rowCount());
    EXPECT_LT(saveOf(game).size(), Game::MAX_SAVE_LENGTH);
  }
}

TEST(TextboundSave, RejectsGarbage) {
  Game game;
  EXPECT_FALSE(game.fromSaveString(""));
  EXPECT_FALSE(game.fromSaveString(nullptr));
  EXPECT_FALSE(game.fromSaveString("not a save"));
  EXPECT_FALSE(game.fromSaveString("1;2;3"));
  // Right shape, wrong version.
  std::vector<std::string> parts = split(saveOf(freshRun()));
  parts[F_VERSION] = "2";
  EXPECT_FALSE(game.fromSaveString(join(parts).c_str()));
}

TEST(TextboundSave, RejectsOutOfRangeIndices) {
  const std::string good = saveOf(freshRun());
  const int bad[][2] = {{F_CLASS, 99}, {F_DEPTH, 0}, {F_DEPTH, 99}, {F_MON, 200}, {F_PHASE, 99}, {F_SK0, 99}};
  for (const auto& item : bad) {
    std::vector<std::string> parts = split(good);
    parts[item[0]] = std::to_string(item[1]);
    Game game;
    EXPECT_FALSE(game.fromSaveString(join(parts).c_str())) << "field " << item[0] << " = " << item[1];
  }
}

// ------------------------------------------------------------------ combat

TEST(TextboundCombat, AttackAlwaysLandsAtLeastOne) {
  // Boss with DEF 4 against a 1-ATK hero: the floor is what keeps this winnable.
  Game game = crafted({{F_PHASE, Game::P_COMBAT}, {F_MON, tb::BOSS_FINAL}, {F_MONHP, 60}, {F_ATK, 1}, {F_DEPTH, 10}});
  const int attack = rowWith(game, "Attack");
  ASSERT_GE(attack, 0);
  game.pick(attack);
  EXPECT_LT(game.state().monsterHp, 60u);
}

TEST(TextboundCombat, DefendHalvesAndSturdyBlocks) {
  // Cutpurse (no Sturdy) takes half; Warden (Sturdy) takes none.
  Game soft = crafted({{F_PHASE, Game::P_COMBAT},
                       {F_MON, 5},
                       {F_MONHP, 40},
                       {F_HP, 30},
                       {F_MAXHP, 30},
                       {F_DEF, 0},
                       {F_DEPTH, 6},
                       {F_TRAITS, 0}});
  const uint8_t before = soft.state().hp;
  soft.pick(rowWith(soft, "Defend"));
  const int softLoss = before - soft.state().hp;
  EXPECT_GT(softLoss, 0);

  Game sturdy = crafted({{F_PHASE, Game::P_COMBAT},
                         {F_MON, 5},
                         {F_MONHP, 40},
                         {F_HP, 30},
                         {F_MAXHP, 30},
                         {F_DEF, 0},
                         {F_DEPTH, 6},
                         {F_TRAITS, tb::TRAIT_STURDY}});
  sturdy.pick(rowWith(sturdy, "Defend"));
  EXPECT_EQ(sturdy.state().hp, 30u);
}

TEST(TextboundCombat, HpZeroEndsTheRun) {
  Game game = crafted({{F_PHASE, Game::P_COMBAT}, {F_MON, 7}, {F_MONHP, 60}, {F_HP, 1}, {F_DEF, 0}, {F_DEPTH, 9}});
  game.pick(rowWith(game, "Attack"));
  EXPECT_EQ(game.phase(), Game::P_DEAD);
  EXPECT_EQ(game.state().hp, 0u);
  EXPECT_EQ(game.rowCount(), 1);
}

TEST(TextboundCombat, BossesCannotBeFled) {
  Game boss = crafted({{F_PHASE, Game::P_COMBAT}, {F_MON, tb::BOSS_MID}, {F_MONHP, 40}, {F_DEPTH, 5}});
  const int flee = rowWith(boss, "Flee");
  ASSERT_GE(flee, 0);
  EXPECT_TRUE(boss.rowDimmed(flee));
  boss.pick(flee);
  EXPECT_EQ(boss.phase(), Game::P_COMBAT);

  Game normal = crafted({{F_PHASE, Game::P_COMBAT}, {F_MON, 0}, {F_MONHP, 6}, {F_DEPTH, 1}});
  EXPECT_FALSE(normal.rowDimmed(rowWith(normal, "Flee")));
}

TEST(TextboundCombat, QuickTraitAlwaysEscapes) {
  Game game = crafted({{F_PHASE, Game::P_COMBAT}, {F_MON, 0}, {F_MONHP, 6}, {F_DEPTH, 1}, {F_TRAITS, tb::TRAIT_QUICK}});
  game.pick(rowWith(game, "Flee"));
  EXPECT_EQ(game.phase(), Game::P_ROOM);
}

TEST(TextboundCombat, KillingAMonsterPaysAndOffersContinue) {
  Game game = crafted({{F_PHASE, Game::P_COMBAT}, {F_MON, 0}, {F_MONHP, 1}, {F_ATK, 9}, {F_GOLD, 0}, {F_DEPTH, 1}});
  game.pick(rowWith(game, "Attack"));
  EXPECT_EQ(game.phase(), Game::P_AFTERMATH);
  EXPECT_EQ(game.state().gold, tb::kMonsters[0].gold);
  ASSERT_EQ(game.rowCount(), 1);
  EXPECT_EQ(labelOf(game, 0), "Continue");
}

// ------------------------------------------------------------------ skills

TEST(TextboundSkills, CooldownBlocksReuseThenTicksDown) {
  Game game = crafted({{F_PHASE, Game::P_COMBAT}, {F_MON, 5}, {F_MONHP, 60}, {F_HP, 40}, {F_MAXHP, 40}, {F_DEPTH, 6}});
  ASSERT_EQ(game.state().skills[0], 0u);  // Warden knows Cleave
  const uint8_t cooldown = game.effectiveCooldown(0);
  ASSERT_GT(cooldown, 1u);

  game.pick(rowWith(game, "Cleave"));
  EXPECT_EQ(game.state().cooldowns[0], cooldown);

  // Attacking a few times walks it back to zero, one per action.
  for (uint8_t i = 0; i < cooldown; i++) {
    const uint8_t before = game.state().cooldowns[0];
    game.pick(rowWith(game, "Attack"));
    if (game.phase() != Game::P_COMBAT) break;
    EXPECT_EQ(game.state().cooldowns[0], before - 1);
  }
}

TEST(TextboundSkills, CooldownIsClearedByANewFight) {
  Game game = crafted({{F_PHASE, Game::P_ROOM}, {F_ROOM, 4}, {F_DEPTH, 2}, {F_CD0, 3}});
  ASSERT_EQ(game.state().cooldowns[0], 3u);
  // kRooms[4]'s second choice is a fight on this level.
  game.pick(1);
  ASSERT_EQ(game.phase(), Game::P_COMBAT);
  EXPECT_EQ(game.state().cooldowns[0], 0u);
  EXPECT_EQ(game.state().firstTurn, 1u);
}

TEST(TextboundSkills, StunCostsTheMonsterItsTurn) {
  // Slot 0 holds Stun; the hero should take nothing back this turn.
  Game game = crafted({{F_PHASE, Game::P_COMBAT},
                       {F_MON, 5},
                       {F_MONHP, 60},
                       {F_HP, 30},
                       {F_MAXHP, 30},
                       {F_DEF, 0},
                       {F_DEPTH, 6},
                       {F_SK0, 3}});
  const uint8_t before = game.state().hp;
  const int row = rowWith(game, "Stun");
  ASSERT_GE(row, 0);
  game.pick(row);
  EXPECT_EQ(game.state().hp, before);
  EXPECT_EQ(game.state().stunned, 0u);  // consumed by the skipped turn
}

TEST(TextboundSkills, GuardAndRallyLastTheWholeFight) {
  Game guard = crafted(
      {{F_PHASE, Game::P_COMBAT}, {F_MON, 5}, {F_MONHP, 60}, {F_HP, 30}, {F_MAXHP, 30}, {F_DEPTH, 6}, {F_SK0, 4}});
  guard.pick(rowWith(guard, "Guard"));
  EXPECT_EQ(guard.state().guard, tb::kSkills[4].arg);

  Game rally = crafted(
      {{F_PHASE, Game::P_COMBAT}, {F_MON, 5}, {F_MONHP, 60}, {F_HP, 30}, {F_MAXHP, 30}, {F_DEPTH, 6}, {F_SK0, 5}});
  rally.pick(rowWith(rally, "Rally"));
  EXPECT_EQ(rally.state().rally, tb::kSkills[5].arg);
}

TEST(TextboundSkills, UpgradeLowersCooldownThenRaisesEffect) {
  Game level0 = crafted({{F_SM0, 0}});
  Game level1 = crafted({{F_SM0, 1}});
  Game level2 = crafted({{F_SM0, 2}});
  EXPECT_EQ(level1.effectiveCooldown(0), level0.effectiveCooldown(0) - 1);
  EXPECT_EQ(level1.effectiveArg(0), level0.effectiveArg(0));
  EXPECT_GT(level2.effectiveArg(0), level0.effectiveArg(0));
  EXPECT_EQ(level2.effectiveCooldown(0), level1.effectiveCooldown(0));
}

TEST(TextboundSkills, SkillMenuOpensOnlyWhenMoreThanOneIsKnown) {
  Game one = crafted({{F_PHASE, Game::P_COMBAT}, {F_MON, 0}, {F_MONHP, 20}, {F_DEPTH, 1}});
  EXPECT_GE(rowWith(one, "Cleave"), 0);  // used straight from the combat list
  EXPECT_EQ(rowWith(one, "Skill"), -1);

  Game two = crafted({{F_PHASE, Game::P_COMBAT}, {F_MON, 0}, {F_MONHP, 20}, {F_DEPTH, 1}, {F_SK0 + 1, 2}});
  const int skill = rowWith(two, "Skill");
  ASSERT_GE(skill, 0);
  two.pick(skill);
  EXPECT_EQ(two.phase(), Game::P_SKILL_MENU);
  EXPECT_EQ(two.rowCount(), 3);  // two skills plus Back
  two.pick(2);
  EXPECT_EQ(two.phase(), Game::P_COMBAT);
}

TEST(TextboundSkills, CursedSkillIsUnusableAndLiftsAfterItsFights) {
  // Cursed with two fights left.
  const int cursedMod = 0x04 | (2 << 3);
  Game game =
      crafted({{F_PHASE, Game::P_COMBAT}, {F_MON, 0}, {F_MONHP, 1}, {F_ATK, 9}, {F_DEPTH, 1}, {F_SM0, cursedMod}});
  ASSERT_TRUE(game.skillCursed(0));
  const int row = rowWith(game, "Cleave");
  ASSERT_GE(row, 0);
  EXPECT_TRUE(game.rowDimmed(row));
  game.pick(row);
  EXPECT_EQ(game.phase(), Game::P_COMBAT);  // refused, no turn spent

  game.pick(rowWith(game, "Attack"));  // wins the fight
  ASSERT_EQ(game.phase(), Game::P_AFTERMATH);
  EXPECT_TRUE(game.skillCursed(0));
  EXPECT_EQ(game.curseFightsLeft(0), 1u);
}

TEST(TextboundSkills, HolyWaterLiftsACurse) {
  const int cursedMod = 0x04 | (3 << 3);
  Game game = crafted({{F_PHASE, Game::P_COMBAT},
                       {F_MON, 5},
                       {F_MONHP, 60},
                       {F_HP, 30},
                       {F_MAXHP, 30},
                       {F_DEPTH, 6},
                       {F_SM0, cursedMod},
                       {F_INV0, 4}});
  ASSERT_TRUE(game.skillCursed(0));
  const int item = rowWith(game, "Item");
  ASSERT_GE(item, 0);
  game.pick(item);
  ASSERT_EQ(game.phase(), Game::P_ITEM_MENU);
  game.pick(rowWith(game, "Holy Water"));
  EXPECT_FALSE(game.skillCursed(0));
}

TEST(TextboundSkills, CursingWithNothingKnownIsHarmless) {
  // Shrine room, no skills at all: the effect must not touch an empty slot.
  Game game = crafted({{F_PHASE, Game::P_ROOM}, {F_ROOM, 1}, {F_DEPTH, 3}, {F_SK0, tb::NONE}});
  ASSERT_EQ(game.knownSkillCount(), 0);
  game.pick(1);  // "Take the coins" -> EF_CURSE
  EXPECT_EQ(game.knownSkillCount(), 0);
  EXPECT_NE(game.phase(), Game::P_DEAD);
}

TEST(TextboundSkills, LearningWithNoRoomAsksWhatToDrop) {
  // Three skills known, shrine teacher offers a fourth.
  Game game = crafted(
      {{F_PHASE, Game::P_ROOM}, {F_ROOM, 12}, {F_DEPTH, 6}, {F_GOLD, 200}, {F_SK0, 0}, {F_SK0 + 1, 1}, {F_SK0 + 2, 2}});
  ASSERT_EQ(game.knownSkillCount(), 3);
  game.pick(0);  // "Let him show you" -> EF_TEACH
  ASSERT_EQ(game.phase(), Game::P_REPLACE_PICK);
  ASSERT_EQ(game.rowCount(), 4);  // three skills plus "keep what I have"

  // Keeping leaves the set untouched.
  Game keeping = game;
  keeping.pick(3);
  EXPECT_EQ(keeping.state().skills[0], 0u);
  EXPECT_EQ(keeping.state().skills[1], 1u);
  EXPECT_EQ(keeping.state().skills[2], 2u);
  EXPECT_EQ(keeping.phase(), Game::P_ROOM);

  // Replacing swaps exactly one slot and clears its modifiers.
  game.pick(1);
  EXPECT_EQ(game.state().skills[0], 0u);
  EXPECT_NE(game.state().skills[1], 1u);
  EXPECT_EQ(game.state().skillMods[1], 0u);
  EXPECT_EQ(game.knownSkillCount(), 3);
}

TEST(TextboundSkills, UnburdenTradesASkillForMaxHp) {
  Game game = crafted({{F_PHASE, Game::P_ROOM}, {F_ROOM, 1}, {F_DEPTH, 4}, {F_HP, 10}, {F_MAXHP, 20}});
  const uint8_t maxHp = game.state().maxHp;
  game.pick(3);  // "Unburden yourself here"
  ASSERT_EQ(game.phase(), Game::P_FORGET_PICK);
  game.pick(0);
  EXPECT_EQ(game.knownSkillCount(), 0);
  EXPECT_EQ(game.state().maxHp, maxHp + 2);
}

// ------------------------------------------------------------------ items

TEST(TextboundItems, ChestOffersTwoAndTakesOne) {
  Game game = crafted({{F_PHASE, Game::P_ROOM}, {F_ROOM, 3}, {F_DEPTH, 3}});
  game.pick(0);  // "Open it"
  ASSERT_EQ(game.phase(), Game::P_CHEST_PICK);
  ASSERT_EQ(game.rowCount(), 3);  // two items plus "take neither"
  game.pick(2);
  EXPECT_EQ(game.phase(), Game::P_ROOM);
}

TEST(TextboundItems, FullPackAsksWhatToDrop) {
  Game game = crafted({{F_PHASE, Game::P_ROOM},
                       {F_ROOM, 3},
                       {F_DEPTH, 3},
                       {F_INV0, 0},
                       {F_INV0 + 1, 0},
                       {F_INV0 + 2, 0},
                       {F_INV0 + 3, 0},
                       {F_INV0 + 4, 0},
                       {F_INV0 + 5, 0}});
  game.pick(0);
  ASSERT_EQ(game.phase(), Game::P_CHEST_PICK);
  const uint8_t offered = game.state().offer[0];
  game.pick(0);
  if (tb::itemGoesInPack(tb::kItems[offered].kind)) {
    ASSERT_EQ(game.phase(), Game::P_DROP_PICK);
    EXPECT_EQ(game.rowCount(), 7);  // six slots plus "keep what I have"
    game.pick(0);
    EXPECT_EQ(game.state().inv[0], offered);
    EXPECT_EQ(game.phase(), Game::P_ROOM);
  } else {
    // Tools are used on pickup and never need a slot.
    EXPECT_EQ(game.phase(), Game::P_ROOM);
  }
}

TEST(TextboundItems, VialIdentityIsFixedForTheWholeRun) {
  // Same seed, same vial, same outcome -- twice, from different HP.
  auto drink = [](uint32_t seed, int hp) {
    Game game = crafted({{F_PHASE, Game::P_COMBAT},
                         {F_MON, 5},
                         {F_MONHP, 60},
                         {F_HP, hp},
                         {F_MAXHP, 60},
                         {F_DEPTH, 6},
                         {F_INV0, tb::VIAL_A}},
                        0, seed);
    game.pick(rowWith(game, "Item"));
    game.pick(rowWith(game, "Cloudy Vial"));
    return static_cast<int>(game.state().hp) - hp;
  };
  const int firstChange = drink(4321, 30);
  const int secondChange = drink(4321, 40);
  EXPECT_EQ(firstChange > 0, secondChange > 0);
}

TEST(TextboundItems, BuyingAtTheShopCostsGoldAndKeepsTheStallOpen) {
  Game game = crafted({{F_PHASE, Game::P_ROOM}, {F_ROOM, 2}, {F_DEPTH, 3}, {F_GOLD, 300}});
  game.pick(0);  // "Look over the cloth"
  ASSERT_EQ(game.phase(), Game::P_SHOP);
  const uint16_t before = game.state().gold;
  game.pick(0);
  EXPECT_LT(game.state().gold, before);
  EXPECT_EQ(game.phase(), Game::P_SHOP);
  game.pick(game.rowCount() - 1);  // leave
  EXPECT_EQ(game.phase(), Game::P_ROOM);
}

TEST(TextboundItems, ShopRowsAreDimmedWhenTooExpensive) {
  Game game = crafted({{F_PHASE, Game::P_ROOM}, {F_ROOM, 2}, {F_DEPTH, 3}, {F_GOLD, 0}});
  game.pick(0);
  ASSERT_EQ(game.phase(), Game::P_SHOP);
  EXPECT_TRUE(game.rowDimmed(0));
  game.pick(0);
  EXPECT_EQ(game.state().gold, 0u);
  EXPECT_EQ(game.phase(), Game::P_SHOP);
}

// ------------------------------------------------------------------ progression

TEST(TextboundProgress, MonsterStrengthRisesWithDepth) {
  for (uint8_t id = 0; id < tb::MONSTER_COUNT; id++) {
    if (tb::kMonsters[id].boss) continue;
    EXPECT_LT(Game::monsterMaxHp(id, 1), Game::monsterMaxHp(id, 9));
    EXPECT_LE(Game::monsterAtk(id, 1), Game::monsterAtk(id, 9));
  }
  // Bosses are authored, not scaled.
  EXPECT_EQ(Game::monsterMaxHp(tb::BOSS_FINAL, 10), tb::kMonsters[tb::BOSS_FINAL].hp);
}

TEST(TextboundProgress, DescendingIntoDepthFiveAndTenLandsOnABoss) {
  for (int from : {4, 9}) {
    Game game = crafted({{F_PHASE, Game::P_ROOM}, {F_ROOM, 4}, {F_DEPTH, from}});
    game.pick(0);  // "Go down"
    ASSERT_EQ(game.state().depth, from + 1);
    ASSERT_EQ(game.phase(), Game::P_ROOM);
    ASSERT_EQ(game.rowCount(), 1) << "boss landings offer no way around";
    game.pick(0);
    EXPECT_EQ(game.phase(), Game::P_COMBAT);
    EXPECT_TRUE(tb::kMonsters[game.state().monsterIdx].boss);
  }
}

TEST(TextboundProgress, EveryThirdDepthOffersAPerk) {
  Game game = crafted({{F_PHASE, Game::P_ROOM}, {F_ROOM, 4}, {F_DEPTH, 2}});
  game.pick(0);
  ASSERT_EQ(game.state().depth, 3u);
  ASSERT_EQ(game.phase(), Game::P_PERK_PICK);
  ASSERT_EQ(game.rowCount(), 3);
  const uint8_t maxHp = game.state().maxHp;
  const uint8_t atk = game.state().atk;
  const int known = game.knownSkillCount();
  game.pick(0);
  const bool changed = game.state().maxHp != maxHp || game.state().atk != atk || game.knownSkillCount() != known ||
                       game.state().traits != tb::kClasses[0].traits || game.skillLevel(0) > 0;
  EXPECT_TRUE(changed);
}

TEST(TextboundProgress, BeatingTheMidBossOwesAPerk) {
  Game game = crafted({{F_PHASE, Game::P_COMBAT}, {F_MON, tb::BOSS_MID}, {F_MONHP, 1}, {F_ATK, 20}, {F_DEPTH, 5}});
  game.pick(rowWith(game, "Attack"));
  ASSERT_EQ(game.phase(), Game::P_AFTERMATH);
  EXPECT_EQ(game.state().pendingPerk, 1u);
  game.pick(0);
  EXPECT_EQ(game.phase(), Game::P_PERK_PICK);
}

TEST(TextboundProgress, BeatingTheFinalBossEndsInTheEpilogue) {
  Game game = crafted({{F_PHASE, Game::P_COMBAT}, {F_MON, tb::BOSS_FINAL}, {F_MONHP, 1}, {F_ATK, 20}, {F_DEPTH, 10}});
  game.pick(rowWith(game, "Attack"));
  ASSERT_EQ(game.phase(), Game::P_WON);
  const std::string epilogue = game.narrative();
  for (uint8_t i = 0; i < tb::EPILOGUE_PAGES; i++) {
    EXPECT_NE(epilogue.find(tb::kEpilogue[i]), std::string::npos) << "epilogue page " << static_cast<int>(i);
  }
  ASSERT_EQ(game.rowCount(), 1);
  game.pick(0);
  EXPECT_EQ(game.phase(), Game::P_TITLE);
}

TEST(TextboundProgress, DepthNeverPassesTheBottom) {
  Game game = crafted({{F_PHASE, Game::P_ROOM}, {F_ROOM, 4}, {F_DEPTH, 10}});
  game.pick(0);
  EXPECT_LE(game.state().depth, tb::MAX_DEPTH);
}

// ------------------------------------------------------------------ content

TEST(TextboundContent, EveryRoomTemplateIsWellFormed) {
  for (uint8_t i = 0; i < tb::ROOM_COUNT; i++) {
    const tb::RoomTemplate& room = tb::kRooms[i];
    EXPECT_GE(room.choiceCount, 1) << "room " << static_cast<int>(i);
    EXPECT_LE(room.choiceCount, 4) << "room " << static_cast<int>(i);
    EXPECT_LE(room.minDepth, room.maxDepth) << "room " << static_cast<int>(i);
    EXPECT_LT(strlen(room.text), 500u) << "room " << static_cast<int>(i);
    for (uint8_t c = 0; c < room.choiceCount; c++) {
      EXPECT_NE(room.choices[c].label, nullptr);
      EXPECT_GT(strlen(room.choices[c].label), 0u);
    }
  }
}

TEST(TextboundContent, EveryDepthHasSomethingToDrawFrom) {
  for (uint8_t depth = 1; depth <= tb::MAX_DEPTH; depth++) {
    int rooms = 0;
    for (uint8_t i = 0; i < tb::ROOM_COUNT; i++) {
      if (tb::kRooms[i].weight > 0 && tb::kRooms[i].minDepth <= depth && tb::kRooms[i].maxDepth >= depth) rooms++;
    }
    EXPECT_GE(rooms, 5) << "depth " << static_cast<int>(depth);

    int monsters = 0;
    for (uint8_t i = 0; i < tb::MONSTER_COUNT; i++) {
      const tb::MonsterDef& m = tb::kMonsters[i];
      if (!m.boss && m.minDepth <= depth && m.maxDepth >= depth) monsters++;
    }
    EXPECT_GE(monsters, 1) << "depth " << static_cast<int>(depth);
  }
}

TEST(TextboundContent, EverySkillHasALearnPerk) {
  for (uint8_t id = 0; id < tb::SKILL_COUNT; id++) {
    bool found = false;
    for (uint8_t p = 0; p < tb::PERK_COUNT; p++) {
      if (tb::kPerks[p].kind == tb::PK_LEARN && tb::kPerks[p].arg == id) found = true;
    }
    EXPECT_TRUE(found) << "skill " << static_cast<int>(id);
  }
}

// ------------------------------------------------------------------ fuzz

TEST(TextboundFuzz, RandomPlayNeverWedgesOrOverflows) {
  for (uint32_t seed = 1; seed <= 400; seed++) {
    Game game;
    Game::Hooks hooks;
    game.startRun(seed * 2654435761u + 1u, static_cast<uint8_t>(seed % tb::CLASS_COUNT), hooks);
    uint32_t noise = seed * 7919u + 13u;
    for (int step = 0; step < 300; step++) {
      const int count = game.rowCount();
      ASSERT_GT(count, 0) << "seed " << seed << " step " << step << " phase " << game.phase();
      ASSERT_LE(count, 8);
      noise = noise * 1103515245u + 12345u;
      const int row = static_cast<int>((noise >> 16) % static_cast<uint32_t>(count));

      char label[96];
      game.rowLabel(row, label, sizeof(label));
      ASSERT_GT(strlen(label), 0u) << "seed " << seed << " row " << row << " phase " << game.phase();
      ASSERT_LT(strlen(game.narrative()), 900u);

      game.pick(row);
      ASSERT_LE(game.state().hp, game.state().maxHp);
      ASSERT_LE(game.state().depth, tb::MAX_DEPTH);
      if (game.runActive()) {
        ASSERT_LT(saveOf(game).size(), Game::MAX_SAVE_LENGTH);
      }
      if (game.phase() == Game::P_TITLE) break;
    }
  }
}

TEST(TextboundFuzz, ARunCanBeSavedAndResumedAtAnyPoint) {
  for (uint32_t seed = 1; seed <= 60; seed++) {
    Game game = freshRun(static_cast<uint8_t>(seed % tb::CLASS_COUNT), seed * 2246822519u + 7u);
    uint32_t noise = seed * 104729u + 11u;
    for (int step = 0; step < 80; step++) {
      if (!game.runActive()) break;
      const std::string saved = saveOf(game);
      Game reloaded;
      ASSERT_TRUE(reloaded.fromSaveString(saved.c_str())) << "seed " << seed << " step " << step;
      ASSERT_EQ(saveOf(reloaded), saved);
      ASSERT_EQ(reloaded.rowCount(), game.rowCount());

      const int count = game.rowCount();
      if (count == 0) break;
      noise = noise * 1103515245u + 12345u;
      game.pick(static_cast<int>((noise >> 16) % static_cast<uint32_t>(count)));
    }
  }
}
