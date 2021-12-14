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
#include "ShaderConstant.h"
#include "VolumetricClouds.h"
#include "PixelShader.h"

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
	mVolumetricClouds = nullptr;
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

	delete mVolumetricClouds;
}


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


void DeferredRendering::Initialize()
{
	PS_DirectLight = RwCreateCompiledPixelShader("DeferredDirectLightPS");
	PS_PointAndSpotLight = RwCreateCompiledPixelShader("DeferredPointAndSpotLightPS");
	PS_TargetLight = RwCreateCompiledPixelShader("DeferredFinalPassPS");
	PS_ShadowScreen = RwCreateCompiledPixelShader("ShadowScreen");
	PS_AtmosphereScattering = RwCreateCompiledPixelShader("AtmosphericScattering");
	VS_Quad = RwCreateCompiledVertexShader("GaussianBlur");

	mPointSpotLightPixelShader = new PixelShader();
	mPointSpotLightPixelShader->CreateFromBinary("DeferredPointAndSpotLightPS");
	
	mFXAAVertexShader = RwCreateCompiledVertexShader("FXAA_VS");
	mFXAAPixelShader = RwCreateCompiledPixelShader("FXAA_PS");

	mGaussianBlurVertexShader = RwCreateCompiledVertexShader("GaussianBlur");
	mGaussianBlurXPixelShader = RwCreateCompiledPixelShader("GaussianBlurX");
	mGaussianBlurYPixelShader = RwCreateCompiledPixelShader("GaussianBlurY");

	mBloomPixelShader = RwCreateCompiledPixelShader("BloomPS");
	mDownFilter4PixelShader = RwCreateCompiledPixelShader("DownFilter4");
	mBloomCombinePixelShader = RwCreateCompiledPixelShader("BloomCombine");

	int width, height;
	width = RsGlobal.maximumWidth;
	height = RsGlobal.maximumHeight;

	m_shadowScreenRaster = RwD3D9RasterCreate(width, height, D3DFMT_A8R8G8B8, rwRASTERTYPECAMERATEXTURE);
	m_screenRaster = RwD3D9RasterCreate(width, height, D3DFMT_A16B16G16R16F, rwRASTERTYPECAMERATEXTURE);
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

	mVolumetricClouds = new VolumetricClouds();
	mVolumetricClouds->Initialize();

	mPostProcessing = new PostProcessing();
	mPostProcessing->Initialize();

	mVolumetricLight = new VolumetricLight();
	mVolumetricLight->Initialize();

	mAmbientOcclusion = new AmbientOcclusion();
	mAmbientOcclusion->Initialize();
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

	for(size_t i = 0; i < 4; i++)
	{
		rwD3D9SetSamplerState(i, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
		rwD3D9SetSamplerState(i, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
		rwD3D9SetSamplerState(i, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
		rwD3D9SetSamplerState(i, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
		rwD3D9SetSamplerState(i, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
		rwD3D9SetSamplerState(i, D3DSAMP_ADDRESSW, D3DTADDRESS_WRAP);
	}

	_rwD3D9RWSetRasterStage(m_graphicsLight, 0);
	_rwD3D9RWSetRasterStage(m_graphicsBuffer[0], 1);
	_rwD3D9RWSetRasterStage(m_graphicsBuffer[1], 2);
	_rwD3D9RWSetRasterStage(m_graphicsBuffer[2], 3);
	_rwD3D9RWSetRasterStage(m_graphicsBuffer[3], 4);
	_rwD3D9RWSetRasterStage(m_shadowScreenRaster, 5);

	// We need to disable Z buffer
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, FALSE);
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, FALSE);
	RwRenderStateSet(rwRENDERSTATECULLMODE, (void*)rwCULLMODECULLNONE);
	RwD3D9SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);

	if(!CGame::currArea && CGameIdle::m_fShadowDNBalance < 1.0)
		CascadedShadow();

	mAmbientOcclusion->Render();

	RwD3D9SetRenderTarget(0, m_graphicsLight);

	DirectLight();
	PointAndSpotLight();


	// Restore render target and draw light to final raster
	RwD3D9RestoreRenderTargets(4);
	FinalPass();

	AtmosphericScattering();

	IDirect3DSurface9* screenSurface;
	auto screenExt = RASTEREXTFROMRASTER(m_screenRaster);
	screenExt->texture->GetSurfaceLevel(0, &screenSurface);

	RwD3DDevice->StretchRect(RwD3D9RenderSurface, NULL, screenSurface, NULL, D3DTEXF_NONE);
	screenSurface->Release();

	mVolumetricClouds->Render(m_screenRaster);
	//mVolumetricLight->Render();

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

	mPostProcessing->RenderFXAA();
	//mPostProcessing->RenderBloom();
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

	_rwD3D9SetPixelShader(mPointSpotLightPixelShader->GetObject());

	Lights::SortByDistance(TheCamera.GetPosition().ToRwV3d());

	XMVECTOR value[4];


	XMMATRIX view, projection;

	RwD3D9GetTransform(D3DTS_VIEW, &view);
	view = XMMatrixInverse(NULL, view);

	RwD3D9GetTransform(D3DTS_PROJECTION, &projection);
	
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
									 DualParaboloidReflection::m_raster[1],
		mAmbientOcclusion->mSSAORaster};

	float fMipMapLODBias = -1000;
	for(size_t i = 0; i < 5; i++)
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

void DeferredRendering::AtmosphericScattering()
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

#include "imgui.h"
void DeferredRendering::imguiParameters()
{
	mVolumetricLight->UpdateImgui();
	mAmbientOcclusion->UpdateImgui();

	/*if(ImGui::BeginTabItem("Cloud"))
	{
		ImGui::EndTabItem();
		ImGui::InputFloat("VolumeBox_top", &VolumeBox_top, 2.0, 100.0);
		ImGui::InputFloat("VolumeBox_bottom", &VolumeBox_bottom, 2, 100.0);

		ImGui::InputFloat("Atomesphere_Distance", &Atomesphere_Distance, 100.0, 200.0);
		ImGui::InputFloat("Atomesphere_Smoothness", &Atomesphere_Smoothness, 100, 200.0);
		ImGui::InputFloat3("Atomesphere_Smoothness", Atomesphere, "%.1f");

		ImGui::InputFloat3("cloud_shift", cloud_shift, "%.1f");

		ImGui::InputFloat("BodyTop", &BodyTop,2.0, 100.0);
		ImGui::InputFloat("BodyMiddle", &BodyMiddle, 2, 100.0);
		ImGui::InputFloat("BodyBottom", &BodyBottom, 2.0, 100.0);
		ImGui::InputFloat("BodyThickness", &BodyThickness, 0.01, 0.1, "%.1f");
	}*/
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