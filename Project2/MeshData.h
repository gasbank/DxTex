#ifndef __MESHDATA_H__
#define __MESHDATA_H__

#include <vector>
#include<DirectXMath.h>

struct Vertex3
{
	DirectX::XMFLOAT3 Position;
	DirectX::XMFLOAT3 Normal;
	DirectX::XMFLOAT3 TangentU;
	DirectX::XMFLOAT2 TexC;
};

struct MeshData
{
	std::vector<Vertex3> Vertices;
	std::vector<UINT> Indices32;
};

#endif // #ifndef __MESHDATA_H__
