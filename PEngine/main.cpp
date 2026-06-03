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

#include "Matrix.h"

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


#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#include "externals/imgui/imgui_impl_dx12.h"
#include "externals/imgui/imgui_impl_win32.h"
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
#endif

#include "externals/DirectXTex/DirectXTex.h"

//Transform構造体
struct Transform
{
	Vector3 scale;
	Vector3 rotate;
	Vector3 translate;
};

struct VertexData {
	Vector4 position;
	Vector2 texcoord;
};


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

	#ifdef USE_IMGUI
	// ImGuiへイベントを渡す
	if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam)) {
		return true;
	}
	#endif

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
	const std::wstring& filePath,
	//Compilerに使用するProfile
	const wchar_t* profile,
	//初期化で生成したもの3つ
	IDxcUtils* dxcUtils,
	IDxcCompiler3* dxcCompiler,
	IDxcIncludeHandler* includeHandler)
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

// DescriptorHeap生成関数
ID3D12DescriptorHeap* CreateDescriptorHeap(
	ID3D12Device* device,
	D3D12_DESCRIPTOR_HEAP_TYPE heapType,
	UINT numDescriptors,
	bool shaderVisible
) {
	D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDesc{};

	descriptorHeapDesc.Type = heapType;
	descriptorHeapDesc.NumDescriptors = numDescriptors;

	// ShaderVisible設定
	descriptorHeapDesc.Flags =
		shaderVisible
		? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE
		: D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

	ID3D12DescriptorHeap* descriptorHeap = nullptr;

	HRESULT hr = device->CreateDescriptorHeap(
		&descriptorHeapDesc,
		IID_PPV_ARGS(&descriptorHeap)
	);

	assert(SUCCEEDED(hr));

	return descriptorHeap;
}


DirectX::ScratchImage LoadTexture(const std::string& filePath) 
{
	//テクスチャファイルを作ってほしい読んでプログラムで扱えるようにする
	DirectX::ScratchImage image{};
	std::wstring filePathW = ConvertString(filePath);
	HRESULT hr = DirectX::LoadFromWICFile(filePathW.c_str(), DirectX::WIC_FLAGS_FORCE_SRGB, nullptr, image); assert(SUCCEEDED(hr));
	// ミップマップの作成
	DirectX::ScratchImage mipImages{};
	hr = DirectX::GenerateMipMaps(image.GetImages(), image.GetImageCount(), image.GetMetadata(), DirectX::TEX_FILTER_SRGB, 0, mipImages);
	assert(SUCCEEDED(hr));
	// ミップマップ付きのデータを返す
	return mipImages;
}


ID3D12Resource* CreateTextureResource(ID3D12Device* device, const DirectX::TexMetadata& metadata)
{
	// 1. metadataを基にResourceの設定
	D3D12_RESOURCE_DESC resourceDesc{};
	resourceDesc.Width = UINT(metadata.width); // Textureの幅
	resourceDesc.Height = UINT(metadata.height); // Texture®の高さ
	resourceDesc.MipLevels = UINT16(metadata.mipLevels); // mipmapの数
	resourceDesc.DepthOrArraySize = UINT16(metadata.arraySize); //奥行 or 配列Textureの配列数
	resourceDesc.Format = metadata.format; // TextureのFormat
	resourceDesc.SampleDesc.Count = 1; // サンプリングカウント。 1固定
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION(metadata.dimension); // Textureの次元数 普段使っているのは2次元
	// 2. 利用するHeapの設定
	// 非常に特殊な運用。 02_04exで一般的なケース版がある
	D3D12_HEAP_PROPERTIES heapProperties{};
	heapProperties.Type = D3D12_HEAP_TYPE_CUSTOM; // 細かい設定を行う
	heapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_WRITE_BACK; // writeBackポリシーでCPUアクセス可能
	heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_L0; // プロセッサの近くに配置
	// 3. Resource
	ID3D12Resource* resource = nullptr;
	HRESULT hr = device->CreateCommittedResource(
		&heapProperties, // Heapの設定
		D3D12_HEAP_FLAG_NONE, // Heapの特殊な設定。特になし。
		&resourceDesc, // Resourceの設定
		D3D12_RESOURCE_STATE_GENERIC_READ, //初回のResourceState。Textureは基本読むだけ
		nullptr, // Clear最適値。使わないのでnullptr								
		IID_PPV_ARGS(&resource)); // 作成するResourceポインタへのポインタ
	assert(SUCCEEDED(hr));
	return resource;
}

