#include "DebugLine.hlsli"

float4 PSMain(DebugLineOutput input) : SV_TARGET0 {
	return input.color;
}
