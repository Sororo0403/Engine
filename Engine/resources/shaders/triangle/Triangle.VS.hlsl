#include "Triangle.hlsli"

cbuffer Transformation : register(b0)
{
	row_major float4x4 world;
};

struct VertexInput
{
	float3 position : POSITION0;
	float4 color : COLOR0;
};

VertexOutput VSMain(VertexInput input)
{
	VertexOutput output;
	output.position = mul(float4(input.position, 1.0f), world);
	output.color = input.color;
	return output;
}
