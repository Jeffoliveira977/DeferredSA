#include "DeferredRenderer.h"
#include "EnvironmentMapping.h"
#include "CubemapReflection.h"
#include "DualParaboloidReflection.h"
#include "Lights.h"
#include "Quad.h"
#include "CTimeCycle.h"
#include "CascadedShadowRendering.h"
#include "CScene.h"
#include "ShaderManager.h"
#include "CGame.h"
#include "CGameIdle.h"
#include <DirectXMath.h>

RenderingStage gRenderState;
DeferredRendering *DeferredContext;

DeferredRendering::DeferredRendering()
{
	m_shadowScreenRaster = nullptr;
	m_graphicsLight      = nullptr;

	m_graphicsBuffer[0]  = nullptr;
	m_graphicsBuffer[1]  = nullptr;
	m_graphicsBuffer[2]  = nullptr;
	m_graphicsBuffer[3]  = nullptr;

	PS_DirectLight       = nullptr;
	PS_PointAndSpotLight = nullptr;
	PS_TargetLight       = nullptr;
	PS_ShadowScreen      = nullptr;
}

DeferredRendering::~DeferredRendering()
{
	RwRasterDestroy(m_shadowScreenRaster);
	RwRasterDestroy(m_graphicsLight);
	RwRasterDestroy(m_graphicsBuffer[0]);
	RwRasterDestroy(m_graphicsBuffer[1]);
	RwRasterDestroy(m_graphicsBuffer[2]);
	RwRasterDestroy(m_graphicsBuffer[3]);
	
	RwD3D9DeletePixelShader(PS_DirectLight);
	RwD3D9DeletePixelShader(PS_PointAndSpotLight);
	RwD3D9DeletePixelShader(PS_TargetLight);
	RwD3D9DeletePixelShader(PS_ShadowScreen);
}

float VolumetricLightParam[3];


RwRaster* mFXAARaster = nullptr;


void* mFXAAVertexShader = nullptr;
void* mFXAAPixelShader = nullptr;

RwRaster* mGaussianBlurXRaster = nullptr;
RwRaster* mGaussianBlurYRaster = nullptr;
void* mGaussianBlurVertexShader = nullptr;
void* mGaussianBlurXPixelShader = nullptr;
void* mGaussianBlurYPixelShader = nullptr;

RwRaster* mDownFilter4Raster = nullptr;
RwRaster* mBloomRaster = nullptr;

void* mBloomPixelShader = nullptr;
void* mDownFilter4PixelShader = nullptr;
void* mBloomCombinePixelShader = nullptr;


// VOLUMETRIC CLOUDS
RwTexture* mCloudTexture = nullptr;
RwTexture* mWeatherexture = nullptr;
RwTexture* mWorleyexture = nullptr;

RwRaster* mVolumetricCloudRaster = nullptr;
RwRaster* mVolumetricCloudBlurRaster = nullptr;
RwRaster* mVolumetricCloudCombineRaster = nullptr;

void* mVolumetricCloudsPixelShader = nullptr;
void* mVolumetricCloudsBlurPixelShader = nullptr;
void* mVolumetricCloudsCombinePixelShader = nullptr;

