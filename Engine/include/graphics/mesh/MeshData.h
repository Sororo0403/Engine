#pragma once

#include "graphics/mesh/Vertex.h"

#include <cstdint>
#include <vector>

/// <summary>
/// CPU側で保持するメッシュの頂点とインデックス
/// </summary>
struct MeshData {
	std::vector<Vertex> vertices;
	std::vector<std::uint32_t> indices;
};
