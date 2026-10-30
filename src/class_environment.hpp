#pragma once

#include "tufa.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// Class pointers and views are valid while this environment remains alive.
class ClassEnvironment {
public:
	explicit ClassEnvironment(std::filesystem::path root);
	const TufaClass* findOrLoad(std::string_view name, std::string& error);
	const TufaClass* loadScriptPath(std::string_view path, std::string& error);
	std::vector<const TufaClass*> classes() const;

private:
	std::filesystem::path root_;
	std::vector<std::unique_ptr<TufaFile>> files_;
	std::unordered_map<std::string, const TufaClass*> loaded_;
	std::unordered_map<std::string, const TufaClass*> paths_;
	std::vector<std::pair<std::string, std::filesystem::path>> dynamicPackages_;
};
