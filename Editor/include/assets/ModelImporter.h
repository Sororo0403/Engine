#pragma once

#include <filesystem>
#include <string>
class ModelManager;

/// <summary>
/// モデルと依存ファイルの基準フォルダ、Assets内の作成先を指定する
/// </summary>
struct ModelImportOptions {
	std::filesystem::path source;
	std::filesystem::path sourceRoot;
	std::filesystem::path assetRoot;
	std::filesystem::path folder;
	std::filesystem::path folderName;
};

/// <summary>
/// モデルと依存ファイルを新規フォルダへ取り込み、元データや既存アセットは変更しない
/// </summary>
class ModelImporter {
  public:
	/// <summary>
	/// 参照構成を保持してコピーし、取り込み先の参照とGPU読み込みを確認して確定する
	/// </summary>
	/// <param name="options">入力モデル・依存元の基準・Assets内の新規フォルダ名</param>
	/// <param name="models">初期化済みのモデル管理。成功した取り込み先を読み込み済みにする</param>
	/// <param name="imported">成功時だけ置き換える、実行ファイル基準のモデルパス</param>
	/// <param name="error">失敗理由。後片付けできない場合は残った場所も含む</param>
	/// <returns>true:取り込み成功 false:参照・モデル・作成先が無効、またはコピー・読み込み失敗</returns>
	/// <remarks>GLB・glTF・OBJに対応。GPU利用完了後、描画命令の記録前に呼び出す。外部参照はsourceRoot内にある相対参照を使用する</remarks>
	static bool Import(const ModelImportOptions &options, ModelManager &models,
	                   std::filesystem::path &imported, std::string &error);
};
