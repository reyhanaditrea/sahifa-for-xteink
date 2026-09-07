#include "DictionaryStore.h"

#include <ArabicSegments.h>
#include <ArabicStem.h>
#include <ArduinoJson.h>
#include <Epub/htmlEntities.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <StarDictIndex.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <utility>

#include "fontIds.h"
#include "util/DictZip.h"

namespace {
// Bumped when the layout changed to carry the .syn table as well; an older
// file is simply not recognised and gets rebuilt once.
constexpr uint32_t CACHE_MAGIC = 0x44435459;  // DCTY
constexpr uint8_t NULL_TERMINATED_TYPES[] = {'m', 'l', 'g', 't', 'x', 'y', 'k', 'w', 'h', 'n', 'r'};
constexpr uint8_t DEFINITION_TEXT_SIZE_CONFIG_VERSION = 4;
constexpr size_t MAX_METADATA_TEXT_BYTES = 96;
constexpr size_t SAFE_MAX_DEFINITION_BYTES = 16384;
constexpr size_t SAFE_MIN_DEFINITION_BYTES = 1024;
constexpr size_t MAX_SUGGESTION_CANDIDATES = 24;
constexpr size_t MAX_EDIT_DISTANCE_BYTES = 64;
constexpr uint32_t HEAP_SCAN_GUARD_BYTES = 32 * 1024;
constexpr uint32_t HEAP_SMALL_GUARD_BYTES = 16 * 1024;

uint32_t largestFreeBlock() { return ESP.getMaxAllocHeap(); }

bool hasHeapHeadroom(const size_t bytes, const uint32_t guardBytes = HEAP_SMALL_GUARD_BYTES) {
  const uint32_t freeHeap = ESP.getFreeHeap();
  const uint32_t largest = largestFreeBlock();
  return freeHeap > bytes + guardBytes && largest > bytes + 1024;
}

size_t safeDefinitionReadBytes(const size_t requestedBytes) {
  size_t bytes = std::min<size_t>(requestedBytes, SAFE_MAX_DEFINITION_BYTES);
  while (bytes > SAFE_MIN_DEFINITION_BYTES && !hasHeapHeadroom(bytes * 4 + 8192, HEAP_SMALL_GUARD_BYTES)) {
    bytes /= 2;
  }
  if (!hasHeapHeadroom(bytes * 2 + 4096, HEAP_SMALL_GUARD_BYTES)) return 0;
  return bytes;
}

bool hasExtension(const std::string& name, const char* ext) {
  if (name.size() < strlen(ext)) return false;
  std::string suffix = name.substr(name.size() - strlen(ext));
  std::transform(suffix.begin(), suffix.end(), suffix.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return suffix == ext;
}

std::string joinPath(const std::string& dir, const std::string& name) {
  if (dir.empty() || dir == "/") return "/" + name;
  return dir + "/" + name;
}

std::string fileStem(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  const size_t start = slash == std::string::npos ? 0 : slash + 1;
  const size_t dot = path.find_last_of('.');
  if (dot == std::string::npos || dot < start) return path.substr(start);
  return path.substr(start, dot - start);
}

std::string parentDirectory(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  if (slash == std::string::npos || slash == 0) return "/";
  return path.substr(0, slash);
}

std::string pathBaseName(const std::string& path) {
  if (path.empty() || path == "/") return "";
  size_t end = path.size();
  while (end > 1 && path[end - 1] == '/') --end;
  const size_t slash = path.find_last_of('/', end - 1);
  const size_t start = slash == std::string::npos ? 0 : slash + 1;
  return path.substr(start, end - start);
}

std::string limitMetadataText(std::string value, const size_t maxBytes = MAX_METADATA_TEXT_BYTES) {
  if (value.size() <= maxBytes) return value;
  value.resize(maxBytes);
  while (!value.empty() && (static_cast<unsigned char>(value.back()) & 0xC0) == 0x80) {
    value.pop_back();
  }
  return value;
}

void writeU32(uint8_t* buf, uint32_t value) {
  buf[0] = static_cast<uint8_t>(value >> 24);
  buf[1] = static_cast<uint8_t>(value >> 16);
  buf[2] = static_cast<uint8_t>(value >> 8);
  buf[3] = static_cast<uint8_t>(value);
}

uint32_t readU32(const uint8_t* buf) { return StarDictIndex::readBE32(buf); }

bool readLine(HalFile& file, char* buf, size_t len) {
  if (len == 0) return false;
  size_t index = 0;
  while (index + 1 < len) {
    const int c = file.read();
    if (c < 0) break;
    buf[index++] = static_cast<char>(c);
    if (c == '\n') break;
  }
  buf[index] = '\0';
  return index > 0;
}

void stripLineEnd(std::string& value) {
  while (!value.empty() && (value.back() == '\r' || value.back() == '\n' || value.back() == ' ')) {
    value.pop_back();
  }
}

bool parseIfoField(const char* line, const char* key, std::string& out) {
  const size_t keyLen = strlen(key);
  if (strncmp(line, key, keyLen) != 0 || line[keyLen] != '=') return false;
  out = line + keyLen + 1;
  stripLineEnd(out);
  return true;
}

// The device backend for the index reader: every call still goes through
// HalFile, so SD access stays serialized behind storageMutex.
class HalFileReader : public StarDictIndex::Reader {
 public:
  explicit HalFileReader(HalFile& file) : file(file) {}

  bool seekSet(size_t pos) override { return file.seekSet(pos); }
  int read(void* buf, size_t bytes) override { return file.read(buf, bytes); }
  int read() override { return file.read(); }
  size_t position() const override { return file.position(); }
  size_t fileSize() override { return file.fileSize(); }

 private:
  HalFile& file;
};

void appendWithSeparator(std::string& out, const char* data, size_t len) {
  while (len > 0 && (*data == '\0' || *data == '\r' || *data == '\n' || *data == ' ' || *data == '\t')) {
    ++data;
    --len;
  }
  while (len > 0 && (data[len - 1] == '\0' || data[len - 1] == '\r' || data[len - 1] == '\n' || data[len - 1] == ' ' ||
                     data[len - 1] == '\t')) {
    --len;
  }
  if (len == 0) return;
  if (!out.empty() && out.back() != '\n') out.push_back('\n');
  out.append(data, len);
}

bool isValidUtf8(const char* data, const size_t len) {
  for (size_t i = 0; i < len;) {
    const unsigned char c = static_cast<unsigned char>(data[i]);
    if (c < 0x80) {
      ++i;
      continue;
    }

    uint32_t cp = 0;
    size_t needed = 0;
    if ((c & 0xE0) == 0xC0) {
      cp = c & 0x1F;
      needed = 2;
      if (cp == 0) return false;
    } else if ((c & 0xF0) == 0xE0) {
      cp = c & 0x0F;
      needed = 3;
    } else if ((c & 0xF8) == 0xF0) {
      cp = c & 0x07;
      needed = 4;
    } else {
      return false;
    }

    if (i + needed > len) return false;
    for (size_t j = 1; j < needed; ++j) {
      const unsigned char cc = static_cast<unsigned char>(data[i + j]);
      if ((cc & 0xC0) != 0x80) return false;
      cp = (cp << 6) | (cc & 0x3F);
    }

    if ((needed == 2 && cp < 0x80) || (needed == 3 && cp < 0x800) || (needed == 4 && cp < 0x10000) ||
        (cp >= 0xD800 && cp <= 0xDFFF) || cp > 0x10FFFF) {
      return false;
    }
    i += needed;
  }
  return true;
}

void appendUtf8(std::string& out, const uint32_t cp) {
  if (cp == 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return;
  if (cp < 0x80) {
    out.push_back(static_cast<char>(cp));
  } else if (cp < 0x800) {
    out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp < 0x10000) {
    out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
}

uint32_t windows1252Codepoint(const unsigned char c) {
  static constexpr uint16_t C1_MAP[32] = {
      0x20AC, 0x0000, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
      0x2039, 0x0152, 0x0000, 0x017D, 0x0000, 0x0000, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
      0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x0000, 0x017E, 0x0178,
  };
  if (c >= 0x80 && c <= 0x9F) return C1_MAP[c - 0x80];
  return c;
}

bool appendReplacementForUtf8Token(const std::string& input, const size_t pos, std::string& out, size_t& consumed) {
  struct Replacement {
    const char* token;
    const char* value;
  };

  static constexpr Replacement REPLACEMENTS[] = {
      {"\xEF\xBF\xBD", ""},    // Unicode replacement char.
      {"\xE2\x80\xA3", "- "},  // Triangular bullet.
      {"\xE2\x81\x83", "- "},  // Hyphen bullet.
      {"\xE2\x96\xA0", "- "},  // Black square.
      {"\xE2\x96\xBA", "- "},  // Black right pointer.
      {"\xE2\x96\xB8", "- "},  // Small right triangle.
      {"\xE2\x97\x86", "- "},  // Black diamond.
      {"\xE2\x97\x87", "- "},  // White diamond.
      {"\xE2\x97\x8B", "- "},  // White circle.
      {"\xE2\x97\x8F", "- "},  // Black circle.
      {"\xE2\x99\xA6", "- "},  // Diamond suit.
      {"\xCB\x88", ""},        // IPA primary stress.
      {"\xCB\x8C", ""},        // IPA secondary stress.
      {"\xCB\x90", ":"},       // IPA length mark.
      {"\xCB\x91", ":"},       // IPA half-length mark.
      {"\xC9\xA3", "g"},      {"\xC9\xBE", "r"}, {"\xCA\x9D", "y"},  {"\xCA\x8E", "ll"}, {"\xC9\xB2", "n"},
      {"\xCE\xB2", "b"},      {"\xC3\xB0", "d"}, {"\xCE\xB8", "z"},  {"\xCA\x83", "sh"}, {"\xCA\x92", "zh"},
      {"\xC9\x9F", "y"},      {"\xC9\xAB", "l"}, {"\xC9\xB1", "m"},  {"\xC9\xB0", "g"},  {"\xC9\xB8", "f"},
      {"\xC9\x9B", "e"},      {"\xC9\x99", "e"}, {"\xC3\xA6", "ae"}, {"\xC5\x8B", "ng"}, {"\xC9\xAA", "i"},
      {"\xCA\x8A", "u"},      {"\xC9\x94", "o"}, {"\xC9\x91", "a"},  {"\xCA\x8C", "a"},
  };

  for (const auto& replacement : REPLACEMENTS) {
    const size_t len = strlen(replacement.token);
    if (pos + len <= input.size() && input.compare(pos, len, replacement.token) == 0) {
      out += replacement.value;
      consumed = len;
      return true;
    }
  }
  return false;
}

std::string sanitizeDefinitionGlyphs(const std::string& input) {
  std::string out;
  out.reserve(input.size());
  bool inEarlyBracket = false;
  bool earlyPrefix = true;
  int nonSpaceChars = 0;

  for (size_t i = 0; i < input.size();) {
    size_t consumed = 0;
    if (appendReplacementForUtf8Token(input, i, out, consumed)) {
      i += consumed;
      continue;
    }

    const char c = input[i];
    if (earlyPrefix) {
      if (c == '[') {
        inEarlyBracket = true;
      } else if (c == ']') {
        inEarlyBracket = false;
      }

      if (c == '?' && inEarlyBracket) {
        ++i;
        continue;
      }

      if (c == '\n') {
        earlyPrefix = nonSpaceChars < 160;
      } else if (c != ' ' && c != '\t' && c != '\r') {
        ++nonSpaceChars;
        if (nonSpaceChars > 220) earlyPrefix = false;
      }
    }

    out.push_back(c);
    ++i;
  }
  return out;
}

std::string ensureUtf8Text(const char* data, const size_t len) {
  if (len == 0) return "";
  if (isValidUtf8(data, len)) return std::string(data, len);

  std::string out;
  out.reserve(len * 2);
  for (size_t i = 0; i < len; ++i) {
    const unsigned char c = static_cast<unsigned char>(data[i]);
    const uint32_t cp = windows1252Codepoint(c);
    if (cp < 0x20 && c != '\n' && c != '\r' && c != '\t') continue;
    appendUtf8(out, cp);
  }
  return out;
}

void appendNormalizedField(std::string& out, const char* data, const size_t len) {
  const std::string normalized = ensureUtf8Text(data, len);
  appendWithSeparator(out, normalized.data(), normalized.size());
}

void appendLogicalNewline(std::string& out) {
  while (!out.empty() && (out.back() == ' ' || out.back() == '\t')) out.pop_back();
  if (out.empty() || out.back() == '\n') return;
  out.push_back('\n');
}

void appendLogicalSpace(std::string& out) {
  if (out.empty() || out.back() == '\n' || out.back() == ' ' || out.back() == '\t') return;
  out.push_back(' ');
}

bool appendDecodedEntity(const std::string& input, size_t& i, std::string& out) {
  if (input[i] != '&') return false;
  const size_t semi = input.find(';', i + 1);
  if (semi == std::string::npos || semi - i > 24) return false;

  const size_t entityLen = semi - i + 1;
  if (entityLen >= 4 && input[i + 1] == '#') {
    const bool hex = input[i + 2] == 'x' || input[i + 2] == 'X';
    const size_t firstDigit = i + (hex ? 3 : 2);
    if (firstDigit >= semi) return false;

    uint32_t value = 0;
    for (size_t pos = firstDigit; pos < semi; ++pos) {
      const unsigned char c = static_cast<unsigned char>(input[pos]);
      uint8_t digit = 0;
      if (c >= '0' && c <= '9') {
        digit = c - '0';
      } else if (hex && c >= 'a' && c <= 'f') {
        digit = 10 + c - 'a';
      } else if (hex && c >= 'A' && c <= 'F') {
        digit = 10 + c - 'A';
      } else {
        return false;
      }
      value = value * (hex ? 16 : 10) + digit;
      if (value > 0x10FFFF) return false;
    }

    if (value == 0x00A0) {
      appendLogicalSpace(out);
    } else if (value == 0x00AD) {
      // Soft hyphen is a layout hint, not visible text.
    } else if ((value >= 0x2010 && value <= 0x2015) || value == 0x2212) {
      out.push_back('-');
    } else if (value == 0x2022 || value == 0x25E6 || value == 0x2043) {
      out += "- ";
    } else {
      appendUtf8(out, value);
    }
    i = semi + 1;
    return true;
  }

  if (input.compare(i, entityLen, "&nbsp;") == 0 || input.compare(i, entityLen, "&emsp;") == 0 ||
      input.compare(i, entityLen, "&ensp;") == 0 || input.compare(i, entityLen, "&thinsp;") == 0) {
    appendLogicalSpace(out);
    i = semi + 1;
    return true;
  }
  if (input.compare(i, entityLen, "&bull;") == 0 || input.compare(i, entityLen, "&middot;") == 0) {
    out += "- ";
    i = semi + 1;
    return true;
  }
  if (input.compare(i, entityLen, "&ndash;") == 0 || input.compare(i, entityLen, "&mdash;") == 0 ||
      input.compare(i, entityLen, "&minus;") == 0) {
    out.push_back('-');
    i = semi + 1;
    return true;
  }
  if (input.compare(i, entityLen, "&shy;") == 0) {
    i = semi + 1;
    return true;
  }

  if (const char* resolved = lookupHtmlEntity(input.data() + i, entityLen)) {
    out += resolved;
    i = semi + 1;
    return true;
  }
  return false;
}

std::string trimAsciiCopy(const std::string& value) {
  size_t start = 0;
  while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) ++start;
  size_t end = value.size();
  while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1]))) --end;
  return value.substr(start, end - start);
}

