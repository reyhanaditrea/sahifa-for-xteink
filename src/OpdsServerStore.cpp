#include "OpdsServerStore.h"

#include <Logging.h>
#include <ObfuscationUtils.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string_view>

namespace {

// The Midad catalog (midad.one, formerly foulad.one) was an account-only server that
// this firmware no longer talks to. Its entry holds a per-device token; dropping it on
// load keeps that credential from lingering on the SD card. Host comparison only:
// scheme, port and path have all changed over the catalog's life.
bool isRetiredMidadCatalog(std::string_view url) {
  const auto schemeEnd = url.find("://");
  if (schemeEnd == std::string_view::npos) return false;
  url.remove_prefix(schemeEnd + 3);
  const auto authorityEnd = url.find_first_of("/?#");
  std::string_view host = authorityEnd == std::string_view::npos ? url : url.substr(0, authorityEnd);
  const auto at = host.rfind('@');
  if (at != std::string_view::npos) host.remove_prefix(at + 1);
  const auto colon = host.find(':');
  if (colon != std::string_view::npos) host = host.substr(0, colon);

  const auto equalsIgnoreCase = [](std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
             return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
           });
  };
  return equalsIgnoreCase(host, "midad.one") || equalsIgnoreCase(host, "foulad.one");
}

}  // namespace

void OpdsServerStore::toJson(JsonDocument& doc) const {
  JsonArray arr = doc["servers"].to<JsonArray>();
  for (const auto& server : servers) {
    JsonObject obj = arr.add<JsonObject>();
    obj["name"] = server.name;
    obj["url"] = server.url;
    obj["username"] = server.username;
    obj["password_obf"] = obfuscation::obfuscateToBase64(server.password);
  }
}

bool OpdsServerStore::fromJson(JsonVariantConst doc) {
  // Tolerate a missing/invalid 'servers' key (treat as empty list); only a
  // JSON parse error is fatal. A null JsonArray iterates zero times.
  servers.clear();
  JsonArrayConst arr = doc["servers"].as<JsonArrayConst>();
  servers.reserve(std::min(arr.size(), MAX_SERVERS));
  bool needsResave = false;

  for (JsonObjectConst obj : arr) {
    if (servers.size() >= OpdsServerStore::MAX_SERVERS) break;
    OpdsServer server;
    server.name = obj["name"] | "";
    server.url = obj["url"] | "";
    server.username = obj["username"] | "";
    server.password = extractPassword(obj, needsResave);

    if (isRetiredMidadCatalog(server.url)) {
      LOG_INF("OPS", "Dropped retired Midad catalog entry: %s", server.url.c_str());
      needsResave = true;
      continue;
    }

    // Two entries with the same URL describe the same server. Collapse them rather
    // than listing it twice; the credential wins over an empty one, otherwise the
    // first entry stands.
    const auto twin =
        std::find_if(servers.begin(), servers.end(), [&](const OpdsServer& s) { return s.url == server.url; });
    if (twin != servers.end()) {
      if (twin->password.empty() && !server.password.empty()) {
        *twin = server;
      }
      needsResave = true;
      LOG_INF("OPS", "Collapsed duplicate catalog entry for %s", server.url.c_str());
      continue;
    }

    servers.push_back(std::move(server));
  }

  LOG_DBG("OPS", "Loaded %zu OPDS servers from file", servers.size());

  if (needsResave) {
    LOG_DBG("OPS", "Resaving JSON with obfuscated passwords");
    requestResave();
  }

  return true;
}

bool OpdsServerStore::addServer(const OpdsServer& server) {
  // Adding a server already present replaces its entry rather than adding a twin.
  const auto existing =
      std::find_if(servers.begin(), servers.end(), [&](const OpdsServer& s) { return s.url == server.url; });
  if (existing != servers.end()) {
    *existing = server;
    LOG_DBG("OPS", "Updated existing server: %s", server.name.c_str());
    return saveToFile();
  }

  if (servers.size() >= MAX_SERVERS) {
    LOG_DBG("OPS", "Cannot add more servers, limit of %zu reached", MAX_SERVERS);
    return false;
  }

  servers.push_back(server);
  LOG_DBG("OPS", "Added server: %s", server.name.c_str());
  return saveToFile();
}

bool OpdsServerStore::updateServer(size_t index, const OpdsServer& server) {
  if (index >= servers.size()) {
    return false;
  }

  servers[index] = server;
  LOG_DBG("OPS", "Updated server: %s", server.name.c_str());
  return saveToFile();
}

bool OpdsServerStore::removeServer(size_t index) {
  if (index >= servers.size()) {
    return false;
  }

  LOG_DBG("OPS", "Removed server: %s", servers[index].name.c_str());
  servers.erase(servers.begin() + static_cast<ptrdiff_t>(index));
  return saveToFile();
}

const OpdsServer* OpdsServerStore::getServer(size_t index) const {
  if (index >= servers.size()) {
    return nullptr;
  }
  return &servers[index];
}
