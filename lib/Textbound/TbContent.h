#pragma once

#include <cstdint>

// Every string here is a flash-resident literal: the tables are constexpr and
// only ever read through const pointers, so nothing lands in DRAM.
namespace tb {

constexpr uint8_t MAX_DEPTH = 10;
constexpr uint8_t MID_BOSS_DEPTH = 5;
constexpr uint8_t INV_SLOTS = 6;
constexpr uint8_t SKILL_SLOTS = 3;
constexpr uint8_t NONE = 0xFF;

// ---------------------------------------------------------------- effects

enum Effect : uint8_t {
  EF_NOTHING = 0,
  EF_DESCEND,       // next depth
  EF_EXPLORE,       // another room at this depth; ambush risk grows
  EF_FIGHT,         // arg = monster id
  EF_FIGHT_RANDOM,  // monster drawn from the depth band
  EF_BOSS,          // the depth's boss
  EF_CHEST,         // pick one of the things in it (1-3, sometimes nothing)
  EF_LOOT,          // take everything found; the pile is 0-3 things
  EF_GOLD_GAIN,     // arg gold
  EF_GOLD_LOSS,
  EF_HEAL,    // arg HP
  EF_DAMAGE,  // arg HP
  EF_GAMBLE,  // arg gold staked
  EF_SHOP,
  EF_REST,      // heal, but may wake something
  EF_CURSE,     // curses a known skill
  EF_CLEANSE,   // lifts every curse
  EF_UNBURDEN,  // forget a skill for +2 max HP
  EF_TEACH,     // arg = gold price; skill drawn from the pool
};

// ---------------------------------------------------------------- skills

enum SkillKind : uint8_t {
  SK_CLEAVE = 0,  // arg = damage multiplier
  SK_BACKSTAB,    // arg = multiplier, only on the fight's first action
  SK_PRAYER,      // arg = HP healed
  SK_STUN,        // the monster loses its next turn
  SK_GUARD,       // arg = DEF for the rest of this fight
  SK_RALLY,       // arg = ATK for the rest of this fight
};

struct SkillDef {
  const char* name;
  const char* blurb;
  uint8_t kind;
  uint8_t arg;
  uint8_t cooldown;
};

inline constexpr SkillDef kSkills[] = {
    {"Cleave", "A heavy two-handed swing.", SK_CLEAVE, 2, 3},
    {"Backstab", "Devastating, but only as an opener.", SK_BACKSTAB, 3, 4},
    {"Prayer", "Words that close a wound.", SK_PRAYER, 8, 4},
    {"Stun", "A blow to the head buys a turn.", SK_STUN, 0, 4},
    {"Guard", "Set your stance for the whole fight.", SK_GUARD, 3, 3},
    {"Rally", "Anger sharpens the arm.", SK_RALLY, 2, 3},
};
constexpr uint8_t SKILL_COUNT = sizeof(kSkills) / sizeof(kSkills[0]);

// ---------------------------------------------------------------- classes

constexpr uint8_t TRAIT_LUCKY = 1 << 0;   // crits more often, better loot rolls
constexpr uint8_t TRAIT_STURDY = 1 << 1;  // Defend blocks outright
constexpr uint8_t TRAIT_QUICK = 1 << 2;   // Flee always works

struct ClassDef {
  const char* name;
  const char* blurb;
  uint8_t hp, atk, def, potions;
  uint8_t traits;
  uint8_t skillId;
};

inline constexpr ClassDef kClasses[] = {
    {"Warden", "Heavy, patient, hard to move. Guard holds.", 22, 5, 2, 1, TRAIT_STURDY, 0},
    {"Cutpurse", "Fast hands, faster feet. Nothing catches you.", 18, 4, 1, 2, TRAIT_QUICK, 1},
    {"Pilgrim", "Walks lucky, mends quickly, hits softly.", 24, 3, 2, 1, TRAIT_LUCKY, 2},
};
constexpr uint8_t CLASS_COUNT = sizeof(kClasses) / sizeof(kClasses[0]);

// ---------------------------------------------------------------- items

enum ItemKind : uint8_t {
  IT_POTION = 0,  // heals power
  IT_BOMB,        // power damage to the monster
  IT_SMOKE,       // guaranteed escape
  IT_HOLY,        // lifts every curse
  IT_VIAL,        // unknown until drunk; one heals, one poisons
  IT_ATK_UP,      // used on pickup: permanent ATK
  IT_DEF_UP,      // permanent DEF
  IT_MAXHP_UP,    // permanent max HP
};

struct ItemDef {
  const char* name;
  uint8_t kind, power, price, minDepth;
};

inline constexpr ItemDef kItems[] = {
    {"Potion", IT_POTION, 8, 12, 1},  // 0
    {"Greater Potion", IT_POTION, 16, 26, 4}, {"Bomb", IT_BOMB, 10, 18, 2},
    {"Smoke Bead", IT_SMOKE, 0, 14, 2},       {"Holy Water", IT_HOLY, 0, 16, 3},
    {"Cloudy Vial", IT_VIAL, 0, 8, 1},  // 5
    {"Black Vial", IT_VIAL, 0, 8, 1},   // 6
    {"Whetstone", IT_ATK_UP, 1, 20, 2},       {"Iron Plate", IT_DEF_UP, 1, 20, 3},
    {"Old Blade", IT_ATK_UP, 2, 32, 5},       {"Traveller's Bread", IT_MAXHP_UP, 3, 22, 3},
};
constexpr uint8_t ITEM_COUNT = sizeof(kItems) / sizeof(kItems[0]);
constexpr uint8_t VIAL_A = 5;
constexpr uint8_t VIAL_B = 6;

// True for items that are drunk/thrown later; the rest are consumed the moment
// they are picked up and never occupy a slot.
constexpr bool itemGoesInPack(uint8_t kind) {
  return kind == IT_POTION || kind == IT_BOMB || kind == IT_SMOKE || kind == IT_HOLY || kind == IT_VIAL;
}

// ---------------------------------------------------------------- perks

enum PerkKind : uint8_t {
  PK_MAXHP = 0,
  PK_ATK,
  PK_DEF,
  PK_TRAIT,    // arg = trait bit
  PK_LEARN,    // arg = skill id
  PK_UPGRADE,  // sharpens a skill already known
};

struct PerkDef {
  const char* name;
  const char* blurb;
  uint8_t kind, arg;
};

inline constexpr PerkDef kPerks[] = {
    {"Toughen", "+4 max HP, and the same again in nerve.", PK_MAXHP, 4},
    {"Sharpen", "+1 ATK.", PK_ATK, 1},
    {"Harden", "+1 DEF.", PK_DEF, 1},
    {"Lucky", "Crits land more often.", PK_TRAIT, TRAIT_LUCKY},
    {"Sturdy", "Defend blocks a blow outright.", PK_TRAIT, TRAIT_STURDY},
    {"Quick", "Flee never fails.", PK_TRAIT, TRAIT_QUICK},
    {"Learn: Cleave", "A heavy two-handed swing.", PK_LEARN, 0},
    {"Learn: Backstab", "Devastating, but only as an opener.", PK_LEARN, 1},
    {"Learn: Prayer", "Words that close a wound.", PK_LEARN, 2},
    {"Learn: Stun", "A blow to the head buys a turn.", PK_LEARN, 3},
    {"Learn: Guard", "Set your stance for the whole fight.", PK_LEARN, 4},
    {"Learn: Rally", "Anger sharpens the arm.", PK_LEARN, 5},
    {"Practise", "Sharpen a skill you already carry.", PK_UPGRADE, 0},
};
constexpr uint8_t PERK_COUNT = sizeof(kPerks) / sizeof(kPerks[0]);

// ---------------------------------------------------------------- monsters

struct MonsterDef {
  const char* name;
  const char* intro;
  uint8_t hp, atk, def, gold, minDepth, maxDepth;
  bool boss;
};

inline constexpr MonsterDef kMonsters[] = {
    {"Cave Rat", "A cave rat the size of a dog turns from the pool. Its eyes hold the torchlight.", 6, 3, 0, 8, 1, 3,
     false},
    {"Blind Crawler", "Something long and pale unfolds from the ceiling, feeling the air where your face is.", 8, 4, 1,
     10, 1, 4, false},
    {"Bone Picker", "A stooped thing looks up from what it was sorting, and does not stop chewing.", 10, 5, 1, 12, 2, 6,
     false},
    {"Ash Hound", "It comes out of the dark already running, and it does not bark.", 12, 6, 2, 14, 3, 7, false},
    {"Pale Monk", "A figure in a grey habit rises from its knees. Under the hood there is no face, only more hood.", 14,
     7, 2, 18, 4, 8, false},
    {"Rust Knight", "Armour walks toward you with nothing inside it but the sound of walking.", 18, 8, 3, 22, 5, 10,
     false},
    {"Lamp Eater", "It swallows your lamplight as it comes, so that it arrives ahead of what you can see.", 16, 9, 2,
     24, 6, 10, false},
    {"Stair Wraith", "The air on the steps goes thin and cold, and takes the shape of someone who fell here.", 20, 10,
     3, 28, 7, 10, false},
    // Bosses, selected by depth and never drawn from the random pool.
    {"Warden of the Fifth",
     "A thing of rust and bone fills the stair below you, and sets down a key too large for any door.", 40, 10, 3, 60,
     5, 5, true},
    {"The Bound Text", "It is a book, and it is the size of a room, and every page of it is looking at you.", 60, 13, 4,
     100, 10, 10, true},
};
constexpr uint8_t MONSTER_COUNT = sizeof(kMonsters) / sizeof(kMonsters[0]);
constexpr uint8_t BOSS_MID = 8;
constexpr uint8_t BOSS_FINAL = 9;

// ---------------------------------------------------------------- rooms

struct Choice {
  const char* label;
  uint8_t effect, arg;
};

struct RoomTemplate {
  const char* text;
  Choice choices[4];
  uint8_t choiceCount, minDepth, maxDepth, weight;
};

inline constexpr RoomTemplate kRooms[] = {
    {"The stair ends in a low chamber that smells of wet iron. To the left a rusted grate stands ajar over a shaft. "
     "Ahead a corridor of cut stone runs straight and dry. To the right you hear dripping water and, under it, "
     "something breathing.",
     {{"Climb down the grate", EF_DAMAGE, 4},
      {"Take the dry corridor", EF_EXPLORE, 0},
      {"Go toward the breathing", EF_FIGHT_RANDOM, 0},
      {"Rest here a moment", EF_REST, 5}},
     4,
     1,
     4,
     10},

    {"A shrine: a squat stone figure, its face worn smooth, a bowl of black water at its feet. Someone has left three "
     "copper coins in the bowl. The air is very still.",
     {{"Drink from the bowl", EF_HEAL, 6},
      {"Take the coins", EF_CURSE, 0},
      {"Leave an offering", EF_CLEANSE, 0},
      {"Unburden yourself here", EF_UNBURDEN, 0}},
     4,
     1,
     10,
     5},

    {"A long hall of pillars, half of them fallen. Between two of them a merchant has set out a cloth, and on it a "
     "lamp and a few things worth carrying. He does not look up.",
     {{"Look over the cloth", EF_SHOP, 0},
      {"Ask what he is doing down here", EF_NOTHING, 0},
      {"Walk on", EF_EXPLORE, 0}},
     3,
     2,
     10,
     6},

    {"A chest sits alone in the middle of a swept floor. The sweeping is the part you do not like.",
     {{"Open it", EF_CHEST, 0}, {"Leave it alone", EF_EXPLORE, 0}},
     2,
     1,
     10,
     9},

    {"The corridor forks. One way slopes down, out of the lamplight. The other runs level, and further along it "
     "something metal shifts.",
     {{"Go down", EF_DESCEND, 0}, {"Stay on this level", EF_FIGHT_RANDOM, 0}},
     2,
     1,
     9,
     10},

    {"A dry well in the floor, and a rope still tied to its ring. Coins glint at the bottom, under a hand's depth of "
     "dust.",
     {{"Climb down for the coins", EF_GOLD_GAIN, 14},
      {"Cut the rope and take it", EF_NOTHING, 0},
      {"Move on", EF_EXPLORE, 0}},
     3,
     1,
     8,
     6},

    {"Three doors, identical, unmarked. Behind one of them somebody is counting.",
     {{"Left door", EF_CHEST, 0}, {"Middle door", EF_FIGHT_RANDOM, 0}, {"Right door", EF_GOLD_GAIN, 10}},
     3,
     2,
     10,
     7},

    {"An old campfire, long cold, and a bedroll nobody came back for. It would be a place to sleep, if you trusted "
     "the quiet.",
     {{"Sleep a while", EF_REST, 9}, {"Search the bedroll", EF_CHEST, 0}, {"Press on", EF_EXPLORE, 0}},
     3,
     2,
     10,
     7},

    {"A gambler sits at a folding table with no opponent, shuffling a deck of blank cards. \"One hand,\" he says, "
     "without looking up. \"Winner keeps the pile.\"",
     {{"Play a hand", EF_GAMBLE, 12}, {"Refuse", EF_EXPLORE, 0}},
     2,
     3,
     10,
     6},

    {"The floor here is written on: line after line of small script, running under the walls in both directions. "
     "Reading it makes your eyes ache.",
     {{"Read as far as you can", EF_CURSE, 0},
      {"Copy a passage and move on", EF_GOLD_GAIN, 8},
      {"Step over it", EF_DESCEND, 0}},
     3,
     3,
     10,
     6},

    {"A flooded gallery, waist deep and cold. Something is moving under the surface, keeping pace with you.",
     {{"Wade across", EF_FIGHT_RANDOM, 0}, {"Go around the long way", EF_DAMAGE, 3}},
     2,
     4,
     10,
     7},

    {"A collapsed archway, and past it a room lit by nothing you can find. In the light, a basin of clean water.",
     {{"Wash in the basin", EF_CLEANSE, 0}, {"Drink deeply", EF_HEAL, 10}, {"Go back", EF_EXPLORE, 0}},
     3,
     3,
     10,
     6},

    {"A cell block, doors all open. In the last cell a man is teaching himself something with his hands, over and "
     "over. He stops when he sees you. \"I could show you,\" he says.",
     {{"Let him show you", EF_TEACH, 18}, {"Leave him to it", EF_EXPLORE, 0}},
     2,
     4,
     10,
     6},

    {"Bones, a great many of them, swept into the corners the way leaves are. Something is still moving among them.",
     {{"Search the bones", EF_CHEST, 0},
      {"Kill what is moving", EF_FIGHT_RANDOM, 0},
      {"Take the stair down", EF_DESCEND, 0}},
     3,
     5,
     10,
     7},

    {"The stair down is here, and so is the smell of it: hot metal and old paper. Below this, the writing on the "
     "walls changes hand.",
     {{"Descend", EF_DESCEND, 0}, {"Search this level first", EF_EXPLORE, 0}},
     2,
     5,
     10,
     8},

    {"A librarian's desk, upright and undisturbed, at the bottom of a dry cistern. The ledger on it is open, and "
     "your name is halfway down the page.",
     {{"Read your entry", EF_CURSE, 0}, {"Tear out the page", EF_DAMAGE, 5}, {"Close the ledger", EF_DESCEND, 0}},
     3,
     6,
     10,
     6},

    {"Someone else got this far. He is sitting with his back to the wall, and his pack is still on.",
     {{"Search him", EF_LOOT, 0}, {"Say something over him", EF_HEAL, 3}, {"Walk past", EF_EXPLORE, 0}},
     3,
     1,
     10,
     8},

    {"Part of the ceiling came down here, a long time ago, on top of whatever was underneath it.",
     {{"Dig through the rubble", EF_LOOT, 0},
      {"Squeeze past the slab", EF_DAMAGE, 3},
      {"Find another way", EF_EXPLORE, 0}},
     3,
     1,
     6,
     7},

    {"Pale mushrooms grow out of the wall in a long shelf, each one turned to face the corridor.",
     {{"Eat one", EF_HEAL, 5}, {"Fill your pockets", EF_LOOT, 0}, {"Leave them", EF_EXPLORE, 0}},
     3,
     1,
     5,
     6},

    {"A nest of chewed rope and bone, still warm, with something small and bright half buried in it.",
     {{"Take what is shining", EF_FIGHT_RANDOM, 0}, {"Back out quietly", EF_EXPLORE, 0}},
     2,
     1,
     6,
     7},

    {"A crack in the wall with cold air coming through it, and behind it the sound of a much larger room.",
     {{"Widen it and go through", EF_EXPLORE, 0},
      {"Listen a while longer", EF_NOTHING, 0},
      {"Take the steps down", EF_DESCEND, 0}},
     3,
     1,
     10,
     6},

    {"A rope bridge over a gap you cannot see the bottom of. Two of the boards are missing, and one is new.",
     {{"Cross carefully", EF_EXPLORE, 0},
      {"Take the stair under it", EF_DESCEND, 0},
      {"Climb down the ropes instead", EF_DAMAGE, 4}},
     3,
     2,
     8,
     7},

    {"A man behind a grate, thin as the bars. \"Coin,\" he says. \"Or water. Or nothing. I have had nothing before.\"",
     {{"Give him coin", EF_GOLD_LOSS, 10},
      {"Sit with him a while", EF_REST, 7},
      {"Ask him about the stair", EF_DESCEND, 0}},
     3,
     2,
     9,
     6},

    {"A gallery of statues, all of them facing the wall. One is holding something behind its back.",
     {{"Take it from the statue", EF_CHEST, 0},
      {"Turn a statue around", EF_FIGHT_RANDOM, 0},
      {"Keep walking", EF_EXPLORE, 0}},
     3,
     3,
     10,
     6},

    {"A wheel the height of two men turns in a dry channel, driving nothing. Coins have been wedged into its teeth.",
     {{"Prise the coins out", EF_GOLD_GAIN, 16},
      {"Jam the wheel", EF_DAMAGE, 4},
      {"Follow the channel down", EF_DESCEND, 0}},
     3,
     4,
     10,
     6},

    {"A pit of the dead: orderly, layered, and picked over already, though not all the way down.",
     {{"Go down into it", EF_LOOT, 0}, {"Burn what you can reach", EF_CURSE, 0}, {"Skirt the edge", EF_EXPLORE, 0}},
     3,
     5,
     10,
     7},
};
constexpr uint8_t ROOM_COUNT = sizeof(kRooms) / sizeof(kRooms[0]);

// Boss landings are entered by depth, never drawn from the pool above.
inline constexpr RoomTemplate kBossRooms[] = {
    {"The stair opens onto a landing over a black drop. Something large is coming up it, one heavy step at a time, "
     "and there is nowhere to go but through.",
     {{"Stand and fight", EF_BOSS, 0}},
     1,
     5,
     5,
     0},
    {"The stair ends. The room at the bottom has no other door, and the thing filling it has been waiting long enough "
     "to have grown into the walls.",
     {{"Face it", EF_BOSS, 0}},
     1,
     10,
     10,
     0},
};

// ---------------------------------------------------------------- prose

inline constexpr const char* kIntro =
    "You go down because the stair is there, and because whatever is written at the bottom of it has your name in it.";

inline constexpr const char* kDeathLine =
    "The dark closes over the place where you were, and goes back to what it was doing.";

inline constexpr const char* kEpilogue[] = {
    "The last page turns itself, and stops. What is written there is short, and it is about you, and it is in your "
    "own hand.",
    "You climb. The writing on the walls thins out as you go, from close script to loose lines to nothing, and then "
    "there is only stone, and then there is a grate, and then there is weather.",
    "You come up into rain, in a field, a long way from where you went down. The stair behind you is gone. What you "
    "carry out is yours, and nobody will believe a word of it.",
};
constexpr uint8_t EPILOGUE_PAGES = sizeof(kEpilogue) / sizeof(kEpilogue[0]);

}  // namespace tb