bool startsWithFoldedWord(const std::string& text, const char* word) {
  const size_t len = strlen(word);
  return text.size() >= len && text.compare(0, len, word) == 0 && (text.size() == len || text[len] == ' ');
}

bool isGrammarHeadingLine(const std::string& line) {
  const std::string folded = ArabicStem::foldLatinUtf8ToAscii(trimAsciiCopy(line));
  if (folded.empty() || folded.size() > 48) return false;

  static constexpr const char* HEADINGS[] = {
      "adjetivo",    "adverbio",    "pronombre",    "conjuncion",  "sustantivo",   "nombre",  "verbo",
      "articulo",    "preposicion", "interjeccion", "locucion",    "prefijo",      "sufijo",  "determinante",
      "contraccion", "abreviatura", "participio",   "gerundio",    "noun",         "verb",    "adjective",
      "adverb",      "pronoun",     "conjunction",  "preposition", "interjection", "article",
  };

  for (const char* heading : HEADINGS) {
    if (startsWithFoldedWord(folded, heading)) return true;
  }
  return false;
}

void appendFormattedLine(std::string& out, const std::string& line) {
  if (!out.empty()) out.push_back('\n');
  out += line;
}

int parenthesisDelta(const std::string& line) {
  int delta = 0;
  for (const char c : line) {
    if (c == '(' || c == '[') {
      ++delta;
    } else if (c == ')' || c == ']') {
      --delta;
    }
  }
  return delta;
}

bool startsWithNoSpacePunctuation(const std::string& line) {
  if (line.empty()) return false;
  const char c = line.front();
  return c == ',' || c == '.' || c == ';' || c == ':' || c == ')' || c == ']';
}

