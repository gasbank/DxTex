#include<Windows.h>
#include<d3d12.h>
#include"d3dx12.h"
#include<dxgi1_6.h>
#include<dxgidebug.h>
#include<d3dcompiler.h>
#include<DirectXColors.h>
#include<DirectXMath.h>
#include<iostream>
#include<string>
#include"DDSTextureLoader12.h"
#include <comdef.h>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "d3dcompiler.lib")

using namespace DirectX;

#define COM_RELEASE(v) if (v != nullptr) v->Release()

#pragma region Exception Handling
class DxException
{
public:
	DxException() = default;
	DxException(HRESULT hr, const std::wstring& functionName, const std::wstring& filename, int lineNumber);

	std::wstring ToString()const;

	HRESULT ErrorCode = S_OK;
	std::wstring FunctionName;
	std::wstring Filename;
	int LineNumber = -1;
};

DxException::DxException(HRESULT hr, const std::wstring& functionName, const std::wstring& filename, int lineNumber) :
	ErrorCode(hr),
	FunctionName(functionName),
	Filename(filename),
	LineNumber(lineNumber)
{
}

std::wstring DxException::ToString()const
{
	// Get the string description of the error code.
	_com_error err(ErrorCode);
	std::wstring msg = err.ErrorMessage();

	return FunctionName + L" failed in " + Filename + L"; line " + std::to_wstring(LineNumber) + L"; error: " + msg;
}

