#include "class_environment.hpp"

#include <algorithm>
#include <array>
#include <set>
#include <system_error>
#include <utility>

namespace {
struct Package { std::string_view name; std::string_view directory; };
constexpr std::array<Package, 8> packages{{
	{"java.game.parts.accessories", "parts/accessories/Scripts"},
	{"java.game.parts", "parts/Scripts"},
	{"java.game", "sl/Scripts/game"},
	{"java.lang", "system/Scripts/lang"},
	{"java.io", "system/Scripts/io"},
	{"java.util", "system/Scripts/util"},
	{"java.render", "system/Scripts/render"},
	{"java.sound", "system/Scripts/sound"},
}};

bool validName(std::string_view name) {
	if (name.empty()) return false;
	bool start = true;
	for (char c : name) {
		if (c == '.') { if (start) return false; start = true; continue; }
		const bool alpha = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '$';
		if (!alpha && (start || c < '0' || c > '9')) return false;
		start = false;
	}
	return !start;
}
}

ClassEnvironment::ClassEnvironment(std::filesystem::path root) : root_(std::move(root)) {}

const TufaClass* ClassEnvironment::findOrLoad(std::string_view name, std::string& error) {
	error.clear();
	if (!validName(name)) { error = "invalid class name"; return nullptr; }
	const std::string key(name);
	const auto present = loaded_.find(key);
	if (present != loaded_.end()) return present->second;
	const Package* package = nullptr;
	for (const auto& candidate : packages) {
		if (name.size() > candidate.name.size() && name.substr(0, candidate.name.size()) == candidate.name &&
		    name[candidate.name.size()] == '.') { package = &candidate; break; }
	}
	if (!package) { error = "unknown class package"; return nullptr; }
	std::string relative(name.substr(package->name.size() + 1));
	for (char& c : relative) if (c == '.') c = '/';
	const std::filesystem::path requested = std::filesystem::path(package->directory) / (relative + ".class");
	std::error_code ec;
	const auto root = std::filesystem::canonical(root_, ec);
	if (ec) { error = "invalid class root"; return nullptr; }
	std::filesystem::path path = std::filesystem::canonical(root / requested, ec);
	if (ec) {
		for (auto it = dynamicPackages_.rbegin(); it != dynamicPackages_.rend(); ++it) {
			if (name.size() > it->first.size() && name.substr(0, it->first.size()) == it->first &&
			    name[it->first.size()] == '.' &&
			    name.find('.', it->first.size() + 1) == std::string_view::npos) {
				std::string leaf(name.substr(it->first.size() + 1));
				std::error_code dynEc;
				auto cand = std::filesystem::canonical(it->second / (leaf + ".class"), dynEc);
				if (!dynEc) { path = std::move(cand); ec.clear(); break; }
			}
		}
	}
	if (ec) { error = "class file not found"; return nullptr; }
	auto base = root.begin(), part = path.begin();
	for (; base != root.end() && part != path.end(); ++base, ++part)
		if (*base != *part) { error = "class file escapes root"; return nullptr; }
	if (base != root.end() || !std::filesystem::is_regular_file(path, ec) || ec) {
		error = "invalid class file"; return nullptr;
	}
	auto parsed = tufaLoadFile(path.string());
	if (!parsed.ok) { error = "invalid class file: " + parsed.error; return nullptr; }
	if (parsed.file.classes.empty()) { error = "empty class file"; return nullptr; }
	bool found = false;
	std::set<std::string> stagedNames;
	for (const auto& cls : parsed.file.classes) {
		if (!cls.hasClassHeader || !validName(cls.name)) { error = "invalid bundled class name"; return nullptr; }
		if (!stagedNames.insert(cls.name).second || loaded_.count(cls.name)) {
			error = "duplicate class: " + cls.name; return nullptr;
		}
		if (cls.name == key) found = true;
	}
	if (!found) { error = "requested class absent from file"; return nullptr; }
	auto owner = std::make_unique<TufaFile>(std::move(parsed.file));
	for (const auto& cls : owner->classes) loaded_.emplace(cls.name, &cls);
	files_.push_back(std::move(owner));
	return loaded_.at(key);
}

std::vector<const TufaClass*> ClassEnvironment::classes() const {
	std::vector<const TufaClass*> result;
	for (const auto& file : files_)
		for (const auto& cls : file->classes) result.push_back(&cls);
	return result;
}

const TufaClass* ClassEnvironment::loadScriptPath(std::string_view raw, std::string& error) {
  error.clear();
  if (raw.empty() || raw.size() >= 256 || raw.front() == '/' || raw.front() == '\\' || raw.find(':') != std::string_view::npos || raw.find('\0') != std::string_view::npos) { error = "invalid script path"; return nullptr; }
  std::string name(raw); std::replace(name.begin(), name.end(), '\\', '/');
  const std::filesystem::path relative(name);
  for (const auto& part : relative) if (part.empty() || part == "." || part == "..") { error = "invalid script path"; return nullptr; }
  if (relative.extension() != ".class") { error = "unsupported script extension"; return nullptr; }
  std::error_code ec; const auto root = std::filesystem::canonical(root_, ec);
  if (ec) { error = "invalid class root"; return nullptr; }
  const auto path = std::filesystem::canonical(root / relative, ec);
  if (ec) { error = "script class file not found"; return nullptr; }
  auto base = root.begin(), part = path.begin();
  for (; base != root.end() && part != path.end(); ++base, ++part) if (*base != *part) { error = "script path escapes root"; return nullptr; }
  if (base != root.end() || !std::filesystem::is_regular_file(path, ec) || ec) { error = "invalid script class file"; return nullptr; }
  const std::string key = path.string();
  const auto existing = paths_.find(key); if (existing != paths_.end()) return existing->second;
  auto parsed = tufaLoadFile(key);
  if (!parsed.ok || parsed.file.classes.empty()) { error = "invalid script class file: " + parsed.error; return nullptr; }
  std::set<std::string> names;
  for (const auto& cls : parsed.file.classes) {
    if (!cls.hasClassHeader || !validName(cls.name) || !names.insert(cls.name).second || loaded_.count(cls.name)) { error = "invalid or duplicate script class: " + cls.name; return nullptr; }
  }
  auto owner = std::make_unique<TufaFile>(std::move(parsed.file));
  const auto* result = &owner->classes.front();
  const auto dot = result->name.rfind('.');
  if (dot != std::string::npos) {
    const std::string pkg = result->name.substr(0, dot);
    const auto dir = path.parent_path();
    bool exists = false;
    for (const auto& entry : dynamicPackages_)
      if (entry.first == pkg && entry.second == dir) exists = true;
    if (!exists) dynamicPackages_.emplace_back(pkg, dir);
  }
  for (const auto& cls : owner->classes) loaded_.emplace(cls.name, &cls);
  files_.push_back(std::move(owner)); paths_.emplace(key, result); return result;
}
