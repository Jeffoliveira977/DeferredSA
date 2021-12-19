#pragma once
#include "CommonD.h"
#include "Frustum.h"
class SpotLight
{
public:

	SpotLight();
	void SetMatrix(XMMATRIX matrix);
	void SetColor(XMFLOAT3 color);
	void SetAngle(float angle);
	void SetPosition(XMFLOAT3 position);
	void SetDirection(XMFLOAT3 direction);
	void SetIntensity(float intensity);
	void SetRadius(float radius);

	XMMATRIX GetViewMatrix();
	XMMATRIX GetProjection();
	XMFLOAT3 GetColor();
	XMFLOAT3 GetDirection();
	XMFLOAT3 GetPosition();
	float GetIntensity();
	float GetAngle();
	float GetRadius();

	void Update();
private:
	Math::Frustum mFrustum;
	XMMATRIX mMatrix;
	XMMATRIX mView;
	XMMATRIX mProjection;
	XMFLOAT3 mColor;
	XMVECTOR mPosition;
	XMVECTOR mDirection;

	float mIntensity;
	float mAngle;
	float mRadius;
};