IDirect3DTexture9* g_CLOUDTEX = nullptr;
IDirect3DTexture9* g_WORLEYTEX = nullptr;
void DeferredRendering::Initialize()
{
	PS_DirectLight = RwCreateCompiledPixelShader("DeferredDirectLightPS");
	PS_PointAndSpotLight = RwCreateCompiledPixelShader("DeferredPointAndSpotLightPS");
	PS_TargetLight = RwCreateCompiledPixelShader("DeferredFinalPassPS");
	PS_ShadowScreen = RwCreateCompiledPixelShader("ShadowScreen");
	PS_AtmosphereScattering = RwCreateCompiledPixelShader("AtmosphericScattering");
	PS_VolumetricLight = RwCreateCompiledPixelShader("VolumetricLight");
	PS_VolumetricLightCombine = RwCreateCompiledPixelShader("VolumetricLightCombine");
	VS_Quad = RwCreateCompiledVertexShader("GaussianBlur");

	mFXAAVertexShader = RwCreateCompiledVertexShader("FXAA_VS");
	mFXAAPixelShader = RwCreateCompiledPixelShader("FXAA_PS");

	mGaussianBlurVertexShader = RwCreateCompiledVertexShader("GaussianBlur");
	mGaussianBlurXPixelShader = RwCreateCompiledPixelShader("GaussianBlurX");
	mGaussianBlurYPixelShader = RwCreateCompiledPixelShader("GaussianBlurY");

	mBloomPixelShader = RwCreateCompiledPixelShader("BloomPS");
	mDownFilter4PixelShader = RwCreateCompiledPixelShader("DownFilter4");
	mBloomCombinePixelShader = RwCreateCompiledPixelShader("BloomCombine");

	mVolumetricCloudsPixelShader = RwCreateCompiledPixelShader("VolumetricClouds");
	mVolumetricCloudsBlurPixelShader = RwCreateCompiledPixelShader("VolumetricCloudsBlur");
	mVolumetricCloudsCombinePixelShader = RwCreateCompiledPixelShader("VolumetricCloudsCombine");

	int width, height;
	width = RsGlobal.maximumWidth;
	height = RsGlobal.maximumHeight;
	m_shadowScreenRaster = RwD3D9RasterCreate(width, height, D3DFMT_A8R8G8B8, rwRASTERTYPECAMERATEXTURE);
	m_screenRaster = RwD3D9RasterCreate(width, height, D3DFMT_A16B16G16R16F, rwRASTERTYPECAMERATEXTURE);
	m_volumetricLight = RwD3D9RasterCreate(width, height, D3DFMT_A8R8G8B8, rwRASTERTYPECAMERATEXTURE);
	mFXAARaster = RwD3D9RasterCreate(width, height, D3DFMT_A16B16G16R16F, rwRASTERTYPECAMERATEXTURE);

	mGaussianBlurXRaster = RwD3D9RasterCreate(width, height, D3DFMT_A16B16G16R16F, rwRASTERTYPECAMERATEXTURE);
	mGaussianBlurYRaster = RwD3D9RasterCreate(width, height, D3DFMT_A16B16G16R16F, rwRASTERTYPECAMERATEXTURE);

	mBloomRaster = RwD3D9RasterCreate(width, height, D3DFMT_A16B16G16R16F, rwRASTERTYPECAMERATEXTURE);
	mDownFilter4Raster = RwD3D9RasterCreate(width, height, D3DFMT_A16B16G16R16F, rwRASTERTYPECAMERATEXTURE);

	// For better quality we will use D3DFMT_A16B16G16R16F
	m_graphicsLight = RwD3D9RasterCreate(width, height, D3DFMT_A16B16G16R16F, rwRASTERTYPECAMERATEXTURE);
	m_graphicsBuffer[0] = RwD3D9RasterCreate(width, height, D3DFMT_A16B16G16R16F, rwRASTERTYPECAMERATEXTURE);
	m_graphicsBuffer[1] = RwD3D9RasterCreate(width, height, D3DFMT_A16B16G16R16F, rwRASTERTYPECAMERATEXTURE);
	m_graphicsBuffer[2] = RwD3D9RasterCreate(width, height, D3DFMT_A16B16G16R16F, rwRASTERTYPECAMERATEXTURE);
	m_graphicsBuffer[3] = RwD3D9RasterCreate(width, height, D3DFMT_A16B16G16R16F, rwRASTERTYPECAMERATEXTURE);


	//mCloudTexture = RwD3D9DDSTextureRead("DeferredSA/textures/cloud", nullptr);
	//mWeatherexture = RwD3D9DDSTextureRead("DeferredSA/textures/weather", nullptr);
	mWorleyexture = LoadTextureFromFile("DeferredSA/textures/distribution.png");
	//if(mCloudTexture ==nullptr)
	//	MessageBox(0, "failed", "Error", MB_OK);

	//HRESULT hr;
	//hr= D3DXCreateTextureFromFileA(RwD3DDevice, "DeferredSA/textures/cloud.dds", &g_CLOUDTEX);
	//hr= D3DXCreateTextureFromFileA(RwD3DDevice, "DeferredSA/textures/worley.dds", &g_WORLEYTEX);
	//if(FAILED(hr))
	//MessageBox(0, "failed", "Error", MB_OK);

	mVolumetricCloudRaster = RwD3D9RasterCreate(width, height, D3DFMT_A16B16G16R16F, rwRASTERTYPECAMERATEXTURE);
	mVolumetricCloudBlurRaster = RwD3D9RasterCreate(width, height, D3DFMT_A16B16G16R16F, rwRASTERTYPECAMERATEXTURE);
	mVolumetricCloudCombineRaster = RwD3D9RasterCreate(width, height, D3DFMT_A16B16G16R16F, rwRASTERTYPECAMERATEXTURE);

	VolumetricLightParam[0] = 97.5;
	VolumetricLightParam[1] = 0.74000001;
	VolumetricLightParam[2] = 0.107;
}