bool endsWithoutJoinSpace(const std::string& line) {
  if (line.empty()) return false;
  const char c = line.back();
  return c == '(' || c == '[' || c == '/' || c == '*' || c == '<';
}

void appendSoftJoinedLine(std::string& current, const std::string& next) {
  if (!current.empty() && !startsWithNoSpacePunctuation(next) && !endsWithoutJoinSpace(current)) {
    current.push_back(' ');
  }
  current += next;
}

std::string stripHeadingMarkers(const std::string& line) {
  const std::string trimmed = trimAsciiCopy(line);
  size_t pos = 0;
  while (pos < trimmed.size() && (trimmed[pos] == '#' || trimmed[pos] == '*')) ++pos;
  while (pos < trimmed.size() && std::isspace(static_cast<unsigned char>(trimmed[pos]))) ++pos;
  if (pos > 0 && pos < trimmed.size()) {
    const std::string candidate = trimmed.substr(pos);
    if (isGrammarHeadingLine(candidate)) return candidate;
  }
  return line;
}

std::vector<std::string> coalesceDefinitionLines(const std::string& input) {
  std::vector<std::string> lines;
  std::string current;
  int openParentheses = 0;

  auto flushCurrent = [&]() {
    if (current.empty()) return;
    lines.push_back(current);
    current.clear();
    openParentheses = 0;
  };

  size_t lineStart = 0;
  for (size_t i = 0; i <= input.size(); ++i) {
    if (i != input.size() && input[i] != '\n') continue;

    std::string line = trimAsciiCopy(input.substr(lineStart, i - lineStart));
    lineStart = i + 1;

    if (line.empty()) {
      flushCurrent();
      if (!lines.empty() && !lines.back().empty()) lines.emplace_back();
      continue;
    }

    const bool heading = isGrammarHeadingLine(line);
    if (!current.empty() && openParentheses > 0 && !heading) {
      appendSoftJoinedLine(current, line);
      openParentheses = std::max(0, openParentheses + parenthesisDelta(line));
      continue;
    }

    flushCurrent();
    current = std::move(line);
    openParentheses = heading ? 0 : std::max(0, parenthesisDelta(current));
  }

  flushCurrent();
  while (!lines.empty() && lines.back().empty()) lines.pop_back();
  return lines;
}

std::string formatDefinitionBlocks(const std::string& input) {
  std::string out;
  out.reserve(input.size() + 32);
  bool hasOutputLine = false;
  bool pendingBlankAfterHeading = false;

  const std::vector<std::string> lines = coalesceDefinitionLines(input);
  for (const std::string& line : lines) {
    const bool empty = trimAsciiCopy(line).empty();
    const bool heading = !empty && isGrammarHeadingLine(line);

    if (empty) {
      if (!out.empty() && out.back() != '\n') out.push_back('\n');
      pendingBlankAfterHeading = false;
      continue;
    }

    if (heading && hasOutputLine && !out.empty() && out.back() != '\n') {
      out.push_back('\n');
    }
    if (pendingBlankAfterHeading && !heading && !out.empty() && out.back() != '\n') {
      out.push_back('\n');
    }

    appendFormattedLine(out, heading ? stripHeadingMarkers(line) : line);
    hasOutputLine = true;
    pendingBlankAfterHeading = heading;
  }

  return out;
}

std::string normalizedTagName(const std::string& input, size_t pos, const size_t end) {
  while (pos < end && std::isspace(static_cast<unsigned char>(input[pos]))) ++pos;
  if (pos < end && (input[pos] == '/' || input[pos] == '!' || input[pos] == '?')) ++pos;
  while (pos < end && std::isspace(static_cast<unsigned char>(input[pos]))) ++pos;

  std::string name;
  name.reserve(8);
  for (; pos < end; ++pos) {
    const unsigned char c = static_cast<unsigned char>(input[pos]);
    if (!(std::isalnum(c) || c == ':' || c == '-' || c == '_')) break;
    if (c == ':') {
      name.clear();
      continue;
    }
    name.push_back(static_cast<char>(std::tolower(c)));
    if (name.size() >= 12) break;
  }
  return name;
}

bool isClosingTag(const std::string& input, size_t pos, const size_t end) {
  while (pos < end && std::isspace(static_cast<unsigned char>(input[pos]))) ++pos;
  return pos < end && input[pos] == '/';
}

bool isBlockTag(const std::string& name) {
  return name == "p" || name == "div" || name == "section" || name == "article" || name == "blockquote" ||
         name == "pre" || name == "def" || name == "ex" || name == "dl" || name == "dt" || name == "dd" ||
         name == "ul" || name == "ol" || name == "table" || name == "tr" || name == "hr" || name == "h1" ||
         name == "h2" || name == "h3" || name == "h4" || name == "h5" || name == "h6";
}

void appendTagSpacing(std::string& out, const std::string& name, const bool closing) {
  if (name.empty()) return;
  if (name == "br" || name == "hr") {
    appendLogicalNewline(out);
    return;
  }
  if (name == "li" && !closing) {
    appendLogicalNewline(out);
    out += "- ";
    return;
  }
  if (name == "dd" && !closing) {
    appendLogicalNewline(out);
    out += "  ";
    return;
  }
  if ((name == "td" || name == "th") && !closing) {
    if (!out.empty() && out.back() != '\n') out.push_back('\t');
    return;
  }
  if (isBlockTag(name) || (closing && name == "li")) {
    appendLogicalNewline(out);
  }
}

bool isNullTerminatedType(uint8_t type) {
  for (const uint8_t t : NULL_TERMINATED_TYPES) {
    if (t == type) return true;
  }
  return false;
}

bool isDisplayDefinitionType(uint8_t type) {
  // Only render textual definition fields. Pronunciation, audio and resource fields often contain unsupported glyphs
  // or binary-ish payloads that look like replacement characters on the e-ink font.
  switch (std::tolower(static_cast<unsigned char>(type))) {
    case 'm':  // plain meaning
    case 'l':  // synonym / gloss
    case 'g':  // Pango markup
    case 'x':  // XDXF markup
    case 'k':  // KingSoft PowerWord markup
    case 'h':  // HTML
    case 'n':  // WordNet data
      return true;
    default:
      return false;
  }
}

void appendTypedField(std::string& out, uint8_t type, const char* data, size_t len) {
  if (!isDisplayDefinitionType(type)) return;
  appendNormalizedField(out, data, len);
}

std::string decodeStarDictData(const std::string& raw, const std::string& sameTypeSequence) {
  if (raw.empty()) return "";
  if (!sameTypeSequence.empty()) {
    std::string out;
    size_t pos = 0;
    for (const char typeChar : sameTypeSequence) {
      if (pos >= raw.size()) break;
      const uint8_t type = static_cast<uint8_t>(typeChar);
      if (isNullTerminatedType(type)) {
        const size_t start = pos;
        while (pos < raw.size() && raw[pos] != '\0') ++pos;
        appendTypedField(out, type, raw.data() + start, pos - start);
        if (pos < raw.size() && raw[pos] == '\0') ++pos;
        continue;
      }
      if (std::isupper(type) && pos + 4 <= raw.size()) {
        const uint32_t fieldSize = StarDictIndex::readBE32(reinterpret_cast<const uint8_t*>(raw.data() + pos));
        pos += 4;
        if (pos + fieldSize > raw.size()) break;
        appendTypedField(out, type, raw.data() + pos, fieldSize);
        pos += fieldSize;
        continue;
      }
      appendTypedField(out, type, raw.data() + pos, raw.size() - pos);
      pos = raw.size();
    }
    return out;
  }

  std::string out;
  size_t pos = 0;
  while (pos < raw.size()) {
    const uint8_t type = static_cast<uint8_t>(raw[pos++]);
    if (isNullTerminatedType(type)) {
      const size_t start = pos;
      while (pos < raw.size() && raw[pos] != '\0') ++pos;
      appendTypedField(out, type, raw.data() + start, pos - start);
      if (pos < raw.size() && raw[pos] == '\0') ++pos;
      continue;
    }
    if (std::isupper(type) && pos + 4 <= raw.size()) {
      const uint32_t fieldSize = StarDictIndex::readBE32(reinterpret_cast<const uint8_t*>(raw.data() + pos));
      pos += 4;
      if (pos + fieldSize > raw.size()) break;
      appendTypedField(out, type, raw.data() + pos, fieldSize);
      pos += fieldSize;
      continue;
    }

    if (isDisplayDefinitionType(type)) {
      out.push_back(static_cast<char>(type));
      out.append(raw.data() + pos, raw.size() - pos);
    }
    break;
  }
  return out;
}

