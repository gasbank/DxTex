#include <Windows.h>
#include <d3d12.h>
#include "d3dx12.h"
#include <dxgi1_6.h>
#include <dxgidebug.h>
#include <d3dcompiler.h>
#include <DirectXColors.h>
#include <DirectXMath.h>
#include <iostream>
#include <string>
#include <map>
#include <comdef.h>
#include "DDSTextureLoader.h"
#include "objparser.h"

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

static DirectX::XMFLOAT4X4 Identity4x4()
{
	static DirectX::XMFLOAT4X4 I(
		1.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f);

	return I;
}

// 디버그 레이어
ID3D12Debug* gDebug = nullptr;

// 장치
ID3D12Device* gDevice = nullptr;

// 스왑 체인 생성을 위한 dxgi팩토리
IDXGIFactory4* gFactory = nullptr;

// 펜스
ID3D12Fence* gFence = nullptr;
// 현재 펜스 숫자
UINT64 gCurrentFence = 0;

// 커맨드 3형제
ID3D12CommandQueue* gCommandQueue = nullptr;
ID3D12CommandAllocator* gCommandAlloc = nullptr;
ID3D12GraphicsCommandList* gCommandList = nullptr;

// 렌더를 위한 스왑체인
IDXGISwapChain* gSwapChain = nullptr;

// 버퍼의 개수
const UINT SwapChainBufferCount = 2;

// 현재 후면 버퍼 인덱스
UINT gCurrentBufferIndex = 0;

// 버퍼 리소스의 배열 - 스왑 체인과 연결됨
ID3D12Resource* gRenderBuffer[SwapChainBufferCount] = { nullptr };
// 깊이 스텐실 버퍼 리소스 - 깊이 스텐실 힙과 연결됨
ID3D12Resource* gDepthStencilBuffer = nullptr;

// rtv, dsv, cbv힙, 각 핸들은 그때그때 만들어 쓴다.
ID3D12DescriptorHeap* gRtvHeap = nullptr;
ID3D12DescriptorHeap* gDsvHeap = nullptr;
ID3D12DescriptorHeap* gCbvHeap = nullptr;
ID3D12DescriptorHeap* gSrvHeap = nullptr;

Microsoft::WRL::ComPtr<ID3D12Resource> gScribbleTex;

// rtv, dsv, cbv 힙의 크기
UINT gRtvHeapSize = 0;
UINT gDsvHeapSize = 0;
UINT gCbvHeapSize = 0;

// 뷰포트
D3D12_VIEWPORT gViewport = { };

// 가위 사각형
D3D12_RECT gScissorRect = { };

// rtv의 버퍼 형식
DXGI_FORMAT gRenderBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
// dsv의 버퍼 형식
DXGI_FORMAT gDepthStencilFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

/*** 초기화 관련 ***/
HRESULT InitD3D(HWND hWnd);

// InitD3D 내부 함수
void CreateDebug();
void CreateFactory();
void CreateDevice();
void CreateCommandObjects();
// 각종 힙을 생성(rtv, dsv, cbv)
void CreateHeaps();
void CreateSwapChain();
void CreateViewport();
void CreateScissorRect();
void CreateFence();

// 텍스쳐 생성 일반화 함수
void CreateTextureResourceFromFile(std::wstring fileName);

// rtv, dsv 관련 리소스 생성
void CreateHeapResources();

/*** 게임 로직 관련 ***/
void Update();

/*** 그리기 관련 ***/
// 커맨드 오브젝트들의 리셋
void PopulateCommandList();
// 펜스 조작
void FlushCommandQueue();
// 그리기
void Render();

/*** 종료 ***/
void Release();

/*** 삼각형 관련 ***/

typedef struct _Vertex
{
	XMFLOAT3 position;
	XMFLOAT3 normal;
	XMFLOAT2 tex;
} Vertex;

ID3D12RootSignature* gRootSignature = nullptr;

std::map<std::string, ID3DBlob*> gShaders;
std::map<std::string, ID3D12PipelineState*> gPSOs;

// MeshData 생성
struct Material
{
	std::string Name;
	std::string TextureFileName;
	int MatCBIndex = -1;

	DirectX::XMFLOAT4 DiffuseAlbedo = { 1.0f, 1.0f, 1.0f, 1.0f };
	DirectX::XMFLOAT3 FresnelR0 = { 0.01f, 0.01f, 0.01f };
	float Roughness = 0.25f;
	DirectX::XMFLOAT4X4 MatTransform = Identity4x4();
};

// MeshData 생성
class MeshData
{
public:
	ID3D12Resource* vertexBuffer;
	D3D12_VERTEX_BUFFER_VIEW vertexBufferView = { };

	ID3D12Resource* indexBuffer;
	D3D12_INDEX_BUFFER_VIEW indexBufferView = { };
	UINT indexCount;

	void Release()
	{
		vertexBuffer->Release();
		indexBuffer->Release();
	}
};

struct RenderItem
{
	RenderItem() = default;

	XMFLOAT4X4 WorldMat = Identity4x4();
	XMFLOAT4X4 TexTransform = Identity4x4();

	MeshData* MeshData;
	Material* Material;
};

std::map<std::string, MeshData> gMeshDatas;
std::map<std::wstring, Microsoft::WRL::ComPtr<ID3D12Resource>> gTexDatas;
std::map<std::string, int> gTexDiffuseSrvHeapIndices;

std::map<std::string, Material> gMaterials;

std::vector<RenderItem> gOpaqueRenderItems;
std::vector<RenderItem> gTransparentRenderItems;
std::vector<RenderItem> gAlphaTestedRenderItems;
std::vector<RenderItem> gNdcRenderItems;

// 초기화
void CreateRootSignature();
void CreatePSO();

// 메쉬 초기화 및 버퍼 생성, 그리기
void CreateMaterials();
void CreateBoxGeometry();
void CreateGrassGeometry();
void CreateWaterGeometry();
void CreateCenterSquareGeometry();
void CreateMonkeyGeometry();
void CreateRenderItems();
void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem>& renderItems);

// 깊이 스텐실 버퍼
void InitDepthStencilBuffer();

// 상수 버퍼
void InitConstantBuffer();

// 셰이더 리소스
void InitShaderResources();

