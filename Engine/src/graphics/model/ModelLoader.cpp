#include "graphics/model/ModelLoader.h"
#include "GltfAnimationMetadata.h"
#include "ModelAnimationLoader.h"
#include "core/Logger.h"
#include "platform/windows/PathUtils.h"
#include <memory>

#include <assimp/DefaultIOSystem.h>
#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <assimp/texture.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace {
/// <summary>
/// glTFの外部ファイルURIだけを復号し、実ファイルの読み書きはAssimpの標準I/Oへ委譲する
/// </summary>
class GltfIOSystem final : public Assimp::IOSystem {
  public:
	explicit GltfIOSystem(const std::filesystem::path &model)
	    : model_(PathUtils::ToUtf8(model)), directory_(model.parent_path()),
	      prefix_(PathUtils::ToUtf8(directory_)) {
		if (!prefix_.ends_with('/')) {
			prefix_ += '/';
		}
	}

	bool Exists(const char *file) const override {
		try {
			return files_.Exists(Resolve(file).c_str());
		} catch (const std::invalid_argument &) {
			return false;
		}
	}

	char getOsSeparator() const override {
		return files_.getOsSeparator();
	}

	Assimp::IOStream *Open(const char *file, const char *mode = "rb") override {
		try {
			return files_.Open(Resolve(file).c_str(), mode);
		} catch (const std::invalid_argument &) {
			return nullptr;
		}
	}

	void Close(Assimp::IOStream *file) override {
		files_.Close(file);
	}

	bool ComparePaths(const char *a, const char *b) const override {
		try {
			return files_.ComparePaths(Resolve(a).c_str(), Resolve(b).c_str());
		} catch (const std::invalid_argument &) {
			return false;
		}
	}

  private:
	/// <summary>
	/// モデル本体の実パスを保ち、モデルフォルダに続く外部URI部分だけを復号する
	/// </summary>
	/// <param name="file">Assimpが要求したUTF-8パス</param>
	/// <returns>標準I/Oへ渡すUTF-8の実パス。無効な相対URIは例外</returns>
	std::string Resolve(const char *file) const {
		std::string path = file ? file : "";
		std::replace(path.begin(), path.end(), '\\', '/');

		// フォルダ名やモデル名にある%は、実パスの文字としてそのまま使用する。
		if (path == model_) {
			return path;
		}

		if (path.starts_with(prefix_)) {
			return PathUtils::ToUtf8(
			    directory_ /
			    PathUtils::DecodeRelativeUri(
			        std::string_view{path}.substr(prefix_.size())));
		}

		if (!PathUtils::FromUtf8(path).has_root_path()) {
			return PathUtils::ToUtf8(directory_ /
			                         PathUtils::DecodeRelativeUri(path));
		}

		return path;
	}

	Assimp::DefaultIOSystem files_;
	std::string model_;
	std::filesystem::path directory_;
	std::string prefix_;
};
} // namespace