std::string stripHtmlAndEntities(const std::string& input) {
  std::string out;
  out.reserve(input.size());

  for (size_t i = 0; i < input.size();) {
    const char c = input[i];
    if (c == '<') {
      const size_t tagEnd = input.find('>', i + 1);
      if (tagEnd == std::string::npos) {
        ++i;
        continue;
      }
      const bool closing = isClosingTag(input, i + 1, tagEnd);
      appendTagSpacing(out, normalizedTagName(input, i + 1, tagEnd), closing);
      i = tagEnd + 1;
      continue;
    }
    if (c == '&') {
      if (appendDecodedEntity(input, i, out)) continue;
      out.push_back(c);
      ++i;
      continue;
    }
    out.push_back(c == '\0' ? '\n' : c);
    ++i;
  }

  std::string compact;
  compact.reserve(out.size());
  bool lastSpace = false;
  bool atLineStart = true;
  int consecutiveNewlines = 0;
  int lineStartSpaces = 0;
  for (char c : out) {
    if (c == '\r') continue;
    if (c == '\0') c = '\n';
    if (c == '\n') {
      while (!compact.empty() && compact.back() == ' ') compact.pop_back();
      if (!compact.empty() && consecutiveNewlines < 2) {
        compact.push_back('\n');
        ++consecutiveNewlines;
      }
      lastSpace = false;
      atLineStart = true;
      lineStartSpaces = 0;
      continue;
    }
    if (c == '\t' || c == ' ') {
      if (atLineStart) {
        const int spaces = c == '\t' ? 2 : 1;
        for (int s = 0; s < spaces && lineStartSpaces < 4; ++s) {
          compact.push_back(' ');
          ++lineStartSpaces;
        }
      } else if (c == '\t') {
        if (!compact.empty() && compact.back() != ' ') compact.push_back(' ');
        compact.push_back(' ');
        lastSpace = true;
      } else if (!lastSpace) {
        compact.push_back(' ');
        lastSpace = true;
      }
      continue;
    }
    compact.push_back(c);
    lastSpace = false;
    atLineStart = false;
    consecutiveNewlines = 0;
  }
  while (!compact.empty() && (compact.back() == ' ' || compact.back() == '\n')) compact.pop_back();
  return formatDefinitionBlocks(sanitizeDefinitionGlyphs(compact));
}

bool startsWithAsciiPrefix(const std::string& text, const std::string& prefix) {
  return text.size() >= prefix.size() && StarDictIndex::compareKeys(text.substr(0, prefix.size()), prefix) == 0;
}

bool isSpanishLanguage(const DictionaryEntry& entry) {
  const std::string id = ArabicStem::foldLatinUtf8ToAscii(entry.languageId);
  const std::string lang = ArabicStem::foldLatinUtf8ToAscii(entry.lang);
  return id == "spanish" || id == "es" || id == "espanol" || startsWithAsciiPrefix(lang, "es");
}

int commonPrefixLength(const std::string& a, const std::string& b) {
  const size_t maxLen = std::min(a.size(), b.size());
  size_t len = 0;
  while (len < maxLen && a[len] == b[len]) ++len;
  return static_cast<int>(len);
}

std::string suggestionScoreKey(const std::string& word) {
  std::string folded = ArabicStem::foldLatinUtf8ToAscii(word);
  return folded.empty() ? ArabicStem::lowercaseLatinUtf8(word) : folded;
}

int editDistanceLimited(const std::string& a, const std::string& b, int maxDist) {
  const int m = static_cast<int>(a.size());
  const int n = static_cast<int>(b.size());
  if (std::abs(m - n) > maxDist) return maxDist + 1;
  if (n > static_cast<int>(MAX_EDIT_DISTANCE_BYTES) || m > static_cast<int>(MAX_EDIT_DISTANCE_BYTES)) {
    return maxDist + 1;
  }

  // uint16_t, not int: this row is 65 entries, so an int version is 260 bytes of
  // stack -- over CLAUDE.md's 256-byte per-frame budget, and this runs in a tight
  // loop over thousands of index entries from findSuggestions(). Every value here
  // is bounded by max(m, n) <= MAX_EDIT_DISTANCE_BYTES, so 16 bits is ample.
  // Deliberately still on the stack rather than static: findSuggestions() is
  // called from activity code, and a function-local static would make this
  // non-reentrant for no gain now that the array is only 130 bytes.
  uint16_t dp[MAX_EDIT_DISTANCE_BYTES + 1];
  for (int j = 0; j <= n; ++j) dp[j] = static_cast<uint16_t>(j);
  for (int i = 1; i <= m; ++i) {
    uint16_t prev = dp[0];
    dp[0] = static_cast<uint16_t>(i);
    int rowMin = dp[0];
    for (int j = 1; j <= n; ++j) {
      const uint16_t old = dp[j];
      if (a[i - 1] == b[j - 1]) {
        dp[j] = prev;
      } else {
        dp[j] = static_cast<uint16_t>(
            1 + std::min({static_cast<int>(prev), static_cast<int>(dp[j]), static_cast<int>(dp[j - 1])}));
      }
      prev = old;
      rowMin = std::min(rowMin, static_cast<int>(dp[j]));
    }
    if (rowMin > maxDist) return maxDist + 1;
  }
  return dp[n];
}

// (isEnglishLanguage was removed here: the English suffix stems in
// ArabicStem::fallbackForms() now run unconditionally, since this predicate matched
// almost no real-world dictionary. See the comment at that call site.)

}  // namespace

DictionaryStore& DictionaryStore::getInstance() {
  static DictionaryStore instance;
  return instance;
}

void DictionaryStore::loadConfig() {
  configLoaded = true;
  activeIfoPath.clear();
  definitionTextSize = DEF_TEXT_SMALL;
  if (!Storage.exists(CONFIG_PATH)) return;

  const String json = Storage.readFile(CONFIG_PATH);
  if (json.isEmpty()) return;

  JsonDocument doc;
  if (deserializeJson(doc, json) != DeserializationError::Ok) return;
  activeIfoPath = doc["activeIfoPath"] | std::string("");
  const int storedSize = doc["definitionTextSize"] | static_cast<int>(definitionTextSize);
  const uint8_t sizeVersion = doc["definitionTextSizeVersion"] | static_cast<uint8_t>(0);
  if (sizeVersion == 0) {
    definitionTextSize = storedSize == 2 ? static_cast<uint8_t>(DEF_TEXT_LARGE) : static_cast<uint8_t>(DEF_TEXT_SMALL);
  } else if (sizeVersion == 1) {
    definitionTextSize = storedSize == 2 ? static_cast<uint8_t>(DEF_TEXT_LARGE) : static_cast<uint8_t>(DEF_TEXT_SMALL);
  } else if (sizeVersion < DEFINITION_TEXT_SIZE_CONFIG_VERSION) {
    definitionTextSize = storedSize >= 3 ? static_cast<uint8_t>(DEF_TEXT_LARGE) : static_cast<uint8_t>(DEF_TEXT_SMALL);
  } else if (storedSize >= 0 && storedSize < DEF_TEXT_SIZE_COUNT) {
    definitionTextSize = static_cast<uint8_t>(storedSize);
  }
}

bool DictionaryStore::saveConfig() const {
  Storage.mkdir("/.crosspoint");
  JsonDocument doc;
  doc["activeIfoPath"] = activeIfoPath;
  doc["definitionTextSize"] = definitionTextSize;
  doc["definitionTextSizeVersion"] = DEFINITION_TEXT_SIZE_CONFIG_VERSION;
  const std::string tempPath = std::string(CONFIG_PATH) + ".tmp";
  Storage.remove(tempPath.c_str());

  HalFile file;
  if (!Storage.openFileForWrite("DICT", tempPath, file)) return false;
  serializeJson(doc, file);
  file.flush();
  file.close();

  Storage.remove(CONFIG_PATH);
  if (!Storage.rename(tempPath.c_str(), CONFIG_PATH)) {
    Storage.remove(tempPath.c_str());
    return false;
  }
  return true;
}

void DictionaryStore::clearActiveOnlyEntry() {
  activeOnlyEntry = DictionaryEntry{};
  activeOnlyLoaded = false;
}