// 빛 방향 추가
struct ObjectConstantBuffer
{
	XMFLOAT4X4 World;
	XMFLOAT4X4 WorldCenterSquare;
	XMFLOAT4X4 WorldViewProjection;
	XMFLOAT3 EyePos;
	float padding2; // TexOffset을 x,y 모두 적용하려면 padding2를 추가해서 4개 단위로 기록해야 한다.
	XMFLOAT2 TexOffset;
	double padding3;
	XMFLOAT3 lightDirection;
	float padding4;
	XMFLOAT4 gFogColor;
	float gFogStart;
	float gFogRange;

	char padding[512
		- sizeof(World)
		- sizeof(WorldCenterSquare)
		- sizeof(WorldViewProjection)
		- sizeof(EyePos)
		- sizeof(TexOffset)
		- sizeof(padding2)
		- sizeof(padding3)
		- sizeof(lightDirection)
		- sizeof(padding4)
		- sizeof(gFogColor)
		- sizeof(gFogStart)
		- sizeof(gFogRange)
	];
};

ID3D12Resource* gConstantBuffer = nullptr;
ObjectConstantBuffer gConstantBufferData;
UINT8* gCbvDataBegin;

// 모델 뷰 프로젝션
XMFLOAT4X4 gWorld = Identity4x4();
XMFLOAT4X4 gView = Identity4x4();
XMFLOAT4X4 gProj = Identity4x4();

float gTheta;
float gPhi = XM_PIDIV4;
float gRadius = 5.0f;

// 키 입력
bool isLeftKeyPressed = false;
bool isRightKeyPressed = false;

/*** Win32 관련 ***/

HWND gHWnd = nullptr;
UINT gClientWidth = 800;
UINT gClientHeight = 600;

// Win32 이벤트 폴링 함수
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

int WINAPI wWinMain(
	_In_ HINSTANCE hInstance,
	_In_opt_ HINSTANCE hPrevInstance,
	_In_ LPWSTR    lpCmdLine,
	_In_ int       nCmdShow)
{
	LPCWSTR className = L"DX12Study!";

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

#pragma warning(push)
#pragma warning(disable : 4297)
	ThrowIfFailed(RegisterClass(&wc));
#pragma warning(pop)

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

	// CreateWindow의 dwStyle에 WS_VISIBLE을 넣었다면 필요 없음.
	//ShowWindow(gHWnd, nCmdShow);

	// WM_PAINT 메시지를 강제 호출. 최초 생성 시 그려줘야 하지만 DX이므로 필요 없음.
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
		// ESC를 눌러도 종료할 수 있게 처리.
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
		// PostQuitMessage()는 메시지에 WM_QUIT을 넣는다.
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

	CreateRootSignature();
	CreatePSO();

	CreateCommandObjects();
	CreateHeaps();
	CreateSwapChain();
	CreateHeapResources();
	CreateFence();

	CreateTextureResourceFromFile(L"scribble.dds");
	CreateTextureResourceFromFile(L"grass.dds");
	CreateTextureResourceFromFile(L"bricks.dds");
	CreateTextureResourceFromFile(L"water.dds");
	CreateTextureResourceFromFile(L"WireFence.dds");

	CreateMaterials();

	CreateBoxGeometry();
	CreateGrassGeometry();
	CreateWaterGeometry();
	CreateCenterSquareGeometry();
	CreateMonkeyGeometry();

	CreateRenderItems();

	InitDepthStencilBuffer();
	InitConstantBuffer();

	InitShaderResources();

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
	// 모든게 기본값이므로 그냥 넣어도 된다.
	D3D12_COMMAND_QUEUE_DESC queueDesc = { };

	ThrowIfFailed(gDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&gCommandQueue)));
	ThrowIfFailed(gDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&gCommandAlloc)));
	// 커맨드 리스트를 초기화할 때 파이프라인 상태 객체를 넣어준다.
	// 여러 개의 PSO 사용하므로 nullptr 넣는다.
	ThrowIfFailed(gDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, gCommandAlloc, nullptr, IID_PPV_ARGS(&gCommandList)));

	// 무조건 닫아준다.
	ThrowIfFailed(gCommandList->Close());
}

void CreateHeaps()
{
	// 각종 힙을 생성

	// rtv
	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvHeapDesc.NumDescriptors = SwapChainBufferCount;

	ThrowIfFailed(gDevice->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&gRtvHeap)));

	// dsv
	D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
	dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	dsvHeapDesc.NumDescriptors = 1;

	ThrowIfFailed(gDevice->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&gDsvHeap)));

	// cbv
	D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc = {};
	cbvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	cbvHeapDesc.NumDescriptors = 1;
	cbvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

	ThrowIfFailed(gDevice->CreateDescriptorHeap(&cbvHeapDesc, IID_PPV_ARGS(&gCbvHeap)));

	// srv
	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	// 텍스쳐 개수 변경
	srvHeapDesc.NumDescriptors = 5;
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
	// rtv, dsv 힙에 묶일 리소스 생성

	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(gRtvHeap->GetCPUDescriptorHandleForHeapStart(), gCurrentBufferIndex, gRtvHeapSize);
	for (size_t i = 0; i < SwapChainBufferCount; i++)
	{
		ThrowIfFailed(gSwapChain->GetBuffer(static_cast<UINT>(i), IID_PPV_ARGS(&gRenderBuffer[i])));
		gDevice->CreateRenderTargetView(gRenderBuffer[i], nullptr, rtvHandle);
		rtvHandle.Offset(gRtvHeapSize);
	}
}

static UINT64 frameIndex;

