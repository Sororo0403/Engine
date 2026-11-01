#pragma once

#include <filesystem>

/// <summary>
/// モデルのパスを、パネル間のドラッグ操作で受け渡す
/// </summary>
class ModelAssetDragDrop {
  public:
	/// <summary>
	/// 直前のUI項目をモデルのドラッグ元として扱う
	/// </summary>
	/// <param name="path">ドラッグするモデルのパス</param>
	/// <returns>true:ドラッグ中 false:操作なし</returns>
	static bool DrawSource(const std::filesystem::path &path);

	/// <summary>
	/// 直前のUI項目へ確定したモデルのドロップを取得する
	/// </summary>
	/// <returns>受け取ったモデルのパス。未確定、対象外、データ不正の場合は空</returns>
	static std::filesystem::path AcceptTarget();

  private:
	static constexpr const char *kPayloadType = "EDITOR_MODEL_PATH";
};
