cbuffer ConstantBuffer 
{
    float4x4 projectMatrix;
    float4x4 clipMatrix;
    float4 baseColor;
    float4 channelFlag;
}
Texture2D    mainTexture : register(t0);
SamplerState mainSampler : register(s0);
Texture2D   maskTexture : register(t1);
    
struct VS_IN
{
    float3 pos : POSITION0;
    float4 spos : POSITION1;
    float3 norm : NORMAL;
    float3 tan    : TANGENT0;
    float3 binorm : BINORMAL0;
    float4 dif : COLOR0;
    float4 spc : COLOR1;
    float2 uv  : TEXCOORD0;
    float2 suv : TEXCOORD1;
};
struct VS_OUT 
{
    float4 Position     : SV_POSITION;
    float2 uv           : TEXCOORD0;
    float4 clipPosition : TEXCOORD1;
};
    
/* Setup mask shader */
VS_OUT VertSetupMask(VS_IN In) 
{
    VS_OUT Out = (VS_OUT)0;
    Out.Position     = mul( float4( In.pos, 1.0f ), projectMatrix);
    Out.clipPosition = mul( float4( In.pos, 1.0f ), projectMatrix);
    Out.uv.x = In.uv.x;
    Out.uv.y = 1.0 - In.uv.y;
    return Out;
}
float4 PixelSetupMask(VS_OUT In) : SV_Target
{
    float isInside =
		  step( baseColor.x, In.clipPosition.x / In.clipPosition.w)
		* step( baseColor.y, In.clipPosition.y / In.clipPosition.w)
		* step(              In.clipPosition.x / In.clipPosition.w, baseColor.z)
		* step(              In.clipPosition.y / In.clipPosition.w, baseColor.w);
	return channelFlag * mainTexture.Sample(mainSampler, In.uv).a * isInside;
}
    
/* Vertex Shader */
/* normal */
VS_OUT VertNormal(VS_IN In) 
{
    VS_OUT Out = (VS_OUT)0;
    Out.Position = mul( float4( In.pos, 1.0f ), projectMatrix );
    Out.uv.x =       In.uv.x;
    Out.uv.y = 1.0 - In.uv.y;
    return Out;
}
/* masked */
VS_OUT VertMasked(VS_IN In) 
{
    VS_OUT Out = (VS_OUT)0;
    Out.Position     = mul( float4( In.pos, 1.0f ), projectMatrix );
    Out.clipPosition = mul( float4( In.pos, 1.0f ), clipMatrix );
    Out.uv.x =       In.uv.x;
    Out.uv.y = 1.0 - In.uv.y;
    return Out;
}
    
/* Pixel Shader */
/* normal */
float4 PixelNormal(VS_OUT In) : SV_Target
{
    float4 color = mainTexture.Sample( mainSampler, In.uv ) * baseColor;
    color.xyz *= color.w;
    return color;
}
    
/* normal premult alpha */
float4 PixelNormalPremult(VS_OUT In) : SV_Target
{
    float4 color = mainTexture.Sample( mainSampler, In.uv ) * baseColor;
    return color;
}
    
/* masked */
float4 PixelMasked(VS_OUT In) : SV_Target
{
    float4 color    = mainTexture.Sample( mainSampler, In.uv ) * baseColor;
    color.xyz      *= color.w;
    float4 clipMask = ( 1.0 - maskTexture.Sample(mainSampler, In.clipPosition.xy / In.clipPosition.w ) ) * channelFlag;
    float maskVal   = clipMask.r + clipMask.g + clipMask.b + clipMask.a;
    color           = color * maskVal;
    return color;
}

/* masked inverted*/
float4 PixelMaskedInverted( VS_OUT In ) : SV_Target
{
	float4 color    = mainTexture.Sample( mainSampler, In.uv ) * baseColor;
	color.xyz      *= color.w;
	float4 clipMask = ( 1.0 - maskTexture.Sample( mainSampler, In.clipPosition.xy / In.clipPosition.w ) ) * channelFlag;
	float maskVal   = clipMask.r + clipMask.g + clipMask.b + clipMask.a;
	color           = color * ( 1.0 - maskVal );
	return color;
}

/* masked premult alpha */
float4 PixelMaskedPremult(VS_OUT In) : SV_Target
{
    float4 color    = mainTexture.Sample( mainSampler, In.uv ) * baseColor;
    float4 clipMask = ( 1.0 - maskTexture.Sample(mainSampler, In.clipPosition.xy / In.clipPosition.w ) ) * channelFlag;
    float maskVal   = clipMask.r + clipMask.g + clipMask.b + clipMask.a;
    color = color * maskVal;
    return color;
};

/* masked inverted premult alpha */
float4 PixelMaskedInvertedPremult( VS_OUT In ) : SV_Target
{
	float4 color    = mainTexture.Sample( mainSampler, In.uv ) * baseColor;
	float4 clipMask = ( 1.0 - maskTexture.Sample( mainSampler, In.clipPosition.xy / In.clipPosition.w ) ) * channelFlag;
	float maskVal   = clipMask.r + clipMask.g + clipMask.b + clipMask.a;
	color           = color * ( 1.0 - maskVal );
	return color;
};