void Update()
{
	if (isLeftKeyPressed)
	{
		gTheta += 0.005f;
	}

	if (isRightKeyPressed)
	{
		gTheta -= 0.005f;
	}

	// 카메라 설정
	float x = gRadius * sinf(gPhi) * cosf(gTheta);
	float z = gRadius * sinf(gPhi) * sinf(gTheta);
	float y = gRadius * cosf(gPhi);

	XMVECTOR pos = XMVectorSet(x, y, z, 1);
	XMVECTOR target = XMVectorZero();
	XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

	XMMATRIX view = XMMatrixLookAtLH(pos, target, up);
	XMStoreFloat4x4(&gView, view);

	float aspectRatio = ((float)gClientWidth) / gClientHeight;
	XMMATRIX proj = XMMatrixPerspectiveFovLH(XM_PIDIV2, aspectRatio, 1, 1000);
	XMStoreFloat4x4(&gProj, proj);

	XMMATRIX world = XMLoadFloat4x4(&gWorld);
	auto worldViewProjection = XMMatrixTranspose(world * view * proj);

	frameIndex++;
	XMStoreFloat4x4(&gConstantBufferData.WorldCenterSquare, XMMatrixTranspose(XMMatrixTranslation(sinf(frameIndex / 1000.0f), 0.5f, 0)));

	XMFLOAT4X4 gWorld = Identity4x4();
	XMStoreFloat4x4(&gConstantBufferData.World, world);
	XMStoreFloat4x4(&gConstantBufferData.WorldViewProjection, worldViewProjection);
	XMStoreFloat3(&gConstantBufferData.EyePos, pos);

	XMVECTOR texOffset = XMVectorSet(gTheta, gTheta, 0, 0);
	XMStoreFloat2(&gConstantBufferData.TexOffset, texOffset);

	// 빛 벡터 추가
	XMVECTOR lightDirection = XMVectorSet(1.5f, -1.0f, 1.5f, 1);
	XMStoreFloat3(&gConstantBufferData.lightDirection, lightDirection);

	XMVECTOR fogColor = XMVectorSet(1, 1, 1, 0);
	XMStoreFloat4(&gConstantBufferData.gFogColor, fogColor);

	gConstantBufferData.gFogStart = 5.0f;
	gConstantBufferData.gFogRange = 50.0f;

	memcpy(gCbvDataBegin, &gConstantBufferData, sizeof(gConstantBufferData));
}

void PopulateCommandList()
{
	// 커맨드리스트, 할당자 리셋 및 렌더 명령 기록
	ThrowIfFailed(gCommandAlloc->Reset());
	// 파이프라인 상태 객체 기본값 넣기
	ThrowIfFailed(gCommandList->Reset(gCommandAlloc, gPSOs["opaque"]));

	// 루트 서명 넣기
	gCommandList->SetGraphicsRootSignature(gRootSignature);

	// RS
	gCommandList->RSSetViewports(1, &gViewport);
	gCommandList->RSSetScissorRects(1, &gScissorRect);

	// rtv 처리
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(gRtvHeap->GetCPUDescriptorHandleForHeapStart(), gCurrentBufferIndex, gRtvHeapSize);
	CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(gDsvHeap->GetCPUDescriptorHandleForHeapStart(), 0, gDsvHeapSize);

	// OMSetRenderTargets은 render target을 그리기 전에 미리 핸들을 먼저 얻은 다음 호출해야 한다.
	gCommandList->OMSetRenderTargets(1, &rtvHandle, false, &dsvHandle);

	gCommandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

	gCommandList->ResourceBarrier(1,
		&CD3DX12_RESOURCE_BARRIER::Transition(
			gRenderBuffer[gCurrentBufferIndex], D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET)
	);

	// 렌더 명령 기록
	gCommandList->ClearRenderTargetView(rtvHandle, Colors::AliceBlue, 0, nullptr);

	// 각종 물체 그리기 및 PSO 변경
	
	/*gCommandList->SetPipelineState(gPSOs["alphaTested"]);

	DrawRenderItems(gCommandList, gAlphaTestedRenderItems);

	gCommandList->SetPipelineState(gPSOs["transparent"]);

	DrawRenderItems(gCommandList, gTransparentRenderItems);*/

	gCommandList->SetPipelineState(gPSOs["ndc"]);
	gCommandList->OMSetStencilRef(255);
	DrawRenderItems(gCommandList, gNdcRenderItems);

	gCommandList->SetPipelineState(gPSOs["opaque"]);
	DrawRenderItems(gCommandList, gOpaqueRenderItems);

	gCommandList->ResourceBarrier(1,
		&CD3DX12_RESOURCE_BARRIER::Transition(
			gRenderBuffer[gCurrentBufferIndex], D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT)
	);

	ThrowIfFailed(gCommandList->Close());
}

void FlushCommandQueue()
{
	// 펜스 값 증가 및 대기
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

	// 첫 번째 인자는 vsync의 여부
	ThrowIfFailed(gSwapChain->Present(0, 0));

	gCurrentBufferIndex = (gCurrentBufferIndex + 1) % SwapChainBufferCount;

	FlushCommandQueue();
}

