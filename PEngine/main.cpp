// ==============================
// ライブラリ
// ==============================

#include <filesystem>
#include <fstream>
#include <chrono>

#include <windows.h>
#include <string>

#pragma warning(push)
#include <format>

#pragma warning(disable:4023)
#include <cstdint>

#include <dxcapi.h>
#pragma comment(lib, "dxcompiler.lib")

#include "Vector.h"

// DirectX12
#include <d3d12.h>
#include <dxgi1_6.h>
#include <cassert>
#include <dxgidebug.h>

// ダンプ出力
#include <dbghelp.h>
#include <strsafe.h>

#pragma comment(lib, "dbghelp.lib")
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxguid.lib")

#pragma warning(pop)


// ==============================
// ログ関連
// ==============================

void Log(const std::string& message) {
	OutputDebugStringA(message.c_str());
}

void Log(std::ostream& os, const std::string& message) {
	os << message << std::endl;
	OutputDebugStringA(message.c_str());
}


// ==============================
// 文字列変換
// ==============================

// UTF-8 → UTF-16
std::wstring ConvertString(const std::string& str) {

	if (str.empty()) {
		return {};
	}

	int sizeNeeded = MultiByteToWideChar(
		CP_UTF8,
		0,
		str.data(),
		static_cast<int>(str.size()),
		nullptr,
		0
	);

	std::wstring result(sizeNeeded, 0);

	MultiByteToWideChar(
		CP_UTF8,
		0,
		str.data(),
		static_cast<int>(str.size()),
		result.data(),
		sizeNeeded
	);

	return result;
}

// UTF-16 → UTF-8
std::string ConvertString(const std::wstring& str) {

	if (str.empty()) {
		return {};
	}

	int sizeNeeded = WideCharToMultiByte(
		CP_UTF8,
		0,
		str.data(),
		static_cast<int>(str.size()),
		nullptr,
		0,
		nullptr,
		nullptr
	);

	std::string result(sizeNeeded, 0);

	WideCharToMultiByte(
		CP_UTF8,
		0,
		str.data(),
		static_cast<int>(str.size()),
		result.data(),
		sizeNeeded,
		nullptr,
		nullptr
	);

	return result;
}


// ==============================
// ウィンドウプロシージャ
// ==============================

LRESULT CALLBACK WindowProc(
	HWND hwnd,
	UINT msg,
	WPARAM wparam,
	LPARAM lparam
) {

	switch (msg) {

	case WM_DESTROY:

		PostQuitMessage(0);
		return 0;
	}

	return DefWindowProc(hwnd, msg, wparam, lparam);
}


// ==============================
// クラッシュダンプ出力
// ==============================

