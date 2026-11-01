#include "DebugLine.hlsli"

cbuffer LineConstants : register(b0)
{
	row_major float4x4 viewProjection;
};

struct VertexInput
{
	float3 position : POSITION0;
	float4 color : COLOR0;
};

DebugLineOutput VSMain(VertexInput input)
{
	DebugLineOutput output;

	output.position =
	    mul(float4(input.position, 1.0f), viewProjection);

	output.color = input.color;

	return output;
}
