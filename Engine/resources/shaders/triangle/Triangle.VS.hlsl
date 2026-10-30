#include "Triangle.hlsli"

cbuffer Transformation : register(b0)
{
	row_major float4x4 worldViewProjection;
	row_major float4x4 worldInverseTranspose;
};

struct VertexInput
{
	float3 position : POSITION0;
	float4 color : COLOR0;
	float2 texcoord : TEXCOORD0;
	float3 normal : NORMAL0;
};

VertexOutput VSMain(VertexInput input)
{
	VertexOutput output;

	output.position =
	    mul(float4(input.position, 1.0f), worldViewProjection);

	output.color = input.color;
	output.texcoord = input.texcoord;

	// 左上の3×3部分を使い、移動の影響を受けずに法線を変換する
	output.normal =
	    mul(input.normal, (float3x3) worldInverseTranspose);

	return output;
}