#ifndef ThrowIfFailed
#define ThrowIfFailed(x)                                              \
{                                                                     \
    HRESULT hr__ = (x);                                               \
    std::wstring wfn = AnsiToWString(__FILE__);                       \
    if(FAILED(hr__)) { throw DxException(hr__, L#x, wfn, __LINE__); } \
}
#endif

inline std::wstring AnsiToWString(const std::string& str)
{
	WCHAR buffer[512];
	MultiByteToWideChar(CP_ACP, 0, str.c_str(), -1, buffer, 512);
	return std::wstring(buffer);
}

#pragma endregion

// ����� ���̾�
ID3D12Debug* gDebug = nullptr;

// ��ġ
ID3D12Device* gDevice = nullptr;

// ���� ü�� ������ ���� dxgi���丮
IDXGIFactory4* gFactory = nullptr;

// �潺
ID3D12Fence* gFence = nullptr;
// ���� �潺 ����
UINT64 gCurrentFence = 0;

// Ŀ�ǵ� 3����
ID3D12CommandQueue* gCommandQueue = nullptr;
ID3D12CommandAllocator* gCommandAlloc = nullptr;
ID3D12GraphicsCommandList* gCommandList = nullptr;

// ������ ���� ����ü��
IDXGISwapChain* gSwapChain = nullptr;

// ������ ����
const UINT SwapChainBufferCount = 2;

// ���� �ĸ� ���� �ε���
UINT gCurrentBufferIndex = 0;

// ���� ���ҽ��� �迭 - ���� ü�ΰ� �����
ID3D12Resource* gRenderBuffer[SwapChainBufferCount] = { nullptr };
// ���� ���ٽ� ���� ���ҽ� - ���� ���ٽ� ���� �����
//ID3D12Resource* gDepthStencilBuffer = nullptr;

// rtv, dsv, cbv��, srv�� �� �ڵ��� �׶��׶� ����� ����.
ID3D12DescriptorHeap* gRtvHeap = nullptr;
ID3D12DescriptorHeap* gDsvHeap = nullptr;
ID3D12DescriptorHeap* gCbvHeap = nullptr;
ID3D12DescriptorHeap* gSrvHeap = nullptr;

// rtv, dsv, cbv ���� ũ��
UINT gRtvHeapSize = 0;
UINT gDsvHeapSize = 0;
UINT gCbvHeapSize = 0;

// ����Ʈ
D3D12_VIEWPORT gViewport = { };

// ���� �簢��
D3D12_RECT gScissorRect = { };

// rtv�� ���� ����
DXGI_FORMAT gRenderBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
// dsv�� ���� ����
DXGI_FORMAT gDepthStencilFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

/*** �ʱ�ȭ ���� ***/
HRESULT InitD3D(HWND hWnd);

// InitD3D ���� �Լ�
void CreateDebug();
void CreateFactory();
void CreateDevice();
void CreateCommandObjects();
// ���� ���� ����(rtv, dsv, cbv)
void CreateHeaps();
void CreateSwapChain();
void CreateViewport();
void CreateScissorRect();
void CreateFence();

// DDS �ؽ��� ���� �ε�
void LoadScribbleTextureResource();

// rtv, dsv, srv ���� ���ҽ� ����
void CreateHeapResources();

/*** ���� ���� ���� ***/
void Update();

/*** �׸��� ���� ***/
// Ŀ�ǵ� ������Ʈ���� ����
void PopulateCommandList();
// �潺 ����
void FlushCommandQueue();
// �׸���
void Render();

/*** ���� ***/
void Release();

/*** �ﰢ�� ���� ***/

typedef struct _Vertex
{
	XMFLOAT3 position;
	XMFLOAT3 normal;
	XMFLOAT2 tex;
} Vertex;

ID3D12Resource* gVertexBuffer;
D3D12_VERTEX_BUFFER_VIEW gVertexBufferView = { };

ID3D12Resource* gIndexBuffer;
D3D12_INDEX_BUFFER_VIEW gIndexBufferView = { };
UINT gIndexCount;

ID3D12RootSignature* gRootSignature = nullptr;
ID3D12PipelineState* gPSO = nullptr;

ID3D12Resource* gScribbleTex = nullptr;

// �ʱ�ȭ
void CreateRootSignature();
void CreatePSO();

// �ﰢ�� �޽� �ʱ�ȭ �� ���ؽ� ���� ����
void InitTriangle();

// �ﰢ�� �׸���
void RenderTriangle();

// ��� ����

void InitConstantBuffer();

struct ObjectConstantBuffer
{
	XMFLOAT4X4 WorldViewProjection;
	XMFLOAT3 EyePos;
	char padding[256 - sizeof(WorldViewProjection) - sizeof(EyePos)];
};

ID3D12Resource* gConstantBuffer = nullptr;
ObjectConstantBuffer gConstantBufferData;
UINT8* gCbvDataBegin;

// �� �� ��������
XMFLOAT4X4 gWorld = XMFLOAT4X4(
	1.0f, 0.0f, 0.0f, 0.0f,
	0.0f, 1.0f, 0.0f, 0.0f,
	0.0f, 0.0f, 1.0f, 0.0f,
	0.0f, 0.0f, 0.0f, 1.0f);

XMFLOAT4X4 gView = XMFLOAT4X4(
	1.0f, 0.0f, 0.0f, 0.0f,
	0.0f, 1.0f, 0.0f, 0.0f,
	0.0f, 0.0f, 1.0f, 0.0f,
	0.0f, 0.0f, 0.0f, 1.0f);

XMFLOAT4X4 gProj = XMFLOAT4X4(
	1.0f, 0.0f, 0.0f, 0.0f,
	0.0f, 1.0f, 0.0f, 0.0f,
	0.0f, 0.0f, 1.0f, 0.0f,
	0.0f, 0.0f, 0.0f, 1.0f);

float gTheta;
float gPhi = XM_PIDIV4;
float gRadius = 5.0f;

// Ű �Է�
bool isLeftKeyPressed = false;
bool isRightKeyPressed = false;

/*** Win32 ���� ***/

HWND gHWnd = nullptr;
UINT gClientWidth = 800;
UINT gClientHeight = 600;

// Win32 �̺�Ʈ ���� �Լ�
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

int WINAPI wWinMain(
	_In_ HINSTANCE hInstance,
	_In_opt_ HINSTANCE hPrevInstance,
	_In_ LPWSTR    lpCmdLine,
	_In_ int       nCmdShow)
{
	LPCWSTR className = L"DX12Triangle!";

	WNDCLASS wc;
	wc.style = 0;
	wc.lpfnWndProc = WndProc;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hInstance = 0;
	wc.hIcon = LoadIcon(NULL, IDI_WINLOGO);
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = 0;
	wc.lpszMenuName = 0;
	wc.lpszClassName = className;

	ThrowIfFailed(RegisterClass(&wc));

	gHWnd = CreateWindow(
		className,
		className,
		WS_OVERLAPPEDWINDOW | WS_VISIBLE, //WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, CW_USEDEFAULT, gClientWidth, gClientHeight,
		nullptr,
		nullptr,
		hInstance,
		nullptr);

	if (gHWnd == nullptr)
	{
		OutputDebugString(L"Failed to create hWnd");
		return 1;
	}

	InitD3D(gHWnd);

	// CreateWindow�� dwStyle�� WS_VISIBLE�� �־��ٸ� �ʿ� ����.
	//ShowWindow(gHWnd, nCmdShow);

	// WM_PAINT �޽����� ���� ȣ��. ���� ���� �� �׷���� ������ DX�̹Ƿ� �ʿ� ����.
	//UpdateWindow(gHWnd);

	MSG msg = { 0 };

	while (msg.message != WM_QUIT)
	{
		if (PeekMessage(&msg, 0, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		else
		{
			Update();
			Render();
		}
	}

	Release();

	return 0;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_KEYDOWN:
		// ESC�� ������ ������ �� �ְ� ó��.
		if (wParam == VK_ESCAPE)
		{
			PostQuitMessage(0);
		}
		else if (wParam == VK_LEFT)
		{
			isLeftKeyPressed = true;
		}
		else if (wParam == VK_RIGHT)
		{
			isRightKeyPressed = true;
		}
		break;
	case WM_KEYUP:
		if (wParam == VK_LEFT)
		{
			isLeftKeyPressed = false;
		}
		else if (wParam == VK_RIGHT)
		{
			isRightKeyPressed = false;
		}
		break;
	case WM_DESTROY:
		// PostQuitMessage()�� �޽����� WM_QUIT�� �ִ´�.
		PostQuitMessage(0);
		break;
	case WM_SIZE:
		gClientWidth = LOWORD(lParam);
		gClientHeight = HIWORD(lParam);
		break;
	}

	return DefWindowProc(hWnd, message, wParam, lParam);
}

HRESULT InitD3D(HWND hWnd)
{
	gHWnd = hWnd;

	CreateDebug();
	CreateFactory();
	CreateDevice();

	// TODO: �ﰢ���� �׸��� ���� ��Ʈ ����� ���������� ���� ��ü
	CreateRootSignature();
	CreatePSO();

	LoadScribbleTextureResource();

	CreateCommandObjects();
	CreateHeaps();
	CreateSwapChain();
	CreateHeapResources();
	CreateFence();

	InitTriangle();

	InitConstantBuffer();

	CreateViewport();
	CreateScissorRect();

	return S_OK;
}

void CreateDebug()
{
#ifdef _DEBUG
	ThrowIfFailed(D3D12GetDebugInterface(IID_PPV_ARGS(&gDebug)));
	gDebug->EnableDebugLayer();
#endif
}

void CreateFactory()
{
	ThrowIfFailed(CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, IID_PPV_ARGS(&gFactory)));
}

void CreateDevice()
{
	ThrowIfFailed(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&gDevice)));
}

