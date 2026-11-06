// SkinningConstantsと同じ上限。末尾の1枠は所属ノードの変換。
cbuffer Skinning : register(b6) {
	row_major float4x4 skinTransforms[257];
	row_major float4x4 skinNormals[257];
	uint skinBoneCount;
	uint3 skinPadding;
};

struct SkinnedVertexInput {
	float3 position : POSITION0;
	float4 color : COLOR0;
	float2 texcoord : TEXCOORD0;
	float3 normal : NORMAL0;
	uint4 bones0 : BLENDINDICES0;
	uint4 bones1 : BLENDINDICES1;
	float4 weights0 : BLENDWEIGHT0;
	float4 weights1 : BLENDWEIGHT1;
};

void SkinVertex(SkinnedVertexInput input, out float3 position, out float3 normal) {
	position = 0;
	normal = 0;
	float total = 0;
	[unroll] for (uint i = 0; i < 8; ++i) {
		uint bone = i < 4 ? input.bones0[i] : input.bones1[i - 4];
		float weight = i < 4 ? input.weights0[i] : input.weights1[i - 4];
		if (weight > 0 && bone < skinBoneCount) {
			position += mul(float4(input.position, 1), skinTransforms[bone]).xyz * weight;
			normal += mul(input.normal, (float3x3)skinNormals[bone]) * weight;
			total += weight;
		}
	}
	if (total == 0) {
		position = mul(float4(input.position, 1), skinTransforms[skinBoneCount]).xyz;
		normal = mul(input.normal, (float3x3)skinNormals[skinBoneCount]);
	}
	normal /= sqrt(max(dot(normal, normal), 1.0e-20f));
}