static LONG WINAPI ExportDump(EXCEPTION_POINTERS* exception) {

	SYSTEMTIME time;
	GetLocalTime(&time);

	CreateDirectory(L"Dumps", nullptr);

	wchar_t filePath[MAX_PATH] = {};

	StringCchPrintfW(
		filePath,
		MAX_PATH,
		L"Dumps/%04d-%02d%02d-%02d%02d.dmp",
		time.wYear,
		time.wMonth,
		time.wDay,
		time.wHour,
		time.wMinute
	);

	HANDLE fileHandle = CreateFile(
		filePath,
		GENERIC_WRITE,
		FILE_SHARE_WRITE,
		nullptr,
		CREATE_ALWAYS,
		FILE_ATTRIBUTE_NORMAL,
		nullptr
	);

	MINIDUMP_EXCEPTION_INFORMATION dumpInfo{};
	dumpInfo.ThreadId = GetCurrentThreadId();
	dumpInfo.ExceptionPointers = exception;
	dumpInfo.ClientPointers = TRUE;

	MiniDumpWriteDump(
		GetCurrentProcess(),
		GetCurrentProcessId(),
		fileHandle,
		MiniDumpNormal,
		&dumpInfo,
		nullptr,
		nullptr
	);

	CloseHandle(fileHandle);

	return EXCEPTION_EXECUTE_HANDLER;

}

	IDxcBlob* CompileShader(
		//CompilerするShaderファイルへのパス
		const std::wstring & filePath,
		//Compilerに使用するProfile
		const wchar_t* profile,
		//初期化で生成したもの3つ
		IDxcUtils * dxcUtils,
		IDxcCompiler3 * dxcCompiler,
		IDxcIncludeHandler * includeHandler)
	{
		//この中身をこの後書いていく
		//1,hlslファイルを読む
		//シェーダーをコンパイルする旨をログに出す
		Log(ConvertString(std::format(L"Begin CompileShader, path!{}, profile;{}\n", filePath, profile)));
		//hlslファイルを読む
		IDxcBlobEncoding* shaderSource = nullptr;
		HRESULT hr = dxcUtils->LoadFile(
			filePath.c_str(),
			nullptr,
			&shaderSource
		);
		//エラーなら止める
		assert(SUCCEEDED(hr));
		//読み込んだファイルの内容を設定する
		DxcBuffer shaderSourceBuffer{};
		shaderSourceBuffer.Ptr = shaderSource->GetBufferPointer();
		shaderSourceBuffer.Size = shaderSource->GetBufferSize();
		shaderSourceBuffer.Encoding = DXC_CP_UTF8;

		//2,Compileする
		LPCWSTR arguments[] = {
			filePath.c_str(), //コンパイルするファイル
			L"-E", L"main", //エントリーポイント
			L"-T", profile, //コンパイルするProfile
			L"-Zi",L"-Qembed_debug", //デバッグ情報を埋め込む
			L"-Od", //最適化なし
			L"-Zpr", //行優先のメモリレイアウト
		};
		//Compileする
		IDxcResult* ShaderResult = nullptr;

		hr = dxcCompiler->Compile(
			&shaderSourceBuffer,		//コンパイルするソースコード
			arguments,					//コンパイルオプション
			_countof(arguments),		//コンパイルオプションの数
			includeHandler,				//includeに対応するための設定
			IID_PPV_ARGS(&ShaderResult) //コンパイル結果
		);

		//コンパイルエラーではなくdxcが起動できないなど致命的な状況
		assert(SUCCEEDED(hr));

		//3,警告やエラーの確認
		IDxcBlobUtf8* shaderError = nullptr;
		ShaderResult->GetOutput(
			DXC_OUT_ERRORS,
			IID_PPV_ARGS(&shaderError),
			nullptr
		);

		if (shaderError != nullptr && shaderError->GetStringLength() != 0)
		{
			Log(shaderError->GetStringPointer());
			//コンパイルエラーがある場合は止める
			assert(false);
		}
		
		//4,Compile結果を受け取って返す
		IDxcBlob* shaderBlob = nullptr;
		hr = ShaderResult->GetOutput(
			DXC_OUT_OBJECT,
			IID_PPV_ARGS(&shaderBlob),
			nullptr
		);
		assert(SUCCEEDED(hr));
		//成功したログを出す
		Log(ConvertString(std::format(L"Compile Succeeded!, path!{}, profile;{}\n", filePath, profile)));
		//もう使わないリソースを解放
		shaderSource->Release();
		ShaderResult->Release();
		//実行用バイナリを返す
		return shaderBlob;


	}


	//Resource作成の関数化
	ID3D12Resource* CreateBufferResource(ID3D12Device* device, size_t sizeInBytes)
	{
		//頂点リソース用のヒープの設定
		D3D12_HEAP_PROPERTIES uploadHeapProperties{};
		uploadHeapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;// UploadHeap
		//頂点リソースの設定
		D3D12_RESOURCE_DESC vertexResourceDesc{};
		//バッファリソース。テクスチャの場合はまた別の設定をする
		vertexResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		vertexResourceDesc.Width = sizeInBytes;//リソースのサイズ。今回はVector4を3頂点分 //バッファの場合はこれらは1にする決まり
		vertexResourceDesc.Height = 1;
		vertexResourceDesc.DepthOrArraySize = 1;
		vertexResourceDesc.MipLevels = 1;
		vertexResourceDesc.SampleDesc.Count = 1; //バッファの場合はこれにする決まり
		vertexResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		//実際に頂点リソースを作る
		ID3D12Resource* resource = nullptr;
		HRESULT hr = device->CreateCommittedResource(&uploadHeapProperties, D3D12_HEAP_FLAG_NONE,
			&vertexResourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
			IID_PPV_ARGS(&resource));
		assert(SUCCEEDED(hr));
		return resource;
	}



