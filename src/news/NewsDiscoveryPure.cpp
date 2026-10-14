// The parsing half of NewsDiscovery: no networking, no Arduino, so the host
// test target can compile it directly. The fetching half lives in
// NewsDiscovery.cpp.
#include <cstring>

#include "news/NewsDiscovery.h"

namespace {

char lower(const char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c | 0x20) : c; }

bool isSpace(const char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

bool startsWithCi(const char* haystack, const size_t len, const char* needle) {
  const size_t n = strlen(needle);
  if (len < n) return false;
  for (size_t i = 0; i < n; i++) {
    if (lower(haystack[i]) != lower(needle[i])) return false;
  }
  return true;
}

// Case-insensitive search inside a length-bounded buffer (the page head is not
// NUL-terminated when it comes straight off the socket).
const char* findCi(const char* haystack, const size_t len, const char* needle) {
  const size_t n = strlen(needle);
  if (n == 0 || len < n) return nullptr;
  for (size_t i = 0; i + n <= len; i++) {
    if (startsWithCi(haystack + i, len - i, needle)) return haystack + i;
  }
  return nullptr;
}

// Reads the value of attribute `name` from a tag body. Returns the value start
// and length; false when the attribute is absent.
bool attrValue(const char* tag, const size_t tagLen, const char* name, const char*& valueOut, size_t& valueLen) {
  const size_t nameLen = strlen(name);
  for (size_t i = 0; i + nameLen < tagLen; i++) {
    // Attribute names must start at a boundary, or href= would match a
    // hypothetical data-href=.
    if (i > 0 && !isSpace(tag[i - 1])) continue;
    if (!startsWithCi(tag + i, tagLen - i, name)) continue;
    size_t j = i + nameLen;
    while (j < tagLen && isSpace(tag[j])) j++;
    if (j >= tagLen || tag[j] != '=') continue;
    j++;
    while (j < tagLen && isSpace(tag[j])) j++;
    if (j >= tagLen) return false;
    char quote = 0;
    if (tag[j] == '"' || tag[j] == '\'') {
      quote = tag[j];
      j++;
    }
    const size_t start = j;
    while (j < tagLen && (quote ? tag[j] != quote : (!isSpace(tag[j]) && tag[j] != '>'))) j++;
    valueOut = tag + start;
    valueLen = j - start;
    return true;
  }
  return false;
}

bool copyBounded(const char* src, const size_t srcLen, char* out, const size_t outLen) {
  if (srcLen + 1 > outLen) return false;
  memcpy(out, src, srcLen);
  out[srcLen] = '\0';
  return true;
}

// Start of the host portion, i.e. just past "scheme://" when present.
const char* hostStart(const char* url) {
  if (const char* sep = strstr(url, "://")) return sep + 3;
  return url;
}

}  // namespace