void CreateCommandObjects()
{
	// ���� �⺻���̹Ƿ� �׳� �־ �ȴ�.
	D3D12_COMMAND_QUEUE_DESC queueDesc = { };
	//queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	//queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

	ThrowIfFailed(gDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&gCommandQueue)));
	ThrowIfFailed(gDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&gCommandAlloc)));
	// TODO: Ŀ�ǵ� ����Ʈ�� �ʱ�ȭ�� �� ���������� ���� ��ü�� �־��ش�.
	ThrowIfFailed(gDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, gCommandAlloc, gPSO, IID_PPV_ARGS(&gCommandList)));

	// ������ �ݾ��ش�.
	ThrowIfFailed(gCommandList->Close());
}

void CreateHeaps()
{
	// ���� ���� ����

	// rtv
	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvHeapDesc.NumDescriptors = SwapChainBufferCount;

	ThrowIfFailed(gDevice->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&gRtvHeap)));

	// TODO: dsv
	{
	}

	// TODO: cbv
	D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc = {};
	cbvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	cbvHeapDesc.NumDescriptors = 1;
	cbvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

	ThrowIfFailed(gDevice->CreateDescriptorHeap(&cbvHeapDesc, IID_PPV_ARGS(&gCbvHeap)));

	// TODO: srv
	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.NumDescriptors = 1;
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

	ThrowIfFailed(gDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&gSrvHeap)));

	gRtvHeapSize = gDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	gDsvHeapSize = gDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
	gCbvHeapSize = gDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

