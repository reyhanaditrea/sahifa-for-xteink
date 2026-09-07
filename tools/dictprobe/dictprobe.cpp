// Runs the device's dictionary lookup against a real StarDict directory on the
// host, so a stemming or index change can be measured in seconds instead of a
// build-flash-tap cycle.
//
// It links the same lib/ArabicStem and lib/StarDictIndex the firmware does. What
// it does NOT reproduce is everything DictionaryStore owns around them: the SD
// layer, the checkpoint cache on disk, definition decoding, and the UI. A hit
// here means the index probe found the headword, not that the device could
// render its article.

#include <ArabicSegments.h>
#include <ArabicStem.h>
#include <StarDictIndex.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

#include "StdioReader.h"

namespace {

struct Dictionary {
  std::string idxPath;
  std::string synPath;
  std::string name;
  std::string sameTypeSequence;
  bool spanish = false;
};

struct Options {
  std::string dictDir;
  bool json = false;
  std::vector<std::string> words;
  std::string wordsFile;
  std::string epubPath;
  bool synAudit = false;
  bool verbose = false;
};

void printUsage() {
  std::printf(
      "usage: dictprobe <dictdir> [word...]\n"
      "       dictprobe <dictdir> --words <file>   one UTF-8 query per line,\n"
      "                                            or \"previous<TAB>query\" to give it context\n"
      "       dictprobe <dictdir> --epub <file>    tokenise an EPUB, then probe every word\n"
      "       dictprobe <dictdir> --syn-audit      resolve every .syn key and report misses\n"
      "       dictprobe ... --verbose              print every probe, not just the hit\n");
}

std::string toLowerAscii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

// Mirrors DictionaryStore::loadEntryFromIfoPath: one .ifo per directory, with
// the .idx and .syn beside it under the same stem.
bool loadDictionary(const std::string& dir, Dictionary& out) {
  std::string ifoPath;
  for (const auto& entry : std::filesystem::directory_iterator(dir)) {
    if (!entry.is_regular_file()) continue;
    if (toLowerAscii(entry.path().extension().string()) == ".ifo") {
      if (!ifoPath.empty()) {
        std::fprintf(stderr, "more than one .ifo in %s -- ambiguous, like the device's scan\n", dir.c_str());
        return false;
      }
      ifoPath = entry.path().string();
    }
  }
  if (ifoPath.empty()) {
    std::fprintf(stderr, "no .ifo in %s\n", dir.c_str());
    return false;
  }

  const std::string stem = (std::filesystem::path(dir) / std::filesystem::path(ifoPath).stem()).string();
  out.idxPath = stem + ".idx";
  out.synPath = stem + ".syn";
  if (!std::filesystem::exists(out.idxPath)) {
    std::fprintf(stderr, "no .idx beside %s (a .idx.gz must be decompressed first)\n", ifoPath.c_str());
    return false;
  }
  if (!std::filesystem::exists(out.synPath)) out.synPath.clear();

  std::ifstream ifo(ifoPath, std::ios::binary);
  std::string line;
  std::string languageId = std::filesystem::path(dir).filename().string();
  std::string lang;
  while (std::getline(ifo, line)) {
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
    const size_t eq = line.find('=');
    if (eq == std::string::npos) continue;
    const std::string key = line.substr(0, eq);
    const std::string value = line.substr(eq + 1);
    if (key == "bookname") out.name = value;
    if (key == "sametypesequence") out.sameTypeSequence = value;
    if (key == "lang") lang = value;
    if (key == "idxoffsetbits" && value == "64") {
      std::fprintf(stderr, "idxoffsetbits=64 is not supported, same as on the device\n");
      return false;
    }
  }
  const std::string foldedId = ArabicStem::foldLatinUtf8ToAscii(languageId);
  const std::string foldedLang = ArabicStem::foldLatinUtf8ToAscii(lang);
  out.spanish =
      foldedId == "spanish" || foldedId == "es" || foldedId == "espanol" || foldedLang.compare(0, 2, "es") == 0;
  if (out.name.empty()) out.name = languageId;
  return true;
}

struct Probe {
  enum class Source { Exact, ExactSyn, Fallback, FallbackSyn };
  Source source = Source::Exact;
  std::string candidate;
  std::string headword;
  size_t formIndex = 0;
  // Where the definition lives in the .dict; serve.py reads it from there.
  uint32_t dictOffset = 0;
  uint32_t dictSize = 0;
};

const char* sourceName(const Probe::Source source) {
  switch (source) {
    case Probe::Source::Exact:
      return "exact";
    case Probe::Source::ExactSyn:
      return "exact-syn";
    case Probe::Source::Fallback:
      return "fallback";
    case Probe::Source::FallbackSyn:
      return "fallback-syn";
  }
  return "?";
}

// Enough for the values here: keys are ASCII, and everything else is UTF-8 text
// that JSON passes through as-is once quotes and backslashes are escaped.
std::string jsonEscape(const std::string& value) {
  std::string out;
  out.reserve(value.size() + 8);
  for (const char c : value) {
    if (c == '"' || c == '\\') {
      out.push_back('\\');
      out.push_back(c);
    } else if (static_cast<unsigned char>(c) < 0x20) {
      char buf[8];
      std::snprintf(buf, sizeof(buf), "\\u%04x", c);
      out += buf;
    } else {
      out.push_back(c);
    }
  }
  return out;
}

// Where a candidate's article lives, when the candidate has one. Every candidate
// is probed, not just the ones ahead of the answer, so the reader can be shown
// the OTHER readings of the word -- أَقُل is قال and it is also اقل, and which of
// the two was wanted is not always the one the rules ranked first.
struct Resolved {
  std::string headword;
  uint32_t offset = 0;
  uint32_t size = 0;
};

// Layer 1 of the lookup, as JSON: the word cut into its parts, each with the
// grammatical name the device shows. `{"form":"عرض","join":"عرض + هم","parts":[...]}`
std::string candidateJson(const std::string& query, const ArabicStem::Candidate& candidate,
                          const Resolved& resolved = {}) {
  ArabicStem::Segment segments[ArabicStem::MAX_SEGMENTS];
  const size_t count = ArabicStem::segments(query, candidate, segments, ArabicStem::MAX_SEGMENTS);
  std::string out = "{\"form\":\"" + jsonEscape(candidate.form) + "\",\"join\":\"" +
                    jsonEscape(ArabicStem::joinSegments(segments, count)) + "\",\"standing\":\"" +
                    jsonEscape(ArabicStem::standingLabel(ArabicStem::standing(query, candidate))) + "\",\"parts\":[";
  for (size_t i = 0; i < count; ++i) {
    if (i > 0) out += ",";
    out += "{\"text\":\"";
    out += jsonEscape(segments[i].text);
    out += "\",\"kind\":\"";
    out += segments[i].kind == ArabicStem::Segment::Core ? "core" : "affix";
    out += "\",\"label\":\"";
    out += jsonEscape(segments[i].label);
    out += "\"}";
  }
  out += "]";
  if (!resolved.headword.empty()) {
    out += ",\"entry\":{\"headword\":\"" + jsonEscape(resolved.headword) +
           "\",\"offset\":" + std::to_string(resolved.offset) + ",\"size\":" + std::to_string(resolved.size) + "}";
  }
  out += "}";
  return out;
}

std::string candidateJoin(const std::string& query, const ArabicStem::Candidate& candidate) {
  ArabicStem::Segment segments[ArabicStem::MAX_SEGMENTS];
  const size_t count = ArabicStem::segments(query, candidate, segments, ArabicStem::MAX_SEGMENTS);
  return ArabicStem::joinSegments(segments, count);
}

class Lookup {
 public:
  Lookup(StdioReader& idx, const StarDictIndex::CheckpointTable& idxTable, StdioReader* syn,
         const StarDictIndex::CheckpointTable* synTable)
      : idx(idx), idxTable(idxTable), syn(syn), synTable(synTable) {}