// ==============================
// 定数
// ==============================

const int kClientWidth = 1280;
const int kClientHeight = 720;


// ==============================
// エントリーポイント
// ==============================

int WINAPI WinMain(
	HINSTANCE hInstance,
	HINSTANCE hPrevInstance,
	LPSTR lpCmdLine,
	int nCmdShow
) {

	// ==============================
	// クラッシュハンドラ登録
	// ==============================

	SetUnhandledExceptionFilter(ExportDump);


	// ==============================
	// ウィンドウ作成
	// ==============================

	WNDCLASS wc{};

	wc.lpfnWndProc = WindowProc;
	wc.lpszClassName = L"CG2WindowClass";
	wc.hInstance = GetModuleHandle(nullptr);
	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);

	RegisterClass(&wc);

	RECT wrc = { 0,0,kClientWidth,kClientHeight };

	AdjustWindowRect(
		&wrc,
		WS_OVERLAPPEDWINDOW,
		FALSE
	);

	HWND hwnd = CreateWindow(
		wc.lpszClassName,
		L"CG2",
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		wrc.right - wrc.left,
		wrc.bottom - wrc.top,
		nullptr,
		nullptr,
		wc.hInstance,
		nullptr
	);

	assert(hwnd != nullptr);

	ShowWindow(hwnd, SW_SHOW);


	// ==============================
	// DirectX Debug Layer
	// ==============================

#ifdef _DEBUG

	ID3D12Debug1* debugController = nullptr;

	if (SUCCEEDED(
		D3D12GetDebugInterface(
			IID_PPV_ARGS(&debugController)
		)
	)) {

		debugController->EnableDebugLayer();

		debugController->SetEnableGPUBasedValidation(TRUE);
	}

