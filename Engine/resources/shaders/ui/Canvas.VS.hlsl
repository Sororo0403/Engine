cbuffer Element : register(b0) {
    float4 originAndXAxis;
    float4 yAxis;
    float4 tint;
    float4 uvRect;
};
struct Output {
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};
Output main(uint id : SV_VertexID) {
    static const float2 corners[6] = {
        float2(0, 0), float2(1, 0), float2(0, 1),
        float2(0, 1), float2(1, 0), float2(1, 1)
    };
    Output result;
    result.uv = uvRect.xy + corners[id] * uvRect.zw;
    result.position = float4(originAndXAxis.xy +
                             corners[id].x * originAndXAxis.zw +
                             corners[id].y * yAxis.xy, 0, 1);
    return result;
}
