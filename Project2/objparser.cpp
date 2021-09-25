#include "objparser.h"

std::vector<std::string> SplitString(std::string input, const char* sep)
{
	std::vector<std::string> result;
	char* temp = nullptr;
	char* next = nullptr;

	temp = strtok_s((char*)input.data(), sep, &next);
	do
	{
		result.push_back(temp);
	} while (temp = strtok_s(next, sep, &next));

	return result;
}

ObjModel ObjParse(LPCWSTR fileName)
{
	std::ifstream objFile(fileName);
	std::string objText;

	ObjModel o;

	std::string sep = " \n"; // ����, ���ο�
	std::string sep_index = "/"; // ���� �� ����(v), �����ؽ���(vt), �������(vn)

	while (std::getline(objFile, objText))
	{
		//cout << objText;
		if (objText[0] == 'g')
		{
			auto splitted = SplitString(objText, sep.c_str());
			o.name = splitted[1];
		}
		else if (objText[0] == 'v')
		{
			if (objText[1] == 'n')
			{
				auto splitted = SplitString(objText, sep.c_str());
				ObjVertexNormal vn = { };
				vn.x = stof(splitted[1]);
				vn.y = stof(splitted[2]);
				vn.z = stof(splitted[3]);
				o.vertexNormalVectors.push_back(vn);
			}
			else if (objText[1] == 't')
			{
				auto splitted = SplitString(objText, sep.c_str());
				ObjVertexTexCoord vt = { };
				vt.x = stof(splitted[1]);
				vt.y = stof(splitted[2]);
				vt.z = stof(splitted[3]);
				o.vertexTexCoordVectors.push_back(vt);
			}
			else
			{
				auto splitted = SplitString(objText, sep.c_str());
				ObjVertex v = { };
				v.x = stof(splitted[1]);
				v.y = stof(splitted[2]);
				v.z = stof(splitted[3]);
				o.vertices.push_back(v);
			}
		}
		else if (objText[0] == 'f')
		{
			auto splitted = SplitString(objText, sep.c_str()); //f 0/0/0 1/1/1 2/2/2
			ObjFace f = { };

			// �� �� f ����
			splitted.erase(splitted.begin());
			for (size_t i = 0; i < splitted.size(); i++)
			{
				auto faceIndices = SplitString(splitted[i], sep.c_str());

				for (size_t j = 0; j < faceIndices.size(); j++)
				{
					if (faceIndices[j].find("/") != std::string::npos)
					{
						auto vInfos = SplitString(faceIndices[j], sep_index.c_str());

						// �⺻ ���ؽ� ���� �Է�
						f.vs[i] = stoi(vInfos[0]);

						auto slashes = count(faceIndices[j].begin(), faceIndices[j].end(), '/');

						// ������ 3���̰� �����ð� 2�� => vt, vn
						if (vInfos.size() == 3)
						{
							f.vns[i] = stoi(vInfos[2]);
						}
						else
						{
							// ������ 2���� ���
							if (slashes == 2)
							{
								// �����ð� 2�� => vn
								f.vns[i] = stoi(vInfos[1]);
							}
							else
							{
								// �����ð� 1�� => vt
								f.vts[i] = stoi(vInfos[1]);
							}
						}
					}
					else
					{
						f.vs[i] = stoi(faceIndices[j]);
					}
				}
			}

			o.faces.push_back(f);
		}
	}

	for (size_t i = 0; i < o.faces.size(); i++)
	{
		o.indices.push_back(o.faces[i].vs[0]);
		o.indices.push_back(o.faces[i].vs[1]);
		o.indices.push_back(o.faces[i].vs[2]);

		o.vertexTexCoords.push_back(o.faces[i].vts[0]);
		o.vertexTexCoords.push_back(o.faces[i].vts[1]);
		o.vertexTexCoords.push_back(o.faces[i].vts[2]);

		o.vertexNormals.push_back(o.faces[i].vns[0]);
		o.vertexNormals.push_back(o.faces[i].vns[1]);
		o.vertexNormals.push_back(o.faces[i].vns[2]);
	}

	objFile.close();
	return o;
}