#include "Triangle.hlsli"

cbuffer Transformation : register(b0)
{
	row_major float4x4 worldViewProjection;
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
	output.position = mul(float4(input.position, 1.0f), worldViewProjection);
	output.color = input.color;
	output.texcoord = input.texcoord;
	output.normal = input.normal;
	return output;
}