bool DictionaryStore::loadEntryFromIfoPath(const std::string& ifoPath, DictionaryEntry& entry) const {
  if (!hasExtension(ifoPath, ".ifo") || !Storage.exists(ifoPath.c_str())) return false;

  entry = DictionaryEntry{};
  entry.ifoPath = ifoPath;
  entry.directoryPath = parentDirectory(ifoPath);
  entry.languageId = limitMetadataText(pathBaseName(entry.directoryPath), 48);
  entry.name = limitMetadataText(fileStem(entry.ifoPath));

  const std::string base = entry.ifoPath.substr(0, entry.ifoPath.size() - 4);
  entry.idxPath = base + ".idx";
  entry.dictPath = base + ".dict";
  entry.synPath = base + ".syn";
  entry.cachePath = base + ".cpridx";

  HalFile ifo;
  if (Storage.openFileForRead("DICT", entry.ifoPath, ifo)) {
    char line[256];
    while (readLine(ifo, line, sizeof(line))) {
      std::string value;
      if (parseIfoField(line, "bookname", value)) {
        entry.name = limitMetadataText(std::move(value));
      } else if (parseIfoField(line, "lang", value)) {
        entry.lang = limitMetadataText(std::move(value), 32);
      } else if (parseIfoField(line, "wordcount", value)) {
        entry.wordCount = static_cast<uint32_t>(strtoul(value.c_str(), nullptr, 10));
      } else if (parseIfoField(line, "idxfilesize", value)) {
        entry.idxFileSize = static_cast<uint32_t>(strtoul(value.c_str(), nullptr, 10));
      } else if (parseIfoField(line, "sametypesequence", value)) {
        entry.sameTypeSequence = limitMetadataText(std::move(value), 16);
      } else if (parseIfoField(line, "idxoffsetbits", value)) {
        // StarDict's optional 64-bit index variant: records are 8-byte offset +
        // 4-byte size (12 bytes) instead of 4+4. Every reader here assumes 8
        // (see findIndexHit/headwordAtOrdinal/ensurePrepared), so without this
        // gate such a dictionary parses as garbage rather than failing: the
        // offset comes out as the high half of the real one (usually 0), the
        // size as the low half, and the stream desyncs 4 bytes per entry --
        // which then gets baked into the .cpridx cache with no error surfaced.
        // Rare in the wild, so reject rather than implement a second record
        // layout for it.
        entry.unsupportedFormat = strtoul(value.c_str(), nullptr, 10) == 64;
      }
    }
    ifo.close();
  }

  // Probe for the compressed variant BEFORE deciding missingFiles: a dictionary
  // shipping only <stem>.dict.dz has no plain .dict, and testing for one first
  // would flag it as missing (readDefinition() handles compressed=true fine via
  // DictZip, so this must not fall through to the missingFiles rejection below).
  entry.compressed = !Storage.exists(entry.dictPath.c_str()) && Storage.exists((entry.dictPath + ".dz").c_str());
  entry.missingFiles =
      !Storage.exists(entry.idxPath.c_str()) || (!Storage.exists(entry.dictPath.c_str()) && !entry.compressed);
  if (!Storage.exists(entry.synPath.c_str())) {
    entry.synPath.clear();
  }

  if (entry.idxFileSize == 0 && Storage.exists(entry.idxPath.c_str())) {
    HalFile idx;
    if (Storage.openFileForRead("DICT", entry.idxPath, idx)) {
      entry.idxFileSize = static_cast<uint32_t>(idx.fileSize());
      idx.close();
    }
  }
  // Needed before the cache is read, not after: it is what tells a cache built
  // for a since-replaced .syn from one still describing the file on disk.
  if (!entry.synPath.empty()) {
    HalFile syn;
    if (Storage.openFileForRead("DICT", entry.synPath, syn)) {
      entry.synFileSize = static_cast<uint32_t>(syn.fileSize());
      syn.close();
    }
  }

  return !entry.ifoPath.empty();
}

void DictionaryStore::scan() {
  if (!configLoaded) loadConfig();
  clearActiveOnlyEntry();
  std::vector<DictionaryEntry>().swap(entries);
  activeIndex = -1;
  scanned = true;

  bool scanLimitReached = false;
  auto containsIfo = [this](const std::string& ifoPath) {
    return std::find_if(entries.begin(), entries.end(),
                        [&ifoPath](const DictionaryEntry& entry) { return entry.ifoPath == ifoPath; }) != entries.end();
  };

  auto ensureEntryCapacity = [this](const size_t desiredSize) {
    if (desiredSize <= entries.capacity()) return true;
    size_t nextCapacity = entries.capacity() == 0 ? 8 : entries.capacity() * 2;
    nextCapacity = std::min<size_t>(nextCapacity, MAX_SCAN_ENTRIES);
    nextCapacity = std::max(nextCapacity, desiredSize);
    const size_t bytes = nextCapacity * sizeof(DictionaryEntry);
    if (!hasHeapHeadroom(bytes, HEAP_SCAN_GUARD_BYTES)) {
      LOG_INF("DICT", "Skipping remaining dictionaries: heap too low for list growth free=%u largest=%u need=%u",
              ESP.getFreeHeap(), largestFreeBlock(), static_cast<unsigned>(bytes));
      return false;
    }
    entries.reserve(nextCapacity);
    return true;
  };

  auto appendEntry = [&](DictionaryEntry&& entry) {
    if (entries.size() >= MAX_SCAN_ENTRIES) {
      scanLimitReached = true;
      return false;
    }
    if (!ensureEntryCapacity(entries.size() + 1)) {
      scanLimitReached = true;
      return false;
    }
    entries.push_back(std::move(entry));
    return true;
  };

  if (!activeIfoPath.empty()) {
    DictionaryEntry activeEntry;
    if (loadEntryFromIfoPath(activeIfoPath, activeEntry)) {
      appendEntry(std::move(activeEntry));
    }
  }

  auto root = Storage.open(DICTIONARY_ROOT);
  if (!root || !root.isDirectory()) {
    if (root) root.close();
    if (!entries.empty() && entries[0].ifoPath == activeIfoPath) activeIndex = 0;
    return;
  }

  root.rewindDirectory();
  char name[128];
  for (auto child = root.openNextFile(); child; child = root.openNextFile()) {
    child.getName(name, sizeof(name));
    if (!child.isDirectory() || name[0] == '.') {
      child.close();
      continue;
    }

    const std::string languageId = name;
    const std::string dirPath = joinPath(DICTIONARY_ROOT, languageId);
    child.close();

    auto dir = Storage.open(dirPath.c_str());
    if (!dir || !dir.isDirectory()) {
      if (dir) dir.close();
      continue;
    }

    dir.rewindDirectory();
    char fileName[192];
    for (auto file = dir.openNextFile(); file; file = dir.openNextFile()) {
      file.getName(fileName, sizeof(fileName));
      file.close();
      const std::string ifoName = fileName;
      if (!hasExtension(ifoName, ".ifo")) continue;

      DictionaryEntry entry;
      const std::string ifoPath = joinPath(dirPath, ifoName);
      if (containsIfo(ifoPath) || !loadEntryFromIfoPath(ifoPath, entry)) continue;
      entry.languageId = limitMetadataText(languageId, 48);
      if (!appendEntry(std::move(entry))) break;
    }
    dir.close();
    if (scanLimitReached) break;
  }
  root.close();

  std::sort(entries.begin(), entries.end(), [](const DictionaryEntry& a, const DictionaryEntry& b) {
    if (a.languageId != b.languageId) return a.languageId < b.languageId;
    return a.name < b.name;
  });

  for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
    if (entries[i].ifoPath == activeIfoPath) {
      activeIndex = i;
      break;
    }
  }

  if (activeIndex < 0 && entries.size() == 1 && !entries[0].missingFiles && !entries[0].unsupportedFormat) {
    activeIndex = 0;
    activeIfoPath = entries[0].ifoPath;
    saveConfig();
  }
}

bool DictionaryStore::setActiveIndex(const int index) {
  if (!scanned) scan();
  if (index < 0 || index >= static_cast<int>(entries.size())) return false;
  if (entries[index].missingFiles || entries[index].unsupportedFormat) return false;
  activeIndex = index;
  activeIfoPath = entries[index].ifoPath;
  clearActiveOnlyEntry();
  return saveConfig();
}

void DictionaryStore::clearActiveIfMatches(const std::string& ifoPath) {
  if (!configLoaded) loadConfig();
  if (activeIfoPath != ifoPath) return;
  activeIfoPath.clear();
  activeIndex = -1;
  clearActiveOnlyEntry();
  saveConfig();
}

std::string DictionaryStore::getActiveLabel() const {
  const DictionaryEntry* entry = activeEntry();
  if (!entry) return "";
  return entry->languageId + " - " + entry->name;
}

bool DictionaryStore::setDefinitionTextSize(const uint8_t size) {
  if (size >= DEF_TEXT_SIZE_COUNT) return false;
  definitionTextSize = size;
  return saveConfig();
}

const char* DictionaryStore::definitionTextSizeLabel(const uint8_t size) {
  switch (size) {
    case DEF_TEXT_LARGE:
      return tr(STR_LARGE);
    case DEF_TEXT_READER:
      return tr(STR_DEFINITION_READING_FONT);
    case DEF_TEXT_SMALL:
    default:
      return tr(STR_SMALL);
  }
}