void DeferredRendering::BindFirstPass()
{
	gRenderState = stageDeferred;

	rwD3D9SetRenderTargets(m_graphicsBuffer, 4, 0);
	ShaderContext->SetViewProjectionMatrix(4, true);
	ShaderContext->SetViewMatrix(4);
}

void DeferredRendering::BindLastPass()
{
	DefinedState();

	ShaderContext->SetInverseViewMatrix(0);
	ShaderContext->SetProjectionMatrix(4);

	//for(size_t i = 0; i < 4; i++)
	//{
	//	rwD3D9SetSamplerState(i, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
	//	rwD3D9SetSamplerState(i, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
	//	rwD3D9SetSamplerState(i, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
	//	rwD3D9SetSamplerState(i, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
	//	rwD3D9SetSamplerState(i, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
	//	//rwD3D9SetSamplerState(i, D3DSAMP_ADDRESSW, D3DTADDRESS_WRAP);
	//	//auto rasExt = RASTEREXTFROMRASTER(m_graphicsBuffer[i]);
	//	//RwD3DDevice->SetTexture(i + 1, rasExt->texture);

	//}

	_rwD3D9RWSetRasterStage(m_graphicsLight, 0);
	_rwD3D9RWSetRasterStage(m_graphicsBuffer[0], 1);
	_rwD3D9RWSetRasterStage(m_graphicsBuffer[1], 2);
	_rwD3D9RWSetRasterStage(m_graphicsBuffer[2], 3);
	_rwD3D9RWSetRasterStage(m_graphicsBuffer[3], 4);
	_rwD3D9RWSetRasterStage(m_shadowScreenRaster, 5);

	// Only use pixel shader
	_rwD3D9SetVertexShader(NULL);

	// We need to disable Z buffer
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, FALSE);
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, FALSE);
	RwRenderStateSet(rwRENDERSTATECULLMODE, (void*)rwCULLMODECULLNONE);
	RwD3D9SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);

	if(!CGame::currArea && CGameIdle::m_fShadowDNBalance < 1.0)
		CascadedShadow();

	RwD3D9SetRenderTarget(0, m_graphicsLight);

	DirectLight();
	PointAndSpotLight();

	

	// Restore render target and draw light to final raster
	RwD3D9RestoreRenderTargets(4);
	FinalPass();

	AtmosphericScattering();
	//VolumetricLight();
	VolumetricClouds();
	_rwD3D9SetPixelShader(NULL);
	_rwD3D9SetVertexShader(NULL);
}

void DeferredRendering::RenderPostProcessing()
{
	DefinedState();
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
	RwD3D9SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);
	RwRenderStateSet(rwRENDERSTATECULLMODE, (void*)rwCULLMODECULLNONE);
	RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
	ShaderContext->SetInverseViewMatrix(0);
	ShaderContext->SetProjectionMatrix(4);
	// VolumetricLight();

	//FXAA();
	//Bloom();
	// VolumetricClouds();
}

void DeferredRendering::DirectLight()
{
	ShaderContext->SetTimecyProps(8);

	RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
	_rwD3D9SetPixelShader(PS_DirectLight);
	DrawScreenQuad();
}

