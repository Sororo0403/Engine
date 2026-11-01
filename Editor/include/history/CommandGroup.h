#pragma once

#include "history/ObjectCommand.h"
#include "history/ModelPathCommand.h"
#include "history/RenameCommand.h"
#include "history/TransformCommand.h"

#include <variant>
#include <vector>

class Scene;

/// <summary>
/// 複数の編集操作を、1回のUndo・Redoとして扱う
/// </summary>
class CommandGroup {
  public:
	using Command =
	    std::variant<TransformCommand, ObjectCommand, RenameCommand, ModelPathCommand>;

	/// <summary>
	/// 実行順に並んだ操作を保持する。シーンへの反映は行わない
	/// </summary>
	/// <param name="commands">実行順の操作一覧</param>
	explicit CommandGroup(std::vector<Command> commands) noexcept;

	/// <summary>
	/// 全操作を逆順で元に戻す
	/// </summary>
	/// <param name="scene">編集対象のシーン</param>
	/// <returns>true:全操作成功 false:シーンと履歴が一致しない</returns>
	bool Undo(Scene &scene) const;

	/// <summary>
	/// 全操作を順方向に再実行する
	/// </summary>
	/// <param name="scene">編集対象のシーン</param>
	/// <returns>true:全操作成功 false:シーンと履歴が一致しない</returns>
	bool Redo(Scene &scene) const;

  private:
	std::vector<Command> commands_;
};