int DictionaryStore::getDefinitionFontId(const int readerFontId) const {
  if (!configLoaded) {
    const_cast<DictionaryStore*>(this)->loadConfig();
  }

  switch (definitionTextSize) {
    case DEF_TEXT_SMALL:
    default:
      return UI_10_FONT_ID;
    case DEF_TEXT_LARGE:
      return UI_12_FONT_ID;
    case DEF_TEXT_READER:
      // Handing back the reader's own font id makes the definition match the page
      // behind it in BOTH scripts at once. Latin text draws in the reading face
      // directly; Arabic text resolves through ArabicFontSystem, where any font id
      // outside the three UI tiers (SMALL/UI_10/UI_12, pinned to Tajawal) falls
      // through to the Arabic reading font -- so the reading face is picked up
      // without this needing to know which script the definition is in.
      return readerFontId;
  }
}

bool DictionaryStore::hasActiveDictionary() const {
  const_cast<DictionaryStore*>(this)->ensureActiveEntryLoaded();
  return activeEntry() != nullptr;
}

void DictionaryStore::ensureScanned() {
  if (!scanned) scan();
}

bool DictionaryStore::ensureActiveEntryLoaded() {
  if (!configLoaded) loadConfig();
  if (activeEntry()) return true;

  if (activeIfoPath.empty()) {
    if (!scanned) scan();
    return activeEntry() != nullptr;
  }

  if (!activeOnlyLoaded || activeOnlyEntry.ifoPath != activeIfoPath) {
    clearActiveOnlyEntry();
    if (!loadEntryFromIfoPath(activeIfoPath, activeOnlyEntry)) return false;
    activeOnlyLoaded = true;
  }
  return activeEntry() != nullptr;
}

DictionaryEntry* DictionaryStore::activeEntry() {
  if (activeIndex >= 0 && activeIndex < static_cast<int>(entries.size())) return &entries[activeIndex];
  if (activeOnlyLoaded) return &activeOnlyEntry;
  return nullptr;
}

const DictionaryEntry* DictionaryStore::activeEntry() const {
  if (activeIndex >= 0 && activeIndex < static_cast<int>(entries.size())) return &entries[activeIndex];
  if (activeOnlyLoaded) return &activeOnlyEntry;
  return nullptr;
}

bool DictionaryStore::prepareActive(const std::function<void(int percent)>& onProgress) {
  ensureActiveEntryLoaded();
  DictionaryEntry* entry = activeEntry();
  if (!entry) return false;
  return ensurePrepared(*entry, onProgress);
}

// Cache layout: header, then the .idx table (offsets, then ordinals), then the
// same pair for the .syn table. Both source file sizes are in the header, so a
// dictionary whose .syn was added or replaced rebuilds on its own.
bool DictionaryStore::loadCheckpointCache(DictionaryEntry& entry) {
  entry.idxTable.clear();
  entry.synTable.clear();

  HalFile file;
  if (!Storage.openFileForRead("DICT", entry.cachePath, file)) return false;
  uint8_t header[28];
  if (file.read(header, sizeof(header)) != static_cast<int>(sizeof(header))) return false;
  // Both source sizes are checked: a .syn that has since been added, replaced or
  // removed makes the cached synonym table describe a file that is no longer
  // there, and the two tables are written as one file, so both get rebuilt.
  if (readU32(header) != CACHE_MAGIC || readU32(header + 4) != entry.idxFileSize ||
      readU32(header + 8) != entry.synFileSize) {
    return false;
  }

  const uint32_t idxCount = readU32(header + 12);
  const uint32_t synCount = readU32(header + 20);
  entry.idxTable.totalEntries = readU32(header + 16);
  entry.synTable.totalEntries = readU32(header + 24);
  entry.idxTable.fileSize = entry.idxFileSize;
  entry.synTable.fileSize = entry.synFileSize;

  if (idxCount == 0 || idxCount > StarDictIndex::MAX_CHECKPOINT_COUNT ||
      synCount > StarDictIndex::MAX_CHECKPOINT_COUNT ||
      !hasHeapHeadroom(static_cast<size_t>(idxCount + synCount) * sizeof(uint32_t) * 2, HEAP_SMALL_GUARD_BYTES)) {
    return false;
  }

  const auto readVector = [&file](std::vector<uint32_t>& out, const uint32_t count) {
    out.reserve(count);
    uint8_t buf[4];
    for (uint32_t i = 0; i < count; ++i) {
      if (file.read(buf, 4) != 4) return false;
      out.push_back(readU32(buf));
    }
    return true;
  };

  if (!readVector(entry.idxTable.offsets, idxCount) || !readVector(entry.idxTable.ordinals, idxCount) ||
      !readVector(entry.synTable.offsets, synCount) || !readVector(entry.synTable.ordinals, synCount)) {
    entry.idxTable.clear();
    entry.synTable.clear();
    return false;
  }
  return !entry.idxTable.empty();
}

bool DictionaryStore::saveCheckpointCache(const DictionaryEntry& entry) const {
  if (entry.idxTable.empty()) return false;
  const std::string tempPath = entry.cachePath + ".tmp";
  Storage.remove(tempPath.c_str());

  HalFile file;
  if (!Storage.openFileForWrite("DICT", tempPath, file)) return false;

  const auto abort = [&file, &tempPath]() {
    file.close();
    Storage.remove(tempPath.c_str());
    return false;
  };

  uint8_t header[28];
  writeU32(header, CACHE_MAGIC);
  writeU32(header + 4, entry.idxFileSize);
  writeU32(header + 8, entry.synFileSize);
  writeU32(header + 12, static_cast<uint32_t>(entry.idxTable.offsets.size()));
  writeU32(header + 16, entry.idxTable.totalEntries);
  writeU32(header + 20, static_cast<uint32_t>(entry.synTable.offsets.size()));
  writeU32(header + 24, entry.synTable.totalEntries);
  if (file.write(header, sizeof(header)) != sizeof(header)) return abort();

  const auto writeVector = [&file](const std::vector<uint32_t>& values) {
    uint8_t buf[4];
    for (const uint32_t value : values) {
      writeU32(buf, value);
      if (file.write(buf, 4) != 4) return false;
    }
    return true;
  };
  if (!writeVector(entry.idxTable.offsets) || !writeVector(entry.idxTable.ordinals) ||
      !writeVector(entry.synTable.offsets) || !writeVector(entry.synTable.ordinals)) {
    return abort();
  }

  file.flush();
  file.close();
  Storage.remove(entry.cachePath.c_str());
  if (!Storage.rename(tempPath.c_str(), entry.cachePath.c_str())) {
    Storage.remove(tempPath.c_str());
    return false;
  }
  return true;
}

bool DictionaryStore::ensurePrepared(DictionaryEntry& entry, const std::function<void(int percent)>& onProgress) {
  // compressed is NOT a rejection here: index preparation only reads entry.idxPath
  // (always plain), never entry.dictPath -- readDefinition() is what handles .dz.
  if (entry.missingFiles || entry.unsupportedFormat) return false;
  if (!entry.idxTable.empty()) return true;
  if (loadCheckpointCache(entry)) return true;

  StarDictIndex::ScanHooks hooks;
  // Feeds the watchdog and lets the render/input task run: lookup() calls this
  // with no progress callback, so the first scan of a large index would
  // otherwise be one unbroken, input-blind stall.
  hooks.yield = [] { delay(1); };
  hooks.hasHeadroom = [](const size_t bytes) { return hasHeapHeadroom(bytes, HEAP_SMALL_GUARD_BYTES); };
  // A plain function pointer plus context, rather than handing the library a
  // std::function: it heap-allocates its closure and costs KBs of binary per
  // signature (see CLAUDE.md).
  if (onProgress) {
    hooks.progress = [](void* ctx, const int percent) {
      (*static_cast<const std::function<void(int)>*>(ctx))(percent);
    };
    hooks.progressCtx = const_cast<std::function<void(int percent)>*>(&onProgress);
  }

  {
    HalFile idx;
    if (!Storage.openFileForRead("DICT", entry.idxPath, idx)) return false;
    HalFileReader reader(idx);
    entry.idxFileSize = static_cast<uint32_t>(idx.fileSize());
    if (!StarDictIndex::buildCheckpoints(reader, StarDictIndex::IDX_RECORD_BYTES, entry.idxTable, hooks)) {
      return false;
    }
  }

  if (!entry.synPath.empty()) {
    HalFile syn;
    if (Storage.openFileForRead("DICT", entry.synPath, syn)) {
      HalFileReader reader(syn);
      entry.synFileSize = static_cast<uint32_t>(syn.fileSize());
      // A .syn that cannot be indexed costs the synonym step, not the
      // dictionary: lookups still run, they just miss the alternate spellings.
      if (!StarDictIndex::buildCheckpoints(reader, StarDictIndex::SYN_RECORD_BYTES, entry.synTable, hooks)) {
        LOG_ERR("DICT", "Synonym index unusable: %s", entry.synPath.c_str());
        entry.synTable.clear();
      }
    }
  }

  saveCheckpointCache(entry);
  return true;
}