  // Same order as DictionaryStore::lookup: the query itself through the index
  // and the .syn, then each fallback form the same way.
  bool run(const std::string& query, const bool spanish, const ArabicStem::Neighbour before, Probe& probe,
           std::vector<ArabicStem::Candidate>& forms, const bool verbose) {
    forms = ArabicStem::fallbackCandidates(query, spanish, before);

    const auto settle = [&probe](const Probe::Source source, const std::string& candidate, const size_t index) {
      probe.source = source;
      probe.candidate = candidate;
      probe.formIndex = index;
      return true;
    };

    if (probeOne(query, verbose, "idx", probe)) return settle(Probe::Source::Exact, query, 0);
    if (probeSyn(query, verbose, probe)) return settle(Probe::Source::ExactSyn, query, 0);
    for (size_t i = 0; i < forms.size(); ++i) {
      if (probeOne(forms[i].form, verbose, "idx", probe)) return settle(Probe::Source::Fallback, forms[i].form, i);
      if (probeSyn(forms[i].form, verbose, probe)) return settle(Probe::Source::FallbackSyn, forms[i].form, i);
    }
    return false;
  }

  // Every candidate through the index, past the one that answered. One extra
  // binary search each, and it is what lets the reader see that أَقُل is also اقل.
  std::vector<Resolved> resolveAll(const std::vector<ArabicStem::Candidate>& forms) {
    std::vector<Resolved> found(forms.size());
    for (size_t i = 0; i < forms.size(); ++i) {
      Probe probe;
      if (!probeOne(forms[i].form, false, "idx", probe) && !probeSyn(forms[i].form, false, probe)) continue;
      found[i] = Resolved{probe.headword, probe.dictOffset, probe.dictSize};
    }
    return found;
  }

