#include "gameplay/ScriptComponent.h"
#include "platform/windows/PathUtils.h"
#include "scripting/LuaScript.h"
#include <set>

bool ScriptComponent::IsValid() const {
	if ((!asset.empty() && (!PathUtils::HasExtension(asset, ".lua") ||
	                        asset.native().find(L'\0') != std::wstring::npos ||
	                        (asset.has_root_path() && !asset.is_absolute()))) ||
	    references.size() > kMaximumReferences)
		return false;
	std::string normalized, error;
	if (!LuaScript::NormalizeState(initialState, normalized, error))
		return false;
	std::set<std::string> names;
	for (const auto &[name, id] : references) {
		(void)id;
		if (name.empty() || name.size() > kMaximumReferenceNameBytes ||
		    name.find('\0') != std::string::npos || !names.insert(name).second)
			return false;
	}
	return true;
}