void Release()
{
	for each (auto var in gTexDatas)
	{
		gTexDatas[var.first].Reset();
	}

	COM_RELEASE(gConstantBuffer);
	COM_RELEASE(gDepthStencilBuffer);

	for each (auto var in gMeshDatas)
	{
		gMeshDatas[var.first].Release();
	}

	gScribbleTex.Reset();

	for each (auto var in gPSOs)
	{
		gPSOs[var.first]->Release();
	}

	for each (auto var in gShaders)
	{
		gShaders[var.first]->Release();
	}

	COM_RELEASE(gRootSignature);

	COM_RELEASE(gFence);

	for (size_t i = 0; i < SwapChainBufferCount; i++)
	{
		COM_RELEASE(gRenderBuffer[i]);
	}

	COM_RELEASE(gSwapChain);

	COM_RELEASE(gSrvHeap);
	COM_RELEASE(gCbvHeap);
	COM_RELEASE(gDsvHeap);
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
	CD3DX12_ROOT_PARAMETER rootParams[2];

	{
		CD3DX12_DESCRIPTOR_RANGE range[1];
		range[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);
		rootParams[0].InitAsDescriptorTable(1, &range[0]);
	}

	{
		CD3DX12_DESCRIPTOR_RANGE range[1];
		range[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
		rootParams[1].InitAsDescriptorTable(1, &range[0]);
	}

	// TODO: 기본 샘플러 생성 
	CD3DX12_STATIC_SAMPLER_DESC sampler(0);

	ID3DBlob* signature = nullptr;
	ID3DBlob* error = nullptr;

	// 샘플러 변경
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
		{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, sizeof(XMFLOAT3) + sizeof(XMFLOAT3), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
	};

	ID3DBlob* vertexShader = nullptr;
	ID3DBlob* ndcVS = nullptr;
	ID3DBlob* pixelShader = nullptr;
	ID3DBlob* alphaTestedPS = nullptr;
	ID3DBlob* error = nullptr;

	UINT compileFlags = 0;
#if defined(_DEBUG)
	compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

	const D3D_SHADER_MACRO defines[] =
	{
		//"FOG", "1",
		NULL, NULL
	};

	const D3D_SHADER_MACRO alphaTestDefines[] =
	{
		//"FOG", "1",
		"ALPHA_TEST", "1",
		NULL, NULL
	};

	LPVOID errorBufferPtr = nullptr;

	// 불투명, 투명, 알파테스트 쉐이더 및 pso 생성
	{
		D3DCompileFromFile(L"shaders.hlsl", nullptr, nullptr, "VSMain", "vs_5_0", compileFlags, 0, &vertexShader, &error);
		errorBufferPtr = error->GetBufferPointer();
		OutputDebugStringA((LPCSTR)errorBufferPtr);

		gShaders["VS"] = vertexShader;
	}

	{
		const D3D_SHADER_MACRO defines[] =
		{
			"NDC", "",
			NULL, NULL
		};

		D3DCompileFromFile(L"shaders.hlsl", defines, nullptr, "VSMain", "vs_5_0", compileFlags, 0, &ndcVS, &error);
		errorBufferPtr = error->GetBufferPointer();
		OutputDebugStringA((LPCSTR)errorBufferPtr);

		gShaders["ndcVS"] = ndcVS;
	}

	D3DCompileFromFile(L"shaders.hlsl", defines, nullptr, "PSMain", "ps_5_0", compileFlags, 0, &pixelShader, &error);
	errorBufferPtr = error->GetBufferPointer();
	OutputDebugStringA((LPCSTR)errorBufferPtr);

	gShaders["opaquePS"] = pixelShader;

	D3DCompileFromFile(L"shaders.hlsl", alphaTestDefines, nullptr, "PSMain", "ps_5_0", compileFlags, 0, &alphaTestedPS, &error);
	errorBufferPtr = error->GetBufferPointer();
	OutputDebugStringA((LPCSTR)errorBufferPtr);

	gShaders["alphaTestedPS"] = alphaTestedPS;

	// opaque
	D3D12_GRAPHICS_PIPELINE_STATE_DESC opaquePSODesc = { };
	opaquePSODesc.pRootSignature = gRootSignature;
	opaquePSODesc.VS = CD3DX12_SHADER_BYTECODE(gShaders["VS"]);
	opaquePSODesc.PS = CD3DX12_SHADER_BYTECODE(gShaders["opaquePS"]);
	opaquePSODesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	opaquePSODesc.SampleMask = UINT_MAX;
	opaquePSODesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	opaquePSODesc.DepthStencilState.DepthEnable = true;
	opaquePSODesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	opaquePSODesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
	opaquePSODesc.DepthStencilState.StencilEnable = true;
	opaquePSODesc.DepthStencilState.StencilReadMask = 0xff;
	opaquePSODesc.DepthStencilState.StencilWriteMask = 0xff;
	opaquePSODesc.DepthStencilState.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
	opaquePSODesc.DepthStencilState.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
	opaquePSODesc.DepthStencilState.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
	opaquePSODesc.DepthStencilState.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_EQUAL;
	opaquePSODesc.DepthStencilState.BackFace = opaquePSODesc.DepthStencilState.FrontFace;
	opaquePSODesc.InputLayout = { inputElementDesc, _countof(inputElementDesc) };
	opaquePSODesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	opaquePSODesc.NumRenderTargets = 1;
	opaquePSODesc.RTVFormats[0] = gRenderBufferFormat;
	opaquePSODesc.DSVFormat = gDepthStencilFormat;
	opaquePSODesc.SampleDesc.Count = 1;
	opaquePSODesc.SampleDesc.Quality = 0;

	ThrowIfFailed(gDevice->CreateGraphicsPipelineState(&opaquePSODesc, IID_PPV_ARGS(&gPSOs["opaque"])));

	// transparent
	D3D12_GRAPHICS_PIPELINE_STATE_DESC transparentPSODesc = opaquePSODesc;

	D3D12_RENDER_TARGET_BLEND_DESC transparencyBlendDesc;
	transparencyBlendDesc.BlendEnable = true;
	transparencyBlendDesc.LogicOpEnable = false;

	transparencyBlendDesc.SrcBlend = D3D12_BLEND_SRC_ALPHA;
	transparencyBlendDesc.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
	transparencyBlendDesc.BlendOp = D3D12_BLEND_OP_ADD;

	transparencyBlendDesc.SrcBlendAlpha = D3D12_BLEND_ONE;
	transparencyBlendDesc.DestBlendAlpha = D3D12_BLEND_ZERO;
	transparencyBlendDesc.BlendOpAlpha = D3D12_BLEND_OP_ADD;

	transparencyBlendDesc.LogicOp = D3D12_LOGIC_OP_NOOP;
	transparencyBlendDesc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

	transparentPSODesc.BlendState.RenderTarget[0] = transparencyBlendDesc;
	ThrowIfFailed(gDevice->CreateGraphicsPipelineState(&transparentPSODesc, IID_PPV_ARGS(&gPSOs["transparent"])));

	// alpha test
	D3D12_GRAPHICS_PIPELINE_STATE_DESC alphaTestedPSODesc = opaquePSODesc;
	alphaTestedPSODesc.PS = CD3DX12_SHADER_BYTECODE(gShaders["alphaTestedPS"]);
	alphaTestedPSODesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	ThrowIfFailed(gDevice->CreateGraphicsPipelineState(&alphaTestedPSODesc, IID_PPV_ARGS(&gPSOs["alphaTested"])));

	// NDC
	D3D12_GRAPHICS_PIPELINE_STATE_DESC ndcPSODesc = opaquePSODesc;
	ndcPSODesc.VS = CD3DX12_SHADER_BYTECODE(gShaders["ndcVS"]);
	ndcPSODesc.BlendState.RenderTarget[0].RenderTargetWriteMask = 0; // 실제 화면에 렌더링되는 것이 아니다
	ndcPSODesc.DepthStencilState.DepthEnable = false;
	ndcPSODesc.DepthStencilState.StencilEnable = true;
	ndcPSODesc.DepthStencilState.StencilReadMask = 0xff;
	ndcPSODesc.DepthStencilState.StencilWriteMask = 0xff;
	ndcPSODesc.DepthStencilState.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
	ndcPSODesc.DepthStencilState.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
	ndcPSODesc.DepthStencilState.FrontFace.StencilPassOp = D3D12_STENCIL_OP_REPLACE;
	ndcPSODesc.DepthStencilState.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
	ndcPSODesc.DepthStencilState.BackFace = opaquePSODesc.DepthStencilState.FrontFace;
	ThrowIfFailed(gDevice->CreateGraphicsPipelineState(&ndcPSODesc, IID_PPV_ARGS(&gPSOs["ndc"])));
}

void InitDepthStencilBuffer()
{
	D3D12_RESOURCE_DESC resourceDesc;
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	resourceDesc.Alignment = 0;
	resourceDesc.Width = gClientWidth;
	resourceDesc.Height = gClientHeight;
	resourceDesc.DepthOrArraySize = 1;
	resourceDesc.MipLevels = 1;
	resourceDesc.Format = gDepthStencilFormat;
	resourceDesc.SampleDesc.Count = 1;
	resourceDesc.SampleDesc.Quality = 0;
	resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

	D3D12_CLEAR_VALUE clearValue;
	clearValue.Format = gDepthStencilFormat;
	clearValue.DepthStencil.Depth = 1.0f;
	clearValue.DepthStencil.Stencil = 0;
	
	ThrowIfFailed(gDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&resourceDesc,
		D3D12_RESOURCE_STATE_DEPTH_WRITE,
		&clearValue,
		IID_PPV_ARGS(&gDepthStencilBuffer)
	));

	D3D12_DEPTH_STENCIL_VIEW_DESC viewDesc;
	viewDesc.Format = gDepthStencilFormat;
	viewDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	viewDesc.Flags = D3D12_DSV_FLAG_NONE;
	viewDesc.Texture2D.MipSlice = 0;
	gDevice->CreateDepthStencilView(gDepthStencilBuffer, &viewDesc, gDsvHeap->GetCPUDescriptorHandleForHeapStart());
}

