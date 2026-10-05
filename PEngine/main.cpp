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

#include<fstream>
#include<sstream>

#include <xaudio2.h>

#pragma comment(lib,"xaudio2.lib")


#include <numbers>

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

#include "DebugCamera.h"

#pragma warning(pop)


#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#include "externals/imgui/imgui_impl_dx12.h"
#include "externals/imgui/imgui_impl_win32.h"
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
#endif

#include "externals/DirectXTex/DirectXTex.h"
#include "externals/DirectXTex/d3dx12.h"

#include <vector>

#include <wrl.h>
//using Microsoft::WRL::ComPtr;

#include "Input.h"

DebugCamera* gDebugCamera = nullptr;

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
	Vector3 normal;
};

// ==============================
// UVを持たないモデル用頂点データ
// ==============================
struct VertexDataNoUV {
	Vector4 position;
	Vector3 normal;
};

// ==============================
// Lightingの種類
// ==============================
enum class LightingType : int32_t {
	kNone = 0,        // Lightingなし
	kLambert = 1,     // Lambert反射
	kHalfLambert = 2, // Half Lambert
};

// ==============================
// Material
// ==============================
struct Material {
	Vector4 color;
	int32_t lightingType;
	float padding[3];
	Matrix4x4 uvTransform;
};

struct TransformationMatrix {
	Matrix4x4 WVP;
	Matrix4x4 World;
};

struct DirectionalLight {
	Vector4 color;		//ライトの色
	Vector3 direction;	//ライトの向き
	float intensity;	//輝度
};

const uint32_t kSubdivision = 16;
const uint32_t kVertexCount = kSubdivision * kSubdivision * 6;
const float pi = 3.1415926535f;

struct MaterialData {
	std::string name;
	std::string textureFilePath;

	// SRV Heap内で使用する番号
	uint32_t textureIndex = 0;
};

// ==============================
// 1つのMeshが持つデータ
// Meshごとに独立したVertexBufferを持つ
// ==============================
struct MeshData {
	std::string name;
	std::vector<VertexData> vertices;

	// OBJのusemtlで指定されたMaterial
	std::string materialName;
	uint32_t materialIndex = 0;

	Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource = nullptr;
	D3D12_VERTEX_BUFFER_VIEW vertexBufferView{};
};

struct ModelData {
	// 従来の単一Mesh描画との互換用
	std::vector<VertexData> vertices;

	// MultiMesh用
	std::vector<MeshData> meshes;

	// 従来モデルとの互換用
	MaterialData material;

	// MultiMaterial用
	std::vector<MaterialData> materials;
};

struct D3D12ResourceLeakChecker {
	~D3D12ResourceLeakChecker() {
		//リソースリークチェック
		Microsoft::WRL::ComPtr<IDXGIDebug1> debug;
		if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&debug)))) {
			debug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_ALL);
			debug->ReportLiveObjects(DXGI_DEBUG_APP, DXGI_DEBUG_RLO_ALL);
			debug->ReportLiveObjects(DXGI_DEBUG_D3D12, DXGI_DEBUG_RLO_ALL);
		}
	}
};

//チャンクヘッダ
struct ChunkHeader {
	char id[4];		//チャンク毎ID
	int32_t size;	//チャンクサイズ
};

//RIFFヘッダチャンク
struct RiffHeader {
	ChunkHeader chunk;	//"RIFF"
	char type[4];		//"WAVE"
};

//FMTチャンク
struct FormatChunk {
	ChunkHeader chunk;	//"fmt"
	WAVEFORMATEX fmt;	//波形フォーマット
};

//音声データ
struct SoundData {
	//波形フォーマット
	WAVEFORMATEX wfex;
	//バッファの先頭アドレス
	BYTE* pBuffer;
	//バッファのサイズ
	unsigned int bufferSize;
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

	case WM_MOUSEWHEEL:

		if (gDebugCamera != nullptr) {

			const short wheel =
				GET_WHEEL_DELTA_WPARAM(wparam);

			gDebugCamera->AddWheelDelta(
				static_cast<float>(wheel) /
				static_cast<float>(WHEEL_DELTA)
			);
		}

		return 0;


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
Microsoft::WRL::ComPtr<ID3D12Resource> CreateBufferResource(ID3D12Device* device, size_t sizeInBytes)
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
	Microsoft::WRL::ComPtr<ID3D12Resource> resource = nullptr;
	HRESULT hr = device->CreateCommittedResource(&uploadHeapProperties, D3D12_HEAP_FLAG_NONE,
		&vertexResourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
		IID_PPV_ARGS(&resource));
	assert(SUCCEEDED(hr));
	return resource;
}

// DescriptorHeap生成関数
Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> CreateDescriptorHeap(
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

	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptorHeap = nullptr;

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


Microsoft::WRL::ComPtr<ID3D12Resource> CreateTextureResource(ID3D12Device* device, const DirectX::TexMetadata& metadata)
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
	heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

	// 3. Resource
	Microsoft::WRL::ComPtr<ID3D12Resource> resource = nullptr;
	HRESULT hr = device->CreateCommittedResource(
		&heapProperties, // Heapの設定
		D3D12_HEAP_FLAG_NONE, // Heapの特殊な設定。特になし。
		&resourceDesc, // Resourceの設定
		D3D12_RESOURCE_STATE_COPY_DEST, // データ転送される設定
		nullptr, // Clear最適値。使わないのでnullptr								
		IID_PPV_ARGS(&resource)); // 作成するResourceポインタへのポインタ
	assert(SUCCEEDED(hr));
	return resource;
}

[[nodiscard]]
Microsoft::WRL::ComPtr<ID3D12Resource> UploadTextureData(
	ID3D12Resource* texture,
	const DirectX::ScratchImage& mipImages,
	ID3D12Device* device,
	ID3D12GraphicsCommandList* commandList
) {
	// 転送する各MipMapの情報
	std::vector<D3D12_SUBRESOURCE_DATA> subresources;

	DirectX::PrepareUpload(
		device,
		mipImages.GetImages(),
		mipImages.GetImageCount(),
		mipImages.GetMetadata(),
		subresources
	);

	// 転送用バッファに必要なサイズ
	uint64_t intermediateSize =
		GetRequiredIntermediateSize(
			texture,
			0,
			static_cast<UINT>(subresources.size())
		);

	// UploadHeap上の転送用リソース
	Microsoft::WRL::ComPtr<ID3D12Resource> intermediateResource =
		CreateBufferResource(
			device,
			static_cast<size_t>(intermediateSize)
		);

	// UploadHeapからVRAMのTextureへ転送命令を積む
	UpdateSubresources(
		commandList,
		texture,
		intermediateResource.Get(),
		0,
		0,
		static_cast<UINT>(subresources.size()),
		subresources.data()
	);

	// COPY_DESTからシェーダーで読み込める状態へ変更
	D3D12_RESOURCE_BARRIER barrier{};

	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource = texture;
	barrier.Transition.Subresource =
		D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore =
		D3D12_RESOURCE_STATE_COPY_DEST;
	barrier.Transition.StateAfter =
		D3D12_RESOURCE_STATE_GENERIC_READ;

	commandList->ResourceBarrier(1, &barrier);

	return intermediateResource;
}


Microsoft::WRL::ComPtr<ID3D12Resource> CreateDepthStencilTextureResource(
	ID3D12Device* device,
	int32_t width,
	int32_t height
)
{
	// 生成するResourceの設定
	D3D12_RESOURCE_DESC resourceDesc{};
	resourceDesc.Width = width; // Texture の幅
	resourceDesc.Height = height; // Textureの高さ
	resourceDesc.MipLevels = 1; // mipmapの数
	resourceDesc.DepthOrArraySize = 1; // 奥行　or 配列Textureの配列数
	resourceDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT; // DepthStencilとして利用可能なフォーマット
	resourceDesc.SampleDesc.Count = 1; // サンプリングカウント。 1固定
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; // 2次元
	resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL; // DepthStencilとして使う通知
	// 利用するHeapの設定
	D3D12_HEAP_PROPERTIES heapProperties{};
	heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT; // VRAM上に配置

	// 深度値のクリア設定
	D3D12_CLEAR_VALUE depthClearValue{};
	depthClearValue.DepthStencil.Depth = 1.0f; // 1.0f (最大値) でクリア
	depthClearValue.Format = DXGI_FORMAT_D24_UNORM_S8_UINT; // フォーマット。 Resourceと合わせる

	// Resource
	Microsoft::WRL::ComPtr<ID3D12Resource> resource = nullptr;
	HRESULT hr = device->CreateCommittedResource(
		&heapProperties,					// Heapoの設定
		D3D12_HEAP_FLAG_NONE,				// Heapの特殊な設定。特になし。
		&resourceDesc,						// Resourceの設定
		D3D12_RESOURCE_STATE_DEPTH_WRITE,	//深度値を書き込む状態で開始
		&depthClearValue,					// Clear最適値
		IID_PPV_ARGS(&resource));				// 作成するResourceポインタへのポインタ 
	assert(SUCCEEDED(hr));

	return resource;
}

D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHandle(Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptorHeap, uint32_t descriptorSize, uint32_t index)
{
	D3D12_CPU_DESCRIPTOR_HANDLE handleCPU = descriptorHeap->GetCPUDescriptorHandleForHeapStart();
	handleCPU.ptr += (descriptorSize * index);
	return handleCPU;
}

D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandle(Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptorHeap, uint32_t descriptorSize, uint32_t index)
{
	D3D12_GPU_DESCRIPTOR_HANDLE handleGPU = descriptorHeap->GetGPUDescriptorHandleForHeapStart();
	handleGPU.ptr += (descriptorSize * index);
	return handleGPU;
}

// ==============================
// MTLファイル読み込み
// newmtlごとにMaterialを分割する
// ==============================
std::vector<MaterialData> LoadMaterialTemplateFile(
	const std::string& directoryPath,
	const std::string& filename
) {
	std::vector<MaterialData> materials;

	std::ifstream file(directoryPath + "/" + filename);
	assert(file.is_open());

	MaterialData currentMaterial{};
	bool hasMaterial = false;

	std::string line;

	while (std::getline(file, line)) {
		std::istringstream s(line);

		std::string identifier;
		s >> identifier;

		if (identifier.empty() || identifier[0] == '#') {
			continue;
		}

		if (identifier == "newmtl") {
			if (hasMaterial) {
				materials.push_back(currentMaterial);
			}

			currentMaterial = MaterialData{};
			s >> currentMaterial.name;
			hasMaterial = true;
		}
		else if (identifier == "map_Kd") {
			std::string textureFilename;
			s >> textureFilename;

			currentMaterial.textureFilePath =
				directoryPath + "/" + textureFilename;
		}
	}

	if (hasMaterial) {
		materials.push_back(currentMaterial);
	}

	return materials;
}

// ==============================
// Material名から配列番号を検索
// ==============================
uint32_t FindMaterialIndex(
	const ModelData& modelData,
	const std::string& materialName
) {
	for (
		uint32_t i = 0;
		i < static_cast<uint32_t>(modelData.materials.size());
		++i
		) {
		if (modelData.materials[i].name == materialName) {
			return i;
		}
	}

	return 0;
}

ModelData LoadObjectFile(
	const std::string& directoryPath,
	const std::string& filename
) {
	ModelData modelData;

	std::vector<Vector4> positions;
	std::vector<Vector3> normals;
	std::vector<Vector2> texcoords;

	MeshData currentMesh;
	bool hasCurrentMesh = false;
	std::string currentMaterialName;

	std::ifstream file(directoryPath + "/" + filename);
	assert(file.is_open());

	auto ToArrayIndex = [](int32_t objIndex, size_t arraySize) -> size_t {
		if (objIndex > 0) {
			return static_cast<size_t>(objIndex - 1);
		}

		return static_cast<size_t>(
			static_cast<int64_t>(arraySize) + objIndex
			);
		};

	auto PushCurrentMesh = [&]() {
		if (hasCurrentMesh && !currentMesh.vertices.empty()) {
			if (currentMesh.materialName.empty()) {
				currentMesh.materialName = currentMaterialName;
			}

			modelData.meshes.push_back(std::move(currentMesh));
			currentMesh = MeshData{};
		}
		};

	std::string line;

	while (std::getline(file, line)) {
		std::istringstream s(line);

		std::string identifier;
		s >> identifier;

		if (identifier.empty() || identifier[0] == '#') {
			continue;
		}

		if (identifier == "o" || identifier == "g") {
			PushCurrentMesh();

			currentMesh = MeshData{};
			s >> currentMesh.name;

			if (currentMesh.name.empty()) {
				currentMesh.name =
					"Mesh" + std::to_string(modelData.meshes.size());
			}

			currentMaterialName.clear();
			hasCurrentMesh = true;
		}
		else if (identifier == "usemtl") {
			std::string nextMaterialName;
			s >> nextMaterialName;

			// 同じo/gの途中でMaterialが変わるケースにも対応
			if (
				hasCurrentMesh &&
				!currentMesh.vertices.empty() &&
				currentMesh.materialName != nextMaterialName
				) {
				const std::string meshName = currentMesh.name;
				PushCurrentMesh();

				currentMesh = MeshData{};
				currentMesh.name =
					meshName + "_Material" +
					std::to_string(modelData.meshes.size());
				hasCurrentMesh = true;
			}

			currentMaterialName = nextMaterialName;
			currentMesh.materialName = currentMaterialName;
		}
		else if (identifier == "v") {
			Vector4 position{};
			s >> position.x >> position.y >> position.z;

			position.x *= -1.0f;
			position.w = 1.0f;
			positions.push_back(position);
		}
		else if (identifier == "vt") {
			Vector2 texcoord{};
			s >> texcoord.x >> texcoord.y;

			texcoord.y = 1.0f - texcoord.y;
			texcoords.push_back(texcoord);
		}
		else if (identifier == "vn") {
			Vector3 normal{};
			s >> normal.x >> normal.y >> normal.z;

			normal.x *= -1.0f;
			normals.push_back(normal);
		}
		else if (identifier == "f") {
			if (!hasCurrentMesh) {
				currentMesh = MeshData{};
				currentMesh.name = "DefaultMesh";
				currentMesh.materialName = currentMaterialName;
				hasCurrentMesh = true;
			}

			std::vector<VertexData> faceVertices;
			std::string vertexDefinition;

			while (s >> vertexDefinition) {
				std::istringstream vertexStream(vertexDefinition);

				std::string positionIndexString;
				std::string texcoordIndexString;
				std::string normalIndexString;

				std::getline(vertexStream, positionIndexString, '/');
				std::getline(vertexStream, texcoordIndexString, '/');
				std::getline(vertexStream, normalIndexString, '/');

				assert(!positionIndexString.empty());

				const int32_t positionIndex =
					std::stoi(positionIndexString);

				const size_t positionArrayIndex =
					ToArrayIndex(positionIndex, positions.size());

				assert(positionArrayIndex < positions.size());

				Vector2 texcoord{ 0.0f, 0.0f };
				Vector3 normal{ 0.0f, 1.0f, 0.0f };

				if (!texcoordIndexString.empty()) {
					const int32_t texcoordIndex =
						std::stoi(texcoordIndexString);

					const size_t texcoordArrayIndex =
						ToArrayIndex(texcoordIndex, texcoords.size());

					assert(texcoordArrayIndex < texcoords.size());
					texcoord = texcoords[texcoordArrayIndex];
				}

				if (!normalIndexString.empty()) {
					const int32_t normalIndex =
						std::stoi(normalIndexString);

					const size_t normalArrayIndex =
						ToArrayIndex(normalIndex, normals.size());

					assert(normalArrayIndex < normals.size());
					normal = normals[normalArrayIndex];
				}

				faceVertices.push_back({
					positions[positionArrayIndex],
					texcoord,
					normal
					});
			}

			for (
				size_t i = 1;
				i + 1 < faceVertices.size();
				++i
				) {
				const VertexData triangle[3] = {
					faceVertices[i + 1],
					faceVertices[i],
					faceVertices[0]
				};

				for (const VertexData& vertex : triangle) {
					currentMesh.vertices.push_back(vertex);
					modelData.vertices.push_back(vertex);
				}
			}
		}
		else if (identifier == "mtllib") {
			std::string materialFilename;
			s >> materialFilename;

			modelData.materials =
				LoadMaterialTemplateFile(
					directoryPath,
					materialFilename
				);
		}
	}

	PushCurrentMesh();

	for (MeshData& mesh : modelData.meshes) {
		mesh.materialIndex =
			FindMaterialIndex(
				modelData,
				mesh.materialName
			);
	}

	// 従来の単一Material参照との互換性を維持
	if (!modelData.materials.empty()) {
		modelData.material = modelData.materials.front();
	}

	return modelData;
}