namespace NewsDiscovery {

bool looksLikeFeedHead(const char* buf, const size_t len) {
  if (!buf) return false;
  size_t i = 0;
  if (len >= 3 && static_cast<unsigned char>(buf[0]) == 0xEF && static_cast<unsigned char>(buf[1]) == 0xBB &&
      static_cast<unsigned char>(buf[2]) == 0xBF) {
    i = 3;
  }
  while (i < len && isSpace(buf[i])) i++;
  const char* p = buf + i;
  const size_t remaining = len - i;
  if (remaining == 0) return false;
  // An XML declaration alone is not proof (XHTML pages carry one), so when the
  // document starts with <?xml we look further in for the root element.
  if (startsWithCi(p, remaining, "<?xml")) {
    return findCi(p, remaining, "<rss") != nullptr || findCi(p, remaining, "<feed") != nullptr ||
           findCi(p, remaining, "<rdf:rdf") != nullptr;
  }
  return startsWithCi(p, remaining, "<rss") || startsWithCi(p, remaining, "<feed") ||
         startsWithCi(p, remaining, "<rdf:rdf");
}

bool urlOrigin(const char* url, char* out, const size_t outLen) {
  if (!url || !out) return false;
  const char* host = hostStart(url);
  if (*host == '\0' || *host == '/') return false;
  const char* pathStart = strpbrk(host, "/?#");
  const size_t len = pathStart ? static_cast<size_t>(pathStart - url) : strlen(url);
  return copyBounded(url, len, out, outLen);
}

bool urlHost(const char* url, char* out, const size_t outLen) {
  if (!url || !out) return false;
  const char* host = hostStart(url);
  const char* end = strpbrk(host, "/?#:");
  size_t len = end ? static_cast<size_t>(end - host) : strlen(host);
  if (len == 0) return false;
  if (len > 4 && startsWithCi(host, len, "www.")) {
    host += 4;
    len -= 4;
  }
  return copyBounded(host, len, out, outLen);
}

bool urlWithWwwToggled(const char* url, char* out, const size_t outLen) {
  if (!url || !out) return false;
  const char* host = hostStart(url);
  if (*host == '\0' || *host == '/') return false;
  const size_t schemeLen = static_cast<size_t>(host - url);
  const char* rest = strpbrk(host, "/?#");
  const size_t hostLen = rest ? static_cast<size_t>(rest - host) : strlen(host);

  const bool hasWww = hostLen > 4 && startsWithCi(host, hostLen, "www.");
  const char* newHost = hasWww ? host + 4 : host;
  const size_t newHostLen = hasWww ? hostLen - 4 : hostLen;
  const char* prefix = hasWww ? "" : "www.";
  const size_t prefixLen = strlen(prefix);
  const size_t restLen = rest ? strlen(rest) : 0;

  if (schemeLen + prefixLen + newHostLen + restLen + 1 > outLen) return false;
  size_t at = 0;
  memcpy(out + at, url, schemeLen);
  at += schemeLen;
  memcpy(out + at, prefix, prefixLen);
  at += prefixLen;
  memcpy(out + at, newHost, newHostLen);
  at += newHostLen;
  if (rest) memcpy(out + at, rest, restLen);
  at += restLen;
  out[at] = '\0';
  return true;
}

bool normalizeInputUrl(const char* input, char* out, const size_t outLen) {
  if (!input || !out) return false;
  while (isSpace(*input)) input++;
  size_t len = strlen(input);
  while (len > 0 && isSpace(input[len - 1])) len--;
  if (len == 0) return false;

  const bool hasScheme = startsWithCi(input, len, "http://") || startsWithCi(input, len, "https://");
  const char* prefix = hasScheme ? "" : "https://";
  const size_t prefixLen = strlen(prefix);
  if (prefixLen + len + 1 > outLen) return false;
  memcpy(out, prefix, prefixLen);
  memcpy(out + prefixLen, input, len);
  out[prefixLen + len] = '\0';
  return true;
}

bool findAlternateLink(const char* html, const size_t len, const char* baseUrl, char* out, const size_t outLen) {
  if (!html || !out) return false;
  size_t offset = 0;
  while (offset < len) {
    const char* tagStart = findCi(html + offset, len - offset, "<link");
    if (!tagStart) return false;
    const size_t remaining = len - static_cast<size_t>(tagStart - html);
    const char* tagEnd = static_cast<const char*>(memchr(tagStart, '>', remaining));
    const size_t tagLen = tagEnd ? static_cast<size_t>(tagEnd - tagStart) : remaining;
    offset = static_cast<size_t>(tagStart - html) + (tagLen > 0 ? tagLen : 1);

    const char* value = nullptr;
    size_t valueLen = 0;
    if (!attrValue(tagStart, tagLen, "type", value, valueLen)) continue;
    const bool isFeedType = (valueLen >= 20 && (startsWithCi(value, valueLen, "application/rss+xml") ||
                                                startsWithCi(value, valueLen, "application/atom+xml"))) ||
                            startsWithCi(value, valueLen, "application/rss+xml") ||
                            startsWithCi(value, valueLen, "application/atom+xml") ||
                            startsWithCi(value, valueLen, "application/rdf+xml");
    if (!isFeedType) continue;
    // rel is usually "alternate" but some sites omit it entirely; only reject a
    // rel that says something else (e.g. rel="self" inside an embedded feed).
    if (attrValue(tagStart, tagLen, "rel", value, valueLen) && !startsWithCi(value, valueLen, "alternate")) continue;
    if (!attrValue(tagStart, tagLen, "href", value, valueLen) || valueLen == 0) continue;

    if (startsWithCi(value, valueLen, "http://") || startsWithCi(value, valueLen, "https://")) {
      return copyBounded(value, valueLen, out, outLen);
    }
    char origin[128];
    if (!urlOrigin(baseUrl, origin, sizeof(origin))) return false;
    if (value[0] == '/') {
      const size_t originLen = strlen(origin);
      if (originLen + valueLen + 1 > outLen) return false;
      memcpy(out, origin, originLen);
      memcpy(out + originLen, value, valueLen);
      out[originLen + valueLen] = '\0';
      return true;
    }
    // Relative without a leading slash: resolve against the origin root, which
    // is right for the head of a site's landing page.
    const size_t originLen = strlen(origin);
    if (originLen + 1 + valueLen + 1 > outLen) return false;
    memcpy(out, origin, originLen);
    out[originLen] = '/';
    memcpy(out + originLen + 1, value, valueLen);
    out[originLen + 1 + valueLen] = '\0';
    return true;
  }
  return false;
}

}  // namespace NewsDiscovery
