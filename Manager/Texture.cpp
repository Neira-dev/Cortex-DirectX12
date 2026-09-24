#include "Texture.h"
#include "DirectX12.h"
#include <cctype>
#include "../AssetPackCore/GlobalPak.h"

#pragma comment(lib, "dxguid.lib")

Texture* Texture::instance = nullptr;
const string Texture::DefaultWhiteKey = "__default_white__";
const string Texture::DefaultBlackKey = "__default_black__";
const string Texture::DefaultNormalKey = "__default_normal__";

Texture* Texture::Get()
{
	if (instance == nullptr)
	{
		instance = new Texture();
	}
	return instance;
}

void Texture::Del()
{
	if (instance != nullptr)
	{
		delete instance;
		instance = nullptr;
	}
}

bool Texture::Load(const string& filePath)
{
	if (IsLoadedOrPending(filePath))
	{
		return true;
	}

	return LoadInternal(filePath);
}

bool Texture::LoadDefault(const string& key, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
	if (IsLoadedOrPending(key))
	{
		return true;
	}

	D3D12_RESOURCE_DESC texDesc = {};
	texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	texDesc.Width = 1;
	texDesc.Height = 1;
	texDesc.DepthOrArraySize = 1;
	texDesc.MipLevels = 1;
	texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	texDesc.SampleDesc.Count = 1;
	texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

	uint8_t pixel[4] = { r, g, b, a };

	std::vector<D3D12_SUBRESOURCE_DATA> subResources(1);
	subResources[0].pData = pixel;
	subResources[0].RowPitch = 4;
	subResources[0].SlicePitch = 4;

	return BeginUpload(key, texDesc, subResources);
}

bool Texture::LoadDefaults()
{
	bool ok = true;
	ok = LoadDefault(DefaultWhiteKey, 255, 255, 255, 255) && ok;
	ok = LoadDefault(DefaultBlackKey, 0, 0, 0, 255) && ok;
	ok = LoadDefault(DefaultNormalKey, 128, 128, 255, 255) && ok;
	return ok;
}

bool Texture::LoadInternal(const string& filePath)
{
	// 拡張子がhdrならRadiance(.hdr)形式として読み込む(それ以外は通常のWIC画像として読み込む)
	bool isHdr = false;
	size_t dotPos = filePath.find_last_of('.');
	if (dotPos != string::npos)
	{
		string ext = filePath.substr(dotPos + 1);
		for (char& c : ext)
		{
			c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
		}
		isHdr = (ext == "hdr");
	}

	// パックアーカイブ(.cpak)が開いていてこの仮想パスを含む場合は、復号済みバイト列を
	// メモリから読み込む。開いていない/含まれない場合は従来通りルーズファイルを読む。
	std::vector<uint8_t> pakBytes;
	if (AssetPack::IsGlobalPakOpen() && AssetPack::GlobalPakReader().Has(filePath))
	{
		pakBytes = AssetPack::GlobalPakReader().Read(filePath);
	}

	DirectX::ScratchImage scratch;
	HRESULT hr;
	if (!pakBytes.empty())
	{
		hr = isHdr
			? DirectX::LoadFromHDRMemory(pakBytes.data(), pakBytes.size(), nullptr, scratch)
			: DirectX::LoadFromWICMemory(pakBytes.data(), pakBytes.size(), DirectX::WIC_FLAGS_NONE, nullptr, scratch);
	}
	else
	{
		std::wstring wFilePath(filePath.begin(), filePath.end());
		hr = isHdr
			? DirectX::LoadFromHDRFile(wFilePath.c_str(), nullptr, scratch)
			: DirectX::LoadFromWICFile(wFilePath.c_str(), DirectX::WIC_FLAGS_NONE, nullptr, scratch);
	}
	if (FAILED(hr))
	{
		return false;
	}

	// ミップマップ生成
	DirectX::ScratchImage mipChain;
	hr = DirectX::GenerateMipMaps(
		*scratch.GetImages(),
		DirectX::TEX_FILTER_DEFAULT,
		0, // 0 = 最大ミップレベルまで生成する
		mipChain
	);
	if (FAILED(hr))
	{
		return false;
	}

	const DirectX::TexMetadata& meta = mipChain.GetMetadata();

	D3D12_RESOURCE_DESC texDesc = {};
	texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	texDesc.Width = meta.width;
	texDesc.Height = static_cast<UINT>(meta.height);
	texDesc.DepthOrArraySize = 1;
	texDesc.MipLevels = static_cast<UINT16>(meta.mipLevels);
	texDesc.Format = meta.format;
	texDesc.SampleDesc.Count = 1;
	texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

	// サブリソースはmipChainが持つCPUメモリを指しているだけなので、
	// この関数を抜ける前にBeginUpload内でUploadヒープへコピーし終える
	std::vector<D3D12_SUBRESOURCE_DATA> subResources(meta.mipLevels);
	for (size_t i = 0; i < meta.mipLevels; i++)
	{
		const DirectX::Image* img = mipChain.GetImage(i, 0, 0);
		subResources[i].pData = img->pixels;
		subResources[i].RowPitch = img->rowPitch;
		subResources[i].SlicePitch = img->slicePitch;
	}

	return BeginUpload(filePath, texDesc, subResources);
}

