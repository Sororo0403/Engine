#pragma once

#include <filesystem>
#include <string>
#include <vector>

/// <summary>
/// Editorが使用するモデル・画像・音声・シーン・Prefab・ゲーム設定アセットとフォルダの一覧を管理する
/// </summary>
class AssetDatabase {
  public:
	/// <summary>
	/// アセットを検索するフォルダを指定する。ファイルの検索はまだ行わない
	/// </summary>
	/// <param name="rootDirectory">絶対パス、または実行ファイルのフォルダを基準にした相対パス</param>
	/// <param name="sceneDirectory">プロジェクトのシーンフォルダ。空ならAssets内を使う</param>
	explicit AssetDatabase(std::filesystem::path rootDirectory,
	                       std::filesystem::path sceneDirectory = {});

	AssetDatabase(const AssetDatabase &) = delete;
	AssetDatabase &operator=(const AssetDatabase &) = delete;
	AssetDatabase(AssetDatabase &&) noexcept = default;
	AssetDatabase &operator=(AssetDatabase &&) noexcept = default;

	/// <summary>
	/// 配下のフォルダを含めてアセットの一覧を更新する
	/// </summary>
	/// <returns>true:成功 false:検索失敗。失敗時は最後に取得した一覧を保持する</returns>
	bool Refresh();

	/// <summary>
	/// 新しい検索先の読み込み成功後だけ、検索先と一覧を切り替える
	/// </summary>
	/// <param name="rootDirectory">絶対パス、または実行ファイル基準の相対パス</param>
	/// <returns>true:切り替え成功 false:検索先と一覧を保持し、失敗理由を設定する</returns>
	bool SetRootDirectory(std::filesystem::path rootDirectory);

	/// <summary>
	/// 検索対象のフォルダを取得する
	/// </summary>
	/// <returns>指定した相対パス、または検索成功後に正規化した絶対パス</returns>
	const std::filesystem::path &GetRootDirectory() const;

	/// <summary>
	/// モデルファイルの一覧を取得する
	/// </summary>
	/// <returns>検索先が絶対指定なら絶対パス、それ以外は実行ファイル基準の相対パスの一覧</returns>
	const std::vector<std::filesystem::path> &GetModels() const;

	/// <summary>
	/// 材質で選択する画像ファイルの一覧を取得する
	/// </summary>
	/// <returns>検索先と同じ絶対・相対指定のPNG・JPEG・BMP・TIFF・GIFファイル一覧</returns>
	const std::vector<std::filesystem::path> &GetTextures() const;

	/// <summary>
	/// 音源で選択するWAVファイルの候補一覧を取得する。内容は割り当て時と再生時に検証する
	/// </summary>
	/// <returns>検索先と同じ絶対・相対指定のWAV一覧</returns>
	const std::vector<std::filesystem::path> &GetAudioClips() const;

	/// <summary>
	/// 標準のシーンファイル名を持つ項目を取得する。内容の検証は開く時に行う
	/// </summary>
	/// <returns>検索先と同じ絶対・相対指定の.scene.jsonファイル一覧</returns>
	const std::vector<std::filesystem::path> &GetScenes() const;

	/// <summary>
	/// TriggerやLuaの遷移先として選択できるシーン一覧を取得する
	/// </summary>
	const std::vector<std::filesystem::path> &GetSceneTargets() const;

	/// <summary>
	/// Prefab専用の標準名を持つ候補を取得する。内容は配置時に検証する
	/// </summary>
	/// <returns>検索先と同じ絶対・相対指定の.prefab.json一覧</returns>
	const std::vector<std::filesystem::path> &GetPrefabs() const;

	/// <summary>
	/// 標準名を持つゲーム設定ファイルを取得する。内容は読み込み時に検証する
	/// </summary>
	/// <returns>検索先と同じ絶対・相対指定の*.settings.json一覧</returns>
	const std::vector<std::filesystem::path> &GetGameConfigurations() const;

	/// <summary>
	/// 空のフォルダを含む、検索先配下のフォルダ一覧を取得する
	/// </summary>
	/// <returns>検索先と同じ絶対・相対指定の一覧。検索先自身は含まない</returns>
	const std::vector<std::filesystem::path> &GetDirectories() const;

	/// <summary>
	/// 最後の検索で発生したエラーを取得する
	/// </summary>
	/// <returns>エラーメッセージ。成功時は空文字列</returns>
	const std::string &GetError() const;

	/// <summary>
	/// 一覧とエラーをクリアする。検索対象のフォルダは保持する
	/// </summary>
	void Clear();

	/// <summary>
	/// スクリプトとして割り当てるLuaファイルの一覧を取得する
	/// </summary>
	/// <returns>検索先と同じ絶対・相対指定の.lua一覧。構文は保存・再生時に検証する</returns>
	const std::vector<std::filesystem::path> &GetScripts() const;
	const std::vector<std::filesystem::path> &GetAnimationClips() const;

  private:
	std::filesystem::path rootDirectory_;
	std::filesystem::path sceneDirectory_;
	std::vector<std::filesystem::path> models_;
	std::vector<std::filesystem::path> textures_;
	std::vector<std::filesystem::path> audioClips_;
	std::vector<std::filesystem::path> scenes_;
	std::vector<std::filesystem::path> projectScenes_;
	std::vector<std::filesystem::path> scripts_, animationClips_;
	std::vector<std::filesystem::path> prefabs_;
	std::vector<std::filesystem::path> gameConfigurations_;
	std::vector<std::filesystem::path> directories_;
	std::string error_;
};
