#include "D3D9BaseTexture.h"

IDirect3DTexture9* D3D9BaseTexture::GetObject()
{
	return mD3D9Texture;
}

D3D9BaseTexture::D3D9BaseTexture(RwRaster* raster)
{
	mRaster = raster;
	mD3D9Texture = nullptr;
}

D3D9BaseTexture::~D3D9BaseTexture()
{
	mRaster = nullptr;
}

void D3D9BaseTexture::Initialize()
{
}

void D3D9BaseTexture::Unitialize()
{
}

void D3D9BaseTexture::Lock(uint flags, uint level, void* pixelsIn)
{
}

void D3D9BaseTexture::Unlock()
{
}