// ==============================
// ModelData内の全MeshにVertexBufferを作成
// ==============================
void CreateMeshVertexBuffers(
	ID3D12Device* device,
	ModelData& modelData
) {
	assert(device != nullptr);

	for (MeshData& mesh : modelData.meshes) {
		assert(!mesh.vertices.empty());

		mesh.vertexResource =
			CreateBufferResource(
				device,
				sizeof(VertexData) *
				mesh.vertices.size()
			);

		VertexData* mappedVertexData = nullptr;

		HRESULT hr = mesh.vertexResource->Map(
			0,
			nullptr,
			reinterpret_cast<void**>(
				&mappedVertexData
				)
		);
		assert(SUCCEEDED(hr));

		std::memcpy(
			mappedVertexData,
			mesh.vertices.data(),
			sizeof(VertexData) *
			mesh.vertices.size()
		);

		mesh.vertexBufferView.BufferLocation =
			mesh.vertexResource->GetGPUVirtualAddress();

		mesh.vertexBufferView.SizeInBytes =
			static_cast<UINT>(
				sizeof(VertexData) *
				mesh.vertices.size()
				);

		mesh.vertexBufferView.StrideInBytes =
			sizeof(VertexData);
	}
}

// ==============================
// UVなしOBJ読み込み
// v//vn または v/vt/vn に対応
// 三角形・四角形などは三角形へ分割する
// ==============================
std::vector<VertexDataNoUV> LoadObjectFileNoUV(
	const std::string& directoryPath,
	const std::string& filename
) {
	std::vector<VertexDataNoUV> vertices;
	std::vector<Vector4> positions;
	std::vector<Vector3> normals;

	std::ifstream file(directoryPath + "/" + filename);
	assert(file.is_open());

	std::string line;

	while (std::getline(file, line)) {
		std::istringstream s(line);
		std::string identifier;
		s >> identifier;

		if (identifier == "v") {
			Vector4 position{};
			s >> position.x >> position.y >> position.z;

			// 右手系から左手系へ変換
			position.x *= -1.0f;
			position.w = 1.0f;
			positions.push_back(position);
		}
		else if (identifier == "vn") {
			Vector3 normal{};
			s >> normal.x >> normal.y >> normal.z;

			// 右手系から左手系へ変換
			normal.x *= -1.0f;
			normals.push_back(normal);
		}
		else if (identifier == "f") {
			std::vector<VertexDataNoUV> faceVertices;
			std::string vertexDefinition;

			while (s >> vertexDefinition) {
				std::istringstream vertexStream(vertexDefinition);
				std::string positionIndexString;
				std::string texcoordIndexString;
				std::string normalIndexString;

				std::getline(vertexStream, positionIndexString, '/');
				std::getline(vertexStream, texcoordIndexString, '/');
				std::getline(vertexStream, normalIndexString, '/');

				assert(!positionIndexString.empty());
				assert(!normalIndexString.empty());

				const int32_t positionIndex = std::stoi(positionIndexString);
				const int32_t normalIndex = std::stoi(normalIndexString);

				auto ToArrayIndex = [](int32_t objIndex, size_t arraySize) -> size_t {
					if (objIndex > 0) {
						return static_cast<size_t>(objIndex - 1);
					}
					return static_cast<size_t>(static_cast<int64_t>(arraySize) + objIndex);
					};

				const size_t positionArrayIndex = ToArrayIndex(positionIndex, positions.size());
				const size_t normalArrayIndex = ToArrayIndex(normalIndex, normals.size());

				assert(positionArrayIndex < positions.size());
				assert(normalArrayIndex < normals.size());

				faceVertices.push_back({
					positions[positionArrayIndex],
					normals[normalArrayIndex]
					});
			}

			// 3頂点以上の面を三角形ファンへ変換する
			for (size_t i = 1; i + 1 < faceVertices.size(); ++i) {
				// 座標系変換に合わせて頂点順を反転
				vertices.push_back(faceVertices[i + 1]);
				vertices.push_back(faceVertices[i]);
				vertices.push_back(faceVertices[0]);
			}
		}
	}

	return vertices;
}


SoundData SoundLoadWave(const char* filename) {

	//①ファイルオープン
	//ファイル入力ストリームのインスタンス
	std::ifstream file;
	//.wavファイルをバイナリモードで開く
	file.open(filename, std::ios_base::binary);
	//ファイルオープン失敗を検出する
	assert(file.is_open());

	//②.wavデータ読み込み
	//RIFFヘッダーの読み込み
	RiffHeader riff;
	file.read((char*)&riff, sizeof(riff));
	//ファイルがRIFFかチェック
	if (strncmp(riff.chunk.id, "RIFF", 4) != 0) {
		assert(0);
	}
	//タイプがWAVEかチェック
	if (strncmp(riff.type, "WAVE", 4) != 0) {
		assert(0);
	}

	//Formatチャンクの読み込み
	FormatChunk format = {};
	//チャンクヘッダーの確認
	file.read((char*)&format, sizeof(ChunkHeader));
	if (strncmp(format.chunk.id, "fmt ", 4) != 0) {
		assert(0);
	}

	//チャンク本体の読み込み
	assert(format.chunk.size <= sizeof(format.fmt));
	file.read((char*)&format.fmt, format.chunk.size);

	//Dataチャンクの読み込み 
	ChunkHeader data;
	file.read((char*)&data, sizeof(data));
	//JUNKチャンクを検出した場合
	if (strncmp(data.id, "JUNK", 4) == 0) {
		// 読み取り位置をJUNKチャンクの終わりまで進める
		file.seekg(data.size, std::ios_base::cur);
		// 再読み込み
		file.read((char*)&data, sizeof(data));
	}

	if (strncmp(data.id, "data", 4) != 0) {
		assert(0);
	}

	// Data チャンクのデータ部 (波形データ) の読み込み 
	char* pBuffer = new char[data.size];
	file.read(pBuffer, data.size);

	char id[5] = {};
	memcpy(id, data.id, 4);
	id[4] = '\0';

	OutputDebugStringA(id);
	OutputDebugStringA("\n");

	//③ファイルクローズ
	//Wave ファイルを閉じる 
	file.close();

	//④読み込んだ音声データをreturn
	//returnする為の音声データ
	SoundData soundData = {};

	soundData.wfex = format.fmt;
	soundData.pBuffer = reinterpret_cast<BYTE*>(pBuffer);
	soundData.bufferSize = data.size;
	return soundData;
}

//音声データ解放
void SoundUnload(SoundData* soundData)
{
	// バッファのメモリを解放 
	delete[] soundData->pBuffer;
	soundData->pBuffer = 0;
	soundData->bufferSize = 0;
	soundData->wfex = {};
}

