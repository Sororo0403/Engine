#include "history/CommandGroup.h"

#include "scene/Scene.h"

#include <utility>

CommandGroup::CommandGroup(std::vector<Command> commands) noexcept
    : commands_(std::move(commands)) {
}

void CommandGroup::RewriteObjects(
    const SceneAssetReferences::ObjectRewrite &rewrite) {
	for (auto &entry : commands_) {
		std::visit([&](auto &command) {
			if constexpr (requires { command.RewriteObjects(rewrite); }) {
				command.RewriteObjects(rewrite);
			}
		}, entry);
	}
}

bool CommandGroup::Undo(Scene &scene) const {
	if (commands_.empty()) {
		return true;
	}

	return scene.ApplyTransaction([this](Scene &working) {
		for (auto it = commands_.rbegin(); it != commands_.rend(); ++it) {
			const bool succeeded = std::visit(
			    [&working](const auto &command) {
				    return command.Undo(working);
			    },
			    *it);

			if (!succeeded) {
				return false;
			}
		}

		return true;
	});
}

bool CommandGroup::Redo(Scene &scene) const {
	if (commands_.empty()) {
		return true;
	}

	return scene.ApplyTransaction([this](Scene &working) {
		for (const auto &entry : commands_) {
			const bool succeeded = std::visit(
			    [&working](const auto &command) {
				    return command.Redo(working);
			    },
			    entry);

			if (!succeeded) {
				return false;
			}
		}

		return true;
	});
}
