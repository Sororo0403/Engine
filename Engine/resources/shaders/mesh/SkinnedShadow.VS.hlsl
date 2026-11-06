#include "Skinning.hlsli"

cbuffer Transformation : register(b0) {
	row_major float4x4 worldViewProjection;
	row_major float4x4 worldInverseTranspose;
	row_major float4x4 world;
};

cbuffer ShadowBuffer : register(b5) {
	row_major float4x4 shadowViewProjection;
};

float4 VSMain(SkinnedVertexInput input) : SV_Position {
	float3 position, normal;
	SkinVertex(input, position, normal);
	return mul(mul(float4(position, 1), world), shadowViewProjection);
}
