cbuffer Element : register(b0) {
    float4 originAndXAxis;
    float4 yAxis;
    float4 tint;
    float4 uvRect;
    float4 monochromeLevels;
    float monochromeSoftness;
};
Texture2D image : register(t0);
SamplerState imageSampler : register(s0);
float4 main(float4 position : SV_POSITION, float2 uv : TEXCOORD0) : SV_TARGET0 {
    float4 color = image.Sample(imageSampler, uv) * tint;
    if (monochromeLevels.x > 0.0f) {
        float luminance = dot(color.rgb, float3(.2126, .7152, .0722));
        float threshold = monochromeLevels.w;
        float tone = smoothstep(threshold - monochromeSoftness,
                                threshold + monochromeSoftness, luminance);
        float gray = lerp(monochromeLevels.y, monochromeLevels.z, tone);
        color.rgb = lerp(color.rgb, gray.xxx, monochromeLevels.x);
    }
    return color;
}
