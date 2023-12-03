cbuffer cb : register(b0) {
	float depthOut;
	float3 radius;
	float2 invClusterResolution;
	float2 projectionCenter;
	float2 yFix;
	float edgeRadius;
	float _padding;
};

float4 main(float4 position : SV_POSITION) : SV_TARGET {
	// working in blocks of 8x8 pixels
	float2 pos = float2(position.x, position.y * yFix.x + yFix.y);
	float2 toCenter = pos.xy * 0.125f * invClusterResolution.xy - projectionCenter;
	float distToCenter = length(toCenter) * 2;

	if( distToCenter < edgeRadius )
		discard;

	return float4(0, 0, 0, 0);	
}
