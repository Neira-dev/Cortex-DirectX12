#include "Mesh.h"
#define NOMINMAX
#include "DirectX12.h"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <cstdint>
#include "../AssetPackCore/GlobalPak.h"
#include "../AssetPackCore/PathHash.h"

Mesh* Mesh::instance = nullptr;

namespace
{
	// テクスチャパスをモデルファイルからの相対パスとして解決する
	std::string ResolveTexturePath(const std::string& modelFilePath, const std::string& texturePath)
	{
		if (texturePath.empty())
		{
			return texturePath;
		}

		// ドライブレター付き絶対パス、UNCパス、ルートパスはそのまま使用する
		bool isAbsolute =
			(texturePath.size() >= 2 && texturePath[1] == ':') ||
			(texturePath.size() >= 2 && (texturePath[0] == '\\' || texturePath[0] == '/') && (texturePath[1] == '\\' || texturePath[1] == '/'));

		if (isAbsolute)
		{
			return texturePath;
		}

		size_t texNamePos = texturePath.find_last_of("\\/");
		std::string texFileName = (texNamePos == std::string::npos) ? texturePath : texturePath.substr(texNamePos + 1);

		size_t pos = modelFilePath.find_last_of("\\/");
		std::string modelDir = (pos == std::string::npos) ? "" : modelFilePath.substr(0, pos + 1);

		return modelDir + texFileName;
	}
}

Mesh* Mesh::Get()
{
	if (instance == nullptr)
	{
		instance = new Mesh();
	}
	return instance;
}

void Mesh::Del()
{
	if (instance != nullptr)
	{
		delete instance;
		instance = nullptr;
	}
}