void CreateSwapChain()
{
	DXGI_SWAP_CHAIN_DESC swapChainDesc = { };

	swapChainDesc.BufferDesc.Width = gClientWidth;
	swapChainDesc.BufferDesc.Height = gClientHeight;

	swapChainDesc.BufferDesc.RefreshRate.Numerator = 60;
	swapChainDesc.BufferDesc.RefreshRate.Denominator = 1;

	swapChainDesc.BufferDesc.Format = gRenderBufferFormat;
	swapChainDesc.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
	swapChainDesc.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;

	swapChainDesc.SampleDesc.Count = 1;
	swapChainDesc.SampleDesc.Quality = 0;

	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.BufferCount = 2;
	swapChainDesc.OutputWindow = gHWnd;
	swapChainDesc.Windowed = TRUE;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swapChainDesc.Flags = 0;

	ThrowIfFailed(gFactory->CreateSwapChain(gCommandQueue, &swapChainDesc, &gSwapChain));
}

void CreateViewport()
{
	gViewport = { };
	gViewport.TopLeftX = 0;
	gViewport.TopLeftY = 0;
	gViewport.Width = static_cast<FLOAT>(gClientWidth);
	gViewport.Height = static_cast<FLOAT>(gClientHeight);
	gViewport.MinDepth = 0;
	gViewport.MaxDepth = 1;
}

void CreateScissorRect()
{
	gScissorRect = { };
	gScissorRect.left = 0;
	gScissorRect.top = 0;
	gScissorRect.right = gClientWidth;
	gScissorRect.bottom = gClientHeight;
}

void CreateFence()
{
	ThrowIfFailed(gDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gFence)));
}

void CreateHeapResources()
{
	// rtv, dsv ���� ���� ���ҽ� ����

	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(gRtvHeap->GetCPUDescriptorHandleForHeapStart(), gCurrentBufferIndex, gRtvHeapSize);
	for (size_t i = 0; i < SwapChainBufferCount; i++)
	{
		ThrowIfFailed(gSwapChain->GetBuffer(i, IID_PPV_ARGS(&gRenderBuffer[i])));
		gDevice->CreateRenderTargetView(gRenderBuffer[i], nullptr, rtvHandle);
		rtvHandle.Offset(gRtvHeapSize);
	}

	// TODO: dsv
	{
	}

	// srv
	CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle(gSrvHeap->GetCPUDescriptorHandleForHeapStart());
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = gScribbleTex->GetDesc().Format;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = gScribbleTex->GetDesc().MipLevels;
	srvDesc.Texture2D.PlaneSlice = 0;
	srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
	gDevice->CreateShaderResourceView(gScribbleTex, &srvDesc, srvHandle);
}

