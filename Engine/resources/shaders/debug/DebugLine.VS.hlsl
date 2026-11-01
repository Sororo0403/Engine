#include "DebugLine.hlsli"

cbuffer LineConstants : register(b0)
{
	row_major float4x4 viewProjection;
	float4 lineStart;
	float4 lineEnd;
	float4 lineColor;
};

DebugLineOutput VSMain(uint vertexId : SV_VertexID)
{
	DebugLineOutput output;

	float4 position = vertexId == 0 ? lineStart : lineEnd;

	output.position = mul(position, viewProjection);
	output.color = lineColor;

	return output;
}
