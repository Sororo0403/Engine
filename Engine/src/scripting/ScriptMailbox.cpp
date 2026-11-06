#include "scripting/ScriptMailbox.h"
#include "platform/windows/StringConversion.h"
#include "scene/Scene.h"
#include "scripting/LuaScript.h"
#include <algorithm>
#include <exception>
#include <stdexcept>
#include <utility>

bool ScriptMailbox::Enqueue(SceneObjectId receiver, SceneObjectId sender,
                            std::string_view name, std::string_view data,
                            std::string &error) {
	try {
		if (!receiver || name.empty() ||
		    name.size() > ScriptMessage::kMaximumNameBytes ||
		    name.find('\0') != std::string_view::npos ||
		    StringConversion::Utf8ToWide(std::string(name)).empty())
			throw std::runtime_error(
			    "Messages require a receiver and 1 to 64 UTF-8 name bytes.");
		std::string normalized;
		if (data.size() > ScriptMessage::kMaximumDataBytes ||
		    !LuaScript::NormalizeState(data, normalized, error) ||
		    normalized.size() > ScriptMessage::kMaximumDataBytes)
			throw std::runtime_error(
			    "Message data requires a JSON object of at most 4 KiB.");
		const auto size = name.size() + normalized.size();
		if (sent_ >= kMaximumFrameMessages ||
		    pending_.size() >= kMaximumPending || size > kMaximumBytes - bytes_)
			throw std::runtime_error(
			    "Script message queue count or byte limit exceeded.");
		pending_.push_back(
		    {receiver, sender, std::string(name), std::move(normalized)});
		bytes_ += size;
		++sent_;
		error.clear();
		return true;
	} catch (const std::exception &exception) {
		error = exception.what();
		return false;
	}
}

std::vector<ScriptMessage> ScriptMailbox::BeginStep(bool deliver) {
	sent_ = 0;
	if (!deliver)
		return {};
	bytes_ = 0;
	return std::exchange(pending_, {});
}

void ScriptMailbox::RemoveMissingReceivers(const Scene &scene) {
	std::erase_if(pending_, [&](const auto &message) {
		return !scene.Contains(message.receiver);
	});
	bytes_ = 0;
	for (const auto &message : pending_)
		bytes_ += message.name.size() + message.data.size();
}

void ScriptMailbox::Clear() {
	pending_.clear();
	bytes_ = sent_ = 0;
}
