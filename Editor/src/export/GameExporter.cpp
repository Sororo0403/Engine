#include "export/GameExporter.h"
#include "assets/AssetDependencies.h"
#include "gameplay/SceneSimulation.h"
#include "platform/windows/PathUtils.h"
#include "scene/SceneSerializer.h"
#include <Windows.h>
#include <algorithm>
#include <array>
#include <fstream>
#include <json.hpp>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {
namespace fs = std::filesystem;

/// <summary>
/// Windowsのパス文字列を大文字・小文字を区別せず順序付けする
/// </summary>
struct PathLess {
	bool operator()(const fs::path &a, const fs::path &b) const {
		return CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE) ==
		       CSTR_LESS_THAN;
	}
};

/// <summary>
/// 正規化したパスが基準フォルダ内なら、表記の大小に依存しない相対パスを返す
/// </summary>
/// <param name="root">正規化済みの基準フォルダ</param>
/// <param name="path">正規化済みの対象</param>
/// <returns>基準からの相対パス。外部の場合は例外</returns>
fs::path RelativeInside(const fs::path &root, const fs::path &path) {
	auto a = root.begin(), b = path.begin();

	for (; a != root.end(); ++a, ++b) {
		if (b == path.end() || CompareStringOrdinal(a->c_str(), -1, b->c_str(),
		                                            -1, TRUE) != CSTR_EQUAL) {
			throw std::runtime_error(
			    "A referenced file is outside resources: " +
			    PathUtils::ToUtf8(path));
		}
	}

	fs::path relative;

	for (; b != path.end(); ++b) {
		relative /= *b;
	}

	return relative;
}