void InitConstantBuffer()
{
	static_assert(sizeof(ObjectConstantBuffer) % 256 == 0, "constant buffer must be multiple of 256 byte");
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

void InitShaderResources()
{
	CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle(gSrvHeap->GetCPUDescriptorHandleForHeapStart());

	// TODO: 텍스쳐 순서 보장하기?
	int index = 0;
	for each (auto texPair in gTexDatas)
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = { };
		srvDesc.Format = texPair.second->GetDesc().Format;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.Texture2D.MipLevels = texPair.second->GetDesc().MipLevels;
		srvDesc.Texture2D.PlaneSlice = 0;
		srvDesc.Texture2D.ResourceMinLODClamp = 0;

		gDevice->CreateShaderResourceView(texPair.second.Get(), &srvDesc, srvHandle);

		std::string texStr;
		texStr.assign(texPair.first.begin(), texPair.first.end());
		gTexDiffuseSrvHeapIndices.insert(std::make_pair(texStr, index));

		srvHandle.Offset(1, gCbvHeapSize);
		index += 1;
	}
}

void CreateTextureResourceFromFile(std::wstring fileName)
{
	ThrowIfFailed(gCommandAlloc->Reset());
	//ThrowIfFailed(gCommandList->Reset(gCommandAlloc, gPSO));
	ThrowIfFailed(gCommandList->Reset(gCommandAlloc, nullptr));

	Microsoft::WRL::ComPtr<ID3D12Resource> uploadHeap;
	gTexDatas[fileName] = nullptr;
	CreateDDSTextureFromFile12(gDevice, gCommandList, fileName.data(), gTexDatas[fileName], uploadHeap);

	// 무조건 닫아준다.
	ThrowIfFailed(gCommandList->Close());

	ID3D12CommandList* cmdLists[] = { gCommandList };
	gCommandQueue->ExecuteCommandLists(_countof(cmdLists), cmdLists);
	FlushCommandQueue();
}

void CreateMaterials()
{
	auto grass = Material();
	grass.Name = "grass";
	grass.TextureFileName = "grass.dds";
	grass.DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	grass.FresnelR0 = XMFLOAT3(0.01f, 0.01f, 0.01f);
	grass.Roughness = 0.125f;

	auto box = Material();
	box.Name = "box";
	box.TextureFileName = "WireFence.dds";
	box.DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	box.FresnelR0 = XMFLOAT3(0.01f, 0.01f, 0.01f);
	box.Roughness = 0.125f;

	// This is not a good water material definition, but we do not have all the rendering
	// tools we need (transparency, environment reflection), so we fake it for now.
	auto water = Material();
	water.Name = "water";
	water.TextureFileName = "water.dds";
	water.DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 0.5f);
	water.FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
	water.Roughness = 0.0f;

	gMaterials["grass"] = grass;
	gMaterials["box"] = box;
	gMaterials["water"] = water;
}

