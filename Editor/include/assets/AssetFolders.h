#pragma once

#include <filesystem>
#include <string>

namespace AssetFolders {
/// <summary>
/// 指定フォルダ内で空いている名前を探し、必要なら拡張子の前に連番を付ける
/// </summary>
std::filesystem::path SuggestAvailableName(
    const std::filesystem::path &directory, std::wstring stem,
    const std::wstring &extension);

/// <summary>
/// 既存の項目と重ならない名前を提案する。ファイルやフォルダは作成しない
/// </summary>
std::string SuggestImportName(const std::filesystem::path &assetRoot,
                              const std::filesystem::path &folder,
                              const std::string &name, bool keepExtension);

/// <summary>
/// 検索先の指定方法に合わせ、検証済みの絶対パスをSceneで使う参照へ変換する
/// </summary>
/// <param name="assetRoot">検索先。絶対指定なら参照も絶対パスにする</param>
/// <param name="absolutePath">呼び出し側で作成先・検索範囲を確認した絶対パス。存在はまだ不要</param>
/// <returns>絶対参照、または実行ファイル基準の相対参照。他ドライブへは絶対参照を使う。無効な指定は例外</returns>
std::filesystem::path MakeReference(const std::filesystem::path &assetRoot,
                                    const std::filesystem::path &absolutePath);

/// <summary>
/// Assets内の既存フォルダを解決し、親参照・ストリーム・検索先の外へ出る別名を拒否する
/// </summary>
/// <param name="assetRoot">Assetsの検索先。相対指定は実行ファイル基準</param>
/// <param name="folder">検索先を基準にした相対フォルダ。空は検索先自身</param>
/// <returns>既存フォルダの正規パス。参照や種類が無効な場合は例外</returns>
std::filesystem::path Resolve(const std::filesystem::path &assetRoot,
                              const std::filesystem::path &folder);

/// <summary>
/// 指定した既存フォルダの下に、単独の新規フォルダを作成する。既存ファイルやフォルダは変更しない
/// </summary>
/// <param name="assetRoot">Assetsの検索先</param>
/// <param name="parent">検索先基準の親フォルダ。空は検索先自身</param>
/// <param name="name">区切りや予約名を含まないフォルダ名</param>
/// <param name="created">成功時だけ置き換える、検索先基準の新規フォルダ</param>
/// <param name="error">失敗理由。成功時は空</param>
/// <returns>true:新規作成成功 false:既存・無効な名前や参照・作成失敗。中間フォルダは作成しない</returns>
bool Create(const std::filesystem::path &assetRoot,
            const std::filesystem::path &parent,
            const std::filesystem::path &name, std::filesystem::path &created,
            std::string &error);
} // namespace AssetFolders