  std::vector<std::string> suggest(const std::string& query, const size_t maxResults) {
    const std::string anchor = ArabicStem::suggestionAnchor(query);
    const uint32_t total = StarDictIndex::seekToNeighbourhood(idx, idxTable, anchor, 2);
    std::vector<std::string> keys;
    for (uint32_t i = 0; i < total && keys.size() < maxResults * 8; ++i) {
      const std::string key = StarDictIndex::readKey(idx);
      if (key.empty()) break;
      uint8_t skip[StarDictIndex::IDX_RECORD_BYTES];
      if (idx.read(skip, sizeof(skip)) != static_cast<int>(sizeof(skip))) break;
      if (StarDictIndex::compareKeys(key, anchor) >= 0) keys.push_back(key);
    }
    if (keys.size() > maxResults) keys.resize(maxResults);
    return keys;
  }

 private:
  bool probeOne(const std::string& word, const bool verbose, const char* what, Probe& probe) {
    StarDictIndex::Hit hit;
    const bool found = StarDictIndex::findKey(idx, idxTable, StarDictIndex::IDX_RECORD_BYTES, word, hit);
    if (verbose) std::printf("    %s:%s -> %s\n", what, word.c_str(), found ? hit.key.c_str() : "miss");
    if (!found) return false;
    probe.headword = hit.key;
    probe.dictOffset = StarDictIndex::readBE32(hit.record);
    probe.dictSize = StarDictIndex::readBE32(hit.record + 4);
    return true;
  }

  bool probeSyn(const std::string& word, const bool verbose, Probe& probe) {
    if (!syn || !synTable || synTable->empty()) return false;
    uint32_t ordinal = 0;
    if (!StarDictIndex::findSynonym(*syn, *synTable, word, ordinal)) {
      if (verbose) std::printf("    syn:%s -> miss\n", word.c_str());
      return false;
    }
    const std::string key = StarDictIndex::keyAtOrdinal(idx, idxTable, StarDictIndex::IDX_RECORD_BYTES, ordinal);
    if (verbose) std::printf("    syn:%s -> %s\n", word.c_str(), key.empty() ? "dangling" : key.c_str());
    if (key.empty()) return false;
    // The synonym resolves to a headword; its definition still comes from the
    // index entry for that headword.
    return probeOne(key, false, "idx", probe);
  }