bool Mesh::Load(const std::string& FILEPATH)
{
	Assimp::Importer importer;

	const aiScene* scene = nullptr;
	const unsigned int flags = aiProcess_Triangulate | aiProcess_FlipUVs;

	// If a packed asset archive (.cpak) is open and contains this virtual path,
	// read the (decrypted) bytes into memory and parse from there instead of
	// touching the loose file on disk. Falls back to the loose file when the
	// path simply is not packed (normal during dev-time loose-file work), so
	// unpacked/dev-time asset paths keep working exactly as before. If the
	// path IS packed but the read itself fails (corrupted/truncated .cpak),
	// that is surfaced explicitly instead of being silently treated the same
	// as "not packed" -- falling through to a loose file in that case would
	// otherwise mask real pak corruption behind a generic "file not found".
	std::vector<uint8_t> pakBytes;
	AssetPack::PakReader::ReadResult pakResult = AssetPack::PakReader::ReadResult::NotFound;
	AssetPack::GlobalPak* pak = AssetPack::GlobalPak::Get();
	if (pak->IsOpen())
	{
		pakResult = pak->Reader().TryRead(FILEPATH, pakBytes);
	}

	if (pakResult == AssetPack::PakReader::ReadResult::Failed)
	{
		std::string warning = "AssetPack: failed to read a packed asset (pak may be corrupted): " + FILEPATH;
		MessageBox(nullptr, warning.c_str(), "AssetPack Warning", MB_OK | MB_ICONWARNING);
	}

	if (pakResult == AssetPack::PakReader::ReadResult::Success)
	{
		size_t dotPos = FILEPATH.find_last_of('.');
		std::string hint = (dotPos == std::string::npos) ? "" : FILEPATH.substr(dotPos + 1);
		scene = importer.ReadFileFromMemory(pakBytes.data(), pakBytes.size(), flags, hint.c_str());
	}
	else
	{
		scene = importer.ReadFile(FILEPATH, flags);
	}

	if (scene == nullptr)
	{
		std::string error = importer.GetErrorString();
		MessageBox(nullptr, error.c_str(), "Assimp Error", MB_OK);
		return false;
	}

	if (scene->mRootNode == nullptr)
	{
		MessageBox(nullptr, "Scene has no root node", "Assimp Error", MB_OK);
		return false;
	}

	int meshCount = scene->mNumMeshes;

	IMesh meshData;
	meshData.subMeshes.reserve(meshCount);

	for (int i = 0; i < meshCount; i++)
	{
		aiMesh* mesh = scene->mMeshes[i];
		ISubMesh subMesh;

		subMesh.vertices.reserve(mesh->mNumVertices);
		subMesh.indices.reserve(mesh->mNumFaces * 3);

		subMesh.SubMeshName = mesh->mName.C_Str();

		for (UINT i = 0; i < mesh->mNumVertices; i++)
		{
			ShaderLayout::Default vertex;

			vertex.position = { mesh->mVertices[i].x, mesh->mVertices[i].y, mesh->mVertices[i].z };

			if (mesh->HasNormals())
			{
				vertex.normal = { mesh->mNormals[i].x, mesh->mNormals[i].y, mesh->mNormals[i].z };
			}
			else
			{
				vertex.normal = { 0.0f, 1.0f, 0.0f };
			}
			if (mesh->HasTextureCoords(0))
			{
				vertex.uv = { mesh->mTextureCoords[0][i].x, mesh->mTextureCoords[0][i].y };
			}
			else
			{
				vertex.uv = { 0.0f, 0.0f };
			}
			subMesh.vertices.push_back(vertex);
		}

		for (UINT i = 0; i < mesh->mNumFaces; i++)
		{
			aiFace face = mesh->mFaces[i];
			for (UINT j = 0; j < face.mNumIndices; j++)
			{
				subMesh.indices.push_back(face.mIndices[j]);
			}
		}

		{
			aiMaterial* material = scene->mMaterials[mesh->mMaterialIndex];
			aiString texturePath;
			aiReturn ret = material->GetTexture(aiTextureType_DIFFUSE, 0, &texturePath);
			if (ret == AI_SUCCESS)
			{
				subMesh.texturePath.Diffuse = ResolveTexturePath(FILEPATH, texturePath.C_Str());
			}
			else
			{
				subMesh.texturePath.Diffuse = "";
			}
			ret = material->GetTexture(aiTextureType_HEIGHT, 0, &texturePath);
			if (ret == AI_SUCCESS)
			{
				subMesh.texturePath.Normal = ResolveTexturePath(FILEPATH, texturePath.C_Str());
			}
			else
			{
				subMesh.texturePath.Normal = "";
			}
			ret = material->GetTexture(aiTextureType_SPECULAR, 0, &texturePath);
			if (ret == AI_SUCCESS)
			{
				subMesh.texturePath.Specular = ResolveTexturePath(FILEPATH, texturePath.C_Str());
			}
			else
			{
				subMesh.texturePath.Specular = "";
			}
			ret = material->GetTexture(aiTextureType_EMISSIVE, 0, &texturePath);
			if (ret == AI_SUCCESS)
			{
				subMesh.texturePath.Emissive = ResolveTexturePath(FILEPATH, texturePath.C_Str());
			}
			else
			{
				subMesh.texturePath.Emissive = "";
			}
			subMesh.texturePath.Metalness = "";
			ret = material->GetTexture(aiTextureType_AMBIENT, 0, &texturePath);
			if (ret == AI_SUCCESS)
			{
				subMesh.texturePath.AmbientOcclusion = ResolveTexturePath(FILEPATH, texturePath.C_Str());
			}
			else
			{
				subMesh.texturePath.AmbientOcclusion = "";
			}
		}

		{
			size_t vertexBufferSize = sizeof(ShaderLayout::Default) * subMesh.vertices.size();
			if (vertexBufferSize == 0 || subMesh.indices.empty())
			{
				MessageBox(nullptr, "Mesh has no vertices or indices", "Mesh Error", MB_OK);
				continue;
			}

			subMesh.vertexBuffer = DirectX::CreateUploadBuffer(vertexBufferSize, subMesh.vertices.data());
			if (subMesh.vertexBuffer == nullptr)
			{
				MessageBox(nullptr, "Vertex buffer creation failed", "Mesh Error", MB_OK);
				continue;
			}

			subMesh.vertexBufferView.BufferLocation = subMesh.vertexBuffer->GetGPUVirtualAddress();
			subMesh.vertexBufferView.SizeInBytes = static_cast<UINT>(vertexBufferSize);
			subMesh.vertexBufferView.StrideInBytes = sizeof(ShaderLayout::Default);

			size_t indexBufferSize = sizeof(uint32_t) * subMesh.indices.size();
			subMesh.indexBuffer = DirectX::CreateUploadBuffer(indexBufferSize, subMesh.indices.data());
			if (subMesh.indexBuffer == nullptr)
			{
				MessageBox(nullptr, "Index buffer creation failed", "Mesh Error", MB_OK);
				continue;
			}

			subMesh.indexBufferView.BufferLocation = subMesh.indexBuffer->GetGPUVirtualAddress();
			subMesh.indexBufferView.SizeInBytes = static_cast<UINT>(indexBufferSize);
			subMesh.indexBufferView.Format = DXGI_FORMAT_R32_UINT;
		}
		meshData.subMeshes.push_back(std::move(subMesh));
	}

	m_meshMap[AssetPack::NormalizePath(FILEPATH)] = meshData;

	return true;
}

const IMesh* Mesh::GetMeshData(const std::string& name)
{
	auto it = m_meshMap.find(AssetPack::NormalizePath(name));
	if (it != m_meshMap.end())
	{
		return &(it->second);
	}
	else
	{
		return nullptr;
	}
}