bool ModelLoader::Load(const std::filesystem::path &filePath,
                       ModelData &modelData) {
	if (filePath.empty()) {
		Logger::Error("ModelLoader::Load: パスが空です。");
		return false;
	}

	// Assimpへ渡すパスをUTF-8にする
	const auto absolutePath =
	    std::filesystem::absolute(filePath).lexically_normal();
	const std::string path = PathUtils::ToUtf8(absolutePath);

	const bool gltf = PathUtils::HasExtension(filePath, ".gltf") ||
	                  PathUtils::HasExtension(filePath, ".glb");
	GltfAnimationMetadata metadata;
	std::string metadataError;

	if (gltf && !metadata.Read(absolutePath, metadataError)) {
		Logger::Error(metadataError);
		return false;
	}

	Assimp::Importer importer;

	if (PathUtils::HasExtension(filePath, ".gltf") ||
	    PathUtils::HasExtension(filePath, ".glb")) {
		// ImporterがI/Oハンドラーを所有し、外部バッファのURIを読み込む際にも使用する。
		importer.SetIOHandler(new GltfIOSystem(absolutePath));
	}

	const unsigned int flags = aiProcess_Triangulate | aiProcess_SortByPType |
	                           aiProcess_JoinIdenticalVertices |
	                           aiProcess_GenNormals |
	                           aiProcess_ConvertToLeftHanded;

	const aiScene *scene = importer.ReadFile(path, flags);

	if (!scene || !scene->mRootNode || !scene->HasMeshes() ||
	    (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) != 0) {
		Logger::Error(
		    std::format("モデルの読み込みに失敗しました。ファイル={}、詳細={}",
		                path, importer.GetErrorString()));
		return false;
	}

	// 全メッシュの読み込みに成功してから呼び出し元へ反映する
	ModelData result;

	// 階層を焼き込む前に、Importerから独立した再生用データを保持する。
	const bool animated =
	    scene->HasAnimations() ||
	    std::any_of(
	        scene->mMeshes, scene->mMeshes + scene->mNumMeshes,
	        [](const aiMesh *mesh) { return mesh && mesh->HasBones(); });
	if (animated) {
		auto animation = std::make_shared<ModelAnimationData>();
		std::string error;

		if (!ModelAnimationLoader::Read(*scene, *animation, error,
		                                gltf ? &metadata : nullptr)) {
			Logger::Error(std::format(
			    "モデルのアニメーションを読み込めません。ファイル={}、詳細={}",
			    path, error));
			return false;
		}

		result.animation = std::move(animation);
	}

	// 既存の静止描画・選択判定は、従来と同じ事前変換済みメッシュを使用する。
	scene = importer.ApplyPostProcessing(aiProcess_PreTransformVertices);

	if (!scene || !scene->HasMeshes()) {
		Logger::Error("モデルの静止メッシュを準備できません。");
		return false;
	}

	// 材質番号が変わらないよう、元の順番で全材質を保存する
	for (unsigned int i = 0; i < scene->mNumMaterials; ++i) {
		const aiMaterial &source = *scene->mMaterials[i];

		aiColor4D color{1.0f, 1.0f, 1.0f, 1.0f};

		// Base Colorを優先し、なければDiffuse Colorを使用する
		if (source.Get(AI_MATKEY_BASE_COLOR, color) != AI_SUCCESS &&
		    source.Get(AI_MATKEY_COLOR_DIFFUSE, color) != AI_SUCCESS) {
			color = aiColor4D{1.0f, 1.0f, 1.0f, 1.0f};
		}

		ModelMaterialData material;
		aiString name;

		if (source.Get(AI_MATKEY_NAME, name) == AI_SUCCESS) {
			material.name = name.C_Str();
		}

		material.baseColor = {color.r, color.g, color.b, color.a};

		if (!LoadBaseColorTexture(*scene, source, filePath,
		                          material.baseColorTexture,
		                          material.baseColorTextureUsesAbsolutePath)) {
			return false;
		}

		result.materials.push_back(std::move(material));
	}

	for (unsigned int meshIndex = 0; meshIndex < scene->mNumMeshes;
	     ++meshIndex) {
		const aiMesh &source = *scene->mMeshes[meshIndex];

		// 点や線だけのメッシュは対象外
		if ((source.mPrimitiveTypes & aiPrimitiveType_TRIANGLE) == 0) {
			continue;
		}

		if (!source.HasPositions() || !source.HasNormals() ||
		    !source.HasFaces()) {
			Logger::Error("モデルの頂点座標・法線・面の情報が不足しています。");
			return false;
		}

		if (source.mMaterialIndex >= scene->mNumMaterials) {
			Logger::Error("モデルの材質番号が範囲外です。");
			return false;
		}

		ModelMeshData part;
		part.materialIndex = source.mMaterialIndex;

		MeshData &mesh = part.mesh;

		mesh.vertices.reserve(source.mNumVertices);
		mesh.indices.reserve(static_cast<std::size_t>(source.mNumFaces) * 3);

		// このメッシュの頂点を読み込む
		for (unsigned int i = 0; i < source.mNumVertices; ++i) {
			Vertex vertex{};

			const aiVector3D &position = source.mVertices[i];
			const aiVector3D &normal = source.mNormals[i];

			vertex.position = {position.x, position.y, position.z};

			vertex.normal = {normal.x, normal.y, normal.z};

			vertex.color = {1.0f, 1.0f, 1.0f, 1.0f};

			if (source.HasVertexColors(0)) {
				const aiColor4D &color = source.mColors[0][i];

				vertex.color = {color.r, color.g, color.b, color.a};
			}

			if (source.HasTextureCoords(0)) {
				const aiVector3D &texcoord = source.mTextureCoords[0][i];

				vertex.texcoord = {texcoord.x, texcoord.y};
			}

			mesh.vertices.push_back(vertex);
		}

		// 頂点番号は、このメッシュ内の番号のまま保存する
		for (unsigned int i = 0; i < source.mNumFaces; ++i) {
			const aiFace &face = source.mFaces[i];

			if (face.mNumIndices != 3) {
				Logger::Error("モデルに三角形以外の面が残っています。");
				return false;
			}

			for (unsigned int corner = 0; corner < 3; ++corner) {
				const unsigned int index = face.mIndices[corner];

				if (index >= source.mNumVertices) {
					Logger::Error("モデルの頂点番号が範囲外です。");
					return false;
				}

				mesh.indices.push_back(index);
			}
		}

		result.meshes.push_back(std::move(part));
	}

	if (result.meshes.empty()) {
		Logger::Error("モデルに描画できる三角形がありません。");
		return false;
	}

	Logger::Info(
	    std::format("モデルを読み込みました。ファイル={}、メッシュ数={}", path,
	                result.meshes.size()));

	modelData = std::move(result);
	return true;
}