//音声再生
void SoundPlayWave(IXAudio2* xAudio2, const SoundData& soundData) {

	HRESULT result;

	// 波形フォーマットを元にSourceVoiceの生成
	IXAudio2SourceVoice* pSourceVoice = nullptr;
	result = xAudio2->CreateSourceVoice(&pSourceVoice, &soundData.wfex);
	assert(SUCCEEDED(result));

	// 再生する波形データの設定 
	XAUDIO2_BUFFER buf{};
	buf.pAudioData = soundData.pBuffer;
	buf.AudioBytes = soundData.bufferSize;
	buf.Flags = XAUDIO2_END_OF_STREAM;

	// 波形データの再生
	result = pSourceVoice->SubmitSourceBuffer(&buf);
	result = pSourceVoice->Start();
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
	D3D12ResourceLeakChecker leakChecker;

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
		L"CG3",
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

	Microsoft::WRL::ComPtr<ID3D12Debug1> debugController = nullptr;

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

	Microsoft::WRL::ComPtr<IDXGIFactory7> factory = nullptr;

	HRESULT hr = CreateDXGIFactory(
		IID_PPV_ARGS(&factory)
	);

	assert(SUCCEEDED(hr));


	// ==============================
	// GPUアダプタ取得
	// ==============================

	Microsoft::WRL::ComPtr<IDXGIAdapter4> adapter = nullptr;

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
	Microsoft::WRL::ComPtr<IDxcIncludeHandler> dxcIncludeHandler = nullptr;

	hr = dxcUtils->CreateDefaultIncludeHandler(
		&dxcIncludeHandler
	);
	assert(SUCCEEDED(hr));


	// ==============================
	// D3D12デバイス生成
	// ==============================

	Microsoft::WRL::ComPtr<ID3D12Device> device = nullptr;

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
			adapter.Get(),
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


	// DepthstencilTextureをウィンドウのサイズで作成
	Microsoft::WRL::ComPtr<ID3D12Resource> depthStencilResource
		= CreateDepthStencilTextureResource(device.Get(), kClientWidth, kClientHeight);


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
	D3D12_ROOT_PARAMETER rootParameters[4] = {};
	rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;		// CVBを使う。b0のbと一致する
	rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;		// PixelShaderで使う
	rootParameters[0].Descriptor.ShaderRegister = 0;						// レジスタ番号0。b0のbと一致する。もしb11と紐づけたいなら11となる

	rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;		// CVBを使う。b1のbと一致する
	rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;	// VertexShaderで使う
	rootParameters[1].Descriptor.ShaderRegister = 1;						// レジスタ番号1。b1のbと一致する。もしb11と紐づけたいなら11となる

	rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;	// ディスクリプタテーブルを使う
	rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;		// PixelShaderで使う
	rootParameters[2].DescriptorTable.pDescriptorRanges = descriptorRange;	// ディスクリプタ範囲
	rootParameters[2].DescriptorTable.NumDescriptorRanges = _countof(descriptorRange);	// ディスクリプタ範囲の数

	rootParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;		// CVBを使う。
	rootParameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;		// PixelShaderで使う
	rootParameters[3].Descriptor.ShaderRegister = 1;						// レジスタ番号1。

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
	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature = nullptr;
	hr = device->CreateRootSignature(0,
		signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(),
		IID_PPV_ARGS(&rootSignature));
	assert(SUCCEEDED(hr));


	// ==========================================
	// Suzanne用RootSignature
	// TextureとSamplerを使わない
	// ==========================================
	D3D12_ROOT_PARAMETER rootParametersSuzanne[3]{};

	// Material : b0 / PixelShader
	rootParametersSuzanne[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParametersSuzanne[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParametersSuzanne[0].Descriptor.ShaderRegister = 0;

	// TransformationMatrix : b1 / VertexShader
	rootParametersSuzanne[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParametersSuzanne[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
	rootParametersSuzanne[1].Descriptor.ShaderRegister = 1;

	// DirectionalLight : b1 / PixelShader
	rootParametersSuzanne[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParametersSuzanne[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParametersSuzanne[2].Descriptor.ShaderRegister = 1;

	D3D12_ROOT_SIGNATURE_DESC descriptionRootSignatureSuzanne{};
	descriptionRootSignatureSuzanne.Flags =
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
	descriptionRootSignatureSuzanne.pParameters = rootParametersSuzanne;
	descriptionRootSignatureSuzanne.NumParameters = _countof(rootParametersSuzanne);

	Microsoft::WRL::ComPtr<ID3DBlob> signatureBlobSuzanne = nullptr;
	Microsoft::WRL::ComPtr<ID3DBlob> errorBlobSuzanne = nullptr;

	hr = D3D12SerializeRootSignature(
		&descriptionRootSignatureSuzanne,
		D3D_ROOT_SIGNATURE_VERSION_1,
		&signatureBlobSuzanne,
		&errorBlobSuzanne
	);

	if (FAILED(hr)) {
		if (errorBlobSuzanne) {
			Log(reinterpret_cast<char*>(errorBlobSuzanne->GetBufferPointer()));
		}
		assert(false);
	}

	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignatureSuzanne = nullptr;
	hr = device->CreateRootSignature(
		0,
		signatureBlobSuzanne->GetBufferPointer(),
		signatureBlobSuzanne->GetBufferSize(),
		IID_PPV_ARGS(&rootSignatureSuzanne)
	);
	assert(SUCCEEDED(hr));



	//InputLayout
	D3D12_INPUT_ELEMENT_DESC inputElementDescs[3] = {};
	inputElementDescs[0].SemanticName = "POSITION";
	inputElementDescs[0].SemanticIndex = 0;
	inputElementDescs[0].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	inputElementDescs[0].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

	inputElementDescs[1].SemanticName = "TEXCOORD";
	inputElementDescs[1].SemanticIndex = 0;
	inputElementDescs[1].Format = DXGI_FORMAT_R32G32_FLOAT;
	inputElementDescs[1].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

	inputElementDescs[2].SemanticName = "NORMAL";
	inputElementDescs[2].SemanticIndex = 0;
	inputElementDescs[2].Format = DXGI_FORMAT_R32G32B32_FLOAT;
	inputElementDescs[2].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

	D3D12_INPUT_LAYOUT_DESC inputLayoutDesc{};
	inputLayoutDesc.pInputElementDescs = inputElementDescs;
	inputLayoutDesc.NumElements = _countof(inputElementDescs);


	// ==========================================
	// Suzanne用InputLayout（POSITION・NORMALのみ）
	// ==========================================
	D3D12_INPUT_ELEMENT_DESC inputElementDescsSuzanne[2]{};

	inputElementDescsSuzanne[0].SemanticName = "POSITION";
	inputElementDescsSuzanne[0].SemanticIndex = 0;
	inputElementDescsSuzanne[0].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	inputElementDescsSuzanne[0].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

	inputElementDescsSuzanne[1].SemanticName = "NORMAL";
	inputElementDescsSuzanne[1].SemanticIndex = 0;
	inputElementDescsSuzanne[1].Format = DXGI_FORMAT_R32G32B32_FLOAT;
	inputElementDescsSuzanne[1].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

	D3D12_INPUT_LAYOUT_DESC inputLayoutDescSuzanne{};
	inputLayoutDescSuzanne.pInputElementDescs = inputElementDescsSuzanne;
	inputLayoutDescSuzanne.NumElements = _countof(inputElementDescsSuzanne);


	//BlendState
	D3D12_BLEND_DESC blendDesc{};
	//すべての色要素を書き込む
	blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	blendDesc.RenderTarget[0].BlendEnable = TRUE;

	blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_ZERO;
	blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
	blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_SRC_COLOR;
	blendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
	blendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
	blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;

	//RasterizerState
	D3D12_RASTERIZER_DESC rasterizerDesc{};
	//裏面(時計回り)を表示しない
	rasterizerDesc.CullMode = D3D12_CULL_MODE_BACK;
	//塗りつぶす
	rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;


	//ShaderをCompile
	Microsoft::WRL::ComPtr<IDxcBlob> vertexShaderBlob = CompileShader(L"Object3d.VS.hlsl",
		L"vs_6_0", dxcUtils, dxcCompiler, dxcIncludeHandler.Get());

	assert(vertexShaderBlob != nullptr);

	Microsoft::WRL::ComPtr<IDxcBlob> pixelShaderBlob = CompileShader(L"Object3d.PS.hlsl",
		L"ps_6_0", dxcUtils, dxcCompiler, dxcIncludeHandler.Get());

	assert(pixelShaderBlob != nullptr);


	// ==========================================
	// Suzanne用ShaderをCompile
	// ==========================================
	Microsoft::WRL::ComPtr<IDxcBlob> vertexShaderBlobSuzanne =
		CompileShader(
			L"Suzanne.VS.hlsl",
			L"vs_6_0",
			dxcUtils,
			dxcCompiler,
			dxcIncludeHandler.Get()
		);
	assert(vertexShaderBlobSuzanne != nullptr);

	Microsoft::WRL::ComPtr<IDxcBlob> pixelShaderBlobSuzanne =
		CompileShader(
			L"Suzanne.PS.hlsl",
			L"ps_6_0",
			dxcUtils,
			dxcCompiler,
			dxcIncludeHandler.Get()
		);
	assert(pixelShaderBlobSuzanne != nullptr);

	//PSOの生成
	D3D12_GRAPHICS_PIPELINE_STATE_DESC graphicsPipelineStateDesc{};
	graphicsPipelineStateDesc.pRootSignature = rootSignature.Get();// RootSignature
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


	//DepthStencil用のPSOも生成する。
	D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};
	//Depthの機能を有効化する
	depthStencilDesc.DepthEnable = true;
	//書き込みします
	depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	//比較関数はLessEqual。近いものほど前に表示される
	depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

	// ==========================================
	// 入力処理初期化処理
	// ==========================================

	Input* input = new Input();

	input->Initialize(
		hInstance,
		hwnd
	);


	//==============================
	// 球生成用
	//==============================

	// VertexResourceの生成
	// 球専用の頂点リソース
	Microsoft::WRL::ComPtr<ID3D12Resource> vertexResourceSphere =
		CreateBufferResource(
			device.Get(),
			sizeof(VertexData) * kVertexCount
		);

	// ==============================
	// Plane読み込み
	// ==============================
	ModelData modelDataPlane =
		LoadObjectFile(
			"resources",
			"plane.obj"
		);

	// ==============================
	// Utah Teapot読み込み
	// ==============================
	ModelData modelDataTeapot =
		LoadObjectFile(
			"resources",
			"teapot.obj"
		);


	// ==============================
	// Stanford Bunny読み込み
	// ==============================
	ModelData modelDataBunny =
		LoadObjectFile(
			"resources",
			"bunny.obj"
		);

	// ==============================
	// Fence読み込み
	// ==============================
	ModelData modelDataFence =
		LoadObjectFile(
			"resources",
			"fence.obj"
		);

	assert(!modelDataFence.vertices.empty());

	// ==============================
	// MultiMesh読み込み
	// PlaneとCubeを別々のMeshとして読み込む
	// ==============================
	ModelData modelDataMultiMesh =
		LoadObjectFile(
			"resources",
			"multiMesh.obj"
		);

	assert(!modelDataMultiMesh.meshes.empty());

	// Meshごとに独立したVertexBufferを作成
	CreateMeshVertexBuffers(
		device.Get(),
		modelDataMultiMesh
	);

	// ==============================
	// MultiMaterial読み込み
	// PlaneとCubeで異なるTextureを使用する
	// ==============================
	ModelData modelDataMultiMaterial =
		LoadObjectFile(
			"resources",
			"multiMaterial.obj"
		);

	assert(!modelDataMultiMaterial.meshes.empty());
	assert(!modelDataMultiMaterial.materials.empty());

	CreateMeshVertexBuffers(
		device.Get(),
		modelDataMultiMaterial
	);


	// ==============================
	// Suzanne読み込み（UVなし）
	// ==============================
	std::vector<VertexDataNoUV> modelDataSuzanne =
		LoadObjectFileNoUV(
			"resources",
			"suzanne.obj"
		);
	assert(!modelDataSuzanne.empty());

	// ==============================
	// Plane用頂点リソース
	// ==============================
	Microsoft::WRL::ComPtr<ID3D12Resource> vertexResourcePlane =
		CreateBufferResource(
			device.Get(),
			sizeof(VertexData) * modelDataPlane.vertices.size()
		);

	// ==============================
	// Utah Teapot用頂点リソース
	// ==============================
	Microsoft::WRL::ComPtr<ID3D12Resource> vertexResourceTeapot =
		CreateBufferResource(
			device.Get(),
			sizeof(VertexData) * modelDataTeapot.vertices.size()
		);

	// ==============================
	// Stanford Bunny用頂点リソース
	// ==============================
	Microsoft::WRL::ComPtr<ID3D12Resource>
		vertexResourceBunny =
		CreateBufferResource(
			device.Get(),
			sizeof(VertexData) *
			modelDataBunny.vertices.size()
		);

	// ==============================
	// Fence用頂点リソース
	// ==============================
	Microsoft::WRL::ComPtr<ID3D12Resource>
		vertexResourceFence =
		CreateBufferResource(
			device.Get(),
			sizeof(VertexData) *
			modelDataFence.vertices.size()
		);

	// ==============================
	// Suzanne用頂点リソース
	// ==============================
	Microsoft::WRL::ComPtr<ID3D12Resource> vertexResourceSuzanne =
		CreateBufferResource(
			device.Get(),
			sizeof(VertexDataNoUV) * modelDataSuzanne.size()
		);


	//indexResourceSpriteの生成
	Microsoft::WRL::ComPtr<ID3D12Resource> indexResourceSprite =
		CreateBufferResource(device.Get(), sizeof(uint32_t) * 6);

	// ==============================
	// Plane用WVP Resource
	// ==============================
	Microsoft::WRL::ComPtr<ID3D12Resource> wvpResourcePlane =
		CreateBufferResource(
			device.Get(),
			sizeof(TransformationMatrix)
		);

	// ==============================
	// Sphere用WVP Resource
	// ==============================
	Microsoft::WRL::ComPtr<ID3D12Resource> wvpResourceSphere =
		CreateBufferResource(
			device.Get(),
			sizeof(TransformationMatrix)
		);

	// ==============================
	// Utah Teapot用WVP Resource
	// ==============================
	Microsoft::WRL::ComPtr<ID3D12Resource> wvpResourceTeapot =
		CreateBufferResource(
			device.Get(),
			sizeof(TransformationMatrix)
		);


	// ==============================
	// Stanford Bunny用WVP Resource
	// ==============================
	Microsoft::WRL::ComPtr<ID3D12Resource>
		wvpResourceBunny =
		CreateBufferResource(
			device.Get(),
			sizeof(TransformationMatrix)
		);

	TransformationMatrix* wvpDataBunny = nullptr;

	// ==============================
	// MultiMesh用WVP Resource
	// 全Meshで同じTransformを使用する
	// ==============================
	Microsoft::WRL::ComPtr<ID3D12Resource>
		wvpResourceMultiMesh =
		CreateBufferResource(
			device.Get(),
			sizeof(TransformationMatrix)
		);

	TransformationMatrix* wvpDataMultiMesh = nullptr;

	// ==============================
	// MultiMaterial用WVP Resource
	// ==============================
	Microsoft::WRL::ComPtr<ID3D12Resource>
		wvpResourceMultiMaterial =
		CreateBufferResource(
			device.Get(),
			sizeof(TransformationMatrix)
		);

	TransformationMatrix* wvpDataMultiMaterial = nullptr;


	// ==============================
	// Suzanne用WVP Resource
	// ==============================
	Microsoft::WRL::ComPtr<ID3D12Resource> wvpResourceSuzanne =
		CreateBufferResource(
			device.Get(),
			sizeof(TransformationMatrix)
		);

	TransformationMatrix* wvpDataSuzanne = nullptr;

	//平行光源用のResourceの生成
	Microsoft::WRL::ComPtr<ID3D12Resource> directionalLightResource = CreateBufferResource(device.Get(), sizeof(DirectionalLight));

	DirectionalLight* directionalLightData = nullptr;

	// DepthStencil用のPSOの生成
	graphicsPipelineStateDesc.DepthStencilState = depthStencilDesc;
	graphicsPipelineStateDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

	hr = device->CreateGraphicsPipelineState(&graphicsPipelineStateDesc,
		IID_PPV_ARGS(&graphicsPipelineState));
	assert(SUCCEEDED(hr));


	// ==========================================
	// Suzanne用PipelineState
	// ==========================================
	D3D12_GRAPHICS_PIPELINE_STATE_DESC graphicsPipelineStateDescSuzanne{};
	graphicsPipelineStateDescSuzanne.pRootSignature = rootSignatureSuzanne.Get();
	graphicsPipelineStateDescSuzanne.InputLayout = inputLayoutDescSuzanne;
	graphicsPipelineStateDescSuzanne.VS = {
		vertexShaderBlobSuzanne->GetBufferPointer(),
		vertexShaderBlobSuzanne->GetBufferSize()
	};
	graphicsPipelineStateDescSuzanne.PS = {
		pixelShaderBlobSuzanne->GetBufferPointer(),
		pixelShaderBlobSuzanne->GetBufferSize()
	};
	graphicsPipelineStateDescSuzanne.BlendState = blendDesc;
	graphicsPipelineStateDescSuzanne.RasterizerState = rasterizerDesc;
	graphicsPipelineStateDescSuzanne.DepthStencilState = depthStencilDesc;
	graphicsPipelineStateDescSuzanne.NumRenderTargets = 1;
	graphicsPipelineStateDescSuzanne.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	graphicsPipelineStateDescSuzanne.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	graphicsPipelineStateDescSuzanne.PrimitiveTopologyType =
		D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	graphicsPipelineStateDescSuzanne.SampleDesc.Count = 1;
	graphicsPipelineStateDescSuzanne.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;

	Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipelineStateSuzanne = nullptr;
	hr = device->CreateGraphicsPipelineState(
		&graphicsPipelineStateDescSuzanne,
		IID_PPV_ARGS(&graphicsPipelineStateSuzanne)
	);
	assert(SUCCEEDED(hr));

	//Sprite用の頂点リソースを作る
	Microsoft::WRL::ComPtr<ID3D12Resource> vertexResourceSprite = CreateBufferResource(device.Get(), sizeof(VertexData) * 6);

	//頂点バッファビューを作成する
	D3D12_VERTEX_BUFFER_VIEW vertexBufferViewSprite{};
	//リソースの先頭アドレスから使う
	vertexBufferViewSprite.BufferLocation = vertexResourceSprite->GetGPUVirtualAddress();
	//使用するリソースのサイズは頂点6つ分のサイズ
	vertexBufferViewSprite.SizeInBytes = sizeof(VertexData) * 6;
	//1頂点当たりのサイズ
	vertexBufferViewSprite.StrideInBytes = sizeof(VertexData);

	//Sprite用のtransformationMatrix用のリソースを作る。Matrix4x4 1つ分のサイズを用意
	Microsoft::WRL::ComPtr<ID3D12Resource> transformationMatrixResourceSprite = CreateBufferResource(device.Get(), sizeof(TransformationMatrix));
	// データを書き込む
	Matrix4x4* transformationMatrixDataSprite = nullptr;
	// 書き込むためのアドレスを取得
	transformationMatrixResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&transformationMatrixDataSprite));
	// 単位行列を書きこんでおく
	*transformationMatrixDataSprite = Matrix::MakeIdentity4x4();

	// Sprite用Material
	Microsoft::WRL::ComPtr<ID3D12Resource> materialResourceSprite =
		CreateBufferResource(device.Get(), sizeof(Material));

	Material* materialDataSprite = nullptr;

	materialResourceSprite->Map(
		0,
		nullptr,
		reinterpret_cast<void**>(&materialDataSprite)
	);

	materialDataSprite->color = { 1.0f,1.0f,1.0f,1.0f };
	materialDataSprite->lightingType =
		static_cast<int32_t>(LightingType::kNone);
	materialDataSprite->uvTransform = Matrix::MakeIdentity4x4();


	// ==========================================
	// MaterialResourceの生成
	// ==========================================
	Microsoft::WRL::ComPtr<ID3D12Resource> materialResource =
		CreateBufferResource(device.Get(), sizeof(Material));
	// Materialデータを書き込む
	Material* materialData = nullptr;
	TransformationMatrix* wvpDataPlane = nullptr;
	TransformationMatrix* wvpDataSphere = nullptr;
	TransformationMatrix* wvpDataTeapot = nullptr;

	materialResource->Map(0, nullptr, reinterpret_cast<void**>(&materialData));

	// 色設定
	// ImGuiで操作する色
	Vector4 materialColor = { 1.0f, 1.0f, 1.0f, 1.0f };
	// 初期値を書き込む
	materialData->color = materialColor;
	materialData->lightingType =
		static_cast<int32_t>(LightingType::kNone);
	materialData->uvTransform = Matrix::MakeIdentity4x4();


	// ==========================================
	// Suzanne専用MaterialResource
	// ==========================================
	Microsoft::WRL::ComPtr<ID3D12Resource> materialResourceSuzanne =
		CreateBufferResource(device.Get(), sizeof(Material));

	Material* materialDataSuzanne = nullptr;

	materialResourceSuzanne->Map(
		0,
		nullptr,
		reinterpret_cast<void**>(&materialDataSuzanne)
	);

	// ImGuiで操作するSuzanne専用色
	Vector4 suzanneColor = { 1.0f, 1.0f, 1.0f, 1.0f };

	materialDataSuzanne->color = suzanneColor;
	materialDataSuzanne->lightingType =
		static_cast<int32_t>(LightingType::kNone);
	materialDataSuzanne->uvTransform = Matrix::MakeIdentity4x4();


	// ==============================
	// Plane用頂点バッファビュー
	// ==============================
	D3D12_VERTEX_BUFFER_VIEW vertexBufferViewPlane{};
	vertexBufferViewPlane.BufferLocation =
		vertexResourcePlane->GetGPUVirtualAddress();
	vertexBufferViewPlane.SizeInBytes =
		static_cast<UINT>(sizeof(VertexData) * modelDataPlane.vertices.size());
	vertexBufferViewPlane.StrideInBytes = sizeof(VertexData);

	// ==============================
	// Utah Teapot用頂点バッファビュー
	// ==============================
	D3D12_VERTEX_BUFFER_VIEW vertexBufferViewTeapot{};
	vertexBufferViewTeapot.BufferLocation =
		vertexResourceTeapot->GetGPUVirtualAddress();
	vertexBufferViewTeapot.SizeInBytes =
		static_cast<UINT>(sizeof(VertexData) * modelDataTeapot.vertices.size());
	vertexBufferViewTeapot.StrideInBytes = sizeof(VertexData);

	//indexBufferViewの作成
	D3D12_INDEX_BUFFER_VIEW indexBufferViewSprite{};
	indexBufferViewSprite.BufferLocation = indexResourceSprite->GetGPUVirtualAddress();
	indexBufferViewSprite.SizeInBytes = sizeof(uint32_t) * 6;
	indexBufferViewSprite.Format = DXGI_FORMAT_R32_UINT;

	// ==============================
	// Stanford Bunny用頂点バッファビュー
	// ==============================
	D3D12_VERTEX_BUFFER_VIEW
		vertexBufferViewBunny{};

	vertexBufferViewBunny.BufferLocation =
		vertexResourceBunny->GetGPUVirtualAddress();

	vertexBufferViewBunny.SizeInBytes =
		static_cast<UINT>(
			sizeof(VertexData) *
			modelDataBunny.vertices.size()
			);

	vertexBufferViewBunny.StrideInBytes =
		sizeof(VertexData);

	// ==============================
	// Fence用頂点バッファビュー
	// ==============================
	D3D12_VERTEX_BUFFER_VIEW vertexBufferViewFence{};

	vertexBufferViewFence.BufferLocation =
		vertexResourceFence->GetGPUVirtualAddress();

	vertexBufferViewFence.SizeInBytes =
		static_cast<UINT>(
			sizeof(VertexData) *
			modelDataFence.vertices.size()
			);

	vertexBufferViewFence.StrideInBytes =
		sizeof(VertexData);

	// ==============================
	// Suzanne用頂点バッファビュー
	// ==============================
	D3D12_VERTEX_BUFFER_VIEW vertexBufferViewSuzanne{};
	vertexBufferViewSuzanne.BufferLocation =
		vertexResourceSuzanne->GetGPUVirtualAddress();
	vertexBufferViewSuzanne.SizeInBytes =
		static_cast<UINT>(sizeof(VertexDataNoUV) * modelDataSuzanne.size());
	vertexBufferViewSuzanne.StrideInBytes = sizeof(VertexDataNoUV);

	// ==============================
	// 球用頂点バッファビュー
	// ==============================
	D3D12_VERTEX_BUFFER_VIEW vertexBufferViewSphere{};
	vertexBufferViewSphere.BufferLocation =
		vertexResourceSphere->GetGPUVirtualAddress();
	vertexBufferViewSphere.SizeInBytes =
		sizeof(VertexData) * kVertexCount;
	vertexBufferViewSphere.StrideInBytes =
		sizeof(VertexData);

	// ==============================
	// Plane頂点データをコピー
	// ==============================
	VertexData* vertexDataPlane = nullptr;
	vertexResourcePlane->Map(
		0,
		nullptr,
		reinterpret_cast<void**>(&vertexDataPlane)
	);

	std::memcpy(
		vertexDataPlane,
		modelDataPlane.vertices.data(),
		sizeof(VertexData) * modelDataPlane.vertices.size()
	);

	// ==============================
	// Utah Teapot頂点データをコピー
	// ==============================
	VertexData* vertexDataTeapot = nullptr;
	vertexResourceTeapot->Map(
		0,
		nullptr,
		reinterpret_cast<void**>(&vertexDataTeapot)
	);

	std::memcpy(
		vertexDataTeapot,
		modelDataTeapot.vertices.data(),
		sizeof(VertexData) * modelDataTeapot.vertices.size()
	);

	// ==============================
	// Stanford Bunny頂点データ
	// ==============================
	VertexData* vertexDataBunny = nullptr;

	vertexResourceBunny->Map(
		0,
		nullptr,
		reinterpret_cast<void**>(
			&vertexDataBunny
			)
	);

	std::memcpy(
		vertexDataBunny,
		modelDataBunny.vertices.data(),
		sizeof(VertexData) *
		modelDataBunny.vertices.size()
	);

	// ==============================
	// Fence頂点データをコピー
	// ==============================
	VertexData* vertexDataFence = nullptr;

	vertexResourceFence->Map(
		0,
		nullptr,
		reinterpret_cast<void**>(&vertexDataFence)
	);

	std::memcpy(
		vertexDataFence,
		modelDataFence.vertices.data(),
		sizeof(VertexData)*
		modelDataFence.vertices.size()
	);


	// ==============================
	// Suzanne頂点データをコピー
	// ==============================
	VertexDataNoUV* vertexDataSuzanne = nullptr;
	vertexResourceSuzanne->Map(
		0,
		nullptr,
		reinterpret_cast<void**>(&vertexDataSuzanne)
	);

	std::memcpy(
		vertexDataSuzanne,
		modelDataSuzanne.data(),
		sizeof(VertexDataNoUV) * modelDataSuzanne.size()
	);


	// Plane用WVP
	wvpResourcePlane->Map(
		0,
		nullptr,
		reinterpret_cast<void**>(&wvpDataPlane)
	);

	// Sphere用WVP
	wvpResourceSphere->Map(
		0,
		nullptr,
		reinterpret_cast<void**>(&wvpDataSphere)
	);

	// Utah Teapot用WVP
	wvpResourceTeapot->Map(
		0,
		nullptr,
		reinterpret_cast<void**>(&wvpDataTeapot)
	);

	wvpResourceBunny->Map(
		0,
		nullptr,
		reinterpret_cast<void**>(
			&wvpDataBunny
			)
	);

	// MultiMesh用WVP
	wvpResourceMultiMesh->Map(
		0,
		nullptr,
		reinterpret_cast<void**>(
			&wvpDataMultiMesh
			)
	);


	// MultiMaterial用WVP
	wvpResourceMultiMaterial->Map(
		0,
		nullptr,
		reinterpret_cast<void**>(
			&wvpDataMultiMaterial
			)
	);

	// Fence用WVP
	Microsoft::WRL::ComPtr<ID3D12Resource>
		wvpResourceFence =
		CreateBufferResource(
			device.Get(),
			sizeof(TransformationMatrix)
		);

	TransformationMatrix* wvpDataFence = nullptr;

	wvpResourceFence->Map(
		0,
		nullptr,
		reinterpret_cast<void**>(&wvpDataFence)
	);

	// Suzanne用WVP
	wvpResourceSuzanne->Map(
		0,
		nullptr,
		reinterpret_cast<void**>(&wvpDataSuzanne)
	);

	directionalLightResource->Map(
		0,
		nullptr,
		reinterpret_cast<void**>(&directionalLightData)
	);



	wvpDataPlane->WVP = Matrix::MakeIdentity4x4();
	wvpDataPlane->World = Matrix::MakeIdentity4x4();

	wvpDataSphere->WVP = Matrix::MakeIdentity4x4();
	wvpDataSphere->World = Matrix::MakeIdentity4x4();

	wvpDataTeapot->WVP = Matrix::MakeIdentity4x4();
	wvpDataTeapot->World = Matrix::MakeIdentity4x4();

	wvpDataBunny->WVP = Matrix::MakeIdentity4x4();
	wvpDataBunny->World = Matrix::MakeIdentity4x4();

	wvpDataMultiMesh->WVP = Matrix::MakeIdentity4x4();
	wvpDataMultiMesh->World = Matrix::MakeIdentity4x4();

	wvpDataMultiMaterial->WVP = Matrix::MakeIdentity4x4();
	wvpDataMultiMaterial->World = Matrix::MakeIdentity4x4();


	wvpDataSuzanne->WVP = Matrix::MakeIdentity4x4();
	wvpDataSuzanne->World = Matrix::MakeIdentity4x4();


	// ライトの初期値
	directionalLightData->color = { 1.0f,1.0f,1.0f,1.0f };
	directionalLightData->direction = { 0.0f,-1.0f,0.0f };
	directionalLightData->intensity = 1.0f;

	// ==============================
	// 球頂点データ
	// ==============================
	VertexData* vertexDataSphere = nullptr;

	vertexResourceSphere->Map(
		0,
		nullptr,
		reinterpret_cast<void**>(&vertexDataSphere)
	);

	// ==============================
	// 球生成
	// ==============================
	for (uint32_t latIndex = 0;
		latIndex < kSubdivision;
		++latIndex)
	{
		float lat0 =
			-pi / 2.0f +
			(pi / kSubdivision) * latIndex;

		float lat1 =
			-pi / 2.0f +
			(pi / kSubdivision) * (latIndex + 1);

		for (uint32_t lonIndex = 0;
			lonIndex < kSubdivision;
			++lonIndex)
		{
			float lon0 =
				(2.0f * pi / kSubdivision) *
				lonIndex;

			float lon1 =
				(2.0f * pi / kSubdivision) *
				(lonIndex + 1);

			uint32_t start =
				(latIndex * kSubdivision + lonIndex) * 6;

			// a
			vertexDataSphere[start + 0].position = {
				cosf(lat0) * cosf(lon0),
				sinf(lat0),
				cosf(lat0) * sinf(lon0),
				1.0f
			};

			vertexDataSphere[start + 0].texcoord = {
				float(lonIndex) / kSubdivision,
				1.0f - float(latIndex) / kSubdivision
			};

			// b
			vertexDataSphere[start + 1].position = {
				cosf(lat1) * cosf(lon0),
				sinf(lat1),
				cosf(lat1) * sinf(lon0),
				1.0f
			};

			vertexDataSphere[start + 1].texcoord = {
				float(lonIndex) / kSubdivision,
				1.0f - float(latIndex + 1) / kSubdivision
			};

			// c
			vertexDataSphere[start + 2].position = {
				cosf(lat0) * cosf(lon1),
				sinf(lat0),
				cosf(lat0) * sinf(lon1),
				1.0f
			};

			vertexDataSphere[start + 2].texcoord = {
				float(lonIndex + 1) / kSubdivision,
				1.0f - float(latIndex) / kSubdivision
			};

			// d
			vertexDataSphere[start + 3].position = {
				cosf(lat0) * cosf(lon1),
				sinf(lat0),
				cosf(lat0) * sinf(lon1),
				1.0f
			};

			vertexDataSphere[start + 3].texcoord = {
				float(lonIndex + 1) / kSubdivision,
				1.0f - float(latIndex) / kSubdivision
			};

			vertexDataSphere[start + 4].position = {
				cosf(lat1) * cosf(lon0),
				sinf(lat1),
				cosf(lat1) * sinf(lon0),
				1.0f
			};

			vertexDataSphere[start + 4].texcoord = {
				float(lonIndex) / kSubdivision,
				1.0f - float(latIndex + 1) / kSubdivision
			};

			vertexDataSphere[start + 5].position = {
				cosf(lat1) * cosf(lon1),
				sinf(lat1),
				cosf(lat1) * sinf(lon1),
				1.0f
			};

			vertexDataSphere[start + 5].texcoord = {
				float(lonIndex + 1) / kSubdivision,
				1.0f - float(latIndex + 1) / kSubdivision
			};

			// 球では位置ベクトルを法線として使用できる
			for (int32_t i = 0; i < 6; ++i) {
				vertexDataSphere[start + i].normal = {
					vertexDataSphere[start + i].position.x,
					vertexDataSphere[start + i].position.y,
					vertexDataSphere[start + i].position.z
				};
			}
		}
	}

	//Sprite用の頂点データ
	VertexData* vertexDataSprite = nullptr;
	vertexResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&vertexDataSprite));

	//Spriteの頂点データを設定
	vertexDataSprite[0].position = { 0.0f, 360.0f, 0.0f, 1.0f };
	vertexDataSprite[0].texcoord = { 0.0f, 1.0f };
	vertexDataSprite[1].position = { 0.0f, 0.0f, 0.0f, 1.0f };
	vertexDataSprite[1].texcoord = { 0.0f, 0.0f };
	vertexDataSprite[2].position = { 640.0f, 360.0f, 0.0f, 1.0f };
	vertexDataSprite[2].texcoord = { 1.0f, 1.0f };
	vertexDataSprite[3].position = { 640.0f, 0.0f, 0.0f, 1.0f };
	vertexDataSprite[3].texcoord = { 1.0f, 0.0f };

	//インデックスリソースにデータを書き込む
	uint32_t* indexDataSprite = nullptr;
	indexResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&indexDataSprite));
	indexDataSprite[0] = 0;	indexDataSprite[1] = 1;	indexDataSprite[2] = 2;
	indexDataSprite[3] = 1;	indexDataSprite[4] = 3;	indexDataSprite[5] = 2;

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


	// ==============================
	// スワップチェーン生成
	// ==============================

	Microsoft::WRL::ComPtr<IDXGISwapChain4> swapChain = nullptr;

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
		reinterpret_cast<IDXGISwapChain1**>(swapChain.GetAddressOf())
	);

	assert(SUCCEEDED(hr));

	// ==============================
	// RTVヒープ生成
	// ==============================

	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeap = CreateDescriptorHeap(
		device.Get(),
		D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
		2,
		false
	);

	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvDescriptorheap = CreateDescriptorHeap(
		device.Get(),
		D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
		128,
		true
	);


	//DSVヒープ生成。ディスクリプタの数は1。DSVはShader内で触るものではないので、ShaderVisibleにしない
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvHeap = CreateDescriptorHeap(
		device.Get(),
		D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
		1,
		false
	);

	// DescriptorSizeを取得しておく
	const uint32_t descriptorSizeSRV =
		device->GetDescriptorHandleIncrementSize(
			D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	const uint32_t descriptorSizeRTV =
		device->GetDescriptorHandleIncrementSize(
			D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

	const uint32_t descriptorSizeDSV =
		device->GetDescriptorHandleIncrementSize(
			D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

	// DSVヒープにDepthStencilResourceを紐づける
	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};				//DSVの設定。基本的にフォーマットと次元数を指定すればいい
	dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;			// DepthStencilResourceを作るときにこのフォーマットを指定しているので、合わせる必要がある
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D; // DepthStencilResourceを作るときに2Dにしているので、合わせる必要がある
	//DSVHeapの先頭にDSVを作る
	device->CreateDepthStencilView(
		depthStencilResource.Get(),
		&dsvDesc,
		dsvHeap->GetCPUDescriptorHandleForHeapStart()
	);



	// ==============================
	// Texture読み込み
	// ==============================

	// ==============================
	// 1枚目：共通uvChecker
	// ==============================
	DirectX::ScratchImage mipImages =
		LoadTexture("resources/uvChecker.png");

	const DirectX::TexMetadata& metadata =
		mipImages.GetMetadata();

	Microsoft::WRL::ComPtr<ID3D12Resource> textureResource =
		CreateTextureResource(
			device.Get(),
			metadata
		);

	Microsoft::WRL::ComPtr<ID3D12Resource> intermediateResource =
		UploadTextureData(
			textureResource.Get(),
			mipImages,
			device.Get(),
			commandList
		);

	// ==============================
	// 2枚目：Plane用
	// ==============================
	DirectX::ScratchImage mipImages2 =
		LoadTexture(
			modelDataPlane.material.textureFilePath
		);

	const DirectX::TexMetadata& metadata2 =
		mipImages2.GetMetadata();

	Microsoft::WRL::ComPtr<ID3D12Resource> textureResource2 =
		CreateTextureResource(
			device.Get(),
			metadata2
		);

	Microsoft::WRL::ComPtr<ID3D12Resource> intermediateResource2 =
		UploadTextureData(
			textureResource2.Get(),
			mipImages2,
			device.Get(),
			commandList
		);

	// ==============================
	// 3枚目：Utah Teapot用
	// ==============================
	DirectX::ScratchImage mipImagesTeapot =
		LoadTexture(
			modelDataTeapot.material.textureFilePath
		);

	const DirectX::TexMetadata& metadataTeapot =
		mipImagesTeapot.GetMetadata();

	Microsoft::WRL::ComPtr<ID3D12Resource>
		textureResourceTeapot =
		CreateTextureResource(
			device.Get(),
			metadataTeapot
		);

	Microsoft::WRL::ComPtr<ID3D12Resource>
		intermediateResourceTeapot =
		UploadTextureData(
			textureResourceTeapot.Get(),
			mipImagesTeapot,
			device.Get(),
			commandList
		);

	// ==============================
	// 4枚目：Stanford Bunny用
	// ==============================
	DirectX::ScratchImage mipImagesBunny =
		LoadTexture(
			modelDataBunny.material.textureFilePath
		);

	const DirectX::TexMetadata& metadataBunny =
		mipImagesBunny.GetMetadata();

	Microsoft::WRL::ComPtr<ID3D12Resource>
		textureResourceBunny =
		CreateTextureResource(
			device.Get(),
			metadataBunny
		);

	Microsoft::WRL::ComPtr<ID3D12Resource>
		intermediateResourceBunny =
		UploadTextureData(
			textureResourceBunny.Get(),
			mipImagesBunny,
			device.Get(),
			commandList
		);

	// ==============================
	// Fence用Texture
	// ==============================
	DirectX::ScratchImage mipImagesFence =
		LoadTexture(
			modelDataFence.material.textureFilePath
		);

	const DirectX::TexMetadata& metadataFence =
		mipImagesFence.GetMetadata();

	Microsoft::WRL::ComPtr<ID3D12Resource>
		textureResourceFence =
		CreateTextureResource(
			device.Get(),
			metadataFence
		);

	Microsoft::WRL::ComPtr<ID3D12Resource>
		intermediateResourceFence =
		UploadTextureData(
			textureResourceFence.Get(),
			mipImagesFence,
			device.Get(),
			commandList
		);

	// ==============================
	// 5枚目：MultiMaterialのCube用
	// ==============================
	DirectX::ScratchImage mipImagesMonsterBall =
		LoadTexture(
			"resources/monsterBall.png"
		);

	const DirectX::TexMetadata& metadataMonsterBall =
		mipImagesMonsterBall.GetMetadata();

	Microsoft::WRL::ComPtr<ID3D12Resource>
		textureResourceMonsterBall =
		CreateTextureResource(
			device.Get(),
			metadataMonsterBall
		);

	Microsoft::WRL::ComPtr<ID3D12Resource>
		intermediateResourceMonsterBall =
		UploadTextureData(
			textureResourceMonsterBall.Get(),
			mipImagesMonsterBall,
			device.Get(),
			commandList
		);

	// 全テクスチャの転送命令を積んだ後にClose
	hr = commandList->Close();
	assert(SUCCEEDED(hr));

	ID3D12CommandList* commandLists[] = {
		commandList
	};

	commandQueue->ExecuteCommandLists(
		1,
		commandLists
	);

	// ==========================================
	// SRV作成
	// ==========================================

	// metaDataを基にSRVの設定
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = metadata.format;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;//2Dテクスチャ
	srvDesc.Texture2D.MipLevels = UINT(metadata.mipLevels);

	// meataDataCSRVO
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc2{};
	srvDesc2.Format = metadata2.format;
	srvDesc2.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc2.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;//2Dテクスチャ
	srvDesc2.Texture2D.MipLevels = UINT(metadata2.mipLevels);

	// ==============================
	// Utah Teapot用SRV設定
	// ==============================
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDescTeapot{};

	srvDescTeapot.Format =
		metadataTeapot.format;

	srvDescTeapot.Shader4ComponentMapping =
		D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

	srvDescTeapot.ViewDimension =
		D3D12_SRV_DIMENSION_TEXTURE2D;

	srvDescTeapot.Texture2D.MipLevels =
		static_cast<UINT>(
			metadataTeapot.mipLevels
			);

	// ==============================
	// Stanford Bunny用SRV設定
	// ==============================
	D3D12_SHADER_RESOURCE_VIEW_DESC
		srvDescBunny{};

	srvDescBunny.Format =
		metadataBunny.format;

	srvDescBunny.Shader4ComponentMapping =
		D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

	srvDescBunny.ViewDimension =
		D3D12_SRV_DIMENSION_TEXTURE2D;

	srvDescBunny.Texture2D.MipLevels =
		static_cast<UINT>(
			metadataBunny.mipLevels
			);


	// ==============================
	// Fence用SRV設定
	// ==============================
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDescFence{};

	srvDescFence.Format =
		metadataFence.format;

	srvDescFence.Shader4ComponentMapping =
		D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

	srvDescFence.ViewDimension =
		D3D12_SRV_DIMENSION_TEXTURE2D;

	srvDescFence.Texture2D.MipLevels =
		static_cast<UINT>(
			metadataFence.mipLevels
			);

	// ==============================
	// monsterBall用SRV設定
	// ==============================
	D3D12_SHADER_RESOURCE_VIEW_DESC
		srvDescMonsterBall{};

	srvDescMonsterBall.Format =
		metadataMonsterBall.format;

	srvDescMonsterBall.Shader4ComponentMapping =
		D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

	srvDescMonsterBall.ViewDimension =
		D3D12_SRV_DIMENSION_TEXTURE2D;

	srvDescMonsterBall.Texture2D.MipLevels =
		static_cast<UINT>(
			metadataMonsterBall.mipLevels
			);


	// SRVを作成するDescriptor Heapの場所を決める
	// 先頭はImGuiが使っているのでその次を使う
	D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU =
		GetCPUDescriptorHandle(
			srvDescriptorheap,
			descriptorSizeSRV,
			1);

	D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU =
		GetGPUDescriptorHandle(
			srvDescriptorheap,
			descriptorSizeSRV,
			1);

	D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU2 =
		GetCPUDescriptorHandle(
			srvDescriptorheap,
			descriptorSizeSRV,
			2);

	D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU2 =
		GetGPUDescriptorHandle(
			srvDescriptorheap,
			descriptorSizeSRV,
			2);
	;

	// ==============================
	// Utah Teapot用SRVハンドル
	// ==============================
	D3D12_CPU_DESCRIPTOR_HANDLE
		textureSrvHandleCPUTeapot =
		GetCPUDescriptorHandle(
			srvDescriptorheap,
			descriptorSizeSRV,
			3
		);

	D3D12_GPU_DESCRIPTOR_HANDLE
		textureSrvHandleGPUTeapot =
		GetGPUDescriptorHandle(
			srvDescriptorheap,
			descriptorSizeSRV,
			3
		);

	// ==============================
	// Stanford Bunny用SRVハンドル
	// ==============================

	D3D12_CPU_DESCRIPTOR_HANDLE
		textureSrvHandleCPUBunny =
		GetCPUDescriptorHandle(
			srvDescriptorheap,
			descriptorSizeSRV,
			4
		);

	D3D12_GPU_DESCRIPTOR_HANDLE
		textureSrvHandleGPUBunny =
		GetGPUDescriptorHandle(
			srvDescriptorheap,
			descriptorSizeSRV,
			4
		);

	// ==============================
	// monsterBall用SRVハンドル
	// ==============================
	D3D12_CPU_DESCRIPTOR_HANDLE
		textureSrvHandleCPUMonsterBall =
		GetCPUDescriptorHandle(
			srvDescriptorheap,
			descriptorSizeSRV,
			5
		);

	D3D12_GPU_DESCRIPTOR_HANDLE
		textureSrvHandleGPUMonsterBall =
		GetGPUDescriptorHandle(
			srvDescriptorheap,
			descriptorSizeSRV,
			5
		);

	// ==============================
	// Fence用SRVハンドル
	// ==============================
		D3D12_CPU_DESCRIPTOR_HANDLE
		textureSrvHandleCPUFence =
		GetCPUDescriptorHandle(
			srvDescriptorheap,
			descriptorSizeSRV,
			6
		);

	D3D12_GPU_DESCRIPTOR_HANDLE
		textureSrvHandleGPUFence =
		GetGPUDescriptorHandle(
			srvDescriptorheap,
			descriptorSizeSRV,
			6
		);

	// MaterialごとのSRV番号を設定
	for (
		MaterialData& material :
		modelDataMultiMaterial.materials
		) {
		if (
			material.textureFilePath.find(
				"monsterBall.png"
			) != std::string::npos
			) {
			material.textureIndex = 5;
		}
		else {
			// uvChecker.png
			material.textureIndex = 1;
		}
	}

	// SRVの生成
	// uvChecker
	device->CreateShaderResourceView(
		textureResource.Get(),
		&srvDesc,
		textureSrvHandleCPU
	);

	// Plane
	device->CreateShaderResourceView(
		textureResource2.Get(),
		&srvDesc2,
		textureSrvHandleCPU2
	);

	// Utah Teapot
	device->CreateShaderResourceView(
		textureResourceTeapot.Get(),
		&srvDescTeapot,
		textureSrvHandleCPUTeapot
	);

	// Stanford Bunny
	device->CreateShaderResourceView(
		textureResourceBunny.Get(),
		&srvDescBunny,
		textureSrvHandleCPUBunny
	);

	// monsterBall
	device->CreateShaderResourceView(
		textureResourceMonsterBall.Get(),
		&srvDescMonsterBall,
		textureSrvHandleCPUMonsterBall
	);

	// Fence
	device->CreateShaderResourceView(
		textureResourceFence.Get(),
		&srvDescFence,
		textureSrvHandleCPUFence
	);

	// ==============================
	// バックバッファ取得
	// ==============================

	Microsoft::WRL::ComPtr<ID3D12Resource> backBuffers[2] = { nullptr };

	for (UINT i = 0; i < 2; ++i) {

		hr = swapChain->GetBuffer(
			i,
			IID_PPV_ARGS(&backBuffers[i])
		);

		assert(SUCCEEDED(hr));
	}


	// ==============================
	// RTV&DSV作成
	// ==============================

	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};

	rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	rtvDesc.ViewDimension =
		D3D12_RTV_DIMENSION_TEXTURE2D;

	UINT rtvDescriptorSize = descriptorSizeRTV;

	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles[2];

	rtvHandles[0] =
		GetCPUDescriptorHandle(
			rtvHeap,
			descriptorSizeRTV,
			0);

	rtvHandles[1] =
		GetCPUDescriptorHandle(
			rtvHeap,
			descriptorSizeRTV,
			1);

	for (UINT i = 0; i < 2; ++i) {

		device->CreateRenderTargetView(
			backBuffers[i].Get(),
			&rtvDesc,
			rtvHandles[i]
		);
	}

	//DSVのハンドルも作る
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle =
		GetCPUDescriptorHandle(
			dsvHeap,
			descriptorSizeDSV,
			0);


	// ==============================
	// Fence生成
	// ==============================

	Microsoft::WRL::ComPtr<ID3D12Fence> fence = nullptr;
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
	ImGui_ImplDX12_Init(device.Get(),
		swapChainDesc.BufferCount,
		rtvDesc.Format,
		srvDescriptorheap.Get(),
		srvDescriptorheap->GetCPUDescriptorHandleForHeapStart(),
		srvDescriptorheap->GetGPUDescriptorHandleForHeapStart());
	ImGuiIO& io = ImGui::GetIO();
	io.Fonts->Build();
#endif

	// ==========================================
	// 初期テクスチャ転送コマンドを実行
	// ==========================================

	// GPUへFenceシグナルを送る
	fenceValue++;

	hr = commandQueue->Signal(
		fence.Get(),
		fenceValue
	);
	assert(SUCCEEDED(hr));

	// テクスチャ転送の完了を待つ
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
	// XAudio2の初期化
	// ==========================================

	Microsoft::WRL::ComPtr<IXAudio2> xAudio2;
	IXAudio2MasteringVoice* masterVoice;

	// XAudio2エンジンのインスタンスを生成
	hr = XAudio2Create(
		&xAudio2,
		0,
		XAUDIO2_DEFAULT_PROCESSOR
	);

	// マスターボイス生成
	hr = xAudio2->CreateMasteringVoice(&masterVoice);



	//Transform構造体の定義

	// ==============================
	// Plane用Transform
	// ==============================
	Transform transformPlane{};

	transformPlane.scale = { 1.0f, 1.0f, 1.0f };
	transformPlane.rotate.y = std::numbers::pi_v<float>;
	transformPlane.translate = { 0.0f, 0.0f, 0.0f };

	// ==============================
	// Sphere用Transform
	// ==============================
	Transform transformSphere{
		{1.0f, 1.0f, 1.0f},
		{0.0f, 0.0f, 0.0f},
		{-3.0f, 0.0f, 0.0f}
	};

	// ==============================
	// Utah Teapot用Transform
	// ==============================
	Transform transformTeapot{
		{1.0f, 1.0f, 1.0f},
		{0.0f, 0.0f, 0.0f},
		{0.0f, 0.0f, 0.0f}
	};

	// ==============================
	// Stanford Bunny用Transform
	// ==============================
	Transform transformBunny{
		{1.0f, 1.0f, 1.0f},
		{0.0f, 0.0f, 0.0f},
		{0.0f, 1.0f, 0.0f}
	};

	// ==============================
	// Fence用Transform
	// ==============================
	Transform transformFence{
	{ 1.0f, 1.0f, 1.0f }, // Scale
	{ 0.0f, 0.0f, 0.0f }, // Rotate
	{ 0.0f, 0.0f, 0.0f }  // Translate
	};


	// ==============================
	// MultiMesh用Transform
	// PlaneとCubeをまとめて移動・回転・拡縮する
	// ==============================
	Transform transformMultiMesh{
		{1.0f, 1.0f, 1.0f},
		{0.0f, 0.0f, 0.0f},
		{-3.0f, 1.5f, 0.0f}
	};

	// ==============================
	// MultiMaterial用Transform
	// ==============================
	Transform transformMultiMaterial{
		{1.0f, 1.0f, 1.0f},
		{0.0f, 0.0f, 0.0f},
		{2.0f, 1.5f, 0.0f}
	};


	// ==============================
	// Suzanne用Transform
	// ==============================
	Transform transformSuzanne{
		{1.0f, 1.0f, 1.0f},
		{0.0f, 0.0f, 0.0f},
		{3.0f, 1.5f, -1.0f}
	};

	Transform cameraTransform{
		{1.0f, 1.0f, 1.0f},
		{0.0f, 0.0f, 0.0f},
		{0.0f, 0.0f, -10.0f}
	};

	Transform transformSprite{
		{1.0f, 1.0f, 1.0f },
		{0.0f, 0.0f, 0.0f },
		{0.0f, 0.0f, 0.0f}
	};

	Transform uvTransformSprite{
	{1.0f, 1.0f, 1.0f},
	{0.0f, 0.0f, 0.0f},
	{0.0f, 0.0f, 0.0f}
	};


	bool useModelTexture = false;

	// ==============================
	// Lighting方式
	// ==============================
	int32_t lightingType =
		static_cast<int32_t>(LightingType::kNone);

	const char* lightingTypeNames[] = {
		"None",
		"Lambert",
		"Half Lambert"
	};

	// ==============================
	// 描画ON・OFF
	// ==============================
	bool isDrawPlane = true;
	bool isDrawSphere = false;
	bool isDrawTeapot = false;
	bool isDrawSprite = true;
	bool isDrawBunny = false;
	bool isDrawMultiMesh = false;
	bool isDrawMultiMaterial = false;
	bool isDrawSuzanne = false;
	bool isDrawFence = true;

	//音声読み込み
	SoundData soundData1 = SoundLoadWave("resources/Alarm01.wav");

	DebugCamera debugCamera;
	debugCamera.Initialize();

	// WindowProcから触れるようにする
	gDebugCamera = &debugCamera;

	// false：通常カメラ
	// true ：デバッグカメラ
	bool isDebugCamera = false;

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
			// キーボード情報の取得
			// ==========================================

			input->Update();

			//使い方サンプル
			//数字の0キーが押されていたら
			// 押している間
			if (input->PushKey(DIK_0)) {
				OutputDebugStringA("Hit 0\n");	//出力ウィンドウに「Hit 0」と表示
			}

			// 押した瞬間
			if (input->TriggerKey(DIK_1)) {
				//音声再生
				SoundPlayWave(xAudio2.Get(), soundData1);
			}

			// 離した瞬間
			if (input->ReleaseKey(DIK_2)) {
				OutputDebugStringA("Hit 2\n");
			}


			// F1：通常カメラとデバッグカメラを切り替え
			if (input->TriggerKey(DIK_F1)) {
				isDebugCamera = !isDebugCamera;
			}

			// F2：デバッグカメラのモード切り替え
			if (
				isDebugCamera &&
				input->TriggerKey(DIK_F2)
				) {
				debugCamera.ToggleMode();
			}

			// デバッグカメラ中だけ更新
			if (isDebugCamera) {
				debugCamera.Update();
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
				backBuffers[backBufferIndex].Get();

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
				&dsvHandle
			);

			commandList->ClearDepthStencilView(
				dsvHandle,
				D3D12_CLEAR_FLAG_DEPTH,
				1.0f,
				0,
				0,
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
			ID3D12DescriptorHeap* descriptorHeaps[] = {
				srvDescriptorheap.Get()
			};

			commandList->SetDescriptorHeaps(1, descriptorHeaps);


			// ==========================================
			// ImGui開始
			// ==========================================
#ifdef USE_IMGUI
			ImGui_ImplDX12_NewFrame();
			ImGui_ImplWin32_NewFrame();
			ImGui::NewFrame();

			ImGui::Begin("Material");

			// ==============================
			// 描画設定
			// ==============================
			ImGui::SeparatorText("Draw Settings");

			ImGui::Checkbox("Draw Plane", &isDrawPlane);
			ImGui::Checkbox("Draw Utah Teapot", &isDrawTeapot);
			ImGui::Checkbox("Draw Stanford Bunny", &isDrawBunny);
			ImGui::Checkbox("Draw MultiMesh", &isDrawMultiMesh);
			ImGui::Checkbox("Draw MultiMaterial", &isDrawMultiMaterial);
			ImGui::Checkbox("Draw Suzanne", &isDrawSuzanne);
			ImGui::Checkbox("Draw Sphere", &isDrawSphere);
			ImGui::Checkbox("Draw Sprite", &isDrawSprite);
			ImGui::Checkbox("Draw Fence", &isDrawFence);

			// ==============================
			// MultiMesh操作
			// ==============================
			if (ImGui::CollapsingHeader(
				"MultiMesh Transform",
				ImGuiTreeNodeFlags_DefaultOpen
			)) {
				ImGui::Text(
					"Mesh Count : %d",
					static_cast<int>(
						modelDataMultiMesh.meshes.size()
						)
				);

				for (
					const MeshData& mesh :
					modelDataMultiMesh.meshes
					) {
					ImGui::BulletText(
						"%s : %d vertices",
						mesh.name.c_str(),
						static_cast<int>(
							mesh.vertices.size()
							)
					);
				}

				ImGui::DragFloat3(
					"MultiMesh Translate",
					&transformMultiMesh.translate.x,
					0.01f
				);

				ImGui::DragFloat3(
					"MultiMesh Scale",
					&transformMultiMesh.scale.x,
					0.01f,
					0.01f,
					10.0f
				);

				ImGui::SliderAngle(
					"MultiMesh Rotate X",
					&transformMultiMesh.rotate.x
				);

				ImGui::SliderAngle(
					"MultiMesh Rotate Y",
					&transformMultiMesh.rotate.y
				);

				ImGui::SliderAngle(
					"MultiMesh Rotate Z",
					&transformMultiMesh.rotate.z
				);
			}

			// ==============================
			// MultiMaterial操作
			// ==============================
			if (ImGui::CollapsingHeader(
				"MultiMaterial Transform",
				ImGuiTreeNodeFlags_DefaultOpen
			)) {
				ImGui::Text(
					"Mesh Count : %d",
					static_cast<int>(
						modelDataMultiMaterial.meshes.size()
						)
				);

				for (
					const MeshData& mesh :
					modelDataMultiMaterial.meshes
					) {
					const MaterialData& material =
						modelDataMultiMaterial.materials[
							mesh.materialIndex
						];

					ImGui::BulletText(
						"%s / %s / %s",
						mesh.name.c_str(),
						material.name.c_str(),
						material.textureFilePath.c_str()
					);
				}

				ImGui::DragFloat3(
					"MultiMaterial Translate",
					&transformMultiMaterial.translate.x,
					0.01f
				);

				ImGui::DragFloat3(
					"MultiMaterial Scale",
					&transformMultiMaterial.scale.x,
					0.01f,
					0.01f,
					10.0f
				);

				ImGui::SliderAngle(
					"MultiMaterial Rotate X",
					&transformMultiMaterial.rotate.x
				);

				ImGui::SliderAngle(
					"MultiMaterial Rotate Y",
					&transformMultiMaterial.rotate.y
				);

				ImGui::SliderAngle(
					"MultiMaterial Rotate Z",
					&transformMultiMaterial.rotate.z
				);
			}

			ImGui::ColorEdit4("Color", &materialColor.x);
			ImGui::Checkbox(
				"Use Model Texture",
				&useModelTexture
			);

			// ==============================
			// OBJモデル操作
			// ==============================
			if (ImGui::CollapsingHeader(
				"Plane Transform",
				ImGuiTreeNodeFlags_DefaultOpen
			)) {

				ImGui::DragFloat3(
					"Plane Translate",
					&transformPlane.translate.x,
					0.01f
				);

				ImGui::DragFloat3(
					"Plane Scale",
					&transformPlane.scale.x,
					0.01f,
					0.01f,
					10.0f
				);

				ImGui::SliderAngle(
					"Plane Rotate X",
					&transformPlane.rotate.x
				);

				ImGui::SliderAngle(
					"Plane Rotate Y",
					&transformPlane.rotate.y
				);

				ImGui::SliderAngle(
					"Plane Rotate Z",
					&transformPlane.rotate.z
				);
			}

			// ==============================
			// Sphere操作
			// ==============================
			if (ImGui::CollapsingHeader(
				"Sphere Transform",
				ImGuiTreeNodeFlags_DefaultOpen
			)) {

				ImGui::DragFloat3(
					"Sphere Translate",
					&transformSphere.translate.x,
					0.01f
				);

				ImGui::DragFloat3(
					"Sphere Scale",
					&transformSphere.scale.x,
					0.01f,
					0.01f,
					10.0f
				);

				ImGui::SliderAngle(
					"Sphere Rotate X",
					&transformSphere.rotate.x
				);

				ImGui::SliderAngle(
					"Sphere Rotate Y",
					&transformSphere.rotate.y
				);

				ImGui::SliderAngle(
					"Sphere Rotate Z",
					&transformSphere.rotate.z
				);
			}

			// ==============================
			// Utah Teapot操作
			// ==============================
			if (ImGui::CollapsingHeader(
				"Utah Teapot Transform",
				ImGuiTreeNodeFlags_DefaultOpen
			)) {
				ImGui::DragFloat3(
					"Teapot Translate",
					&transformTeapot.translate.x,
					0.01f
				);

				ImGui::DragFloat3(
					"Teapot Scale",
					&transformTeapot.scale.x,
					0.01f,
					0.001f,
					10.0f
				);

				ImGui::SliderAngle(
					"Teapot Rotate X",
					&transformTeapot.rotate.x
				);

				ImGui::SliderAngle(
					"Teapot Rotate Y",
					&transformTeapot.rotate.y
				);

				ImGui::SliderAngle(
					"Teapot Rotate Z",
					&transformTeapot.rotate.z
				);
			}


			// ==============================
			// Stanford Bunny用 操作
			// ==============================
			if (ImGui::CollapsingHeader(
				"Stanford Bunny Transform",
				ImGuiTreeNodeFlags_DefaultOpen
			)) {

				ImGui::DragFloat3(
					"Bunny Translate",
					&transformBunny.translate.x,
					0.01f
				);

				ImGui::DragFloat3(
					"Bunny Scale",
					&transformBunny.scale.x,
					0.01f,
					0.001f,
					100.0f
				);

				ImGui::SliderAngle(
					"Bunny Rotate X",
					&transformBunny.rotate.x
				);

				ImGui::SliderAngle(
					"Bunny Rotate Y",
					&transformBunny.rotate.y
				);

				ImGui::SliderAngle(
					"Bunny Rotate Z",
					&transformBunny.rotate.z
				);
			}


			// ==============================
			// Suzanne操作
			// ==============================
			if (ImGui::CollapsingHeader(
				"Suzanne Transform",
				ImGuiTreeNodeFlags_DefaultOpen
			)) {
				ImGui::ColorEdit4(
					"Suzanne Color",
					&suzanneColor.x
				);

				ImGui::DragFloat3(
					"Suzanne Translate",
					&transformSuzanne.translate.x,
					0.01f
				);

				ImGui::DragFloat3(
					"Suzanne Scale",
					&transformSuzanne.scale.x,
					0.01f,
					0.01f,
					10.0f
				);

				ImGui::SliderAngle(
					"Suzanne Rotate X",
					&transformSuzanne.rotate.x
				);

				ImGui::SliderAngle(
					"Suzanne Rotate Y",
					&transformSuzanne.rotate.y
				);

				ImGui::SliderAngle(
					"Suzanne Rotate Z",
					&transformSuzanne.rotate.z
				);
			}


			// ==============================
			// Fence操作
			// ==============================
			if (ImGui::CollapsingHeader(
				"Fence Transform",
				ImGuiTreeNodeFlags_DefaultOpen
			)) {
				ImGui::DragFloat3(
					"Fence Translate",
					&transformFence.translate.x,
					0.01f
				);

				ImGui::DragFloat3(
					"Fence Scale",
					&transformFence.scale.x,
					0.01f,
					0.01f,
					10.0f
				);

				ImGui::SliderAngle(
					"Fence Rotate Y",
					&transformFence.rotate.y
				);
			}

			// ===== ライト操作 =====
			ImGui::Separator();
			ImGui::SeparatorText("Lighting");

			ImGui::Combo(
				"Lighting Type",
				&lightingType,
				lightingTypeNames,
				IM_ARRAYSIZE(lightingTypeNames)
			);

			// 色
			ImGui::ColorEdit4("Light Color", &directionalLightData->color.x);

			// 向き（重要）
			ImGui::DragFloat3("Light Direction", &directionalLightData->direction.x, 0.01f);

			// 強さ
			ImGui::DragFloat("Intensity", &directionalLightData->intensity, 0.01f, 0.0f, 10.0f);


			// ===== uvTransform操作 =====
			ImGui::Separator();
			ImGui::Text("uvTransform Sprite");
			// ==============================
			// Sprite本体の操作
			// ==============================
			if (ImGui::CollapsingHeader(
				"Sprite Transform",
				ImGuiTreeNodeFlags_DefaultOpen
			)) {
				ImGui::DragFloat3(
					"Sprite Translate",
					&transformSprite.translate.x,
					1.0f
				);

				ImGui::DragFloat3(
					"Sprite Scale",
					&transformSprite.scale.x,
					0.01f,
					0.01f,
					10.0f
				);

				ImGui::SliderAngle(
					"Sprite Rotate Z",
					&transformSprite.rotate.z
				);
			}

			ImGui::DragFloat2("uvTransform Translate", &uvTransformSprite.translate.x, 0.01f, -10.0f, 10.0f);
			ImGui::DragFloat2("uvTransform Scale", &uvTransformSprite.scale.x, 0.01f, 0.0f, 10.0f);
			ImGui::SliderAngle("UVRotate", &uvTransformSprite.rotate.z);

			// ===== デバッグカメラ操作 =====
			ImGui::Separator();
			ImGui::Text("Debug Camera");

			// 通常カメラとデバッグカメラ切り替え
			ImGui::Checkbox("Use Debug Camera", &isDebugCamera);

			if (isDebugCamera) {

				int cameraMode =
					static_cast<int>(
						debugCamera.GetMode()
						);

				const char* modeNames[] = {
					"Orbit",
					"Free"
				};

				if (ImGui::Combo(
					"Camera Mode",
					&cameraMode,
					modeNames,
					IM_ARRAYSIZE(modeNames)
				)) {
					debugCamera.SetMode(
						static_cast<DebugCamera::Mode>(
							cameraMode
							)
					);
				}

				if (
					debugCamera.GetMode() ==
					DebugCamera::Mode::kOrbit
					) {

					ImGui::Text("Orbit Camera");

					// 注目点
					ImGui::DragFloat3(
						"Camera Target",
						&debugCamera.GetTarget().x,
						0.01f
					);

					// 回転
					ImGui::SliderAngle(
						"Camera Rotate X",
						&debugCamera.GetRotation().x,
						-89.0f,
						89.0f
					);

					ImGui::SliderAngle(
						"Camera Rotate Y",
						&debugCamera.GetRotation().y,
						-180.0f,
						180.0f
					);

					// 距離
					ImGui::DragFloat(
						"Camera Distance",
						&debugCamera.GetDistance(),
						0.1f,
						0.5f,
						500.0f
					);

					// 操作速度
					ImGui::DragFloat(
						"Camera Rotate Speed",
						&debugCamera.GetRotateSpeed(),
						0.0001f,
						0.0001f,
						0.1f,
						"%.4f"
					);

					ImGui::DragFloat(
						"Camera Pan Speed",
						&debugCamera.GetPanSpeed(),
						0.001f,
						0.001f,
						1.0f,
						"%.3f"
					);

					ImGui::DragFloat(
						"Camera Zoom Speed",
						&debugCamera.GetZoomSpeed(),
						0.01f,
						0.01f,
						10.0f
					);

					ImGui::Text(
						"Middle Drag : Orbit"
					);

					ImGui::Text(
						"Shift + Middle Drag : Pan"
					);

					ImGui::Text(
						"Mouse Wheel : Zoom"
					);

				}
				else {

					ImGui::Text("Free Camera");

					ImGui::DragFloat3(
						"Camera Position",
						&debugCamera.GetTranslation().x,
						0.01f
					);

					ImGui::DragFloat(
						"Move Speed",
						&debugCamera.GetMoveSpeed(),
						0.01f,
						0.01f,
						10.0f
					);

					ImGui::Text(
						"Right Drag : Rotate"
					);

					ImGui::Text(
						"WASD : Move"
					);

					ImGui::Text(
						"Space / Shift : Up Down"
					);
				}

				ImGui::SliderAngle(
					"Camera Rotate X",
					&debugCamera.GetRotation().x,
					-89.0f,
					89.0f
				);

				ImGui::SliderAngle(
					"Camera Rotate Y",
					&debugCamera.GetRotation().y,
					-180.0f,
					180.0f
				);

				ImGui::DragFloat(
					"Rotate Speed",
					&debugCamera.GetRotateSpeed(),
					0.0001f,
					0.0001f,
					0.1f,
					"%.4f"
				);

				if (ImGui::Button(
					"Reset Debug Camera"
				)) {
					debugCamera.Reset();
				}
			}

			// ==============================
			// GamePad情報
			// ==============================
			if (ImGui::CollapsingHeader(
				"GamePad",
				ImGuiTreeNodeFlags_DefaultOpen
			)) {
				const bool isConnected =
					input->IsGamePadConnected();

				if (isConnected) {
					ImGui::Text("Connected : Yes");

					const GamePadStick leftStick =
						input->GetLeftStick();

					const GamePadStick rightStick =
						input->GetRightStick();

					const float leftTrigger =
						input->GetLeftTrigger();

					const float rightTrigger =
						input->GetRightTrigger();

					ImGui::Text(
						"Left Stick  : %.2f, %.2f",
						leftStick.x,
						leftStick.y
					);

					ImGui::Text(
						"Right Stick : %.2f, %.2f",
						rightStick.x,
						rightStick.y
					);

					ImGui::Text(
						"Left Trigger  : %.2f",
						leftTrigger
					);

					ImGui::Text(
						"Right Trigger : %.2f",
						rightTrigger
					);

					ImGui::Separator();

					ImGui::Text(
						"A : %s",
						input->PushButton(
							XINPUT_GAMEPAD_A
						)
						? "ON"
						: "OFF"
					);

					ImGui::Text(
						"B : %s",
						input->PushButton(
							XINPUT_GAMEPAD_B
						)
						? "ON"
						: "OFF"
					);

					ImGui::Text(
						"X : %s",
						input->PushButton(
							XINPUT_GAMEPAD_X
						)
						? "ON"
						: "OFF"
					);

					ImGui::Text(
						"Y : %s",
						input->PushButton(
							XINPUT_GAMEPAD_Y
						)
						? "ON"
						: "OFF"
					);
				}
				else {
					ImGui::Text("Connected : No");
				}
			}

			ImGui::End();



			// ==========================================
			// ImGui終了
			// ==========================================

			ImGui::Render();

#endif
			// ==============================
			// Material更新
			// ==============================
			materialData->color = materialColor;
			materialData->lightingType = lightingType;

			// Suzanne専用Material更新
			materialDataSuzanne->color = suzanneColor;
			materialDataSuzanne->lightingType = lightingType;

			// ==============================
			// SpriteのUVTransform更新
			// ==============================
			Matrix4x4 uvTransformMatrixSprite =
				Matrix::MakeScaleMatrix(
					uvTransformSprite.scale
				);

			uvTransformMatrixSprite =
				Matrix::Multiply(
					uvTransformMatrixSprite,
					Matrix::MakeRotateZMatrix(
						uvTransformSprite.rotate.z
					)
				);

			uvTransformMatrixSprite =
				Matrix::Multiply(
					uvTransformMatrixSprite,
					Matrix::MakeTranslateMatrix(
						uvTransformSprite.translate
					)
				);

			// Sprite専用Materialへ毎フレーム反映
			materialDataSprite->uvTransform =
				uvTransformMatrixSprite;

			// ==============================
			// Plane World行列
			// ==============================
			Matrix4x4 worldMatrixPlane =
				Matrix::MakeAffineMatrix(
					transformPlane.scale,
					transformPlane.rotate,
					transformPlane.translate
				);

			// ==============================
			// Sphere World行列
			// ==============================
			Matrix4x4 worldMatrixSphere =
				Matrix::MakeAffineMatrix(
					transformSphere.scale,
					transformSphere.rotate,
					transformSphere.translate
				);

			// ==============================
			// Utah Teapot World行列
			// ==============================
			Matrix4x4 worldMatrixTeapot =
				Matrix::MakeAffineMatrix(
					transformTeapot.scale,
					transformTeapot.rotate,
					transformTeapot.translate
				);

			// ==============================
			// Stanford Bunny World行列
			// =============================

			Matrix4x4 worldMatrixBunny =
				Matrix::MakeAffineMatrix(
					transformBunny.scale,
					transformBunny.rotate,
					transformBunny.translate
				);




			// ==============================
			// MultiMesh World行列
			// ==============================
			Matrix4x4 worldMatrixMultiMesh =
				Matrix::MakeAffineMatrix(
					transformMultiMesh.scale,
					transformMultiMesh.rotate,
					transformMultiMesh.translate
				);


			// ==============================
			// MultiMaterial World行列
			// ==============================
			Matrix4x4 worldMatrixMultiMaterial =
				Matrix::MakeAffineMatrix(
					transformMultiMaterial.scale,
					transformMultiMaterial.rotate,
					transformMultiMaterial.translate
				);


			// Suzanne
			Matrix4x4 worldMatrixSuzanne =
				Matrix::MakeAffineMatrix(
					transformSuzanne.scale,
					transformSuzanne.rotate,
					transformSuzanne.translate
				);

			// ==============================
			// カメラ切り替え
			// ==============================
			Matrix4x4 viewMatrix{};

			if (isDebugCamera) {

				viewMatrix =
					debugCamera.GetViewMatrix();

			}
			else {

				Matrix4x4 cameraMatrix =
					Matrix::MakeAffineMatrix(
						cameraTransform.scale,
						cameraTransform.rotate,
						cameraTransform.translate
					);

				viewMatrix =
					Matrix::Inverse(cameraMatrix);
			}

			// ==============================
			// Projection
			// ==============================
			Matrix4x4 projectionMatrix =
				Matrix::MakePerspectiveFovMatrix(
					0.45f,
					static_cast<float>(kClientWidth) /
					static_cast<float>(kClientHeight),
					0.1f,
					100.0f
				);

			// ==============================
			// ViewProjection
			// ==============================
			Matrix4x4 viewProjectionMatrix =
				Matrix::Multiply(
					viewMatrix,
					projectionMatrix
				);

			// ==============================
			// Plane WVP
			// ==============================
			wvpDataPlane->World =
				worldMatrixPlane;

			wvpDataPlane->WVP =
				Matrix::Multiply(
					worldMatrixPlane,
					viewProjectionMatrix
				);

			// ==============================
			// Sphere WVP
			// ==============================
			wvpDataSphere->World =
				worldMatrixSphere;

			wvpDataSphere->WVP =
				Matrix::Multiply(
					worldMatrixSphere,
					viewProjectionMatrix
				);

			// ==============================
			// Utah Teapot WVP
			// ==============================
			wvpDataTeapot->World =
				worldMatrixTeapot;

			wvpDataTeapot->WVP =
				Matrix::Multiply(
					worldMatrixTeapot,
					viewProjectionMatrix
				);


			// ==============================
			// Stanford Bunny WVP
			// ==============================
			wvpDataBunny->World =
				worldMatrixBunny;

			wvpDataBunny->WVP =
				Matrix::Multiply(
					worldMatrixBunny,
					viewProjectionMatrix
				);


			// ==============================
			// MultiMesh WVP
			// ==============================
			wvpDataMultiMesh->World =
				worldMatrixMultiMesh;

			wvpDataMultiMesh->WVP =
				Matrix::Multiply(
					worldMatrixMultiMesh,
					viewProjectionMatrix
				);


			// ==============================
			// MultiMaterial WVP
			// ==============================
			wvpDataMultiMaterial->World =
				worldMatrixMultiMaterial;

			wvpDataMultiMaterial->WVP =
				Matrix::Multiply(
					worldMatrixMultiMaterial,
					viewProjectionMatrix
				);


			// Suzanne
			wvpDataSuzanne->World = worldMatrixSuzanne;
			wvpDataSuzanne->WVP =
				Matrix::Multiply(
					worldMatrixSuzanne,
					viewProjectionMatrix
				);

			// Fence
			Matrix4x4 worldMatrixFence =
				Matrix::MakeAffineMatrix(
					transformFence.scale,
					transformFence.rotate,
					transformFence.translate
				);

			Matrix4x4 worldViewProjectionMatrixFence =
				Matrix::Multiply(
					worldMatrixFence,
					viewProjectionMatrix
				);

			wvpDataFence->WVP =
				worldViewProjectionMatrixFence;

			wvpDataFence->World =
				worldMatrixFence;


			//Sprite用のWorldViewProjectionMatrixを作る
			Matrix4x4 worldMatrixSprite = Matrix::MakeAffineMatrix(transformSprite.scale, transformSprite.rotate, transformSprite.translate);
			Matrix4x4 ViewMatrixSprite = Matrix::MakeIdentity4x4();
			Matrix4x4 projectionMatrixSprite = Matrix::MakeOrthographicMatrix(0.0f, 0.0f, float(kClientWidth), float(kClientHeight), 0.0f, 100.0f);
			Matrix4x4 worldViewProjectionMatrixSprite = Matrix::Multiply(worldMatrixSprite, Matrix::Multiply(ViewMatrixSprite, projectionMatrixSprite));
			*transformationMatrixDataSprite = worldViewProjectionMatrixSprite;


			//三角形の描画
			commandList->RSSetViewports(1, &viewport);			// ビューポートの設定
			commandList->RSSetScissorRects(1, &scissorRect);	// シザリング矩形の設定
			//RootSignatureとPSOに設定してるけど別途設定が必要
			commandList->SetGraphicsRootSignature(rootSignature.Get()); // RootSignatureの設定
			commandList->SetPipelineState(graphicsPipelineState); // PSOの設定
			commandList->IASetVertexBuffers(0, 1, &vertexBufferViewSphere);			// 頂点バッファビューの設定
			commandList->IASetIndexBuffer(&indexBufferViewSprite); //IBVを設定
			//形状を設定。PSOに設定しているものとはまた別。同じものを設定すると考えておけばいい
			commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST); // トポロジの設定
			//マテリアルCBufferの場所を設定
			commandList->SetGraphicsRootConstantBufferView(0, materialResource->GetGPUVirtualAddress()); // Materialリソースの設定。RootParameterのShaderRegisterと合わせること
			//WVP行列CBufferの場所を設定
			commandList->SetGraphicsRootConstantBufferView(1, wvpResourceSphere->GetGPUVirtualAddress()); // WVPリソースの設定。RootParameterのShaderRegisterと合わせること
			//DirectionalLight用の定数バッファ(CBV)をRootParameter[3]にセットする
			commandList->SetGraphicsRootConstantBufferView(3, directionalLightResource->GetGPUVirtualAddress());
			//SRVのDescriptorTableの先頭を設定。2はrootParamater[2]である
			commandList->SetGraphicsRootDescriptorTable(2, useModelTexture ? textureSrvHandleGPU2 : textureSrvHandleGPU); // SRVの設定。RootParameterのShaderRegisterと合わせること
			//描画！　(DrawCall/ドローコール)。　3頂点で一つのインスタンス。インスタンスについては今後
			// ==========================================
			// Sphere描画
			// ==========================================
			if (isDrawSphere) {

				commandList->IASetVertexBuffers(
					0,
					1,
					&vertexBufferViewSphere
				);

				commandList->IASetIndexBuffer(nullptr);

				// Material
				commandList->SetGraphicsRootConstantBufferView(
					0,
					materialResource->GetGPUVirtualAddress()
				);

				// Sphere専用WVP
				commandList->SetGraphicsRootConstantBufferView(
					1,
					wvpResourceSphere->GetGPUVirtualAddress()
				);

				// Texture
				commandList->SetGraphicsRootDescriptorTable(
					2,
					textureSrvHandleGPU
				);

				// Light
				commandList->SetGraphicsRootConstantBufferView(
					3,
					directionalLightResource->GetGPUVirtualAddress()
				);

				commandList->DrawInstanced(
					kVertexCount,
					1,
					0,
					0
				);
			}

			// ==========================================
			// Plane描画
			// ==========================================
			if (isDrawPlane) {
				commandList->IASetVertexBuffers(
					0,
					1,
					&vertexBufferViewPlane
				);

				commandList->IASetIndexBuffer(nullptr);

				commandList->SetGraphicsRootConstantBufferView(
					0,
					materialResource->GetGPUVirtualAddress()
				);

				commandList->SetGraphicsRootConstantBufferView(
					1,
					wvpResourcePlane->GetGPUVirtualAddress()
				);

				commandList->SetGraphicsRootDescriptorTable(
					2,
					textureSrvHandleGPU2
				);

				commandList->SetGraphicsRootConstantBufferView(
					3,
					directionalLightResource->GetGPUVirtualAddress()
				);

				commandList->DrawInstanced(
					static_cast<UINT>(modelDataPlane.vertices.size()),
					1,
					0,
					0
				);
			}

			// ==========================================
			// Utah Teapot描画
			// ==========================================
			if (isDrawTeapot) {

				commandList->IASetVertexBuffers(
					0,
					1,
					&vertexBufferViewTeapot
				);

				commandList->IASetIndexBuffer(nullptr);

				// Material
				commandList->SetGraphicsRootConstantBufferView(
					0,
					materialResource->GetGPUVirtualAddress()
				);

				// Teapot専用WVP
				commandList->SetGraphicsRootConstantBufferView(
					1,
					wvpResourceTeapot->GetGPUVirtualAddress()
				);

				// Teapot専用checkerBoardテクスチャ
				commandList->SetGraphicsRootDescriptorTable(
					2,
					textureSrvHandleGPUTeapot
				);

				// Light
				commandList->SetGraphicsRootConstantBufferView(
					3,
					directionalLightResource->GetGPUVirtualAddress()
				);

				commandList->DrawInstanced(
					static_cast<UINT>(
						modelDataTeapot.vertices.size()
						),
					1,
					0,
					0
				);
			}

			commandList->SetGraphicsRootDescriptorTable(2, textureSrvHandleGPU);


			// ==========================================
			// Stanford Bunny描画
			// ==========================================
			if (isDrawBunny) {

				commandList->IASetVertexBuffers(
					0,
					1,
					&vertexBufferViewBunny
				);

				commandList->IASetIndexBuffer(nullptr);

				// Material
				commandList->SetGraphicsRootConstantBufferView(
					0,
					materialResource->GetGPUVirtualAddress()
				);

				// Bunny専用WVP
				commandList->SetGraphicsRootConstantBufferView(
					1,
					wvpResourceBunny->GetGPUVirtualAddress()
				);

				// Bunny専用Texture
				commandList->SetGraphicsRootDescriptorTable(
					2,
					textureSrvHandleGPUBunny
				);

				// DirectionalLight
				commandList->SetGraphicsRootConstantBufferView(
					3,
					directionalLightResource->GetGPUVirtualAddress()
				);

				commandList->DrawInstanced(
					static_cast<UINT>(
						modelDataBunny.vertices.size()
						),
					1,
					0,
					0
				);
			}

			// ==========================================
			// Fence描画
			// ==========================================
			if (isDrawFence) {

				commandList->IASetVertexBuffers(
					0,
					1,
					&vertexBufferViewFence
				);

				commandList->IASetIndexBuffer(nullptr);

				// Material
				commandList->SetGraphicsRootConstantBufferView(
					0,
					materialResource->GetGPUVirtualAddress()
				);

				// Fence用WVP
				commandList->SetGraphicsRootConstantBufferView(
					1,
					wvpResourceFence->GetGPUVirtualAddress()
				);

				// Fence専用Texture
				commandList->SetGraphicsRootDescriptorTable(
					2,
					textureSrvHandleGPUFence
				);

				// DirectionalLight
				commandList->SetGraphicsRootConstantBufferView(
					3,
					directionalLightResource->GetGPUVirtualAddress()
				);

				commandList->DrawInstanced(
					static_cast<UINT>(
						modelDataFence.vertices.size()
						),
					1,
					0,
					0
				);
			}

			// ==========================================
			// MultiMesh描画
			// Meshごとに独立したVertexBufferを設定する
			// ==========================================
			if (isDrawMultiMesh) {
				commandList->SetGraphicsRootSignature(
					rootSignature.Get()
				);

				commandList->SetPipelineState(
					graphicsPipelineState
				);

				commandList->IASetPrimitiveTopology(
					D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST
				);

				commandList->IASetIndexBuffer(nullptr);

				// 全Mesh共通Material
				commandList->SetGraphicsRootConstantBufferView(
					0,
					materialResource->GetGPUVirtualAddress()
				);

				// 全Mesh共通Transform
				commandList->SetGraphicsRootConstantBufferView(
					1,
					wvpResourceMultiMesh->GetGPUVirtualAddress()
				);

				// multiMesh.mtlはuvChecker.pngを使用
				commandList->SetGraphicsRootDescriptorTable(
					2,
					textureSrvHandleGPU
				);

				commandList->SetGraphicsRootConstantBufferView(
					3,
					directionalLightResource->
					GetGPUVirtualAddress()
				);

				for (
					const MeshData& mesh :
					modelDataMultiMesh.meshes
					) {
					commandList->IASetVertexBuffers(
						0,
						1,
						&mesh.vertexBufferView
					);

					commandList->DrawInstanced(
						static_cast<UINT>(
							mesh.vertices.size()
							),
						1,
						0,
						0
					);
				}
			}


			// ==========================================
			// MultiMaterial描画
			// MeshごとにVertexBufferとTextureを切り替える
			// ==========================================
			if (isDrawMultiMaterial) {
				commandList->SetGraphicsRootSignature(
					rootSignature.Get()
				);

				commandList->SetPipelineState(
					graphicsPipelineState
				);

				commandList->IASetPrimitiveTopology(
					D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST
				);

				commandList->IASetIndexBuffer(nullptr);

				commandList->SetGraphicsRootConstantBufferView(
					0,
					materialResource->GetGPUVirtualAddress()
				);

				commandList->SetGraphicsRootConstantBufferView(
					1,
					wvpResourceMultiMaterial->
					GetGPUVirtualAddress()
				);

				commandList->SetGraphicsRootConstantBufferView(
					3,
					directionalLightResource->
					GetGPUVirtualAddress()
				);

				for (
					const MeshData& mesh :
					modelDataMultiMaterial.meshes
					) {
					assert(
						mesh.materialIndex <
						modelDataMultiMaterial.materials.size()
					);

					const MaterialData& material =
						modelDataMultiMaterial.materials[
							mesh.materialIndex
						];

					D3D12_GPU_DESCRIPTOR_HANDLE
						textureHandle{};

					if (material.textureIndex == 5) {
						textureHandle =
							textureSrvHandleGPUMonsterBall;
					}
					else {
						textureHandle =
							textureSrvHandleGPU;
					}

					commandList->
						SetGraphicsRootDescriptorTable(
							2,
							textureHandle
						);

					commandList->IASetVertexBuffers(
						0,
						1,
						&mesh.vertexBufferView
					);

					commandList->DrawInstanced(
						static_cast<UINT>(
							mesh.vertices.size()
							),
						1,
						0,
						0
					);
				}
			}


			// ==========================================
			// Suzanne描画（UVなし専用Pipeline）
			// ==========================================
			if (isDrawSuzanne) {
				commandList->SetGraphicsRootSignature(rootSignatureSuzanne.Get());
				commandList->SetPipelineState(graphicsPipelineStateSuzanne.Get());
				commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
				commandList->IASetVertexBuffers(0, 1, &vertexBufferViewSuzanne);
				commandList->IASetIndexBuffer(nullptr);

				// Suzanne専用Material : b0
				commandList->SetGraphicsRootConstantBufferView(
					0,
					materialResourceSuzanne->GetGPUVirtualAddress()
				);

				// TransformationMatrix : b1 (VS)
				commandList->SetGraphicsRootConstantBufferView(
					1,
					wvpResourceSuzanne->GetGPUVirtualAddress()
				);

				// DirectionalLight : b1 (PS)
				commandList->SetGraphicsRootConstantBufferView(
					2,
					directionalLightResource->GetGPUVirtualAddress()
				);

				commandList->DrawInstanced(
					static_cast<UINT>(modelDataSuzanne.size()),
					1,
					0,
					0
				);

				// 後続の通常モデル・Sprite用設定へ戻す
				commandList->SetGraphicsRootSignature(rootSignature.Get());
				commandList->SetPipelineState(graphicsPipelineState);
			}

			// ==========================================
			// スプライト描画
			// ==========================================
			if (isDrawSprite) {

				// Spriteで使用する通常モデル用設定へ確実に戻す
				commandList->SetGraphicsRootSignature(
					rootSignature.Get()
				);

				commandList->SetPipelineState(
					graphicsPipelineState
				);

				commandList->IASetPrimitiveTopology(
					D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST
				);

				// Sprite用VertexBuffer
				commandList->IASetVertexBuffers(
					0,
					1,
					&vertexBufferViewSprite
				);

				// Sprite用IndexBuffer
				commandList->IASetIndexBuffer(
					&indexBufferViewSprite
				);

				// RootParameter[0]
				// Sprite用Material
				commandList->SetGraphicsRootConstantBufferView(
					0,
					materialResourceSprite->GetGPUVirtualAddress()
				);

				// RootParameter[1]
				// Sprite用WVP
				commandList->SetGraphicsRootConstantBufferView(
					1,
					transformationMatrixResourceSprite->
					GetGPUVirtualAddress()
				);

				// RootParameter[2]
				// Sprite用Texture
				commandList->SetGraphicsRootDescriptorTable(
					2,
					textureSrvHandleGPU
				);

				// RootParameter[3]
				// 通常RootSignatureにはDirectionalLightが必要
				// SpriteではlightingTypeがkNoneなので実際の計算には使わない
				commandList->SetGraphicsRootConstantBufferView(
					3,
					directionalLightResource->
					GetGPUVirtualAddress()
				);

				commandList->DrawIndexedInstanced(
					6,
					1,
					0,
					0,
					0
				);
			}

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
				fence.Get(),
				fenceValue
			);

			assert(SUCCEEDED(hr));
		}
	}


	// ==============================
	// GPU終了待機
	// ==============================

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

	//XAudio2解放
	xAudio2.Reset();
	//音声データ解放
	SoundUnload(&soundData1);

	CloseHandle(fenceEvent);

	Log(logStream, "Application End");

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

	// DxcCompiler
	if (dxcCompiler) {
		dxcCompiler->Release();
		dxcCompiler = nullptr;
	}

	// DxcUtils
	if (dxcUtils) {
		dxcUtils->Release();
		dxcUtils = nullptr;
	}

#ifdef _DEBUG

	if (infoQueue) {
		infoQueue->Release();
		infoQueue = nullptr;
	}


#endif

	DestroyWindow(hwnd);

	input->Finalize();

	delete input;
	input = nullptr;

	CoUninitialize();

	return 0;
}