bool Texture::IsLoadedOrPending(const string& key) const
{
	if (m_textureMap.find(key) != m_textureMap.end())
	{
		return true;
	}

	for (const auto& pending : m_pendingTextures)
	{
		if (pending.key == key)
		{
			return true;
		}
	}

	return false;
}

bool Texture::BeginUpload(const string& key, const D3D12_RESOURCE_DESC& texDesc, std::vector<D3D12_SUBRESOURCE_DATA>& subResources)
{
	// ------------------------------------------------------------
	// 1. Copy用コマンドリストが未オープンなら、このバッチの先頭として開く
	//    (すでに開いている場合は他のテクスチャの転送命令に連結して記録する)
	// ------------------------------------------------------------
	if (!m_copyBatchOpen)
	{
		DirectX::DirectX12::Get()->BeginCopy();
		m_copyBatchOpen = true;
	}
	auto* cmdList = DirectX::DirectX12::Get()->GetCommandList(DirectX::DirectX12::CmdListType::Copy);

	PendingTexture pending;
	pending.key = key;

	// ------------------------------------------------------------
	// 2. Defaultヒープにテクスチャ本体を作成(VRAM常駐)
	// ------------------------------------------------------------
	CD3DX12_HEAP_PROPERTIES defaultHeap(D3D12_HEAP_TYPE_DEFAULT);

	HRESULT hr = DirectX::DirectX12::Get()->GetDevice()->CreateCommittedResource(
		&defaultHeap,
		D3D12_HEAP_FLAG_NONE,
		&texDesc,
		D3D12_RESOURCE_STATE_COPY_DEST,
		nullptr,
		IID_PPV_ARGS(&pending.data.texture)
	);
	if (FAILED(hr))
	{
		return false;
	}

	// ------------------------------------------------------------
	// 3. Uploadヒープに中継バッファを作成し、CPUデータを書き込む
	// ------------------------------------------------------------
	UINT64 uploadSize = 0;
	DirectX::DirectX12::Get()->GetDevice()->GetCopyableFootprints(
		&texDesc, 0, static_cast<UINT>(subResources.size()),
		0, nullptr, nullptr, nullptr, &uploadSize
	);

	ComPtr<ID3D12Resource> uploadBuffer;
	CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);
	CD3DX12_RESOURCE_DESC uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadSize);

	hr = DirectX::DirectX12::Get()->GetDevice()->CreateCommittedResource(
		&uploadHeap,
		D3D12_HEAP_FLAG_NONE,
		&uploadDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&uploadBuffer)
	);
	if (FAILED(hr))
	{
		return false;
	}

	// ------------------------------------------------------------
	// 4. Copyコマンドリストへ「Uploadヒープ→Defaultヒープ」のコピー命令を記録する
	//    (ここではExecute/Waitは行わない。GPUへの送信はFlush()でまとめて行う)
	// ------------------------------------------------------------
	UpdateSubresources(
		cmdList,
		pending.data.texture.Get(),
		uploadBuffer.Get(),
		0, 0,
		static_cast<UINT>(subResources.size()),
		subResources.data()
	);

	// uploadBufferはコピー完了(Flush内のEndCopyAndWait)までCPU側で参照を保持しておく必要がある
	m_pendingUploadBuffers.push_back(uploadBuffer);
	m_pendingTextures.push_back(std::move(pending));

	return true;
}