bool DictionaryStore::findIndexHit(const DictionaryEntry& entry, const std::string& word, IndexHit& hit) const {
  HalFile idx;
  if (entry.idxTable.empty() || !Storage.openFileForRead("DICT", entry.idxPath, idx)) return false;
  HalFileReader reader(idx);

  StarDictIndex::Hit found;
  if (!StarDictIndex::findKey(reader, entry.idxTable, StarDictIndex::IDX_RECORD_BYTES, word, found)) return false;
  hit.headword = found.key;
  hit.dictOffset = StarDictIndex::readBE32(found.record);
  hit.dictSize = StarDictIndex::readBE32(found.record + 4);
  return true;
}

std::string DictionaryStore::headwordAtOrdinal(const DictionaryEntry& entry, uint32_t ordinal) const {
  HalFile idx;
  if (entry.idxTable.empty() || !Storage.openFileForRead("DICT", entry.idxPath, idx)) return "";
  HalFileReader reader(idx);
  return StarDictIndex::keyAtOrdinal(reader, entry.idxTable, StarDictIndex::IDX_RECORD_BYTES, ordinal);
}

bool DictionaryStore::lookupSynonym(const DictionaryEntry& entry, const std::string& word,
                                    std::string& canonical) const {
  HalFile syn;
  if (entry.synTable.empty() || !Storage.openFileForRead("DICT", entry.synPath, syn)) return false;
  HalFileReader reader(syn);

  uint32_t ordinal = 0;
  if (!StarDictIndex::findSynonym(reader, entry.synTable, word, ordinal)) return false;
  syn.close();

  canonical = headwordAtOrdinal(entry, ordinal);
  return !canonical.empty();
}

std::string DictionaryStore::readDefinition(const DictionaryEntry& entry, const IndexHit& hit, bool& truncated,
                                            DictionaryLookupResult::Status* outFailureStatus) const {
  const auto fail = [&](DictionaryLookupResult::Status status) {
    if (outFailureStatus) *outFailureStatus = status;
    return std::string();
  };

  const size_t readBytes = safeDefinitionReadBytes(hit.dictSize);
  truncated = readBytes == 0 || hit.dictSize > readBytes;
  if (readBytes == 0) return fail(DictionaryLookupResult::Status::LowMemory);

  std::string raw;
  if (entry.compressed) {
    // Most downloadable StarDict dictionaries ship .dict.dz, not a plain .dict --
    // decompress just the needed byte range (dictzip's chunked format) instead of
    // inflating the whole file. extractEntry() writes to a file, not a buffer, so
    // round-trip through a scratch temp file next to the dictionary; it's a single
    // discrete lookup, not a hot loop, so the extra SD I/O is an acceptable trade
    // against not holding a second copy of the definition data in RAM.
    const std::string dzPath = entry.dictPath + ".dz";
    const std::string tempPath = entry.dictPath + ".dz.tmp";
    Storage.remove(tempPath.c_str());
    HalFile out;
    if (!Storage.openFileForWrite("DICT", tempPath, out)) return fail(DictionaryLookupResult::Status::ReadError);
    DictZip::ExtractError dzErr = DictZip::ExtractError::None;
    const bool extracted =
        DictZip::extractEntry(dzPath.c_str(), hit.dictOffset, static_cast<uint32_t>(readBytes), out, &dzErr);
    out.close();
    if (!extracted) {
      LOG_ERR("DICT", "dictzip extract failed: off=%u size=%zu err=%d", hit.dictOffset, readBytes,
              static_cast<int>(dzErr));
      Storage.remove(tempPath.c_str());
      switch (dzErr) {
        case DictZip::ExtractError::LowMemory:
          return fail(DictionaryLookupResult::Status::LowMemory);
        case DictZip::ExtractError::Decompress:
          return fail(DictionaryLookupResult::Status::DecompressError);
        case DictZip::ExtractError::ReadError:
        case DictZip::ExtractError::None:
        default:
          return fail(DictionaryLookupResult::Status::ReadError);
      }
    }

    HalFile in;
    if (!Storage.openFileForRead("DICT", tempPath, in)) {
      Storage.remove(tempPath.c_str());
      return fail(DictionaryLookupResult::Status::ReadError);
    }
    raw.resize(readBytes);
    const int bytesRead = in.read(raw.data(), readBytes);
    in.close();
    Storage.remove(tempPath.c_str());
    // A short read (0 < bytesRead < readBytes) would otherwise decode as a
    // truncated-but-nonempty definition and report Found with corrupt text.
    if (bytesRead < 0 || static_cast<size_t>(bytesRead) != readBytes) {
      return fail(DictionaryLookupResult::Status::ReadError);
    }
  } else {
    HalFile dict;
    if (!Storage.openFileForRead("DICT", entry.dictPath, dict)) return fail(DictionaryLookupResult::Status::ReadError);

    // The offset/size pair comes from the .idx, i.e. from untrusted SD content: a
    // truncated .dict, an .idx/.dict pair from different builds, or a corrupt
    // cache all yield offsets past EOF. Without this, seekSet+read past the end
    // returns whatever the FS hands back and we render it as a definition.
    // Subtraction (not offset + size) so a size near UINT32_MAX can't wrap.
    const uint32_t dictFileSize = static_cast<uint32_t>(dict.fileSize());
    if (hit.dictOffset > dictFileSize || hit.dictSize > dictFileSize - hit.dictOffset) {
      LOG_ERR("DICT", "Definition out of range: off=%u size=%u file=%u", hit.dictOffset, hit.dictSize, dictFileSize);
      dict.close();
      return fail(DictionaryLookupResult::Status::ReadError);
    }

    if (!dict.seekSet(hit.dictOffset)) {
      dict.close();
      return fail(DictionaryLookupResult::Status::ReadError);
    }

    raw.resize(readBytes);
    const int bytesRead = dict.read(raw.data(), readBytes);
    dict.close();
    // A short read (0 < bytesRead < readBytes) would otherwise decode as a
    // truncated-but-nonempty definition and report Found with corrupt text.
    if (bytesRead < 0 || static_cast<size_t>(bytesRead) != readBytes) {
      return fail(DictionaryLookupResult::Status::ReadError);
    }
  }

  std::string decoded = decodeStarDictData(raw, entry.sameTypeSequence);
  raw.clear();
  std::string().swap(raw);
  if (decoded.size() > MAX_DEFINITION_BYTES) {
    decoded.resize(MAX_DEFINITION_BYTES);
    while (!decoded.empty() && (static_cast<unsigned char>(decoded.back()) & 0xC0) == 0x80) decoded.pop_back();
    truncated = true;
  }
  return stripHtmlAndEntities(decoded);
}