void Update()
{
	// TODO: Ű �Է� �ޱ�
	{
	}
	/*
	const float translationSpeed = 0.0005f;
	const float offsetBound = 1.25f;

	gConstantBufferData.offset.x += translationSpeed;
	if (gConstantBufferData.offset.x > offsetBound)
	{
		gConstantBufferData.offset.x -= offsetBound * 2;
	}
	*/

	if (isLeftKeyPressed)
	{
		gTheta += 0.0005f;
	}

	if (isRightKeyPressed)
	{
		gTheta -= 0.0005f;
	}

	// ī�޶� ����
	float x = gRadius * sinf(gPhi) * cosf(gTheta);
	float y = gRadius * cosf(gPhi);
	float z = gRadius * sinf(gPhi) * sinf(gTheta);

	XMVECTOR pos = XMVectorSet(x, y, z, 1);
	XMVECTOR target = XMVectorZero();
	XMVECTOR up = XMVectorSet(0, 1, 0, 0);

	XMMATRIX view = XMMatrixLookAtLH(pos, target, up);
	XMStoreFloat4x4(&gView, view);

	float aspectRatio = (float)gClientWidth / gClientHeight;
	XMMATRIX proj = XMMatrixPerspectiveFovLH(XM_PIDIV2, aspectRatio, 1.0f, 1000.0f);
	XMStoreFloat4x4(&gProj, proj);

	XMMATRIX world = XMLoadFloat4x4(&gWorld);
	auto worldViewProjection = XMMatrixTranspose(world * view * proj);

	XMStoreFloat4x4(&gConstantBufferData.WorldViewProjection, worldViewProjection);
	XMStoreFloat3(&gConstantBufferData.EyePos, pos);

	memcpy(gCbvDataBegin, &gConstantBufferData, sizeof(gConstantBufferData));
}

void PopulateCommandList()
{
	// Ŀ�ǵ帮��Ʈ, �Ҵ��� ���� �� ���� ��� ���

	ThrowIfFailed(gCommandAlloc->Reset());
	// TODO: ���������� ���� ��ü �ֱ�
	ThrowIfFailed(gCommandList->Reset(gCommandAlloc, gPSO));

	// ��Ʈ ���� �ֱ�
	gCommandList->SetGraphicsRootSignature(gRootSignature);

	ID3D12DescriptorHeap* heaps[] = { gCbvHeap };
	gCommandList->SetDescriptorHeaps(_countof(heaps), heaps);

	gCommandList->SetGraphicsRootDescriptorTable(0, gCbvHeap->GetGPUDescriptorHandleForHeapStart());

	// RS
	gCommandList->RSSetViewports(1, &gViewport);
	gCommandList->RSSetScissorRects(1, &gScissorRect);

	// rtv ó��
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(gRtvHeap->GetCPUDescriptorHandleForHeapStart(), gCurrentBufferIndex, gRtvHeapSize);

	// OMSetRenderTargets�� render target�� �׸��� ���� �̸� �ڵ��� ���� ���� ���� ȣ���ؾ� �Ѵ�.
	gCommandList->OMSetRenderTargets(1, &rtvHandle, false, nullptr);

	gCommandList->ResourceBarrier(1,
		&CD3DX12_RESOURCE_BARRIER::Transition(
			gRenderBuffer[gCurrentBufferIndex], D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET)
	);

	// ���� ��� ���
	gCommandList->ClearRenderTargetView(rtvHandle, Colors::AliceBlue, 0, nullptr);

	// TODO: �ﰢ�� �׸���
	RenderTriangle();

	gCommandList->ResourceBarrier(1,
		&CD3DX12_RESOURCE_BARRIER::Transition(
			gRenderBuffer[gCurrentBufferIndex], D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT)
	);

	// TODO: dsv ó��
	{
	}

	ThrowIfFailed(gCommandList->Close());
}

void FlushCommandQueue()
{
	// �潺 �� ���� �� ���
	gCurrentFence++;

	ThrowIfFailed(gCommandQueue->Signal(gFence, gCurrentFence));

	if (gFence->GetCompletedValue() < gCurrentFence)
	{
		HANDLE eventHandle = CreateEvent(nullptr, false, false, nullptr);

		ThrowIfFailed(gFence->SetEventOnCompletion(gCurrentFence, eventHandle));
		WaitForSingleObject(eventHandle, INFINITE);
		CloseHandle(eventHandle);
	}
}

