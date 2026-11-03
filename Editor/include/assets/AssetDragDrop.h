#pragma once

#include <filesystem>

/// <summary>
/// アセットの種類とパスを、パネル間のドラッグ操作で受け渡す
/// </summary>
class AssetDragDrop {
  public:
	/// <summary>
	/// 誤った対象へ受け渡さないためのアセット種別
	/// </summary>
	enum class Kind { MODEL, SCENE };

	/// <summary>
	/// 直前のUI項目を指定種別のアセットのドラッグ元として扱う
	/// </summary>
	/// <param name="path">一覧で選択したアセットのパス</param>
	/// <param name="kind">モデル、または.scene.json形式のシーン</param>
	/// <returns>true:ドラッグ中 false:操作なし、またはパスが対象外</returns>
	static bool DrawSource(const std::filesystem::path &path, Kind kind);

	/// <summary>
	/// 直前のUI項目へ確定した、指定種別のアセットのドロップを取得する
	/// </summary>
	/// <param name="kind">受け入れるアセット種別。他の種別は受け取らない</param>
	/// <returns>受け取ったパス。未確定・対象外・データ不正の場合は空</returns>
	static std::filesystem::path AcceptTarget(Kind kind);
};