DictionaryLookupResult DictionaryStore::lookup(const std::string& rawWord, const bool includeSuggestions) {
  DictionaryLookupResult result;
  ensureActiveEntryLoaded();
  result.query = cleanWord(rawWord);
  if (result.query.empty()) {
    result.status = DictionaryLookupResult::Status::NotFound;
    return result;
  }

  DictionaryEntry* entry = activeEntry();
  if (!entry) {
    result.status = DictionaryLookupResult::Status::NoDictionary;
    return result;
  }
  result.dictionaryName = entry->name;

  if (!ensurePrepared(*entry)) {
    result.status = DictionaryLookupResult::Status::NotReady;
    return result;
  }

  // Returns false when the index pointed at a headword but the definition bytes
  // couldn't actually be read (SD read/seek failure, corrupt offset, OOM) --
  // those aren't a legitimate empty entry and must not report Found with a
  // blank body, nor collapse into a plain NotFound. The first such failure is
  // remembered in readFailure and reported to the caller if every fallback
  // also comes up empty, so the UI can tell a genuine miss apart from a
  // definition that existed but couldn't be read.
  DictionaryLookupResult::Status readFailure = DictionaryLookupResult::Status::NotFound;
  bool haveReadFailure = false;
  // Heap state doesn't improve between immediate successive lookups, so once
  // one hit fails with LowMemory every fallback attempt would too -- retrying
  // just burns more heap-hostile allocations (dictzip's inflate window, the
  // temp-file round trip) for a result already known to fail.
  bool lowMemoryAbort = false;

  auto finishFound = [&](const IndexHit& hit) {
    result.headword = hit.headword;
    DictionaryLookupResult::Status failStatus = DictionaryLookupResult::Status::NotFound;
    result.definition = readDefinition(*entry, hit, result.truncated, &failStatus);
    if (result.definition.empty()) {
      if (!haveReadFailure) {
        readFailure = failStatus;
        haveReadFailure = true;
      }
      if (failStatus == DictionaryLookupResult::Status::LowMemory) lowMemoryAbort = true;
      return false;
    }
    result.status = DictionaryLookupResult::Status::Found;
    addHistory(result.headword.empty() ? result.query : result.headword);
    return true;
  };

  IndexHit hit;
  if (findIndexHit(*entry, result.query, hit) && finishFound(hit)) return result;

  std::string canonical;
  if (!lowMemoryAbort && lookupSynonym(*entry, result.query, canonical) && findIndexHit(*entry, canonical, hit) &&
      finishFound(hit)) {
    return result;
  }

  // The candidate that answers carries the cut that made it, so the definition
  // screen can show which parts of the word were set aside to find it.
  const auto describe = [&result](const ArabicStem::Candidate& candidate) {
    ArabicStem::Segment segments[ArabicStem::MAX_SEGMENTS];
    const size_t count = ArabicStem::segments(result.query, candidate, segments, ArabicStem::MAX_SEGMENTS);
    if (count > 1) result.decomposition = ArabicStem::joinSegments(segments, count);
  };

  for (const ArabicStem::Candidate& fallback :
       ArabicStem::fallbackCandidates(result.query, isSpanishLanguage(*entry))) {
    if (lowMemoryAbort) break;
    if (findIndexHit(*entry, fallback.form, hit) && finishFound(hit)) {
      describe(fallback);
      return result;
    }
    if (lowMemoryAbort) break;
    if (lookupSynonym(*entry, fallback.form, canonical) && findIndexHit(*entry, canonical, hit) && finishFound(hit)) {
      describe(fallback);
      return result;
    }
  }

  if (haveReadFailure) {
    result.status = readFailure;
    return result;
  }

  result.status = DictionaryLookupResult::Status::NotFound;
  if (includeSuggestions) {
    result.suggestions = findSuggestions(*entry, result.query, 8);
  }
  return result;
}

std::vector<std::string> DictionaryStore::findSuggestions(const DictionaryEntry& entry, const std::string& word,
                                                          const int maxResults) const {
  std::vector<std::string> results;
  if (entry.idxTable.empty()) return results;

  HalFile idx;
  if (!Storage.openFileForRead("DICT", entry.idxPath, idx)) return results;
  HalFileReader reader(idx);

  // Anchors the neighbourhood scan and the scoring below. ArabicStem::suggestionAnchor()
  // rather than the raw query so a word like the definite-article form of
  // "ancients" lands near its bare stem instead of among unrelated al-prefixed
  // headwords that merely sort near it -- and rather than the most-stripped
  // fallback form, which is the one most likely to be a wrong strip. See that
  // function for why the difference matters here and not on the lookup path.
  const std::string anchorWord = ArabicStem::suggestionAnchor(word);

  // Two checkpoint segments either side of the anchor: enough neighbourhood for
  // a near-miss to be in range, bounded so a not-found lookup cannot walk the
  // whole index.
  const uint32_t totalToScan = StarDictIndex::seekToNeighbourhood(reader, entry.idxTable, anchorWord, 2);

  const std::string scoreWord = suggestionScoreKey(anchorWord);
  const int maxDistance = std::max(2, static_cast<int>(scoreWord.size()) / 3 + 1);

  struct Candidate {
    std::string word;
    int score;
    int commonPrefix;
    int lengthDelta;
  };
  std::vector<Candidate> candidates;
  if (!hasHeapHeadroom(MAX_SUGGESTION_CANDIDATES * sizeof(Candidate), HEAP_SMALL_GUARD_BYTES)) return results;
  candidates.reserve(MAX_SUGGESTION_CANDIDATES);
  auto candidateBetter = [](const Candidate& a, const Candidate& b) {
    if (a.score != b.score) return a.score < b.score;
    if (a.commonPrefix != b.commonPrefix) return a.commonPrefix > b.commonPrefix;
    if (a.lengthDelta != b.lengthDelta) return a.lengthDelta < b.lengthDelta;
    return a.word.size() < b.word.size();
  };
  auto addCandidate = [&](const std::string& key, const int score, const int commonPrefix, const int lengthDelta) {
    if (key.empty() || StarDictIndex::compareKeys(key, word) == 0) return;
    Candidate candidate{key, score, commonPrefix, lengthDelta};
    if (candidates.size() < MAX_SUGGESTION_CANDIDATES) {
      candidates.push_back(std::move(candidate));
      return;
    }
    auto worst = std::max_element(candidates.begin(), candidates.end(), candidateBetter);
    if (worst != candidates.end() && candidateBetter(candidate, *worst)) {
      *worst = std::move(candidate);
    }
  };

  // NOTE: there used to be an "exact-fallback pass" here that re-ran
  // findIndexHit()/lookupSynonym() over fallbackForms to seed candidates. It was
  // dead code: findSuggestions() has exactly one caller (lookup(), below the
  // NotFound branch), and that caller has already tried every one of these same
  // forms through the same two functions immediately beforehand -- if any had
  // hit, lookup() would have returned early and never called us. ArabicStem::fallbackForms()
  // is deterministic and nothing mutates `entry` in between, so the pass could
  // only ever re-derive the same misses. For Arabic (up to ~12 fallback forms)
  // that cost 12 full index scans plus 12 .syn binary searches on every
  // not-found lookup -- roughly a second of guaranteed-useless SD I/O.
  // ArabicStem::fallbackForms() is no longer called here at all: anchorWord now comes from
  // ArabicStem::suggestionAnchor(), which shares the normalization but stops short of the
  // strips that can corrupt a word.

  for (uint32_t i = 0; i < totalToScan; ++i) {
    const std::string key = StarDictIndex::readKey(reader);
    if (key.empty()) break;
    uint8_t skip[StarDictIndex::IDX_RECORD_BYTES];
    if (reader.read(skip, sizeof(skip)) != static_cast<int>(sizeof(skip))) break;
    if (StarDictIndex::compareKeys(key, word) == 0) continue;

    const std::string scoreKey = suggestionScoreKey(key);
    const int score = editDistanceLimited(scoreKey, scoreWord, maxDistance);
    if (score <= maxDistance) {
      addCandidate(key, score, commonPrefixLength(scoreKey, scoreWord),
                   std::abs(static_cast<int>(scoreKey.size()) - static_cast<int>(scoreWord.size())));
    }
  }
  idx.close();

  std::sort(candidates.begin(), candidates.end(), candidateBetter);

  for (const Candidate& candidate : candidates) {
    if (std::find(results.begin(), results.end(), candidate.word) == results.end()) {
      results.push_back(candidate.word);
      if (static_cast<int>(results.size()) >= maxResults) break;
    }
  }
  return results;
}

std::string DictionaryStore::cleanWord(const std::string& word) { return ArabicStem::cleanWord(word); }

std::vector<std::string> DictionaryStore::getHistory() {
  std::vector<std::string> history;
  HalFile file;
  if (!Storage.openFileForRead("DICT", HISTORY_PATH, file)) return history;

  std::string line;
  while (true) {
    const int c = file.read();
    if (c < 0) break;
    if (c == '\r') continue;
    if (c == '\n') {
      if (!line.empty()) {
        history.push_back(line);
        if (history.size() >= MAX_HISTORY_ITEMS) break;
        line.clear();
      }
      continue;
    }
    if (line.size() < 128) line.push_back(static_cast<char>(c));
  }
  if (!line.empty() && history.size() < MAX_HISTORY_ITEMS) history.push_back(line);
  file.close();
  return history;
}

void DictionaryStore::addHistory(const std::string& word) {
  const std::string cleaned = cleanWord(word);
  if (cleaned.empty()) return;

  auto history = getHistory();
  history.erase(std::remove(history.begin(), history.end(), cleaned), history.end());
  history.insert(history.begin(), cleaned);
  if (history.size() > MAX_HISTORY_ITEMS) history.resize(MAX_HISTORY_ITEMS);

  Storage.mkdir("/.crosspoint");
  const std::string tempPath = std::string(HISTORY_PATH) + ".tmp";
  Storage.remove(tempPath.c_str());
  HalFile file;
  if (!Storage.openFileForWrite("DICT", tempPath, file)) return;
  for (const std::string& item : history) {
    file.write(item.c_str(), item.size());
    file.write(static_cast<uint8_t>('\n'));
  }
  file.flush();
  file.close();
  Storage.remove(HISTORY_PATH);
  if (!Storage.rename(tempPath.c_str(), HISTORY_PATH)) {
    Storage.remove(tempPath.c_str());
  }
}

void DictionaryStore::clearHistory() { Storage.remove(HISTORY_PATH); }