bool Texture::Flush()
{
	if (!m_copyBatchOpen)
	{
		return true; // 積まれている転送はない
	}

	// ------------------------------------------------------------
	// 1. ここまでに積んだ転送命令をまとめて実行し、一度だけ完了を待つ
	// ------------------------------------------------------------
	DirectX::DirectX12::Get()->EndCopyAndWait();
	m_pendingUploadBuffers.clear(); // 転送が完了したのでUploadバッファは解放してよい

	// ------------------------------------------------------------
	// 2. バリア: COPY_DEST -> PIXEL_SHADER_RESOURCE
	//    Copy Queueでは直接この状態遷移ができないため、Direct Queueでまとめて実行する
	// ------------------------------------------------------------
	DirectX::DirectX12::Get()->BeginCopy(DirectX::DirectX12::CmdListType::Direct);
	auto* directList = DirectX::DirectX12::Get()->GetCommandList(DirectX::DirectX12::CmdListType::Direct);

	std::vector<D3D12_RESOURCE_BARRIER> barriers;
	barriers.reserve(m_pendingTextures.size());
	for (auto& pending : m_pendingTextures)
	{
		barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(
			pending.data.texture.Get(),
			D3D12_RESOURCE_STATE_COPY_DEST,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
		));
	}
	directList->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());

	DirectX::DirectX12::Get()->EndCopyAndWait(DirectX::DirectX12::CmdListType::Direct);

	// ------------------------------------------------------------
	// 3. SRVを作成し、管理マップへ登録する
	// ------------------------------------------------------------
	bool ok = true;
	for (auto& pending : m_pendingTextures)
	{
		if (!CreateSRV(pending.data))
		{
			ok = false;
			continue;
		}
		m_textureMap[pending.key] = std::move(pending.data);
	}

	m_pendingTextures.clear();
	m_copyBatchOpen = false;

	return ok;
}

bool Texture::CreateSRV(TextureData& data)
{
	// ディスクリプタのインデックスを確保
	DirectX::Descriptor* heap = DirectX::Descriptor::Get();
	data.srvIndex = heap->Allocate();
	if (data.srvIndex == UINT_MAX)
	{
		return false;
	}

	// SRV記述
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = data.texture->GetDesc().Format;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Texture2D.MipLevels = data.texture->GetDesc().MipLevels;

	DirectX::DirectX12::Get()->GetDevice()->CreateShaderResourceView(
		data.texture.Get(),
		&srvDesc,
		heap->GetCPUHandle(data.srvIndex)
	);

	return true;
}

D3D12_GPU_DESCRIPTOR_HANDLE Texture::GetSRVGpuHandle(const string& filePath)
{
	auto it = m_textureMap.find(filePath);
	if (it == m_textureMap.end())
	{
		// 未確定(Flush前)のテクスチャが要求された場合はここで確定させる
		Flush();
		it = m_textureMap.find(filePath);
		if (it == m_textureMap.end())
		{
			return {};
		}
	}

	DirectX::Descriptor* heap = DirectX::Descriptor::Get();
	return heap->GetGPUHandle(it->second.srvIndex);
}

ID3D12Resource* Texture::GetTexture(const string& filePath)
{
	auto it = m_textureMap.find(filePath);
	if (it == m_textureMap.end())
	{
		Flush();
		it = m_textureMap.find(filePath);
		if (it == m_textureMap.end())
		{
			return nullptr;
		}
	}
	return it->second.texture.Get();
}