void UploadTextureData(ID3D12Resource* texture, const DirectX::ScratchImage& mipImages)
{
	// Meta情報を取得
	const DirectX::TexMetadata& metadata = mipImages.GetMetadata();
	// 全MipMapについて
	for (size_t mipLevel = 0; mipLevel < metadata.mipLevels; ++mipLevel) {
		// MipMapLevelを指定して各Imageを取得
		const DirectX::Image* ing = mipImages.GetImage(mipLevel, 0, 0); //Texturel
		HRESULT hr = texture->WriteToSubresource(
		UINT(mipLevel), 
		nullptr,				// 全領域へコピー
		ing->pixels,			// 元データアドレス
		UINT(ing->rowPitch),	// 1ラインサイズ
		UINT(ing->slicePitch)	// 1枚サイズ
		);
		assert(SUCCEEDED(hr));
	}
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

	//comの初期化
	CoInitializeEx(0, COINIT_MULTITHREADED);


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

	D3D12_DESCRIPTOR_RANGE descriptorRange[1] = {};
	descriptorRange[0].BaseShaderRegister = 0;	//0から始まる
	descriptorRange[0].NumDescriptors = 1;		//1個
	descriptorRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;	//SRV
	descriptorRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND; //offsetは自動で計算


	D3D12_STATIC_SAMPLER_DESC staticSamplers[1] = {};
	staticSamplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;			//バイリニアフィルタ
	staticSamplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;		// 0～1の範囲外をリピート
	staticSamplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	staticSamplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	staticSamplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;		// 比較しない
	staticSamplers[0].MaxLOD = D3D12_FLOAT32_MAX;						// ありったけのMipmapを使う
	staticSamplers[0].ShaderRegister = 0;								// レジスタ番号0を使う
	staticSamplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL; // PixelShaderで使う
	descriptionRootSignature.pStaticSamplers = staticSamplers;
	descriptionRootSignature.NumStaticSamplers = _countof(staticSamplers);


	//RootParameterを作成。複数設定できるので配列にする。今回は結果一つだけなので長さ１の配列
	D3D12_ROOT_PARAMETER rootParameters[3] = {};
	rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;		// CVBを使う。b0のbと一致する
	rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;		// PixelShaderで使う
	rootParameters[0].Descriptor.ShaderRegister = 0;						// レジスタ番号0。b0のbと一致する。もしb11と紐づけたいなら11となる
	rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;		// CVBを使う。b1のbと一致する
	rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;	// VertexShaderで使う
	rootParameters[1].Descriptor.ShaderRegister = 0;						// レジスタ番号0。b1のbと一致する。もしb11と紐づけたいなら11となる
	rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;	// ディスクリプタテーブルを使う
	rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;		// PixelShaderで使う
	rootParameters[2].DescriptorTable.pDescriptorRanges = descriptorRange;	// ディスクリプタ範囲
	rootParameters[2].DescriptorTable.NumDescriptorRanges = _countof(descriptorRange);	// ディスクリプタ範囲の数
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
	D3D12_INPUT_ELEMENT_DESC inputElementDescs[2] = {};
	inputElementDescs[0].SemanticName = "POSITION";
	inputElementDescs[0].SemanticIndex = 0;
	inputElementDescs[0].Format = DXGI_FORMAT_R32G32B32_FLOAT;
	inputElementDescs[0].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
	inputElementDescs[1].SemanticName = "TEXCOORD";
	inputElementDescs[1].SemanticIndex = 0;
	inputElementDescs[1].Format = DXGI_FORMAT_R32G32_FLOAT;
	inputElementDescs[1].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
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
	graphicsPipelineStateDesc.SampleDesc.Count = 1;
	graphicsPipelineStateDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
	// 実際に生成
	ID3D12PipelineState* graphicsPipelineState = nullptr;
	hr = device->CreateGraphicsPipelineState(&graphicsPipelineStateDesc,
		IID_PPV_ARGS(&graphicsPipelineState));
	assert(SUCCEEDED(hr));


	//VertexResourceの生成
	ID3D12Resource* vertexResource = CreateBufferResource(device, sizeof(VertexData) * 3);

	//WVP用のリソースを作る。
	ID3D12Resource* wvpResource = CreateBufferResource(device, sizeof(Matrix4x4));

	// ==========================================
	// MaterialResourceの生成
	// ==========================================
	ID3D12Resource* materialResource = CreateBufferResource(device, sizeof(Vector4));
	// Materialデータを書き込む
	Vector4* materialData = nullptr;
	Matrix4x4* wvpData = nullptr;

	materialResource->Map(0, nullptr, reinterpret_cast<void**>(&materialData));

	// 色設定
	// ImGuiで操作する色
	Vector4 materialColor = { 1.0f, 1.0f, 1.0f, 1.0f };
	// 初期値を書き込む
	*materialData = materialColor;

	//VertexBufferViewの作成
// 頂点バッファビューを作成する
	D3D12_VERTEX_BUFFER_VIEW vertexBufferView{};
	// リソースの先頭のアドレスから使う
	vertexBufferView.BufferLocation = vertexResource->GetGPUVirtualAddress();
	// 使用するリソースのサイズは頂点3つ分のサイズ
	vertexBufferView.SizeInBytes = sizeof(VertexData) * 3;
	// 1頂点あたりのサイズ
	vertexBufferView.StrideInBytes = sizeof(VertexData);

	// 頂点データをリソースにコピー
	VertexData* vertexData = nullptr;
	//書き込むためのアドレスを取得
	vertexResource->Map(0, nullptr, reinterpret_cast<void**>(&vertexData));
	wvpResource->Map(0, nullptr, reinterpret_cast<void**>(&wvpData));

	//単位行列を書き込んでおく
	*wvpData = MakeIdentity4x4();

	//三角形の頂点データ
	//左下
	vertexData[0] = { -0.5f, -0.5f, 0.0f, 1.0f };
	vertexData[0].texcoord = { 0.0f, 1.0f };
	//上
	vertexData[1] = { 0.0f, 0.5f, 0.0f, 1.0f };
	vertexData[1].texcoord = { 0.5f, 0.0f };
	//右下
	vertexData[2] = { 0.5f, -0.5f, 0.0f, 1.0f };
	vertexData[2].texcoord = { 1.0f, 1.0f };

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

	ID3D12DescriptorHeap* rtvHeap = CreateDescriptorHeap(
		device,
		D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
		2,
		false
	);

	ID3D12DescriptorHeap* srvDescriptorheap = CreateDescriptorHeap(
		device,
		D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
		128,
		true
	);



	//Texture読み込み
	DirectX::ScratchImage mipImages = LoadTexture("resources/uvChecker.png");
	const DirectX::TexMetadata& metadata = mipImages.GetMetadata();
	ID3D12Resource* textureResource = CreateTextureResource(device, metadata);
	UploadTextureData(textureResource, mipImages);

	// ==========================================
	// SRV作成
	// ==========================================

	// metaDataを基にSRVの設定
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = metadata.format;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;//2Dテクスチャ
	srvDesc.Texture2D.MipLevels = UINT(metadata.mipLevels);

	// SRVを作成するDescriptor Heapの場所を決める
	D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU = srvDescriptorheap->GetCPUDescriptorHandleForHeapStart();
	D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU = srvDescriptorheap->GetGPUDescriptorHandleForHeapStart();
	// 先頭はImGuiが使っているのでその次を使う
	textureSrvHandleCPU.ptr += device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	textureSrvHandleGPU.ptr += device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	// SRVの生成
	device->CreateShaderResourceView(textureResource, &srvDesc, textureSrvHandleCPU);



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


	#ifdef USE_IMGUI
	//imGuiの初期化
	//こういうものとしてとらえる
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui::StyleColorsDark();
	ImGui_ImplWin32_Init(hwnd);
	ImGui_ImplDX12_Init(device,
		swapChainDesc.BufferCount,
		rtvDesc.Format,
		srvDescriptorheap,
		srvDescriptorheap->GetCPUDescriptorHandleForHeapStart(),
		srvDescriptorheap->GetGPUDescriptorHandleForHeapStart());
	ImGuiIO& io = ImGui::GetIO();
	io.Fonts->Build();
	#endif



	//Transform構造体の定義
	Transform transform{ {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} };

	Transform cameraTransform{ {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -5.0f} };


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

			// 描画用のDescriptor Heap の設定
			ID3D12DescriptorHeap* descriptorHeaps[] = { srvDescriptorheap };
			commandList->SetDescriptorHeaps(1, descriptorHeaps);


			// ==========================================
			// ImGui開始
			// ==========================================
			#ifdef USE_IMGUI
			ImGui_ImplDX12_NewFrame();
			ImGui_ImplWin32_NewFrame();
			ImGui::NewFrame();

			ImGui::Begin("Material");

			ImGui::ColorEdit4("Color", &materialColor.x);

			ImGui::End();

			

			// ==========================================
			// ImGui終了
			// ==========================================

			ImGui::Render();
			#endif



			// WVP行列の更新
			transform.rotate.y += 0.03f; // 毎フレームY軸に回転を加える
			Matrix4x4 worldMatrix = MakeAffineMatrix(transform.scale, transform.rotate, transform.translate);
			*wvpData = worldMatrix;
			// 色更新
			*materialData = materialColor;

			// 3次元的にする
			Matrix4x4 cameraMatrix = MakeAffineMatrix(cameraTransform.scale, cameraTransform.rotate, cameraTransform.translate);
			Matrix4x4 viewMatrix = Inverse(cameraMatrix);
			Matrix4x4 projectionMatrix = MakePerspectiveFovMatrix(0.45f, float(kClientWidth) / float(kClientHeight), 0.1f, 100.0f);
			//WVPMatrixを作る
			Matrix4x4 worldViewProjectionMatrix = Multiply(worldMatrix, Multiply(viewMatrix, projectionMatrix));
			*wvpData = worldViewProjectionMatrix;

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
			//WVP行列CBufferの場所を設定
			commandList->SetGraphicsRootConstantBufferView(1, wvpResource->GetGPUVirtualAddress()); // WVPリソースの設定。RootParameterのShaderRegisterと合わせること
			//SRVのDescriptorTableの先頭を設定。2はrootParamater[2]である
			commandList->SetGraphicsRootDescriptorTable(2, textureSrvHandleGPU); // SRVの設定。RootParameterのShaderRegisterと合わせること
			//描画！　(DrawCall/ドローコール)。　3頂点で一つのインスタンス。インスタンスについては今後
			commandList->DrawInstanced(3, 1, 0, 0);

			#ifdef USE_IMGUI
			// 実際のcommandListのImGuiの描画コマンドを積む
			ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList);
			#endif

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

	#ifdef USE_IMGUI
	// ImGuiの終了処理。 
	// こういうもの。 初期化と逆順に行う
	ImGui_ImplDX12_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
	#endif

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

	if (vertexResource) {
		vertexResource->Release();
		vertexResource = nullptr;
	}

	if (graphicsPipelineState) {
		graphicsPipelineState->Release();
		graphicsPipelineState = nullptr;
	}

	if (signatureBlob) {
		signatureBlob->Release();
		signatureBlob = nullptr;
	}

	if (errorBlob) {
		errorBlob->Release();
		errorBlob = nullptr;
	}

	if (rootSignature) {
		rootSignature->Release();
		rootSignature = nullptr;
	}

	if (pixelShaderBlob) {
		pixelShaderBlob->Release();
		pixelShaderBlob = nullptr;
	}

	if (vertexShaderBlob) {
		vertexShaderBlob->Release();
		vertexShaderBlob = nullptr;
	}

	if (materialResource) {
		materialResource->Release();
		materialResource = nullptr;
	}

	if (wvpResource) {
		wvpResource->Release();
		wvpResource = nullptr;
	}

	// Heap解放
	if (srvDescriptorheap) {
		srvDescriptorheap->Release();
		srvDescriptorheap = nullptr;
	}

	// RTVHeap
	if (rtvHeap) {
		rtvHeap->Release();
		rtvHeap = nullptr;
	}

	//Texture
	if (textureResource) {
		textureResource->Release();
		textureResource = nullptr;
	}


#ifdef _DEBUG

	if (debugController) {
		debugController->Release();
		debugController = nullptr;
	}

	//リソースリークチェック
	IDXGIDebug1* debug = nullptr;

	hr = DXGIGetDebugInterface1(0, IID_PPV_ARGS(&debug));

	if (SUCCEEDED(hr) && debug != nullptr) {

		debug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_ALL);
		debug->ReportLiveObjects(DXGI_DEBUG_APP, DXGI_DEBUG_RLO_ALL);
		debug->ReportLiveObjects(DXGI_DEBUG_D3D12, DXGI_DEBUG_RLO_ALL);

		debug->Release();
		debug = nullptr;
	}

#endif

	DestroyWindow(hwnd);

	CoUninitialize();

	return 0;
}