void Render()
{
	PopulateCommandList();

	ID3D12CommandList* cmdLists[] = { gCommandList };
	gCommandQueue->ExecuteCommandLists(_countof(cmdLists), cmdLists);

	// ù ��° ���ڴ� vsync�� ����
	ThrowIfFailed(gSwapChain->Present(0, 0));

	gCurrentBufferIndex = (gCurrentBufferIndex + 1) % SwapChainBufferCount;

	FlushCommandQueue();
}

void Release()
{
	COM_RELEASE(gConstantBuffer);
	COM_RELEASE(gVertexBuffer);
	COM_RELEASE(gIndexBuffer);
	COM_RELEASE(gScribbleTex);

	COM_RELEASE(gPSO);
	COM_RELEASE(gRootSignature);

	COM_RELEASE(gFence);

	for (size_t i = 0; i < SwapChainBufferCount; i++)
	{
		COM_RELEASE(gRenderBuffer[i]);
	}

	COM_RELEASE(gSwapChain);

	COM_RELEASE(gSrvHeap);
	COM_RELEASE(gCbvHeap);
	COM_RELEASE(gRtvHeap);

	COM_RELEASE(gCommandQueue);
	COM_RELEASE(gCommandAlloc);
	COM_RELEASE(gCommandList);

	COM_RELEASE(gDevice);
	COM_RELEASE(gFactory);

	COM_RELEASE(gDebug);

#if defined(_DEBUG)
	IDXGIDebug1* dxgiDebug;
	ThrowIfFailed(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&dxgiDebug)));
	dxgiDebug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_FLAGS(DXGI_DEBUG_RLO_SUMMARY | DXGI_DEBUG_RLO_IGNORE_INTERNAL));
	COM_RELEASE(dxgiDebug);
#endif
}

void CreateRootSignature()
{
	CD3DX12_DESCRIPTOR_RANGE range[1];
	CD3DX12_ROOT_PARAMETER rootParams[1];

	range[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);
	rootParams[0].InitAsDescriptorTable(1, &range[0]);

	ID3DBlob* signature = nullptr;
	ID3DBlob* error = nullptr;

	CD3DX12_STATIC_SAMPLER_DESC sampler(0);

	ThrowIfFailed(D3D12SerializeRootSignature(
		&CD3DX12_ROOT_SIGNATURE_DESC(_countof(rootParams), rootParams, 1, &sampler, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT),
		D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error));
	ThrowIfFailed(gDevice->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&gRootSignature)));
}

void CreatePSO()
{
	D3D12_INPUT_ELEMENT_DESC inputElementDesc[] =
	{
		{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, sizeof(XMFLOAT3), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, sizeof(XMFLOAT3) + sizeof(XMFLOAT2), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
	};

	ID3DBlob* vertexShader = nullptr;
	ID3DBlob* pixelShader = nullptr;
	ID3DBlob* error = nullptr;

	UINT compileFlags =
#if defined(_DEBUG)
		D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
		0;
#endif

	D3DCompileFromFile(L"shaders.hlsl", nullptr, nullptr, "VSMain", "vs_5_0", compileFlags, 0, &vertexShader, &error);
	auto errorBufferPtr1 = error->GetBufferPointer();
	OutputDebugStringA((LPCSTR)errorBufferPtr1);

	D3DCompileFromFile(L"shaders.hlsl", nullptr, nullptr, "PSMain", "ps_5_0", compileFlags, 0, &pixelShader, &error);
	auto errorBufferPtr2 = error->GetBufferPointer();
	OutputDebugStringA((LPCSTR)errorBufferPtr2);

	D3D12_GRAPHICS_PIPELINE_STATE_DESC psDesc = { };
	psDesc.pRootSignature = gRootSignature;
	psDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShader);
	psDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShader);
	psDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	psDesc.SampleMask = UINT_MAX;
	psDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	psDesc.DepthStencilState.DepthEnable = false;
	psDesc.DepthStencilState.StencilEnable = false;
	psDesc.InputLayout = { inputElementDesc, _countof(inputElementDesc) };
	psDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psDesc.NumRenderTargets = 1;
	psDesc.RTVFormats[0] = gRenderBufferFormat;
	psDesc.DSVFormat = gDepthStencilFormat;
	psDesc.SampleDesc.Count = 1;
	psDesc.SampleDesc.Quality = 0;

	ThrowIfFailed(gDevice->CreateGraphicsPipelineState(&psDesc, IID_PPV_ARGS(&gPSO)));
}