bool ModelLoader::LoadBaseColorTexture(const aiScene &scene,
                                       const aiMaterial &material,
                                       const std::filesystem::path &modelPath,
                                       TextureData &textureData,
                                       bool &absoluteReference) {
	aiString texturePath;

	aiTextureMapping mapping = aiTextureMapping_UV;
	unsigned int uvIndex = 0;

	if (material.GetTexture(aiTextureType_BASE_COLOR, 0, &texturePath, &mapping,
	                        &uvIndex) != AI_SUCCESS &&
	    material.GetTexture(aiTextureType_DIFFUSE, 0, &texturePath, &mapping,
	                        &uvIndex) != AI_SUCCESS) {
		textureData = {};
		return true;
	}

	// 現在のVertexは、UVを1組だけ保持する
	if (mapping != aiTextureMapping_UV || uvIndex != 0) {
		Logger::Error("基本色テクスチャにはUV0を使用してください。");
		return false;
	}

	TextureData result;

	const aiTexture *embedded = scene.GetEmbeddedTexture(texturePath.C_Str());

	if (embedded) {
		if (!embedded->pcData || embedded->mWidth == 0) {
			Logger::Error("埋め込み画像のデータが無効です。");
			return false;
		}

		if (embedded->mHeight == 0) {
			// mHeightが0の場合、mWidthは画像データのバイト数
			result.source = TextureSource::ENCODED;

			const auto *begin =
			    reinterpret_cast<const std::uint8_t *>(embedded->pcData);

			result.bytes.assign(begin, begin + embedded->mWidth);
		} else {
			// 非圧縮画像はRGBA8へ変換する
			result.source = TextureSource::RGBA8;
			result.width = embedded->mWidth;
			result.height = embedded->mHeight;

			const std::size_t pixelCount =
			    static_cast<std::size_t>(result.width) * result.height;

			if (pixelCount > result.bytes.max_size() / 4) {
				Logger::Error("埋め込み画像が大きすぎます。");
				return false;
			}

			const std::string_view format = embedded->achFormatHint;

			if (format == "rgba8888") {
				const auto *begin =
				    reinterpret_cast<const std::uint8_t *>(embedded->pcData);

				result.bytes.assign(begin, begin + pixelCount * 4);
			} else if (format.empty() || format == "bgra8888") {
				result.bytes.resize(pixelCount * 4);

				for (std::size_t i = 0; i < pixelCount; ++i) {
					const aiTexel &pixel = embedded->pcData[i];

					result.bytes[i * 4 + 0] = pixel.r;
					result.bytes[i * 4 + 1] = pixel.g;
					result.bytes[i * 4 + 2] = pixel.b;
					result.bytes[i * 4 + 3] = pixel.a;
				}
			} else {
				Logger::Error("埋め込み画像の非圧縮形式が未対応です。");
				return false;
			}
		}
	} else {
		const std::string name = texturePath.C_Str();

		if (name.empty() || name.front() == '*') {
			Logger::Error("テクスチャの参照先が無効です。");
			return false;
		}

		// UTF-8の画像名からファイルパスを作る
		result.source = TextureSource::FILE;

		// 相対パスはモデルファイルのフォルダを基準にする
		try {
			auto reference = PathUtils::FromUtf8(name);
			absoluteReference = reference.has_root_path();

			if (!reference.has_root_path() &&
			    (PathUtils::HasExtension(modelPath, ".gltf") ||
			     PathUtils::HasExtension(modelPath, ".glb"))) {
				reference = PathUtils::DecodeRelativeUri(name);
			}

			result.filePath =
			    (modelPath.parent_path() / reference).lexically_normal();
		} catch (const std::invalid_argument &error) {
			Logger::Error(
			    std::format("画像の参照先が無効です: {}", error.what()));
			return false;
		}
	}

	textureData = std::move(result);
	return true;
}