  StdioReader& idx;
  const StarDictIndex::CheckpointTable& idxTable;
  StdioReader* syn;
  const StarDictIndex::CheckpointTable* synTable;
};

// Walks the .syn sequentially for ground truth, then asks findSynonym for each
// key. This is the regression check for the resync bug: it used to lose most of
// the file.
int runSynAudit(StdioReader& idx, const StarDictIndex::CheckpointTable& idxTable, StdioReader& syn,
                const StarDictIndex::CheckpointTable& synTable) {
  syn.seekSet(0);
  std::vector<std::pair<std::string, uint32_t>> truth;
  truth.reserve(synTable.totalEntries);
  for (uint32_t i = 0; i < synTable.totalEntries; ++i) {
    const std::string key = StarDictIndex::readKey(syn);
    uint8_t record[StarDictIndex::SYN_RECORD_BYTES];
    if (key.empty() || syn.read(record, sizeof(record)) != static_cast<int>(sizeof(record))) break;
    truth.emplace_back(key, StarDictIndex::readBE32(record));
  }

  size_t missed = 0;
  size_t wrongOrdinal = 0;
  size_t dangling = 0;
  for (const auto& entry : truth) {
    uint32_t ordinal = 0;
    if (!StarDictIndex::findSynonym(syn, synTable, entry.first, ordinal)) {
      if (missed < 20) std::printf("  missed: %s -> %u\n", entry.first.c_str(), entry.second);
      ++missed;
      continue;
    }
    if (ordinal != entry.second) {
      ++wrongOrdinal;
      continue;
    }
    if (StarDictIndex::keyAtOrdinal(idx, idxTable, StarDictIndex::IDX_RECORD_BYTES, ordinal).empty()) ++dangling;
  }

  std::printf("syn-audit: keys=%zu missed=%zu wrong-ordinal=%zu dangling-headword=%zu\n", truth.size(), missed,
              wrongOrdinal, dangling);
  return (missed == 0 && wrongOrdinal == 0) ? 0 : 1;
}

// A query and the word the reader saw in front of it. Arabic settles a word's
// part of speech in its neighbour as often as in its own letters, so the probe
// has to be handed the pair the page actually held -- not the word alone.
struct Query {
  std::string word;
  std::string before;
};

// One query per line. A tab splits the line into "previous<TAB>query", which is
// what serve.py writes; a file without tabs is read as running text, so the line
// before is the word before -- which is what epub_words.py dumps.
std::vector<Query> readWordsFile(const std::string& path) {
  std::vector<Query> queries;
  std::ifstream in(path, std::ios::binary);
  std::string line;
  while (std::getline(in, line)) {
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
    if (line.empty()) continue;
    const size_t tab = line.find('\t');
    if (tab == std::string::npos) {
      queries.push_back(Query{line, queries.empty() ? std::string() : queries.back().word});
    } else {
      queries.push_back(Query{line.substr(tab + 1), line.substr(0, tab)});
    }
  }
  return queries;
}

bool parseArgs(int argc, char** argv, Options& out) {
  if (argc < 2) return false;
  out.dictDir = argv[1];
  for (int i = 2; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--syn-audit") {
      out.synAudit = true;
    } else if (arg == "--verbose") {
      out.verbose = true;
    } else if (arg == "--json") {
      out.json = true;
    } else if (arg == "--words" && i + 1 < argc) {
      out.wordsFile = argv[++i];
    } else if (arg == "--epub" && i + 1 < argc) {
      out.epubPath = argv[++i];
    } else if (arg.rfind("--", 0) == 0) {
      std::fprintf(stderr, "unknown option: %s\n", arg.c_str());
      return false;
    } else {
      out.words.push_back(arg);
    }
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
  // Arabic in argv arrives in the console code page and is mangled; --words with
  // a UTF-8 file is the reliable route. This at least makes output readable.
  SetConsoleOutputCP(CP_UTF8);
#endif

  Options options;
  if (!parseArgs(argc, argv, options)) {
    printUsage();
    return 2;
  }

  Dictionary dictionary;
  if (!loadDictionary(options.dictDir, dictionary)) return 2;

  StdioReader idx(dictionary.idxPath);
  if (!idx.ok()) {
    std::fprintf(stderr, "cannot open %s\n", dictionary.idxPath.c_str());
    return 2;
  }
  StarDictIndex::CheckpointTable idxTable;
  if (!StarDictIndex::buildCheckpoints(idx, StarDictIndex::IDX_RECORD_BYTES, idxTable)) {
    std::fprintf(stderr, "cannot index %s\n", dictionary.idxPath.c_str());
    return 2;
  }

  StdioReader syn(dictionary.synPath);
  StarDictIndex::CheckpointTable synTable;
  const bool haveSyn = !dictionary.synPath.empty() && syn.ok() &&
                       StarDictIndex::buildCheckpoints(syn, StarDictIndex::SYN_RECORD_BYTES, synTable);

  if (!options.json) {
    std::printf("%s: %u headwords, %u synonyms%s\n", dictionary.name.c_str(), idxTable.totalEntries,
                haveSyn ? synTable.totalEntries : 0, dictionary.spanish ? ", spanish rules on" : "");
  }

  if (options.synAudit) {
    if (!haveSyn) {
      std::fprintf(stderr, "no usable .syn in %s\n", options.dictDir.c_str());
      return 2;
    }
    return runSynAudit(idx, idxTable, syn, synTable);
  }

  std::vector<Query> queries;
  // Words given on the command line are read as running text too: the one before
  // is the one before.
  for (const std::string& word : options.words) {
    queries.push_back(Query{word, queries.empty() ? std::string() : queries.back().word});
  }
  if (!options.wordsFile.empty()) {
    const std::vector<Query> fromFile = readWordsFile(options.wordsFile);
    queries.insert(queries.end(), fromFile.begin(), fromFile.end());
  }
  if (!options.epubPath.empty()) {
    // Unzipping and stripping XHTML in C++ would mean linking the whole EPUB
    // stack (expat, miniz, and ZipFile, which pulls in HalStorage), so that half
    // lives in a script beside this file.
    std::fprintf(stderr,
                 "run:  python tools/dictprobe/epub_words.py \"%s\" words.txt --unique\n"
                 "then: dictprobe <dictdir> --words words.txt\n",
                 options.epubPath.c_str());
    return 2;
  }
  if (queries.empty()) {
    printUsage();
    return 2;
  }

  Lookup lookup(idx, idxTable, haveSyn ? &syn : nullptr, haveSyn ? &synTable : nullptr);
  size_t counts[4] = {0, 0, 0, 0};
  size_t misses = 0;

  for (const Query& entry : queries) {
    const std::string& raw = entry.word;
    const std::string query = ArabicStem::cleanWord(raw);
    // The neighbour is read from the word as the page wrote it, marks and all --
    // governs() folds what it needs to.
    const ArabicStem::Neighbour before = ArabicStem::governs(ArabicStem::cleanWord(entry.before));
    if (!options.json) {
      std::printf("query   %s -> %s", raw.c_str(), query.c_str());
      if (before == ArabicStem::Neighbour::Jazm) std::printf("   (jazm, after %s)", entry.before.c_str());
      std::printf("\n");
    }
    if (query.empty()) {
      ++misses;
      if (options.json) {
        std::printf(
            "{\"word\":\"%s\",\"query\":\"\",\"before\":\"%s\",\"governs\":\"\",\"forms\":[],"
            "\"split\":{\"form\":\"\",\"join\":\"\",\"parts\":[]},\"found\":false,\"suggestions\":[]}\n",
            jsonEscape(raw).c_str(), jsonEscape(entry.before).c_str());
      } else {
        std::printf("miss    (empty after cleaning)\n");
      }
      continue;
    }

    Probe probe;
    std::vector<ArabicStem::Candidate> forms;
    const bool found = lookup.run(query, dictionary.spanish, before, probe, forms, options.verbose && !options.json);

    if (options.json) {
      if (found) ++counts[static_cast<size_t>(probe.source)];
      if (!found) ++misses;
      std::printf("{\"word\":\"%s\",\"query\":\"%s\",\"before\":\"%s\",\"governs\":\"%s\",\"forms\":[",
                  jsonEscape(raw).c_str(), jsonEscape(query).c_str(), jsonEscape(entry.before).c_str(),
                  before == ArabicStem::Neighbour::Jazm ? "jazm" : "");
      const std::vector<Resolved> resolved = lookup.resolveAll(forms);
      for (size_t i = 0; i < forms.size(); ++i) {
        std::printf("%s%s", i == 0 ? "" : ",", candidateJson(query, forms[i], resolved[i]).c_str());
      }
      // The split the answer came from. An exact hit cut nothing off, so it is the
      // word itself -- still emitted, so the panel always has one to show.
      // The query itself is the reading the index was asked for first, and when it
      // answered there is no candidate behind it -- so the chooser is handed one.
      //
      // A miss shows the best reading rather than the bare word: يَـٰٓأَيُّهَا is يا + ايها
      // whether or not the dictionary holds ايها, and printing the whole word as one
      // undivided lump tells the reader nothing -- least of all which half of the
      // lookup failed. The first candidate is the highest-ranked reading, which is
      // what all the ordering rules exist to decide.
      const bool cutToAnswer = found && probe.source != Probe::Source::Exact && probe.source != Probe::Source::ExactSyn;
      const ArabicStem::Candidate answering = cutToAnswer                  ? forms[probe.formIndex]
                                              : (!found && !forms.empty()) ? forms[0]
                                                                           : ArabicStem::Candidate{query};
      std::printf("],\"split\":%s", candidateJson(query, answering).c_str());
      if (found) {
        std::printf(
            ",\"found\":true,\"source\":\"%s\",\"formIndex\":%zu,\"candidate\":\"%s\","
            "\"headword\":\"%s\",\"offset\":%u,\"size\":%u}\n",
            sourceName(probe.source), probe.formIndex, jsonEscape(probe.candidate).c_str(),
            jsonEscape(probe.headword).c_str(), probe.dictOffset, probe.dictSize);
      } else {
        std::printf(",\"found\":false,\"suggestions\":[");
        const std::vector<std::string> suggestions = lookup.suggest(query, 8);
        for (size_t i = 0; i < suggestions.size(); ++i) {
          std::printf("%s\"%s\"", i == 0 ? "" : ",", jsonEscape(suggestions[i]).c_str());
        }
        std::printf("]}\n");
      }
      continue;
    }

    std::printf("forms  ");
    for (size_t i = 0; i < forms.size(); ++i) {
      const std::string join = candidateJoin(query, forms[i]);
      if (join == forms[i].form) {
        std::printf(" [%zu]=%s", i, forms[i].form.c_str());
      } else {
        std::printf(" [%zu]=%s (%s)", i, forms[i].form.c_str(), join.c_str());
      }
    }
    std::printf("%s\n", forms.empty() ? " (none)" : "");

    if (found) {
      ++counts[static_cast<size_t>(probe.source)];
      const bool exact = probe.source == Probe::Source::Exact || probe.source == Probe::Source::ExactSyn;
      std::printf("hit     source=%s form=%zu candidate=%s split=%s headword=%s\n", sourceName(probe.source),
                  probe.formIndex, probe.candidate.c_str(),
                  candidateJoin(query, exact ? ArabicStem::Candidate{query} : forms[probe.formIndex]).c_str(),
                  probe.headword.c_str());
    } else {
      ++misses;
      std::printf("miss    suggestions:");
      for (const std::string& suggestion : lookup.suggest(query, 8)) std::printf(" %s", suggestion.c_str());
      std::printf("\n");
    }
  }

  if (options.json) return 0;
  std::printf("summary exact=%zu exact-syn=%zu fallback=%zu fallback-syn=%zu miss=%zu of %zu\n", counts[0], counts[1],
              counts[2], counts[3], misses, queries.size());
  return 0;
}
