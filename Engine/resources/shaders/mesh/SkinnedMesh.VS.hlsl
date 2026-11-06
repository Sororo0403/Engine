#include "Mesh.hlsli"
#include "Skinning.hlsli"

cbuffer Transformation : register(b0) {
	row_major float4x4 worldViewProjection;
	row_major float4x4 worldInverseTranspose;
	row_major float4x4 world;
};

VertexOutput VSMain(SkinnedVertexInput input) {
	float3 position, normal;
	SkinVertex(input, position, normal);
	VertexOutput output;
	output.position = mul(float4(position, 1), worldViewProjection);
	output.worldPosition = mul(float4(position, 1), world).xyz;
	output.normal = mul(normal, (float3x3)worldInverseTranspose);
	output.color = input.color;
	output.texcoord = input.texcoord;
	return output;
}