void CreateBoxGeometry()
{
	Vertex vertices[] =
	{
		// front face
		{{-1.0f, -1.0f, -1.0f}, {0.0f, 0.0f, -1.0f}, {0, 1}},
		{{-1.0f, +1.0f, -1.0f}, {0.0f, 0.0f, -1.0f}, {0, 0}},
		{{+1.0f, +1.0f, -1.0f}, {0.0f, 0.0f, -1.0f}, {1, 0}},
		{{+1.0f, -1.0f, -1.0f}, {0.0f, 0.0f, -1.0f}, {1, 1}},

		// back face
		{{-1.0f, -1.0f, +1.0f}, {0.0f, 0.0f, 1.0f}, {0, 1}},
		{{-1.0f, +1.0f, +1.0f}, {0.0f, 0.0f, 1.0f}, {0, 0}},
		{{+1.0f, +1.0f, +1.0f}, {0.0f, 0.0f, 1.0f}, {1, 0}},
		{{+1.0f, -1.0f, +1.0f}, {0.0f, 0.0f, 1.0f}, {1, 1}},

		// left face
		{{-1.0f, -1.0f, +1.0f}, {-1.0f, 0.0f, 0.0f}, {0, 1}},
		{{-1.0f, +1.0f, +1.0f}, {-1.0f, 0.0f, 0.0f}, {0, 0}},
		{{-1.0f, +1.0f, -1.0f}, {-1.0f, 0.0f, 0.0f}, {1, 0}},
		{{-1.0f, -1.0f, -1.0f}, {-1.0f, 0.0f, 0.0f}, {1, 1}},

		// right face
		{{+1.0f, -1.0f, -1.0f}, {1.0f, 0.0f, 0.0f}, {0, 1}},
		{{+1.0f, +1.0f, -1.0f}, {1.0f, 0.0f, 0.0f}, {0, 0}},
		{{+1.0f, +1.0f, +1.0f}, {1.0f, 0.0f, 0.0f}, {1, 0}},
		{{+1.0f, -1.0f, +1.0f}, {1.0f, 0.0f, 0.0f}, {1, 1}},

		// top face
		{{-1.0f, +1.0f, -1.0f}, {0.0f, 1.0f, 0.0f}, {0, 1}},
		{{-1.0f, +1.0f, +1.0f}, {0.0f, 1.0f, 0.0f}, {0, 0}},
		{{+1.0f, +1.0f, +1.0f}, {0.0f, 1.0f, 0.0f}, {1, 0}},
		{{+1.0f, +1.0f, -1.0f}, {0.0f, 1.0f, 0.0f}, {1, 1}},

		// bottom face
		{{-1.0f, -1.0f, +1.0f}, {0.0f, -1.0f, 0.0f}, {0, 1}},
		{{-1.0f, -1.0f, -1.0f}, {0.0f, -1.0f, 0.0f}, {0, 0}},
		{{+1.0f, -1.0f, -1.0f}, {0.0f, -1.0f, 0.0f}, {1, 0}},
		{{+1.0f, -1.0f, +1.0f}, {0.0f, -1.0f, 0.0f}, {1, 1}},
	};

	UINT16 indices[] =
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

	const UINT vertexBufferSize = sizeof(vertices);
	const UINT indexBufferSize = sizeof(indices);
	UINT gIndexCount = _countof(indices);

	ID3D12Resource* gVertexBuffer;

	// 버텍스 버퍼 생성
	ThrowIfFailed(gDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&gVertexBuffer)
	));

	// 버텍스 버퍼에 삼각형 정보 복사
	UINT8* pVertexDataBegin;
	CD3DX12_RANGE readRange(0, 0);
	ThrowIfFailed(gVertexBuffer->Map(0, nullptr, reinterpret_cast<void**>(&pVertexDataBegin)));
	memcpy(pVertexDataBegin, vertices, sizeof(vertices));
	gVertexBuffer->Unmap(0, nullptr);

	D3D12_VERTEX_BUFFER_VIEW gVertexBufferView;

	// 버텍스 버퍼 뷰 생성
	gVertexBufferView.BufferLocation = gVertexBuffer->GetGPUVirtualAddress();
	gVertexBufferView.SizeInBytes = vertexBufferSize;
	gVertexBufferView.StrideInBytes = sizeof(Vertex);

	ID3D12Resource* gIndexBuffer;
	// 인덱스 버퍼 생성
	ThrowIfFailed(gDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(indexBufferSize),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&gIndexBuffer)
	));

	// 인덱스 버퍼에 삼각형 정보 복사
	UINT8* pIndexDataBegin;
	ThrowIfFailed(gIndexBuffer->Map(0, nullptr, reinterpret_cast<void**>(&pIndexDataBegin)));
	memcpy(pIndexDataBegin, indices, sizeof(indices));
	gIndexBuffer->Unmap(0, nullptr);

	D3D12_INDEX_BUFFER_VIEW gIndexBufferView;

	// 인덱스 버퍼 뷰 생성
	gIndexBufferView.BufferLocation = gIndexBuffer->GetGPUVirtualAddress();
	gIndexBufferView.SizeInBytes = indexBufferSize;
	gIndexBufferView.Format = DXGI_FORMAT_R16_UINT;

	gMeshDatas["box"].vertexBuffer = gVertexBuffer;
	gMeshDatas["box"].vertexBufferView = gVertexBufferView;
	gMeshDatas["box"].indexBuffer = gIndexBuffer;
	gMeshDatas["box"].indexBufferView = gIndexBufferView;
	gMeshDatas["box"].indexCount = gIndexCount;

	FlushCommandQueue();
}

void CreateGrassGeometry()
{
	Vertex vertices[] =
	{
		// top face
		{{-100.0f, -10.0f, -100.0f}, {0.0f, 1.0f, 0.0f}, {0, 1}},
		{{-100.0f, -10.0f, +100.0f}, {0.0f, 1.0f, 0.0f}, {0, 0}},
		{{+100.0f, -10.0f, +100.0f}, {0.0f, 1.0f, 0.0f}, {1, 0}},
		{{+100.0f, -10.0f, -100.0f}, {0.0f, 1.0f, 0.0f}, {1, 1}},
	};

	UINT16 indices[] =
	{
		// front face
		0, 1, 2,
		0, 2, 3,
	};

	const UINT vertexBufferSize = sizeof(vertices);
	const UINT indexBufferSize = sizeof(indices);
	UINT indexCount = _countof(indices);

	ID3D12Resource* vertexBuffer;

	// 버텍스 버퍼 생성
	ThrowIfFailed(gDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&vertexBuffer)
	));

	// 버텍스 버퍼에 삼각형 정보 복사
	UINT8* pVertexDataBegin;
	CD3DX12_RANGE readRange(0, 0);
	ThrowIfFailed(vertexBuffer->Map(0, nullptr, reinterpret_cast<void**>(&pVertexDataBegin)));
	memcpy(pVertexDataBegin, vertices, sizeof(vertices));
	vertexBuffer->Unmap(0, nullptr);

	D3D12_VERTEX_BUFFER_VIEW vertexBufferView;

	// 버텍스 버퍼 뷰 생성
	vertexBufferView.BufferLocation = vertexBuffer->GetGPUVirtualAddress();
	vertexBufferView.SizeInBytes = vertexBufferSize;
	vertexBufferView.StrideInBytes = sizeof(Vertex);

	ID3D12Resource* indexBuffer;
	// 인덱스 버퍼 생성
	ThrowIfFailed(gDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(indexBufferSize),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&indexBuffer)
	));

	// 인덱스 버퍼에 삼각형 정보 복사
	UINT8* pIndexDataBegin;
	ThrowIfFailed(indexBuffer->Map(0, nullptr, reinterpret_cast<void**>(&pIndexDataBegin)));
	memcpy(pIndexDataBegin, indices, sizeof(indices));
	indexBuffer->Unmap(0, nullptr);

	D3D12_INDEX_BUFFER_VIEW indexBufferView;

	// 인덱스 버퍼 뷰 생성
	indexBufferView.BufferLocation = indexBuffer->GetGPUVirtualAddress();
	indexBufferView.SizeInBytes = indexBufferSize;
	indexBufferView.Format = DXGI_FORMAT_R16_UINT;

	gMeshDatas["grass"].vertexBuffer = vertexBuffer;
	gMeshDatas["grass"].vertexBufferView = vertexBufferView;
	gMeshDatas["grass"].indexBuffer = indexBuffer;
	gMeshDatas["grass"].indexBufferView = indexBufferView;
	gMeshDatas["grass"].indexCount = indexCount;

	FlushCommandQueue();
}

