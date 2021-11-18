#pragma once
#ifndef _OBJPARSER_H_
#define _OBJPARSER_H_

#include <Windows.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <array>

// 기본 wavefront .obj 파서
// 파일명, 정점, 색인 정보만 읽어옴.
// 매터리얼 정보는 읽지 않음
// https://en.wikipedia.org/wiki/Wavefront_.obj_file
// 사용법: ObjModel model = ObjParse("cube.obj");

// v [x] [y] [z]
typedef struct _Vector
{
	float x, y, z;
} ObjVector;

typedef ObjVector ObjVertex;
typedef ObjVector ObjVertexTexCoord;
typedef ObjVector ObjVertexNormal;

// f [v0]/[vt0]/[vn0] [v1]/[vt1]/[vn1] [v2]/[vt2]/[vn2]
typedef struct _ObjFace
{
	std::array<int, 3> vs;
	std::array<int, 3> vts;
	std::array<int, 3> vns;
} ObjFace;

class ObjModel
{
public:
	std::string name;

	std::vector<ObjVertex> vertices;

	std::vector<ObjFace> faces;
	std::vector<ObjVertexNormal> vertexNormalVectors;
	std::vector<ObjVertexNormal> vertexTexCoordVectors;

	// 면을 1차원 벡터로 변환.
	std::vector<int> indices;
	std::vector<int> vertexTexCoords;
	std::vector<int> vertexNormals;

public:
	ObjModel() { }
	void PrintInfo()
	{
		std::cout << "name: " << name << std::endl;
		std::cout << "vertices count: " << vertices.size() << std::endl;
		std::cout << "indices count: " << indices.size() << std::endl;
	}
};

std::vector<std::string> SplitString(std::string input, const char* sep);
ObjModel ObjParse(LPCWSTR fileName);

#endif