void InitTriangle()
{
	// �� ���ؽ� & �ε���
	Vertex triangleVertices[] =
	{
		// front face
		{{-1.0f, -1.0f, -1.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, 0.0f}},
		{{-1.0f, +1.0f, -1.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f}},
		{{+1.0f, +1.0f, -1.0f}, {0.0f, 0.0f, -1.0f}, {1.0f, 1.0f}},
		{{+1.0f, -1.0f, -1.0f}, {0.0f, 0.0f, -1.0f}, {1.0f, 0.0f}},

		// back face
		{{-1.0f, -1.0f, +1.0f}, {0.0f, 0.0f, 1.0f}},
		{{-1.0f, +1.0f, +1.0f}, {0.0f, 0.0f, 1.0f}},
		{{+1.0f, +1.0f, +1.0f}, {0.0f, 0.0f, 1.0f}},
		{{+1.0f, -1.0f, +1.0f}, {0.0f, 0.0f, 1.0f}},

		// left face
		{{-1.0f, -1.0f, +1.0f}, {-1.0f, 0.0f, 0.0f}},
		{{-1.0f, +1.0f, +1.0f}, {-1.0f, 0.0f, 0.0f}},
		{{-1.0f, +1.0f, -1.0f}, {-1.0f, 0.0f, 0.0f}},
		{{-1.0f, -1.0f, -1.0f}, {-1.0f, 0.0f, 0.0f}},

		// right face
		{{+1.0f, -1.0f, -1.0f}, {1.0f, 0.0f, 0.0f}},
		{{+1.0f, +1.0f, -1.0f}, {1.0f, 0.0f, 0.0f}},
		{{+1.0f, +1.0f, +1.0f}, {1.0f, 0.0f, 0.0f}},
		{{+1.0f, -1.0f, +1.0f}, {1.0f, 0.0f, 0.0f}},

		// top face
		{{-1.0f, +1.0f, -1.0f}, {0.0f, 1.0f, 0.0f}},
		{{-1.0f, +1.0f, +1.0f}, {0.0f, 1.0f, 0.0f}},
		{{+1.0f, +1.0f, +1.0f}, {0.0f, 1.0f, 0.0f}},
		{{+1.0f, +1.0f, -1.0f}, {0.0f, 1.0f, 0.0f}},

		// bottom face
		{{-1.0f, -1.0f, +1.0f}, {0.0f, -1.0f, 0.0f}},
		{{-1.0f, -1.0f, -1.0f}, {0.0f, -1.0f, 0.0f}},
		{{+1.0f, -1.0f, -1.0f}, {0.0f, -1.0f, 0.0f}},
		{{+1.0f, -1.0f, +1.0f}, {0.0f, -1.0f, 0.0f}},
	};

	UINT16 triangleIndices[] =
	{
		// front face
		0, 1, 2,
		0, 2, 3,

		// back face
		4, 6, 5,
		4, 7, 6,

		// left face
		8, 9, 10,
		8, 10, 11,

		// right face
		12, 13, 14,
		12, 14, 15,

		// top face
		16, 17, 18,
		16, 18, 19,

		// bottom face
		20, 21, 22,
		20, 22, 23
	};

	const UINT vertexBufferSize = sizeof(triangleVertices);
	const UINT indexBufferSize = sizeof(triangleIndices);
	gIndexCount = _countof(triangleIndices);


	// ���ؽ� ���� ����
	ThrowIfFailed(gDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&gVertexBuffer)
	));

	// ���ؽ� ���ۿ� �ﰢ�� ���� ����
	UINT8* pVertexDataBegin;
	ThrowIfFailed(gVertexBuffer->Map(0, nullptr, reinterpret_cast<void**>(&pVertexDataBegin)));
	memcpy(pVertexDataBegin, triangleVertices, sizeof(triangleVertices));
	gVertexBuffer->Unmap(0, nullptr);

	// ���ؽ� ���� �� ����
	gVertexBufferView.BufferLocation = gVertexBuffer->GetGPUVirtualAddress();
	gVertexBufferView.SizeInBytes = vertexBufferSize;
	gVertexBufferView.StrideInBytes = sizeof(Vertex);

	// �ε��� ���� ����
	ThrowIfFailed(gDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(indexBufferSize),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&gIndexBuffer)
	));

	// �ε��� ���ۿ� �ﰢ�� ���� ����
	UINT8* pIndexDataBegin;
	ThrowIfFailed(gIndexBuffer->Map(0, nullptr, reinterpret_cast<void**>(&pIndexDataBegin)));
	memcpy(pIndexDataBegin, triangleIndices, sizeof(triangleIndices));
	gIndexBuffer->Unmap(0, nullptr);

	// �ε��� ���� �� ����
	gIndexBufferView.BufferLocation = gIndexBuffer->GetGPUVirtualAddress();
	gIndexBufferView.SizeInBytes = indexBufferSize;
	gIndexBufferView.Format = DXGI_FORMAT_R16_UINT;

	FlushCommandQueue();
}