void CreateWaterGeometry()
{
	Vertex vertices[] =
	{
		// top face
		{{-100.0f, 5.0f, -100.0f}, {0.0f, 1.0f, 0.0f}, {0, 1}},
		{{-100.0f, 5.0f, +100.0f}, {0.0f, 1.0f, 0.0f}, {0, 0}},
		{{+100.0f, 5.0f, +100.0f}, {0.0f, 1.0f, 0.0f}, {1, 0}},
		{{+100.0f, 5.0f, -100.0f}, {0.0f, 1.0f, 0.0f}, {1, 1}},
	};

	UINT16 indices[] =
	{
		// front face
		0, 1, 2,
		0, 2, 3,
	};

	const UINT vertexBufferSize = sizeof(vertices);
	const UINT indexBufferSize = sizeof(indices);
	UINT indexCount = _countof(indices);

	ID3D12Resource* vertexBuffer;

	// 버텍스 버퍼 생성
	ThrowIfFailed(gDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&vertexBuffer)
	));

	// 버텍스 버퍼에 삼각형 정보 복사
	UINT8* pVertexDataBegin;
	CD3DX12_RANGE readRange(0, 0);
	ThrowIfFailed(vertexBuffer->Map(0, nullptr, reinterpret_cast<void**>(&pVertexDataBegin)));
	memcpy(pVertexDataBegin, vertices, sizeof(vertices));
	vertexBuffer->Unmap(0, nullptr);

	D3D12_VERTEX_BUFFER_VIEW vertexBufferView;

	// 버텍스 버퍼 뷰 생성
	vertexBufferView.BufferLocation = vertexBuffer->GetGPUVirtualAddress();
	vertexBufferView.SizeInBytes = vertexBufferSize;
	vertexBufferView.StrideInBytes = sizeof(Vertex);

	ID3D12Resource* indexBuffer;
	// 인덱스 버퍼 생성
	ThrowIfFailed(gDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(indexBufferSize),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&indexBuffer)
	));

	// 인덱스 버퍼에 삼각형 정보 복사
	UINT8* pIndexDataBegin;
	ThrowIfFailed(indexBuffer->Map(0, nullptr, reinterpret_cast<void**>(&pIndexDataBegin)));
	memcpy(pIndexDataBegin, indices, sizeof(indices));
	indexBuffer->Unmap(0, nullptr);

	D3D12_INDEX_BUFFER_VIEW indexBufferView;

	// 인덱스 버퍼 뷰 생성
	indexBufferView.BufferLocation = indexBuffer->GetGPUVirtualAddress();
	indexBufferView.SizeInBytes = indexBufferSize;
	indexBufferView.Format = DXGI_FORMAT_R16_UINT;

	gMeshDatas["water"].vertexBuffer = vertexBuffer;
	gMeshDatas["water"].vertexBufferView = vertexBufferView;
	gMeshDatas["water"].indexBuffer = indexBuffer;
	gMeshDatas["water"].indexBufferView = indexBufferView;
	gMeshDatas["water"].indexCount = indexCount;

	FlushCommandQueue();
}

void CreateCenterSquareGeometry()
{
	Vertex vertices[] =
	{
		{{-0.5f, 0.5f, 0}, {0.0f, 0.0f, -1.0f}, {0, 1}},
		{{ 0.5f, 0.5f, 0}, {0.0f, 0.0f, -1.0f}, {1, 1}},
		{{-0.5f,-0.5f, 0}, {0.0f, 0.0f, -1.0f}, {0, 0}},
		{{ 0.5f,-0.5f, 0}, {0.0f, 0.0f, -1.0f}, {1, 0}},
	};

	UINT16 indices[] =
	{
		0, 1, 2,
		2, 1, 3,
	};

	const UINT vertexBufferSize = sizeof(vertices);
	const UINT indexBufferSize = sizeof(indices);
	UINT indexCount = _countof(indices);

	ID3D12Resource* vertexBuffer;

	// 버텍스 버퍼 생성
	ThrowIfFailed(gDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&vertexBuffer)
	));

	// 버텍스 버퍼에 삼각형 정보 복사
	UINT8* pVertexDataBegin;
	CD3DX12_RANGE readRange(0, 0);
	ThrowIfFailed(vertexBuffer->Map(0, nullptr, reinterpret_cast<void**>(&pVertexDataBegin)));
	memcpy(pVertexDataBegin, vertices, sizeof(vertices));
	vertexBuffer->Unmap(0, nullptr);

	D3D12_VERTEX_BUFFER_VIEW vertexBufferView;

	// 버텍스 버퍼 뷰 생성
	vertexBufferView.BufferLocation = vertexBuffer->GetGPUVirtualAddress();
	vertexBufferView.SizeInBytes = vertexBufferSize;
	vertexBufferView.StrideInBytes = sizeof(Vertex);

	ID3D12Resource* indexBuffer;
	// 인덱스 버퍼 생성
	ThrowIfFailed(gDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(indexBufferSize),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&indexBuffer)
	));

	// 인덱스 버퍼에 삼각형 정보 복사
	UINT8* pIndexDataBegin;
	ThrowIfFailed(indexBuffer->Map(0, nullptr, reinterpret_cast<void**>(&pIndexDataBegin)));
	memcpy(pIndexDataBegin, indices, sizeof(indices));
	indexBuffer->Unmap(0, nullptr);

	D3D12_INDEX_BUFFER_VIEW indexBufferView;

	// 인덱스 버퍼 뷰 생성
	indexBufferView.BufferLocation = indexBuffer->GetGPUVirtualAddress();
	indexBufferView.SizeInBytes = indexBufferSize;
	indexBufferView.Format = DXGI_FORMAT_R16_UINT;

	auto geometryName = "centerSquare";

	gMeshDatas[geometryName].vertexBuffer = vertexBuffer;
	gMeshDatas[geometryName].vertexBufferView = vertexBufferView;
	gMeshDatas[geometryName].indexBuffer = indexBuffer;
	gMeshDatas[geometryName].indexBufferView = indexBufferView;
	gMeshDatas[geometryName].indexCount = indexCount;

	FlushCommandQueue();
}

