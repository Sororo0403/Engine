#include "graphics/ModelLoader.h"
#include "core/Logger.h"

#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <assimp/texture.h>

#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <utility>

bool ModelLoader::Load(const std::filesystem::path &filePath,
                       ModelData &modelData) {
	if (filePath.empty()) {
		Logger::Error("ModelLoader::Load: パスが空です。");
		return false;
	}

	// Assimpへ渡すパスをUTF-8にする
	const auto utf8Path = filePath.u8string();

	const std::string path(reinterpret_cast<const char *>(utf8Path.data()),
	                       utf8Path.size());

	Assimp::Importer importer;

	const unsigned int flags =
	    aiProcess_Triangulate | aiProcess_SortByPType |
	    aiProcess_JoinIdenticalVertices | aiProcess_GenNormals |
	    aiProcess_PreTransformVertices | aiProcess_ConvertToLeftHanded;

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
		material.baseColor = {color.r, color.g, color.b, color.a};

		if (!LoadBaseColorTexture(*scene, source, filePath,
		                          material.baseColorTexture)) {
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
                                       ModelTextureData &textureData) {
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

	ModelTextureData result;

	const aiTexture *embedded = scene.GetEmbeddedTexture(texturePath.C_Str());

	if (embedded) {
		if (!embedded->pcData || embedded->mWidth == 0) {
			Logger::Error("埋め込み画像のデータが無効です。");
			return false;
		}

		if (embedded->mHeight == 0) {
			// mHeightが0の場合、mWidthは画像データのバイト数
			result.source = ModelTextureSource::ENCODED;

			const auto *begin =
			    reinterpret_cast<const std::uint8_t *>(embedded->pcData);

			result.bytes.assign(begin, begin + embedded->mWidth);
		} else {
			// 非圧縮画像はRGBA8へ変換する
			result.source = ModelTextureSource::RGBA8;
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
		const std::u8string utf8Name(name.begin(), name.end());

		result.source = ModelTextureSource::FILE;

		// 相対パスはモデルファイルのフォルダを基準にする
		result.filePath =
		    (modelPath.parent_path() / std::filesystem::path(utf8Name))
		        .lexically_normal();
	}

	textureData = std::move(result);
	return true;
}