void RenderTriangle()
{
	// TODO: Input Assembly, Vertex Buffer View, Draw Instance
	gCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	gCommandList->IASetVertexBuffers(0, 1, &gVertexBufferView);
	gCommandList->IASetIndexBuffer(&gIndexBufferView);
	//D3D12 WARNING: ID3D12CommandList::DrawInstanced: Element [0] in the current Input Layout's declaration references input slot 0, but there is no Buffer bound to this slot. This is OK, as reads from an empty slot are defined to return 0. It is also possible the developer knows the data will not be used anyway. This is only a problem if the developer actually intended to bind an input Buffer here.  [ EXECUTION WARNING #202: COMMAND_LIST_DRAW_VERTEX_BUFFER_NOT_SET]
	gCommandList->DrawIndexedInstanced(gIndexCount, 1, 0, 0, 0);
}

void InitConstantBuffer()
{
	static_assert(sizeof(ObjectConstantBuffer) % 256 == 0, "constant buffer must be 256 byte");
	const UINT constantBufferSize = sizeof(ObjectConstantBuffer);

	ThrowIfFailed(gDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(constantBufferSize),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&gConstantBuffer)
	));

	D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = { };
	cbvDesc.BufferLocation = gConstantBuffer->GetGPUVirtualAddress();
	cbvDesc.SizeInBytes = constantBufferSize;
	gDevice->CreateConstantBufferView(&cbvDesc, gCbvHeap->GetCPUDescriptorHandleForHeapStart());

	gConstantBuffer->Map(0, nullptr, reinterpret_cast<void**>(&gCbvDataBegin));
	memcpy(gCbvDataBegin, &gConstantBufferData, sizeof(gConstantBufferData));
	//gConstantBuffer->Unmap(0, nullptr);
}

void LoadScribbleTextureResource()
{
	std::unique_ptr<uint8_t[]> ddsData;
	std::vector<D3D12_SUBRESOURCE_DATA> subresources;
	ThrowIfFailed(LoadDDSTextureFromFile(gDevice, L"scribble.dds", &gScribbleTex, ddsData, subresources));
}