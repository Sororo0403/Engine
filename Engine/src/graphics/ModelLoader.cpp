#include "graphics/ModelLoader.h"
#include "core/Logger.h"

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <cstdint>
#include <format>
#include <string>
#include <utility>

bool ModelLoader::Load(const std::filesystem::path &filePath,
                       MeshData &meshData) {
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

	// 最後まで成功してから呼び出し元へ反映する
	MeshData result;

	for (unsigned int meshIndex = 0; meshIndex < scene->mNumMeshes;
	     ++meshIndex) {
		const aiMesh &source = *scene->mMeshes[meshIndex];

		// 点や線だけのメッシュは対象外
		if ((source.mPrimitiveTypes & aiPrimitiveType_TRIANGLE) == 0) {
			continue;
		}

		if (!source.HasPositions() || !source.HasNormals()) {
			Logger::Error("モデルの頂点座標または法線がありません。");
			return false;
		}

		// 結合後の頂点番号を32ビットで表せるか確認する
		if (source.mNumVertices > UINT32_MAX - result.vertices.size()) {
			Logger::Error("モデルの頂点数が多すぎます。");
			return false;
		}

		// このメッシュの頂点が始まる位置
		const auto vertexOffset =
		    static_cast<std::uint32_t>(result.vertices.size());

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

			result.vertices.push_back(vertex);
		}

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

				result.indices.push_back(vertexOffset + index);
			}
		}
	}

	if (result.vertices.empty() || result.indices.empty()) {
		Logger::Error("モデルに描画できる三角形がありません。");
		return false;
	}

	Logger::Info(std::format(
	    "モデルを読み込みました。ファイル={}、頂点数={}、インデックス数={}",
	    path, result.vertices.size(), result.indices.size()));

	meshData = std::move(result);
	return true;
}