#include "CCamera.h"

void DeferredRendering::PointAndSpotLight()
{
	_rwD3D9SetPixelShaderConstant(8, &Scene.m_pRwCamera->farPlane, 1);
	
	RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDONE);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDONE);

	_rwD3D9SetPixelShader(PS_PointAndSpotLight);

	//Lights::SortByDistance(TheCamera.GetPosition().ToRwV3d());

	XMVECTOR value[4];



	// Spot and point light 
	for(int i = 0; i < Lights::m_nLightCount; i++)
	{
		memcpy(value, &Lights::Buffer()[i], sizeof(LightData));
		_rwD3D9SetPixelShaderConstant(9, value, 
									  sizeof(LightData)); 
		
		DrawScreenQuad();
	}
	//PrintMessage("%d", Lights::m_nLightCount);
}

void DeferredRendering::FinalPass()
{
	_rwD3D9SetPixelShaderConstant(8, EnvironmentMapping::m_paraboloidBasis, 4);

	ShaderContext->SetTimecyProps(12);

	RwRaster* reflectionRasters[] = {CubemapReflection::m_cubeRaster,
									 EnvironmentMapping::m_sphericalRaster,
									 DualParaboloidReflection::m_raster[0],
									 DualParaboloidReflection::m_raster[1]};

	float fMipMapLODBias = -1000;
	for(size_t i = 0; i < 4; i++)
	{
		RwD3DDevice->SetSamplerState(i + 4, D3DSAMP_BORDERCOLOR, 0x0);
		RwD3DDevice->SetSamplerState(i + 4, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
		RwD3DDevice->SetSamplerState(i + 4, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
		RwD3DDevice->SetSamplerState(i + 4, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
		RwD3DDevice->SetSamplerState(i + 4, D3DSAMP_ADDRESSU, D3DTADDRESS_BORDER);
		RwD3DDevice->SetSamplerState(i + 4, D3DSAMP_ADDRESSV, D3DTADDRESS_BORDER);
		RwD3DDevice->SetSamplerState(i + 4, D3DSAMP_ADDRESSW, D3DTADDRESS_BORDER);
		
		auto rasExt = RASTEREXTFROMRASTER(reflectionRasters[i]);
		RwD3DDevice->SetTexture(i + 4, rasExt->texture);
		
		//	_rwD3D9RWSetRasterStage(reflectionRasters[i], i + 4);
	}

	RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
	_rwD3D9SetPixelShader(PS_TargetLight);
	DrawScreenQuad();
}

void  DeferredRendering::AtmosphericScattering()
{
	ShaderContext->SetTimecyProps(8);

	IDirect3DSurface9* screenSurface;
	auto screenExt = RASTEREXTFROMRASTER(m_screenRaster);
	screenExt->texture->GetSurfaceLevel(0, &screenSurface);

	RwD3DDevice->StretchRect(RwD3D9RenderSurface, NULL, screenSurface, NULL, D3DTEXF_NONE);
	_rwD3D9RWSetRasterStage(m_screenRaster, 4);

	screenSurface->Release();

	_rwD3D9SetPixelShader(PS_AtmosphereScattering);
	DrawScreenQuad();
}

void DeferredRendering::VolumetricLight()
{
	ShaderContext->SetTimecyProps(8);

	_rwD3D9SetPixelShaderConstant(13, VolumetricLightParam, 1);

	for(size_t i = 0; i < CascadedShadowManagement->CascadeCount; i++)
	{
		rwD3D9SetSamplerState(i + 4, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
		rwD3D9SetSamplerState(i + 4, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
		rwD3D9SetSamplerState(i + 4, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
		rwD3D9SetSamplerState(i + 4, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
		rwD3D9SetSamplerState(i + 4, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
		rwD3D9RWSetRasterStage(CascadedShadowManagement->m_shadowColorRaster[i], i + 4);
	}

	_rwD3D9SetPixelShaderConstant(14, &CascadedShadowManagement->m_shadowBuffer,
								  sizeof(CascadedShadowManagement->m_shadowBuffer) / sizeof(XMVECTOR));

	_rwD3D9SetPixelShader(PS_VolumetricLight);
	_rwD3D9SetVertexShader(0);
	RwD3D9SetRenderTarget(0, m_volumetricLight);
	DrawScreenQuad();

	IDirect3DSurface9* screenSurface;
	auto screenExt = RASTEREXTFROMRASTER(m_screenRaster);
	screenExt->texture->GetSurfaceLevel(0, &screenSurface);

	RwD3DDevice->StretchRect(RwD3D9RenderSurface, NULL, screenSurface, NULL, D3DTEXF_NONE);
	rwD3D9RWSetRasterStage(m_screenRaster, 4);
	rwD3D9RWSetRasterStage(m_volumetricLight, 5);

	screenSurface->Release();

	_rwD3D9SetPixelShader(PS_VolumetricLightCombine);
	__rwD3D9SetRenderTarget(0, RwD3D9RenderSurface);
	DrawScreenQuad();
}

void DeferredRendering::FXAA()
{
	IDirect3DSurface9* screenSurface;
	auto screenExt = RASTEREXTFROMRASTER(mFXAARaster);
	screenExt->texture->GetSurfaceLevel(0, &screenSurface);

	RwD3DDevice->StretchRect(RwD3D9RenderSurface, NULL, screenSurface, NULL, D3DTEXF_NONE);

	screenSurface->Release();


	XMMATRIX world, view, projection;
	RwD3D9GetTransform(D3DTS_VIEW, &view);
	RwD3D9GetTransform(D3DTS_PROJECTION, &projection);
	world = XMMatrixIdentity();

	_rwD3D9SetVertexShaderConstant(0, &(world * view * projection), 4);
	_rwD3D9SetVertexShader(mFXAAVertexShader);
	_rwD3D9SetPixelShader(mFXAAPixelShader);
	rwD3D9RWSetRasterStage(mFXAARaster, 0);
	__rwD3D9SetRenderTarget(0, RwD3D9RenderSurface);
	DrawScreenQuad();
}

void DeferredRendering::Bloom()
{
	IDirect3DSurface9* screenSurface;
	auto screenExt = RASTEREXTFROMRASTER(m_screenRaster);
	screenExt->texture->GetSurfaceLevel(0, &screenSurface);

	RwD3DDevice->StretchRect(RwD3D9RenderSurface, NULL, screenSurface, NULL, D3DTEXF_NONE);
	screenSurface->Release();

	RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
	_rwD3D9SetVertexShader(mGaussianBlurVertexShader);

	int width, height;
	width = RsGlobal.maximumWidth;
	height = RsGlobal.maximumHeight;

	_rwD3D9SetPixelShader(mBloomPixelShader);
	rwD3D9RWSetRasterStage(m_screenRaster, 0);
	RwD3D9SetRenderTarget(0, mBloomRaster);
	DrawScreenQuad();

	_rwD3D9SetPixelShader(mDownFilter4PixelShader);
	rwD3D9RWSetRasterStage(mBloomRaster, 0);
	RwD3D9SetRenderTarget(0, mDownFilter4Raster);
	DrawScreenQuad();

	_rwD3D9SetPixelShader(mDownFilter4PixelShader);
	rwD3D9RWSetRasterStage(mDownFilter4Raster, 0);
	RwD3D9SetRenderTarget(0, mBloomRaster);
	DrawScreenQuad();

	/*RwV2d samplesOffsets[15];
	float sampleWeights[15];
	GetGaussianOffsets(true, width / height, samplesOffsets, sampleWeights);*/
	//_rwD3D9SetVertexShaderConstant(0, samplesOffsets, sizeof(samplesOffsets));
	//_rwD3D9SetVertexShaderConstant(19, sampleWeights, sizeof(sampleWeights) );
	_rwD3D9SetPixelShader(mGaussianBlurXPixelShader);
	rwD3D9RWSetRasterStage(mBloomRaster, 0);
	RwD3D9SetRenderTarget(0, mGaussianBlurXRaster);
	DrawScreenQuad();

	//GetGaussianOffsets(false, width / height, samplesOffsets, sampleWeights);
	//_rwD3D9SetVertexShaderConstant(0, samplesOffsets, sizeof(samplesOffsets));
	//_rwD3D9SetVertexShaderConstant(19, sampleWeights, sizeof(sampleWeights) );

	_rwD3D9SetPixelShader(mGaussianBlurYPixelShader);
	rwD3D9RWSetRasterStage(mGaussianBlurXRaster, 0);
	RwD3D9SetRenderTarget(0, mGaussianBlurYRaster);
	DrawScreenQuad();

	_rwD3D9SetPixelShader(mBloomCombinePixelShader);
	rwD3D9RWSetRasterStage(mGaussianBlurYRaster, 0);
	rwD3D9RWSetRasterStage(m_screenRaster, 1);
	__rwD3D9SetRenderTarget(0, RwD3D9RenderSurface);
	DrawScreenQuad();

}

void DeferredRendering::VolumetricClouds()
{
	/*RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATECULLMODE, (void*)rwCULLMODECULLNONE);
	RwD3D9SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);
	RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);*/
	IDirect3DSurface9* screenSurface;
	auto screenExt = RASTEREXTFROMRASTER(m_screenRaster);
	screenExt->texture->GetSurfaceLevel(0, &screenSurface);
	RwD3DDevice->StretchRect(RwD3D9RenderSurface, NULL, screenSurface, NULL, D3DTEXF_NONE);
	screenSurface->Release();

	ShaderContext->SetInverseViewMatrix(0);
	ShaderContext->SetProjectionMatrix(4);
	ShaderContext->SetSunColor(8);
	ShaderContext->SetSunDirection(9);

	CVector m_skyTop = {
			 static_cast<float>(CTimeCycle::m_CurrentColours.m_fPostFx1Red) / 255.0f,
			 static_cast<float>(CTimeCycle::m_CurrentColours.m_fPostFx1Green) / 255.0f,
			 static_cast<float>(CTimeCycle::m_CurrentColours.m_fPostFx1Blue) / 255.0f};
	_rwD3D9SetPixelShaderConstant(10, &m_skyTop, 1);
	
	CVector m_skyBottom = {
	   static_cast<float>(CTimeCycle::m_CurrentColours.m_fPostFx2Red) / 255.0f,
	   static_cast<float>(CTimeCycle::m_CurrentColours.m_fPostFx2Green) / 255.0f,
	   static_cast<float>(CTimeCycle::m_CurrentColours.m_fPostFx2Blue) / 255.0f};
	_rwD3D9SetPixelShaderConstant(11, &m_skyBottom, 1);
	float mSettings[2];
	mSettings[0] = CTimeCycle::m_CurrentColours.m_fFogStart;
	mSettings[1] = CTimeCycle::m_CurrentColours.m_fFarClip;
	_rwD3D9SetPixelShaderConstant(12, &mSettings, 1);

	float ti = (float)CTimer__m_snTimeInMilliseconds;
	ti = ti / 1000.0;
	_rwD3D9SetPixelShaderConstant(13, &ti, 1);

	//ShaderContext->SetFogParams(11);


	rwD3D9SetSamplerState(2, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
	rwD3D9SetSamplerState(2, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
	rwD3D9SetSamplerState(2, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
	rwD3D9SetSamplerState(2, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
	rwD3D9SetSamplerState(2, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);

		rwD3D9SetSamplerState(4, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
		rwD3D9SetSamplerState(4, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
		rwD3D9SetSamplerState(4, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
		rwD3D9SetSamplerState(4, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
		rwD3D9SetSamplerState(4, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
		rwD3D9SetSamplerState(4, D3DSAMP_SRGBTEXTURE, TRUE);
		rwD3D9SetSamplerState(4, D3DSAMP_MAXMIPLEVEL, 2);
		rwD3D9SetSamplerState(4, D3DSAMP_MIPMAPLODBIAS, 0);

	rwD3D9SetSamplerState(5, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
	rwD3D9SetSamplerState(5, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
	rwD3D9SetSamplerState(5, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
	rwD3D9SetSamplerState(5, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
	rwD3D9SetSamplerState(5, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
	rwD3D9SetSamplerState(5, D3DSAMP_ADDRESSW, D3DTADDRESS_WRAP);
	rwD3D9SetSamplerState(5, D3DSAMP_SRGBTEXTURE, FALSE);
	rwD3D9SetSamplerState(5, D3DSAMP_MAXMIPLEVEL, 16);
	rwD3D9SetSamplerState(5, D3DSAMP_MIPMAPLODBIAS, 0);
	
	/*_rwD3D9SetPixelShader(mVolumetricCloudsPixelShader);
	rwD3D9RWSetRasterStage(m_graphicsBuffer[1], 2);
	RwD3D9SetRenderTarget(0, mVolumetricCloudRaster);
	DrawScreenQuad();*/

	//_rwD3D9SetPixelShader(mVolumetricCloudsBlurPixelShader);
	//rwD3D9RWSetRasterStage(mVolumetricCloudRaster, 0);
	//rwD3D9RWSetRasterStage(m_graphicsBuffer[1], 1);
	//RwD3D9SetRenderTarget(0, mVolumetricCloudBlurRaster);
	//DrawScreenQuad();

	_rwD3D9SetPixelShader(mVolumetricCloudsPixelShader);
	rwD3D9RWSetRasterStage(m_screenRaster, 4);
	RwD3D9SetTexture(mWorleyexture, 5);
	__rwD3D9SetRenderTarget(0, RwD3D9RenderSurface);
	DrawScreenQuad();

}

#include "imgui.h"
void DeferredRendering::imguiParameters()
{
	if(ImGui::BeginTabItem("Deferred"))
	{
		ImGui::EndTabItem();
		ImGui::InputFloat("RaymarchingDistance", &VolumetricLightParam[0], 0.1, 1.0, "%.1f");
		ImGui::InputFloat("SunlightBlendOffset", &VolumetricLightParam[1], 0.01, 0.1, "%.6f");
		ImGui::InputFloat("SunlightIntensity", &VolumetricLightParam[2], 0.01, 0.1, "%.3f");
	}
}

void DeferredRendering::CascadedShadow()
{
	CascadedShadowManagement->SetParamsBuffer();

	RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
	RwD3D9SetRenderTarget(0, m_shadowScreenRaster);

	ShaderContext->SetSunDirection(11);
	ShaderContext->SetFogParams(12);

	_rwD3D9SetPixelShader(PS_ShadowScreen);	

	for(size_t i = 0; i < CascadedShadowManagement->CascadeCount; i++)
	{
		//RwD3DDevice->SetSamplerState(i + 6, D3DSAMP_MINFILTER, D3DTEXF_POINT);
		//RwD3DDevice->SetSamplerState(i + 6, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
		//RwD3DDevice->SetSamplerState(i + 6, D3DSAMP_MIPFILTER, D3DTEXF_POINT);
		//RwD3DDevice->SetSamplerState(i + 6, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
		//RwD3DDevice->SetSamplerState(i + 6, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
		//RwD3DDevice->SetSamplerState(i + 6, D3DSAMP_BORDERCOLOR, 0xFFFFFFFF);

		rwD3D9SetSamplerState(i + 6, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
		rwD3D9SetSamplerState(i + 6, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
		rwD3D9SetSamplerState(i + 6, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
		rwD3D9SetSamplerState(i + 6, D3DSAMP_ADDRESSU, D3DTADDRESS_BORDER);
		rwD3D9SetSamplerState(i + 6, D3DSAMP_ADDRESSV, D3DTADDRESS_BORDER);
		rwD3D9RWSetRasterStage(CascadedShadowManagement->m_shadowColorRaster[i], i + 6);
	}

	_rwD3D9SetPixelShaderConstant(13, &CascadedShadowManagement->m_shadowBuffer,
								  sizeof(CascadedShadowManagement->m_shadowBuffer) / sizeof(XMVECTOR));
	DrawScreenQuad();
}