/// <summary>
/// リソース参照の別名やリンクによる外部依存を拒否し、通常ファイルを確認する
/// </summary>
/// <param name="root">リソースの正規化済みルート</param>
/// <param name="input">検証する絶対パス</param>
/// <returns>正規化した通常ファイル</returns>
fs::path ResourceFile(const fs::path &root, const fs::path &input) {
	if (input.native().find(L'\0') != std::wstring::npos) {
		throw std::runtime_error(
		    "Resource paths cannot contain null characters.");
	}

	for (auto part = input; part.has_filename(); part = part.parent_path()) {
		const auto attributes = GetFileAttributesW(part.c_str());

		if (attributes != INVALID_FILE_ATTRIBUTES &&
		    (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
			throw std::runtime_error("Copy linked resources into the resources "
			                         "folder before exporting.");
		}
	}

	const auto path = fs::canonical(input);
	const auto relative = RelativeInside(root, path);

	if (relative.empty() || !fs::is_regular_file(path)) {
		throw std::runtime_error("A resource is not a regular file.");
	}

	for (const auto &part : relative) {
		if (part.native().find(L':') != std::wstring::npos) {
			throw std::runtime_error("Resource paths cannot contain streams.");
		}
	}

	return path;
}

/// <summary>
/// 参照するモデル・バッファ・画像・材質ファイルを収集する
/// </summary>
class Assets {
  public:
	explicit Assets(fs::path root) : root_(std::move(root)) {
	}

	fs::path AddModel(const fs::path &input) {
		const auto model = AddFile(input);

		if (!models_.insert(model).second) {
			return model;
		}

		for (const auto &file : AssetDependencies::ModelFiles(model)) {
			AddFile(file);
		}

		return model;
	}

	fs::path AddMaterial(const fs::path &input) {
		const auto material = AddFile(input);

		for (const auto &file : AssetDependencies::MaterialFiles(material)) {
			AddFile(file);
		}

		return material;
	}

	const auto &Files() const {
		return files_;
	}

  private:
	fs::path AddFile(const fs::path &path) {
		const auto file = ResourceFile(root_, path);
		files_.insert(file);
		return file;
	}

	fs::path root_;
	std::set<fs::path, PathLess> files_, models_;
};

/// <summary>
/// 作成したファイルを識別し、別のファイルに置き換わっていないか確認する
/// </summary>
struct FileIdentity {
	DWORD volume = 0;
	DWORD high = 0;
	DWORD low = 0;

	bool Matches(const BY_HANDLE_FILE_INFORMATION &info) const {
		return volume == info.dwVolumeSerialNumber &&
		       high == info.nFileIndexHigh && low == info.nFileIndexLow;
	}
};

/// <summary>
/// ファイル操作に使用するWindowsハンドルを保持する
/// </summary>
class FileHandle {
  public:
	explicit FileHandle(HANDLE value) : value_(value) {
	}

	~FileHandle() {
		if (value_ != INVALID_HANDLE_VALUE) {
			CloseHandle(value_);
		}
	}

	FileHandle(const FileHandle &) = delete;
	FileHandle &operator=(const FileHandle &) = delete;

	HANDLE Get() const {
		return value_;
	}

	HANDLE Release() {
		return std::exchange(value_, INVALID_HANDLE_VALUE);
	}

  private:
	HANDLE value_;
};

/// <summary>
/// 操作対象とWindowsの失敗理由を通知する
/// </summary>
/// <param name="operation">失敗した操作</param>
/// <param name="path">対象の絶対パス</param>
/// <param name="code">失敗直後に取得したWindowsエラー番号</param>
[[noreturn]] void FileError(const char *operation, const fs::path &path,
                            DWORD code) {
	throw std::runtime_error(std::string(operation) + ": " +
	                         PathUtils::ToUtf8(path) + " (Windows error " +
	                         std::to_string(code) + ").");
}

/// <summary>
/// 新規出力へ直接書き込み、失敗時は今回作成したものだけを片付ける
/// </summary>
class OutputDirectory {
  public:
	void Create(const fs::path &path) {
		root_ = path;
		EnsureDirectory(path.parent_path());
		CreateOwnedDirectory(path, true);
	}

	void Copy(const fs::path &source, const fs::path &relative) {
		FileHandle input(CreateFileW(
		    source.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
		    nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));

		if (input.Get() == INVALID_HANDLE_VALUE) {
			FileError("Could not read resource", source, GetLastError());
		}

		BY_HANDLE_FILE_INFORMATION info{};

		if (!GetFileInformationByHandle(input.Get(), &info)) {
			FileError("Could not inspect resource", source, GetLastError());
		}

		if (info.dwFileAttributes &
		    (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) {
			throw std::runtime_error(
			    "A resource is no longer a regular file: " +
			    PathUtils::ToUtf8(source));
		}

		FileHandle output(OpenNewFile(relative));
		std::array<char, 65536> buffer{};

		for (;;) {
			DWORD count = 0;

			if (!ReadFile(input.Get(), buffer.data(), DWORD(buffer.size()),
			              &count, nullptr)) {
				FileError("Could not read resource", source, GetLastError());
			}

			if (count == 0) {
				break;
			}

			Write(output.Get(), buffer.data(), count, root_ / relative);
		}
	}

	void Text(const fs::path &relative, const std::string &text) {
		FileHandle output(OpenNewFile(relative));
		Write(output.Get(), text.data(), text.size(), root_ / relative);
	}

	void Complete() {
		entries_.clear();
	}

	std::string Cleanup() {
		std::size_t remaining = 0;
		std::string first;

		for (auto it = entries_.rbegin(); it != entries_.rend(); ++it) {
			try {
				FileHandle handle(CreateFileW(
				    it->path.c_str(), DELETE | FILE_READ_ATTRIBUTES,
				    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
				    nullptr, OPEN_EXISTING,
				    FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
				    nullptr));

				if (handle.Get() == INVALID_HANDLE_VALUE) {
					const DWORD code = GetLastError();

					if (code == ERROR_FILE_NOT_FOUND ||
					    code == ERROR_PATH_NOT_FOUND) {
						continue;
					}

					FileError("Could not remove incomplete output", it->path,
					          code);
				}

				BY_HANDLE_FILE_INFORMATION info{};

				if (!GetFileInformationByHandle(handle.Get(), &info)) {
					FileError("Could not inspect incomplete output", it->path,
					          GetLastError());
				}

				if (!it->identified || !it->identity.Matches(info)) {
					throw std::runtime_error(
					    "An output item changed and was left untouched: " +
					    PathUtils::ToUtf8(it->path));
				}

				FILE_DISPOSITION_INFO disposition{TRUE};

				if (!SetFileInformationByHandle(
				        handle.Get(), FileDispositionInfo, &disposition,
				        sizeof(disposition))) {
					FileError("Could not remove incomplete output", it->path,
					          GetLastError());
				}
			} catch (const std::exception &e) {
				++remaining;

				if (first.empty()) {
					first = e.what();
				}
			}
		}

		entries_.clear();

		if (remaining == 0) {
			return {};
		}

		return " Cleanup left " + std::to_string(remaining) +
		       " item(s). Output: " + PathUtils::ToUtf8(root_) + ". " + first;
	}

  private:
	struct Entry {
		fs::path path;
		FileIdentity identity{};
		bool identified = false;
	};

	fs::path root_;
	std::vector<Entry> entries_;

	void Identify(HANDLE handle, Entry &entry) {
		BY_HANDLE_FILE_INFORMATION info{};

		if (!GetFileInformationByHandle(handle, &info)) {
			FileError("Could not identify new output", entry.path,
			          GetLastError());
		}

		entry.identity = {info.dwVolumeSerialNumber, info.nFileIndexHigh,
		                  info.nFileIndexLow};
		entry.identified = true;
	}

	void CreateOwnedDirectory(const fs::path &path, bool exclusive) {
		entries_.push_back({path});

		if (!CreateDirectoryW(path.c_str(), nullptr)) {
			const DWORD code = GetLastError();
			entries_.pop_back();

			if (!exclusive && code == ERROR_ALREADY_EXISTS &&
			    fs::is_directory(path)) {
				return;
			}

			FileError("Could not create a new output folder; existing folders "
			          "are not overwritten",
			          path, code);
		}

		FileHandle handle(CreateFileW(
		    path.c_str(), FILE_READ_ATTRIBUTES,
		    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
		    OPEN_EXISTING,
		    FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
		    nullptr));

		if (handle.Get() == INVALID_HANDLE_VALUE) {
			FileError("Could not inspect new output folder", path,
			          GetLastError());
		}

		Identify(handle.Get(), entries_.back());
	}

	void EnsureDirectory(const fs::path &path) {
		if (path.empty() || fs::is_directory(path)) {
			return;
		}

		EnsureDirectory(path.parent_path());
		CreateOwnedDirectory(path, false);
	}

	HANDLE OpenNewFile(const fs::path &relative) {
		const auto path = root_ / relative;
		EnsureDirectory(path.parent_path());
		entries_.push_back({path});
		FileHandle handle(CreateFileW(
		    path.c_str(), GENERIC_WRITE | FILE_READ_ATTRIBUTES, FILE_SHARE_READ,
		    nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));

		if (handle.Get() == INVALID_HANDLE_VALUE) {
			const DWORD code = GetLastError();
			entries_.pop_back();
			FileError("Could not create a new output file", path, code);
		}

		Identify(handle.Get(), entries_.back());
		// 書き込み中はこのハンドルだけを所有し、呼び出し側で必ず閉じる。
		return handle.Release();
	}

	static void Write(HANDLE handle, const char *data, std::size_t size,
	                  const fs::path &path) {
		while (size != 0) {
			const DWORD chunk = DWORD((std::min)(size, std::size_t(65536)));
			DWORD written = 0;

			if (!WriteFile(handle, data, chunk, &written, nullptr)) {
				FileError("Could not write output", path, GetLastError());
			}

			if (written == 0) {
				FileError("Could not write output", path, ERROR_WRITE_FAULT);
			}

			data += written;
			size -= written;
		}
	}
};

/// <summary>
/// 出力を作成する前に必要なファイルと相対パスの衝突を確認する
/// </summary>
class CopyPlan {
  public:
	explicit CopyPlan(fs::path destination)
	    : destination_(std::move(destination)) {
	}

	void Reserve(fs::path relative, const fs::path &source = {}) {
		relative = relative.lexically_normal().make_preferred();

		const auto found = files_.find(relative);

		if (found != files_.end()) {
			if (!source.empty() && !found->second.empty() &&
			    !PathLess{}(source, found->second) &&
			    !PathLess{}(found->second, source)) {
				return;
			}

			throw std::runtime_error("Output paths conflict: " +
			                         PathUtils::ToUtf8(relative));
		}

		if (directories_.contains(relative)) {
			throw std::runtime_error(
			    "An output file conflicts with a folder: " +
			    PathUtils::ToUtf8(relative));
		}

		for (auto parent = relative.parent_path(); !parent.empty();
		     parent = parent.parent_path()) {
			if (files_.contains(parent)) {
				throw std::runtime_error(
				    "An output folder conflicts with a file: " +
				    PathUtils::ToUtf8(parent));
			}

			directories_.insert(parent);
		}

		files_.emplace(relative, source);
	}

	void File(const fs::path &source, const fs::path &relative) {
		if (!fs::is_regular_file(source) ||
		    (GetFileAttributesW(source.c_str()) &
		     FILE_ATTRIBUTE_REPARSE_POINT)) {
			throw std::runtime_error(
			    "A required runtime file is missing or linked: " +
			    PathUtils::ToUtf8(source));
		}

		Reserve(relative, source);
	}

	void Tree(const fs::path &source, const fs::path &relative) {
		if (!fs::is_directory(source) || (GetFileAttributesW(source.c_str()) &
		                                  FILE_ATTRIBUTE_REPARSE_POINT)) {
			throw std::runtime_error(
			    "Required runtime resources are missing: " +
			    PathUtils::ToUtf8(source));
		}

		bool inside = false;
		try {
			RelativeInside(fs::canonical(source), destination_);
			inside = true;
		} catch (const std::runtime_error &) {
		}

		if (inside) {
			throw std::runtime_error(
			    "Output must be outside runtime resource and license folders.");
		}

		for (const auto &entry : fs::recursive_directory_iterator(source)) {
			if (GetFileAttributesW(entry.path().c_str()) &
			    FILE_ATTRIBUTE_REPARSE_POINT) {
				throw std::runtime_error(
				    "Runtime resources cannot contain links.");
			}

			if (entry.is_regular_file()) {
				File(entry.path(),
				     relative / entry.path().lexically_relative(source));
			}
		}
	}

	const auto &Files() const {
		return files_;
	}

  private:
	fs::path destination_;
	std::map<fs::path, fs::path, PathLess> files_;
	std::set<fs::path, PathLess> directories_;
};
} // namespace

bool GameExporter::Export(const Scene &scene, const GameExportOptions &options,
                          std::string &error) {
	OutputDirectory output;
	try {
		error.clear();
		RelativeInside(fs::path(L"resources"),
		               options.config.scenePath.lexically_normal());

		if (!options.config.IsValid()) {
			throw std::runtime_error(
			    "Game title, size or fallback camera is invalid.");
		}

		if (options.destination.empty() ||
		    options.destination.native().find(L'\0') != std::wstring::npos) {
			throw std::runtime_error("Choose a new output folder.");
		}

		const auto destination =
		    fs::weakly_canonical(fs::absolute(options.destination));

		if (fs::exists(destination)) {
			throw std::runtime_error(
			    "Output folder already exists. Choose a new folder.");
		}

		const auto resourceRoot = fs::canonical(options.resourceDirectory);
		const auto runtimeRoot = fs::canonical(options.runtimeDirectory);
		// 出力先が入力リソースの中に入らないことを確認する。
		bool inside = false;

		try {
			RelativeInside(resourceRoot, destination);
			inside = true;
		} catch (const std::runtime_error &) {
		}

		if (inside) {
			throw std::runtime_error(
			    "Output must be outside the resources folder.");
		}

		nlohmann::json build;
		{
			std::ifstream file(runtimeRoot / L"Runtime.build.json");

			if (!file) {
				throw std::runtime_error(
				    "Build the Release configuration before exporting.");
			}

			file >> build;
		}

		if (build.at("configuration") != "Release" ||
		    build.at("architecture") != "x64") {
			throw std::runtime_error(
			    "A Release x64 Runtime build is required.");
		}

		SceneSimulation check;
		std::string problem;

		if (!check.Start(scene, problem, options.config.fallbackCamera)) {
			throw std::runtime_error(problem);
		}

		Assets assets(resourceRoot);
		auto objects = scene.GetObjects();

		for (auto &[id, object] : objects) {
			if (object.meshRenderer) {
				for (auto &[slot, path] :
				     object.meshRenderer->materials.slots) {
					const auto input =
					    path.is_absolute()
					        ? path
					        : resourceRoot /
					              RelativeInside(fs::path(L"resources"),
					                             path.lexically_normal());
					path =
					    fs::path(L"resources") /
					    RelativeInside(resourceRoot, assets.AddMaterial(input));
				}
			}

			if (object.HasMesh()) {
				const auto path =
				    object.mesh->modelPath.is_absolute()
				        ? object.mesh->modelPath
				        : resourceRoot /
				              RelativeInside(
				                  fs::path(L"resources"),
				                  object.mesh->modelPath.lexically_normal());
				const auto model = assets.AddModel(path);
				object.mesh->modelPath = fs::path(L"resources") /
				                         RelativeInside(resourceRoot, model);
			}
		}

		Scene snapshot;
		snapshot.ReplaceObjects(std::move(objects));
		CopyPlan plan(destination);
		plan.Reserve(options.config.scenePath);
		plan.Reserve(L"game.json");
		plan.File(runtimeRoot / L"Runtime.exe", L"Game.exe");

		for (const auto *name :
		     {L"dxcompiler.dll", L"dxil.dll", L"GameInputRedist.dll",
		      L"GameInputRawInputProxy.exe"}) {
			plan.File(runtimeRoot / name, name);
		}

		for (const auto *name :
		     {L"msvcp140.dll", L"vcruntime140.dll", L"vcruntime140_1.dll"}) {
			if (!fs::is_regular_file(runtimeRoot / name)) {
				throw std::runtime_error("Runtime C++ libraries are missing. "
				                         "Rebuild Release Runtime.");
			}
		}

		for (const auto &entry : fs::directory_iterator(runtimeRoot)) {
			const auto name = entry.path().filename().native();

			if (PathUtils::HasExtension(entry.path(), ".dll") &&
			    (name.starts_with(L"msvcp140") ||
			     name.starts_with(L"vcruntime140") ||
			     name == L"concrt140.dll" || name == L"vccorlib140.dll")) {
				plan.File(entry.path(), entry.path().filename());
			}
		}

		for (const auto *shader :
		     {L"shaders/mesh/Mesh.VS.hlsl", L"shaders/mesh/Mesh.PS.hlsl",
		      L"shaders/debug/DebugLine.VS.hlsl",
		      L"shaders/debug/DebugLine.PS.hlsl"}) {
			if (!fs::is_regular_file(runtimeRoot / L"resources/engine" /
			                         shader)) {
				throw std::runtime_error(
				    "Runtime shaders are missing. Rebuild Engine Release.");
			}
		}

		plan.Tree(runtimeRoot / L"resources" / L"engine", L"resources/engine");
		plan.Tree(runtimeRoot / L"licenses", L"licenses");

		for (const auto &file : assets.Files()) {
			plan.File(file, fs::path(L"resources") /
			                    RelativeInside(resourceRoot, file));
		}

		const auto assetLicenses = resourceRoot / L"licenses";

		if (fs::is_directory(assetLicenses)) {
			plan.Tree(assetLicenses, L"licenses/assets");
		}

		std::string sceneText, configText;

		if (!SceneSerializer::Serialize(snapshot, sceneText)) {
			throw std::runtime_error("Could not serialize the exported scene.");
		}

		if (!options.config.Serialize(configText, problem)) {
			throw std::runtime_error(problem);
		}

		output.Create(destination);

		for (const auto &[relative, source] : plan.Files()) {
			if (!source.empty() && relative != L"Game.exe") {
				output.Copy(source, relative);
			}
		}

		Assets exportedAssets(fs::canonical(destination / L"resources"));

		for (const auto &[id, object] : snapshot.GetObjects()) {
			if (object.meshRenderer) {
				for (const auto &[slot, path] :
				     object.meshRenderer->materials.slots) {
					exportedAssets.AddMaterial(destination / path);
				}
			}

			if (object.HasMesh()) {
				exportedAssets.AddModel(destination / object.mesh->modelPath);
			}
		}

		output.Text(options.config.scenePath, sceneText);
		output.Text(L"game.json", configText);
		output.Copy(runtimeRoot / L"Runtime.exe", L"Game.exe");
		output.Complete();
		return true;
	} catch (const std::exception &e) {
		error = e.what();
		error += output.Cleanup();
		return false;
	} catch (...) {
		error = "An unknown error interrupted game export.";
		error += output.Cleanup();
		return false;
	}
}
