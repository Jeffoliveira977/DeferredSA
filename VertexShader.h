#pragma once

#include "CommonD.h"

class VertexShader
{
public:
	VertexShader();
	~VertexShader();

	void CreateFromBinary(string file);
	void CreateFromFile(string file, string profile);

	void* GetShader();
private:
	void* mVertexShader;
};