void CreateMonkeyGeometry()
{/*
	Vertex vertices[] =
	{
		{{-0.5f, 0.5f, 0}, {0.0f, 0.0f, -1.0f}, {0, 1}},
		{{ 0.5f, 0.5f, 0}, {0.0f, 0.0f, -1.0f}, {1, 1}},
		{{-0.5f,-0.5f, 0}, {0.0f, 0.0f, -1.0f}, {0, 0}},
		{{ 0.5f,-0.5f, 0}, {0.0f, 0.0f, -1.0f}, {1, 0}},
	};

	UINT16 indices[] =
	{
		0, 1, 2,
		2, 1, 3,
	};*/

	auto objModel = ObjParse(L"monkey.obj");
	std::vector<Vertex> vertices;
	for (const auto& vert : objModel.vertices)
	{	
		Vertex v;
		v.position.x = vert.x;
		v.position.y = vert.y;
		v.position.z = vert.z;
		v.normal.x = 0;
		v.normal.y = 0;
		v.normal.z = 0;
		v.tex.x = 0;
		v.tex.y = 0;
		vertices.push_back(v);
	}

	std::vector<UINT16> indices;
	for (const auto& ind : objModel.indices)
	{
		indices.push_back(ind - 1);
	}

	const UINT vertexBufferSize = vertices.size() * sizeof(Vertex);
	const UINT indexBufferSize = indices.size() * sizeof(UINT16);
	UINT indexCount = indices.size();

	ID3D12Resource* vertexBuffer;

	// 버텍스 버퍼 생성
	ThrowIfFailed(gDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&vertexBuffer)
	));

	// 버텍스 버퍼에 삼각형 정보 복사
	UINT8* pVertexDataBegin;
	CD3DX12_RANGE readRange(0, 0);
	ThrowIfFailed(vertexBuffer->Map(0, nullptr, reinterpret_cast<void**>(&pVertexDataBegin)));
	memcpy(pVertexDataBegin, &vertices[0], vertexBufferSize);
	vertexBuffer->Unmap(0, nullptr);

	D3D12_VERTEX_BUFFER_VIEW vertexBufferView;

	// 버텍스 버퍼 뷰 생성
	vertexBufferView.BufferLocation = vertexBuffer->GetGPUVirtualAddress();
	vertexBufferView.SizeInBytes = vertexBufferSize;
	vertexBufferView.StrideInBytes = sizeof(Vertex);

	ID3D12Resource* indexBuffer;
	// 인덱스 버퍼 생성
	ThrowIfFailed(gDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(indexBufferSize),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&indexBuffer)
	));

	// 인덱스 버퍼에 삼각형 정보 복사
	UINT8* pIndexDataBegin;
	ThrowIfFailed(indexBuffer->Map(0, nullptr, reinterpret_cast<void**>(&pIndexDataBegin)));
	memcpy(pIndexDataBegin, &indices[0], indexBufferSize);
	indexBuffer->Unmap(0, nullptr);

	D3D12_INDEX_BUFFER_VIEW indexBufferView;

	// 인덱스 버퍼 뷰 생성
	indexBufferView.BufferLocation = indexBuffer->GetGPUVirtualAddress();
	indexBufferView.SizeInBytes = indexBufferSize;
	indexBufferView.Format = DXGI_FORMAT_R16_UINT;

	auto geometryName = "monkey";

	gMeshDatas[geometryName].vertexBuffer = vertexBuffer;
	gMeshDatas[geometryName].vertexBufferView = vertexBufferView;
	gMeshDatas[geometryName].indexBuffer = indexBuffer;
	gMeshDatas[geometryName].indexBufferView = indexBufferView;
	gMeshDatas[geometryName].indexCount = indexCount;

	FlushCommandQueue();
}

void CreateRenderItems()
{
	{
		auto renderItem = std::make_unique<RenderItem>();
		renderItem->WorldMat = Identity4x4();
		renderItem->MeshData = &gMeshDatas["grass"];
		renderItem->Material = &gMaterials["grass"];
		gOpaqueRenderItems.push_back(*renderItem);
	}
	{
		auto renderItem = std::make_unique<RenderItem>();
		renderItem->WorldMat = Identity4x4();
		renderItem->MeshData = &gMeshDatas["box"];
		renderItem->Material = &gMaterials["box"];
		gAlphaTestedRenderItems.push_back(*renderItem);
	}
	{
		auto renderItem = std::make_unique<RenderItem>();
		renderItem->WorldMat = Identity4x4();
		renderItem->MeshData = &gMeshDatas["water"];
		renderItem->Material = &gMaterials["water"];
		gTransparentRenderItems.push_back(*renderItem);
	}
	{
		auto renderItem = std::make_unique<RenderItem>();
		renderItem->WorldMat = Identity4x4();
		renderItem->MeshData = &gMeshDatas["monkey"];
		renderItem->Material = nullptr;
		gNdcRenderItems.push_back(*renderItem);
	}
}

void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem>& renderItems)
{
	for each (auto ri in renderItems)
	{
		// 첫 번째 루트 파라미터
		{
			ID3D12DescriptorHeap* heaps[] = { gCbvHeap };
			gCommandList->SetDescriptorHeaps(_countof(heaps), heaps);
			gCommandList->SetGraphicsRootDescriptorTable(0, gCbvHeap->GetGPUDescriptorHandleForHeapStart());
		}

		// 두 번째 루트 파라미터
		{
			if (ri.Material != nullptr)
			{
				ID3D12DescriptorHeap* heaps[] = { gSrvHeap };
				gCommandList->SetDescriptorHeaps(_countof(heaps), heaps);

				CD3DX12_GPU_DESCRIPTOR_HANDLE tex(gSrvHeap->GetGPUDescriptorHandleForHeapStart());
				//tex.Offset(4, gCbvHeapSize);
				auto texIndex = gTexDiffuseSrvHeapIndices[ri.Material->TextureFileName];
				tex.Offset(texIndex, gCbvHeapSize);
				gCommandList->SetGraphicsRootDescriptorTable(1, tex);
			}
		}

		gCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		gCommandList->IASetVertexBuffers(0, 1, &ri.MeshData->vertexBufferView);
		gCommandList->IASetIndexBuffer(&ri.MeshData->indexBufferView);
		gCommandList->DrawIndexedInstanced(ri.MeshData->indexCount, 1, 0, 0, 0);
	}
}