#endif


	// ==============================
	// ログ初期化
	// ==============================

	std::filesystem::create_directory("logs");

	auto now = std::chrono::system_clock::now();

	auto nowSec =
		std::chrono::time_point_cast<std::chrono::seconds>(now);

	std::chrono::zoned_time localTime{
		std::chrono::current_zone(),
		nowSec
	};

	std::string logFilePath =
		"logs/" +
		std::format("{:%Y%m%d_%H%M%S}", localTime) +
		".log";

	std::ofstream logStream(logFilePath);

	Log(logStream, "Application Start");


	// ==============================
	// DXGIファクトリ生成
	// ==============================

	IDXGIFactory7* factory = nullptr;

	HRESULT hr = CreateDXGIFactory(
		IID_PPV_ARGS(&factory)
	);

	assert(SUCCEEDED(hr));


	// ==============================
	// GPUアダプタ取得
	// ==============================

	IDXGIAdapter4* adapter = nullptr;

	for (
		UINT i = 0;
		factory->EnumAdapterByGpuPreference(
			i,
			DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
			IID_PPV_ARGS(&adapter)
		) != DXGI_ERROR_NOT_FOUND;
		++i
		) {

		DXGI_ADAPTER_DESC3 desc{};

		adapter->GetDesc3(&desc);

		if (!(desc.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE)) {

			Log(
				logStream,
				ConvertString(
					std::format(
						L"Use Adapter : {}",
						desc.Description
					)
				)
			);

			break;
		}

		adapter = nullptr;
	}

	assert(adapter != nullptr);

	// ==============================
	// DXC初期化
	// ==============================

	IDxcUtils* dxcUtils = nullptr;
	IDxcCompiler3* dxcCompiler = nullptr;

	hr = DxcCreateInstance(
		CLSID_DxcUtils,
		IID_PPV_ARGS(&dxcUtils)
	);
	assert(SUCCEEDED(hr));

	hr = DxcCreateInstance(
		CLSID_DxcCompiler,
		IID_PPV_ARGS(&dxcCompiler)
	);
	assert(SUCCEEDED(hr));

	//現時点でincludeはしないが、includeに対応するための設定を行っておく
	IDxcIncludeHandler* dxcIncludeHandler = nullptr;

	hr = dxcUtils->CreateDefaultIncludeHandler(
		&dxcIncludeHandler
	);
	assert(SUCCEEDED(hr));


	// ==============================
	// D3D12デバイス生成
	// ==============================

	ID3D12Device* device = nullptr;

	D3D_FEATURE_LEVEL featureLevels[] = {
		D3D_FEATURE_LEVEL_12_2,
		D3D_FEATURE_LEVEL_12_1,
		D3D_FEATURE_LEVEL_12_0
	};

	const char* featureLevelStrings[] = {
		"12.2",
		"12.1",
		"12.0"
	};

	for (size_t i = 0; i < _countof(featureLevels); ++i) {

		hr = D3D12CreateDevice(
			adapter,
			featureLevels[i],
			IID_PPV_ARGS(&device)
		);

		if (SUCCEEDED(hr)) {

			Log(
				logStream,
				std::format(
					"Feature Level {} supported",
					featureLevelStrings[i]
				)
			);

			break;
		}
	}

	assert(device != nullptr);


	// ==========================================
	// PSO作成に必要なもの
	// ==========================================
	
	// RootSignature作成
	D3D12_ROOT_SIGNATURE_DESC descriptionRootSignature{};
	descriptionRootSignature.Flags =
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	//RootParameterを作成。複数設定できるので配列にする。今回は結果一つだけなので長さ１の配列
	D3D12_ROOT_PARAMETER rootParameters[1] = {};
	rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;		// CVBを使う。b0のbと一致する
	rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;		// PixelShaderで使う
	rootParameters[0].Descriptor.ShaderRegister = 0;						// レジスタ番号0。b0のbと一致する。もしb11と紐づけたいなら11となる
	descriptionRootSignature.pParameters = rootParameters;					// ルートパラメータの配列
	descriptionRootSignature.NumParameters = _countof(rootParameters);		// ルートパラメータの数

	// シリアライズしてバイナリにする
	ID3DBlob* signatureBlob = nullptr; ID3DBlob* errorBlob = nullptr;
	hr = D3D12SerializeRootSignature(&descriptionRootSignature,
		D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &errorBlob);
	if (FAILED(hr)) {
		Log(reinterpret_cast<char*>(errorBlob->GetBufferPointer()));
		assert(false);
	}
	// バイナリを元に生成
	ID3D12RootSignature* rootSignature = nullptr;
	hr = device->CreateRootSignature(0,
		signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(),
		IID_PPV_ARGS(&rootSignature));
	assert(SUCCEEDED(hr));


	//InputLayout
	D3D12_INPUT_ELEMENT_DESC inputElementDescs[1] = {};
	inputElementDescs[0].SemanticName = "POSITION";
	inputElementDescs[0].SemanticIndex = 0;
	inputElementDescs[0].Format = DXGI_FORMAT_R32G32B32_FLOAT;
	inputElementDescs[0].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
	D3D12_INPUT_LAYOUT_DESC inputLayoutDesc{};
	inputLayoutDesc.pInputElementDescs = inputElementDescs;
	inputLayoutDesc.NumElements = _countof(inputElementDescs);


	//BlendState
	D3D12_BLEND_DESC blendDesc{};
	//すべての色要素を書き込む
	blendDesc.RenderTarget[0].RenderTargetWriteMask =
		D3D12_COLOR_WRITE_ENABLE_ALL;


	//RasterizerState
	D3D12_RASTERIZER_DESC rasterizerDesc{};
	//裏面(時計回り)を表示しない
	rasterizerDesc.CullMode = D3D12_CULL_MODE_BACK;
	//塗りつぶす
	rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;


	//ShaderをCompile
	IDxcBlob* vertexShaderBlob = CompileShader(L"Object3D.VS.hlsl", 
		L"vs_6_0", dxcUtils, dxcCompiler, dxcIncludeHandler);

	assert(vertexShaderBlob != nullptr);

	IDxcBlob* pixelShaderBlob = CompileShader(L"Object3D.PS.hlsl",
		L"ps_6_0", dxcUtils, dxcCompiler, dxcIncludeHandler);

	assert(pixelShaderBlob != nullptr);

	//PSOの生成
	D3D12_GRAPHICS_PIPELINE_STATE_DESC graphicsPipelineStateDesc{};
	graphicsPipelineStateDesc.pRootSignature = rootSignature;// RootSignature
	graphicsPipelineStateDesc.InputLayout = inputLayoutDesc;// InputLayout
	graphicsPipelineStateDesc.VS = { vertexShaderBlob->GetBufferPointer(), vertexShaderBlob->GetBufferSize() };// VertexShader
	graphicsPipelineStateDesc.PS = { pixelShaderBlob->GetBufferPointer(), pixelShaderBlob->GetBufferSize() };// PixelShader
	graphicsPipelineStateDesc.BlendState = blendDesc;// BlendState
	graphicsPipelineStateDesc.RasterizerState = rasterizerDesc;// RasterizerState
	//書き込むRTVの情報
	graphicsPipelineStateDesc.NumRenderTargets = 1;
	graphicsPipelineStateDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	// 利用するトポロジ (形状)のタイプ。 三角形
	graphicsPipelineStateDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	// どのように画面に色を打ち込むかの設定 (気にしなくて良い) 
	graphicsPipelineStateDesc. SampleDesc.Count = 1; 
	graphicsPipelineStateDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
	// 実際に生成
	ID3D12PipelineState* graphicsPipelineState = nullptr;
	hr = device->CreateGraphicsPipelineState(&graphicsPipelineStateDesc,
		IID_PPV_ARGS(&graphicsPipelineState));
	assert(SUCCEEDED(hr));


	//VertexResourceの生成
	ID3D12Resource* vertexResource = CreateBufferResource(device, sizeof(Vector4) * 3);

	// ==========================================
	// MaterialResourceの生成
	// ==========================================
	ID3D12Resource* materialResource = CreateBufferResource(device, sizeof(Vector4));
	// Materialデータを書き込む
	Vector4* materialData = nullptr;
	materialResource->Map(0,nullptr,reinterpret_cast<void**>(&materialData));
	// 色設定
	*materialData = { 1.0f, 0.0f, 0.0f, 1.0f }; // 赤

	//VertexBufferViewの作成
	//頂点バッファビューを作成する 
	D3D12_VERTEX_BUFFER_VIEW vertexBufferView{};
	// リソースの先頭のアドレスから使う
	vertexBufferView.BufferLocation = vertexResource->GetGPUVirtualAddress();
	// 使用するリソースのサイズは頂点3つ分のサイズ
	vertexBufferView.SizeInBytes = sizeof(Vector4) * 3;
	// 1頂点あたりのサイズ
	vertexBufferView.StrideInBytes = sizeof(Vector4);

	// 頂点データをリソースにコピー
	Vector4* vertexData = nullptr;
	//書き込むためのアドレスを取得
	vertexResource->Map(0, nullptr, reinterpret_cast<void**>(&vertexData));
	//三角形の頂点データ
	//左下
	vertexData[0] = { -0.5f, -0.5f, 0.0f, 1.0f };
	//上
	vertexData[1] = { 0.0f, 0.5f, 0.0f, 1.0f };
	//右下
	vertexData[2] = { 0.5f, -0.5f, 0.0f, 1.0f };

	//Viewportの設定
	D3D12_VIEWPORT viewport{};
	//クライアント領域のサイズに合わせて画面全体に表示
	viewport.Width = kClientWidth;
	viewport.Height = kClientHeight;
	viewport.TopLeftX = 0;
	viewport.TopLeftY = 0;
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;

	//ScissorRectの設定
	D3D12_RECT scissorRect{};
	//基本的にビューポートと同じ矩形が構成されるようにする
	scissorRect.left = 0;
	scissorRect.right = kClientWidth;
	scissorRect.top = 0;
	scissorRect.bottom = kClientHeight;

	// ==============================
	// InfoQueue設定
	// ==============================

#ifdef _DEBUG

	ID3D12InfoQueue* infoQueue = nullptr;

	if (SUCCEEDED(
		device->QueryInterface(
			IID_PPV_ARGS(&infoQueue)
		)
	)) {

		infoQueue->SetBreakOnSeverity(
			D3D12_MESSAGE_SEVERITY_CORRUPTION,
			TRUE
		);

		infoQueue->SetBreakOnSeverity(
			D3D12_MESSAGE_SEVERITY_ERROR,
			TRUE
		);

		infoQueue->SetBreakOnSeverity(
		D3D12_MESSAGE_SEVERITY_WARNING,
		TRUE
		);

		D3D12_MESSAGE_ID denyIds[] = {
			D3D12_MESSAGE_ID_RESOURCE_BARRIER_MISMATCHING_COMMAND_LIST_TYPE,
		};

		D3D12_MESSAGE_SEVERITY severities[] = {
			D3D12_MESSAGE_SEVERITY_INFO
		};

		D3D12_INFO_QUEUE_FILTER filter{};

		filter.DenyList.NumIDs = _countof(denyIds);
		filter.DenyList.pIDList = denyIds;

		filter.DenyList.NumSeverities = _countof(severities);
		filter.DenyList.pSeverityList = severities;

		infoQueue->PushStorageFilter(&filter);

		infoQueue->Release();
		infoQueue = nullptr;
	}

#endif


	// ==============================
	// コマンドキュー生成
	// ==============================

	ID3D12CommandQueue* commandQueue = nullptr;

	D3D12_COMMAND_QUEUE_DESC queueDesc{};

	hr = device->CreateCommandQueue(
		&queueDesc,
		IID_PPV_ARGS(&commandQueue)
	);

	assert(SUCCEEDED(hr));


	// ==============================
	// コマンドアロケータ生成
	// ==============================

	ID3D12CommandAllocator* commandAllocator = nullptr;

	hr = device->CreateCommandAllocator(
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		IID_PPV_ARGS(&commandAllocator)
	);

	assert(SUCCEEDED(hr));


	// ==============================
	// コマンドリスト生成
	// ==============================

	ID3D12GraphicsCommandList* commandList = nullptr;

	hr = device->CreateCommandList(
		0,
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		commandAllocator,
		nullptr,
		IID_PPV_ARGS(&commandList)
	);

	assert(SUCCEEDED(hr));


	// 最初に閉じておく
	hr = commandList->Close();
	assert(SUCCEEDED(hr));


	// ==============================
	// スワップチェーン生成
	// ==============================

	IDXGISwapChain4* swapChain = nullptr;

	DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};

	swapChainDesc.Width = kClientWidth;
	swapChainDesc.Height = kClientHeight;
	swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapChainDesc.SampleDesc.Count = 1;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.BufferCount = 2;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

	hr = factory->CreateSwapChainForHwnd(
		commandQueue,
		hwnd,
		&swapChainDesc,
		nullptr,
		nullptr,
		reinterpret_cast<IDXGISwapChain1**>(&swapChain)
	);

	assert(SUCCEEDED(hr));


	// ==============================
	// RTVヒープ生成
	// ==============================

	ID3D12DescriptorHeap* rtvHeap = nullptr;

	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};

	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvHeapDesc.NumDescriptors = 2;
	rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

	hr = device->CreateDescriptorHeap(
		&rtvHeapDesc,
		IID_PPV_ARGS(&rtvHeap)
	);

	assert(SUCCEEDED(hr));


	// ==============================
	// バックバッファ取得
	// ==============================

	ID3D12Resource* backBuffers[2] = { nullptr };

	for (UINT i = 0; i < 2; ++i) {

		hr = swapChain->GetBuffer(
			i,
			IID_PPV_ARGS(&backBuffers[i])
		);

		assert(SUCCEEDED(hr));
	}


	// ==============================
	// RTV作成
	// ==============================

	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};

	rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	rtvDesc.ViewDimension =
		D3D12_RTV_DIMENSION_TEXTURE2D;

	UINT rtvDescriptorSize =
		device->GetDescriptorHandleIncrementSize(
			D3D12_DESCRIPTOR_HEAP_TYPE_RTV
		);

	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles[2];

	rtvHandles[0] =
		rtvHeap->GetCPUDescriptorHandleForHeapStart();

	rtvHandles[1].ptr =
		rtvHandles[0].ptr + rtvDescriptorSize;

	for (UINT i = 0; i < 2; ++i) {

		device->CreateRenderTargetView(
			backBuffers[i],
			&rtvDesc,
			rtvHandles[i]
		);
	}


	// ==============================
	// Fence生成
	// ==============================

	ID3D12Fence* fence = nullptr;
	uint64_t fenceValue = 0;

	hr = device->CreateFence(
		fenceValue,
		D3D12_FENCE_FLAG_NONE,
		IID_PPV_ARGS(&fence)
	);

	assert(SUCCEEDED(hr));

	HANDLE fenceEvent =
		CreateEvent(nullptr, FALSE, FALSE, nullptr);

	assert(fenceEvent != nullptr);






	// ==============================
	// メインループ
	// ==============================

	MSG msg{};

	while (msg.message != WM_QUIT) {

		if (PeekMessage(
			&msg,
			nullptr,
			0,
			0,
			PM_REMOVE
		)) {

			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		else {

			// ==========================================
			// GPU待機
			// ==========================================

			if (fence->GetCompletedValue() < fenceValue) {

				hr = fence->SetEventOnCompletion(
					fenceValue,
					fenceEvent
				);

				assert(SUCCEEDED(hr));

				WaitForSingleObject(
					fenceEvent,
					INFINITE
				);
			}

			// ==========================================
			// Reset
			// ==========================================

			hr = commandAllocator->Reset();
			assert(SUCCEEDED(hr));

			hr = commandList->Reset(
				commandAllocator,
				nullptr
			);

			assert(SUCCEEDED(hr));

			// ==========================================
			// バックバッファ取得
			// ==========================================

			UINT backBufferIndex =
				swapChain->GetCurrentBackBufferIndex();

			// ==========================================
			// PRESENT → RENDER_TARGET
			// ==========================================

			D3D12_RESOURCE_BARRIER barrier{};

			barrier.Type =
				D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;

			barrier.Flags =
				D3D12_RESOURCE_BARRIER_FLAG_NONE;

			barrier.Transition.pResource =
				backBuffers[backBufferIndex];

			barrier.Transition.StateBefore =
				D3D12_RESOURCE_STATE_PRESENT;

			barrier.Transition.StateAfter =
				D3D12_RESOURCE_STATE_RENDER_TARGET;

			barrier.Transition.Subresource =
				D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

			commandList->ResourceBarrier(1, &barrier);

			// ==========================================
			// 描画先設定
			// ==========================================

			commandList->OMSetRenderTargets(
				1,
				&rtvHandles[backBufferIndex],
				false,
				nullptr
			);

			// ==========================================
			// 画面クリア
			// ==========================================

			float clearColor[] = {
				0.1f,
				0.25f,
				0.5f,
				1.0f
			};

			commandList->ClearRenderTargetView(
				rtvHandles[backBufferIndex],
				clearColor,
				0,
				nullptr
			);


			//三角形の描画
			commandList->RSSetViewports(1, &viewport);			// ビューポートの設定
			commandList->RSSetScissorRects(1, &scissorRect);	// シザリング矩形の設定
			//RootSignatureとPSOに設定してるけど別途設定が必要
			commandList->SetGraphicsRootSignature(rootSignature); // RootSignatureの設定
			commandList->SetPipelineState(graphicsPipelineState); // PSOの設定
			commandList->IASetVertexBuffers(0, 1, &vertexBufferView);			// 頂点バッファビューの設定
			//形状を設定。PSOに設定しているものとはまた別。同じものを設定すると考えておけばいい
			commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST); // トポロジの設定
			//マテリアルCBufferの場所を設定
			commandList->SetGraphicsRootConstantBufferView(0, materialResource->GetGPUVirtualAddress()); // Materialリソースの設定。RootParameterのShaderRegisterと合わせること
			//描画！　(DrawCall/ドローコール)。　3頂点で一つのインスタンス。インスタンスについては今後
			commandList->DrawInstanced(3, 1, 0, 0);

			// ==========================================
			// RENDER_TARGET → PRESENT
			// ==========================================

			barrier.Transition.StateBefore =
				D3D12_RESOURCE_STATE_RENDER_TARGET;

			barrier.Transition.StateAfter =
				D3D12_RESOURCE_STATE_PRESENT;

			commandList->ResourceBarrier(1, &barrier);

			// ==========================================
			// コマンドリスト終了
			// ==========================================

			hr = commandList->Close();
			assert(SUCCEEDED(hr));

			// ==========================================
			// コマンド実行
			// ==========================================

			ID3D12CommandList* commandLists[] = {
				commandList
			};

			commandQueue->ExecuteCommandLists(
				1,
				commandLists
			);

			// ==========================================
			// 画面表示
			// ==========================================

			hr = swapChain->Present(1, 0);
			assert(SUCCEEDED(hr));

			// ==========================================
			// Fenceシグナル送信
			// ==========================================

			fenceValue++;

			hr = commandQueue->Signal(
				fence,
				fenceValue
			);

			assert(SUCCEEDED(hr));
		}
	}


	// ==============================
	// GPU終了待機
	// ==============================

	fenceValue++;

	hr = commandQueue->Signal(
		fence,
		fenceValue
	);

	assert(SUCCEEDED(hr));

	if (fence->GetCompletedValue() < fenceValue) {

		hr = fence->SetEventOnCompletion(
			fenceValue,
			fenceEvent
		);

		assert(SUCCEEDED(hr));

		WaitForSingleObject(
			fenceEvent,
			INFINITE
		);
	}


	// ==============================
	// 終了処理
	// ==============================

	CloseHandle(fenceEvent);

	Log(logStream, "Application End");

	if (fence) {
		fence->Release();
		fence = nullptr;
	}

	if (rtvHeap) {
		rtvHeap->Release();
		rtvHeap = nullptr;
	}

	for (int i = 0; i < 2; ++i) {

		if (backBuffers[i]) {
			backBuffers[i]->Release();
			backBuffers[i] = nullptr;
		}
	}

	if (swapChain) {
		swapChain->Release();
		swapChain = nullptr;
	}

	if (commandList) {
		commandList->Release();
		commandList = nullptr;
	}

	if (commandAllocator) {
		commandAllocator->Release();
		commandAllocator = nullptr;
	}

	if (commandQueue) {
		commandQueue->Release();
		commandQueue = nullptr;
	}

	if (device) {
		device->Release();
		device = nullptr;
	}

	if (adapter) {
		adapter->Release();
		adapter = nullptr;
	}

	if (factory) {
		factory->Release();
		factory = nullptr;
	}

	if(vertexResource){
		vertexResource->Release();
		vertexResource = nullptr;
	}

	if(graphicsPipelineState){
		graphicsPipelineState->Release();
		graphicsPipelineState = nullptr;
	}

	if(signatureBlob){
		signatureBlob->Release();
		signatureBlob = nullptr;
	}

	if(errorBlob){
		errorBlob->Release();
		errorBlob = nullptr;
	}

	if(rootSignature){
		rootSignature->Release();
		rootSignature = nullptr;
	}

	if(pixelShaderBlob){
		pixelShaderBlob->Release();
		pixelShaderBlob = nullptr;
	}

	if(vertexShaderBlob){
		vertexShaderBlob->Release();
		vertexShaderBlob = nullptr;
	}

	if(materialResource){
		materialResource->Release();
		materialResource = nullptr;
	}

#ifdef _DEBUG

	if (debugController) {
		debugController->Release();
		debugController = nullptr;
	}

	//リソースリークチェック
	IDXGIDebug1* debug;

	if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&debug)))) {
		debug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_ALL);
		debug->ReportLiveObjects(DXGI_DEBUG_APP, DXGI_DEBUG_RLO_ALL);
		debug->ReportLiveObjects(DXGI_DEBUG_D3D12, DXGI_DEBUG_RLO_ALL);
		debug->Release();
	}

#endif

	DestroyWindow(hwnd);

	return 0;
}