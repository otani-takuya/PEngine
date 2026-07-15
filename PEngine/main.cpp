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

#include <wrl.h>
//using Microsoft::WRL::ComPtr;

#include "Input.h"

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

struct Material {
	Vector4 color;
	int32_t enableLighting;
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
	std::string textureFilePath;
};

struct ModelData {
	std::vector<VertexData> vertices;
	MaterialData material;
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
struct ChunkHeader{
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
	heapProperties.Type = D3D12_HEAP_TYPE_CUSTOM; // 細かい設定を行う
	heapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_WRITE_BACK; // writeBackポリシーでCPUアクセス可能
	heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_L0; // プロセッサの近くに配置
	// 3. Resource
	Microsoft::WRL::ComPtr<ID3D12Resource> resource = nullptr;
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

MaterialData LoadMaterialTemplateFile(const std::string& directoryPath, const std::string& filename) {
	//1. 中で必要となる変数の宣言
	MaterialData materialData;							//構築するMaterialData
	std::string line;									//ファイルから読んだ1行を格納するもの

	//2. ファイルを開く
	std::ifstream file(directoryPath + "/" + filename);	//ファイルを開く
	assert(file.is_open());								//とりあえず開けなかったら止める

	//3. 実際にファイルを読み、ModelDataを構築していく
	while (std::getline(file, line)) {
		std::string identifier; std::istringstream s(line);
		s >> identifier;

		// identifierに応じた処理
		if (identifier == "map_Kd") {
			std::string textureFilename; s >> textureFilename;
			// 連結してファイルパスにする
			materialData.textureFilePath = directoryPath + "/" + textureFilename;
		}
	}

	//4. MaterialDataを返す	
	return materialData;
}

ModelData LoadObjectFile(const std::string& directoryPath, const std::string& filename)
{
	//1. 中で必要となる変数の宣言
	ModelData modelData;				//構築するModelData
	std::vector<Vector4> positions;		//位置
	std::vector<Vector3> normals;		//法線
	std::vector<Vector2> texcoords;		//テクスチャ座標
	std::string line;					//ファイルから読んだ1行を格納するもの

	//2. ファイルを開く
	std::ifstream file(directoryPath + "/" + filename);		//ファイルを開く
	assert(file.is_open());		//とりあえず開けなかったら止める

	//3. 実際にファイルを読み、ModelDataを構築していく
	while (std::getline(file, line))
	{
		std::string identifier;
		std::istringstream s(line);
		s >> identifier;	//行の先端の識別子を読む

		//identfierに応じた処理
		if (identifier == "v") {
			Vector4 position;
			s >> position.x >> position.y >> position.z;

			// 右手系 → 左手系へ変換
			position.x *= -1.0f;

			position.w = 1.0f;
			positions.push_back(position);
		}
		else if (identifier == "vt") 
		{
			Vector2 texcoord;
			s >> texcoord.x >> texcoord.y;

			// OBJとDirectXでV方向が逆なので反転する
			texcoord.x = 1.0f - texcoord.x;
			texcoord.y = 1.0f - texcoord.y;

			texcoords.push_back(texcoord);
		}
		else if (identifier == "vn")
		{
			Vector3 normal;
			s >> normal.x >> normal.y >> normal.z;

			// 右手系 → 左手系へ変換
			normal.x *= -1.0f;

			normals.push_back(normal);
		}
		else if (identifier == "f") 
		{
			VertexData triangle[3];
			// 面は三角形限定。 その他は未対応
			for (int32_t faceVertex = 0; faceVertex < 3; ++faceVertex) {
				std::string vertexDefinition;
				s >> vertexDefinition;
				// 頂点の要素へのIndexは 「位置/UV/法線」 で格納されているので、分解してIndexを取得する 
				std::istringstream v(vertexDefinition);
				uint32_t elementIndices[3];
				for (int32_t element = 0; element < 3; ++element) {
					std::string index;
					std::getline(v, index, '/');	///区切りでインデックスを読んでいく
					elementIndices[element] = std::stoi(index);
				}
				// 要素へのIndexから、 実際の要素の値を取得して、 頂点を構築する 
				Vector4 position = positions[elementIndices[0] - 1];
				Vector2 texcoord = texcoords[elementIndices[1] - 1];
				Vector3 normal = normals[elementIndices[2] - 1];
				VertexData vertex = { position, texcoord, normal };
				modelData.vertices.push_back(vertex);
				triangle[faceVertex] = { position, texcoord, normal };
			}
			//頂点を逆順で登録することで、周り順を反転させる
			modelData.vertices.push_back(triangle[2]);
			modelData.vertices.push_back(triangle[1]);
			modelData.vertices.push_back(triangle[0]);
		}
		else if (identifier == "mtllib") 
		{
			//materialTemplateLibraryファイルの名前を取得する
			std::string materialFilename;
			s >> materialFilename;
			//基本的にobjファイルと同一階層にmtlは存在させるので、ディレクトリ名とファイル名を探す
			modelData.material = LoadMaterialTemplateFile(directoryPath, materialFilename);
		}
		
	}

	//4. ModelDataを返す
	return modelData;
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
	SoundData soundData ={};

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
	Microsoft::WRL::ComPtr<IDxcBlob> vertexShaderBlob = CompileShader(L"Object3d.VS.hlsl",
		L"vs_6_0", dxcUtils, dxcCompiler, dxcIncludeHandler.Get());

	assert(vertexShaderBlob != nullptr);

	Microsoft::WRL::ComPtr<IDxcBlob> pixelShaderBlob = CompileShader(L"Object3d.PS.hlsl",
		L"ps_6_0", dxcUtils, dxcCompiler, dxcIncludeHandler.Get());

	assert(pixelShaderBlob != nullptr);

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

	Input::Initialize(hInstance, hwnd);


	//==============================
	// 球生成用
	//==============================

	// VertexResourceの生成
	// ID3D12Resource* vertexResource = CreateBufferResource(device, sizeof(VertexData) * kVertexCount);

	//==============================
	// ModelData用
	//==============================
	//モデル読み込み
	ModelData modelData = LoadObjectFile("resources", "plane.obj");
	//頂点リソースを作る
	Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource =
		CreateBufferResource(device.Get(),
			sizeof(VertexData) * modelData.vertices.size());



	//indexResourceSpriteの生成
	Microsoft::WRL::ComPtr<ID3D12Resource> indexResourceSprite = CreateBufferResource(device.Get(), sizeof(uint32_t) * 6);

	//WVP用のResourceの生成
	Microsoft::WRL::ComPtr<ID3D12Resource> wvpResource = CreateBufferResource(device.Get(), sizeof(TransformationMatrix));


	//平行光源用のResourceの生成
	Microsoft::WRL::ComPtr<ID3D12Resource> directionalLightResource = CreateBufferResource(device.Get(), sizeof(DirectionalLight));

	DirectionalLight* directionalLightData = nullptr;

	// DepthStencil用のPSOの生成
	graphicsPipelineStateDesc.DepthStencilState = depthStencilDesc;
	graphicsPipelineStateDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

	hr = device->CreateGraphicsPipelineState(&graphicsPipelineStateDesc,
		IID_PPV_ARGS(&graphicsPipelineState));
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
	*transformationMatrixDataSprite = MakeIdentity4x4();

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
	materialDataSprite->enableLighting = false;
	materialDataSprite->uvTransform = MakeIdentity4x4();


	// ==========================================
	// MaterialResourceの生成
	// ==========================================
	Microsoft::WRL::ComPtr<ID3D12Resource> materialResource =
		CreateBufferResource(device.Get(), sizeof(Material));
	// Materialデータを書き込む
	Material* materialData = nullptr;
	TransformationMatrix* wvpData = nullptr;

	materialResource->Map(0, nullptr, reinterpret_cast<void**>(&materialData));

	// 色設定
	// ImGuiで操作する色
	Vector4 materialColor = { 1.0f, 1.0f, 1.0f, 1.0f };
	// 初期値を書き込む
	materialData->color = materialColor;
	materialData->enableLighting = true;
	materialData->uvTransform = MakeIdentity4x4();


	//VertexBufferViewの作成
	// 頂点バッファビューを作成する
	D3D12_VERTEX_BUFFER_VIEW vertexBufferView{};
	// リソースの先頭のアドレスから使う
	vertexBufferView.BufferLocation = vertexResource->GetGPUVirtualAddress();
	// 使用するリソースのサイズは頂点のサイズ
	
	// 球生成
	// vertexBufferView.SizeInBytes = sizeof(VertexData) * kVertexCount;

	// ModelData
	vertexBufferView.SizeInBytes = UINT(sizeof(VertexData) * modelData.vertices.size());

	// 1頂点あたりのサイズ
	vertexBufferView.StrideInBytes = sizeof(VertexData);

	//indexBufferViewの作成
	D3D12_INDEX_BUFFER_VIEW indexBufferViewSprite{};
	// リソースの先頭のアドレスから使う
	indexBufferViewSprite.BufferLocation = indexResourceSprite->GetGPUVirtualAddress();
	// 使用するリソースのサイズはインデックス6つ分のサイズ
	indexBufferViewSprite.SizeInBytes = sizeof(uint32_t) * 6;
	//インデックスはuint32_tとする
	indexBufferViewSprite.Format = DXGI_FORMAT_R32_UINT;

	// 頂点データをリソースにコピー
	VertexData* vertexData = nullptr;
	//書き込むためのアドレスを取得
	vertexResource->Map(
		0,
		nullptr,
		reinterpret_cast<void**>(&vertexData)
	);

	wvpResource->Map(
		0,
		nullptr,
		reinterpret_cast<void**>(&wvpData)
	);

	directionalLightResource->Map(
		0,
		nullptr,
		reinterpret_cast<void**>(&directionalLightData)
	);


	// ライトの初期値
	directionalLightData->color = { 1.0f,1.0f,1.0f,1.0f };
	directionalLightData->direction = { 0.0f,-1.0f,0.0f };
	directionalLightData->intensity = 1.0f;

	//単位行列を書き込んでおく
	//*wvpData = MakeIdentity4x4();

	// 球生成
	/*
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
				(latIndex * kSubdivision + lonIndex)
				* 6;

			// a
			vertexData[start + 0].position = {
				cosf(lat0) * cosf(lon0),
				sinf(lat0),
				cosf(lat0) * sinf(lon0),
				1.0f
			};

			vertexData[start + 0].texcoord = {
				float(lonIndex) / kSubdivision,
				1.0f - float(latIndex) / kSubdivision
			};

			// b
			vertexData[start + 1].position = {
				cosf(lat1) * cosf(lon0),
				sinf(lat1),
				cosf(lat1) * sinf(lon0),
				1.0f
			};

			vertexData[start + 1].texcoord = {
				float(lonIndex) / kSubdivision,
				1.0f - float(latIndex + 1) / kSubdivision
			};

			// c
			vertexData[start + 2].position = {
				cosf(lat0) * cosf(lon1),
				sinf(lat0),
				cosf(lat0) * sinf(lon1),
				1.0f
			};


			vertexData[start + 2].texcoord = {
				float(lonIndex + 1) / kSubdivision,
				1.0f - float(latIndex) / kSubdivision
			};

			// d
			vertexData[start + 3].position = {
				cosf(lat0) * cosf(lon1),
				sinf(lat0),
				cosf(lat0) * sinf(lon1),
				1.0f
			};

			vertexData[start + 3].texcoord = {
				float(lonIndex + 1) / kSubdivision,
				1.0f - float(latIndex) / kSubdivision
			};

			vertexData[start + 4].position = {
				cosf(lat1) * cosf(lon0),
				sinf(lat1),
				cosf(lat1) * sinf(lon0),
				1.0f
			};

			vertexData[start + 4].texcoord = {
				float(lonIndex) / kSubdivision,
				1.0f - float(latIndex + 1) / kSubdivision
			};

			vertexData[start + 5].position = {
				cosf(lat1) * cosf(lon1),
				sinf(lat1),
				cosf(lat1) * sinf(lon1),
				1.0f
			};

			vertexData[start + 5].texcoord = {
				float(lonIndex + 1) / kSubdivision,
				1.0f - float(latIndex + 1) / kSubdivision
			};

			// 法線設定
			for (int i = 0; i < 6; i++) {
				vertexData[start + i].normal = {
					vertexData[start + i].position.x,
					vertexData[start + i].position.y,
					vertexData[start + i].position.z
				};
			}
		}

	}
	*/

	std::memcpy(
		vertexData,
		modelData.vertices.data(),
		sizeof(VertexData) * modelData.vertices.size()
	);

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


	// 最初に閉じておく
	hr = commandList->Close();
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



	//Texture読み込み
	DirectX::ScratchImage mipImages = LoadTexture("resources/uvChecker.png");
	const DirectX::TexMetadata& metadata = mipImages.GetMetadata();
	Microsoft::WRL::ComPtr<ID3D12Resource> textureResource = CreateTextureResource(device.Get(), metadata);
	UploadTextureData(textureResource.Get(), mipImages);

	// 2枚目のTextureを読んで転送する
	// モンスターボール用
	//DirectX::ScratchImage mipImages2 = LoadTexture("resources/monsterBall.png");
	//モデルマテリアル用
	DirectX::ScratchImage mipImages2 = LoadTexture(modelData.material.textureFilePath);
	const DirectX::TexMetadata& metadata2 = mipImages2.GetMetadata();
	Microsoft::WRL::ComPtr<ID3D12Resource> textureResource2 = CreateTextureResource(device.Get(), metadata2);
	UploadTextureData(textureResource2.Get(), mipImages2);

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
	// SRVの生成
	device->CreateShaderResourceView(
		textureResource.Get(),
		&srvDesc,
		textureSrvHandleCPU
	);

	device->CreateShaderResourceView(
		textureResource2.Get(),
		&srvDesc2,
		textureSrvHandleCPU2
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
	Transform transform{
		{1.0f, 1.0f, 1.0f},
		{0.0f, 0.0f, 0.0f},
		{0.0f, 0.0f, 0.0f}
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


	bool useMonsterBall = false;

	//音声読み込み
	SoundData soundData1 = SoundLoadWave("resources/Alarm01.wav");

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

			Input::Update();

			//使い方サンプル
			//数字の0キーが押されていたら
			// 押している間
			if (Input::PushKey(DIK_0)) {
				OutputDebugStringA("Hit 0\n");	//出力ウィンドウに「Hit 0」と表示
			}

			// 押した瞬間
			if (Input::TriggerKey(DIK_1)) {
				//音声再生
				SoundPlayWave(xAudio2.Get(), soundData1);
			}

			// 離した瞬間
			if (Input::ReleaseKey(DIK_2)) {
				OutputDebugStringA("Hit 2\n");	
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

			ImGui::ColorEdit4("Color", &materialColor.x);
			ImGui::Checkbox("useMonsterBall", &useMonsterBall);

			// ===== モデル操作 =====
			ImGui::Separator();
			ImGui::Text("Model Transform");

			ImGui::DragFloat3("Model Translate", &transform.translate.x, 0.01f);
			ImGui::DragFloat3("Model Scale", &transform.scale.x, 0.01f, 0.01f, 10.0f);
			ImGui::SliderAngle("Model Rotate X", &transform.rotate.x);
			ImGui::SliderAngle("Model Rotate Y", &transform.rotate.y);
			ImGui::SliderAngle("Model Rotate Z", &transform.rotate.z);

			// ===== ライト操作 =====
			ImGui::Separator();
			ImGui::Text("Directional Light");

			// 色
			ImGui::ColorEdit4("Light Color", &directionalLightData->color.x);

			// 向き（重要）
			ImGui::DragFloat3("Light Direction", &directionalLightData->direction.x, 0.01f);

			// 強さ
			ImGui::DragFloat("Intensity", &directionalLightData->intensity, 0.01f, 0.0f, 10.0f);

			// ===== uvTransform操作 =====
			ImGui::Separator();
			ImGui::Text("uvTransform Sprite");
			ImGui::DragFloat2("uvTransform Translate", &uvTransformSprite.translate.x, 0.01f, -10.0f, 10.0f);
			ImGui::DragFloat2("uvTransform Scale", &uvTransformSprite.scale.x, 0.01f, 0.0f, 10.0f);
			ImGui::SliderAngle("UVRotate", &uvTransformSprite.rotate.z);

			ImGui::End();



			// ==========================================
			// ImGui終了
			// ==========================================

			ImGui::Render();
#endif
			// WVP行列の更新
			//transform.rotate.y += 0.03f; // 毎フレームY軸に回転を加える
			Matrix4x4 worldMatrix = MakeAffineMatrix(transform.scale, transform.rotate, transform.translate);
			wvpData->World = worldMatrix;
			// 色更新
			materialData->color = materialColor;
			materialData->enableLighting = true;

			// 色更新
			materialData->color = materialColor;
			materialData->enableLighting = true;

			// UVTransform更新
			Matrix4x4 uvTransformMatrix =
				MakeScaleMatrix(uvTransformSprite.scale);

			uvTransformMatrix =
				Multiply(uvTransformMatrix,
					MakeRotateZMatrix(uvTransformSprite.rotate.z));

			uvTransformMatrix =
				Multiply(uvTransformMatrix,
					MakeTranslateMatrix(uvTransformSprite.translate));

			materialDataSprite->uvTransform = uvTransformMatrix;

			// 3次元的にする
			Matrix4x4 cameraMatrix = MakeAffineMatrix(cameraTransform.scale, cameraTransform.rotate, cameraTransform.translate);
			Matrix4x4 viewMatrix = Inverse(cameraMatrix);
			Matrix4x4 projectionMatrix = MakePerspectiveFovMatrix(0.45f, float(kClientWidth) / float(kClientHeight), 0.1f, 100.0f);
			//WVPMatrixを作る
			Matrix4x4 worldViewProjectionMatrix = Multiply(worldMatrix, Multiply(viewMatrix, projectionMatrix));
			wvpData->WVP = worldViewProjectionMatrix;
			wvpData->World = worldMatrix;


			//Sprite用のWorldViewProjectionMatrixを作る
			Matrix4x4 worldMatrixSprite = MakeAffineMatrix(transformSprite.scale, transformSprite.rotate, transformSprite.translate);
			Matrix4x4 ViewMatrixSprite = MakeIdentity4x4();
			Matrix4x4 projectionMatrixSprite = MakeOrthographicMatrix(0.0f, 0.0f, float(kClientWidth), float(kClientHeight), 0.0f, 100.0f);
			Matrix4x4 worldViewProjectionMatrixSprite = Multiply(worldMatrixSprite, Multiply(ViewMatrixSprite, projectionMatrixSprite));
			*transformationMatrixDataSprite = worldViewProjectionMatrixSprite;


			//三角形の描画
			commandList->RSSetViewports(1, &viewport);			// ビューポートの設定
			commandList->RSSetScissorRects(1, &scissorRect);	// シザリング矩形の設定
			//RootSignatureとPSOに設定してるけど別途設定が必要
			commandList->SetGraphicsRootSignature(rootSignature.Get()); // RootSignatureの設定
			commandList->SetPipelineState(graphicsPipelineState); // PSOの設定
			commandList->IASetVertexBuffers(0, 1, &vertexBufferView);			// 頂点バッファビューの設定
			commandList->IASetIndexBuffer(&indexBufferViewSprite); //IBVを設定
			//形状を設定。PSOに設定しているものとはまた別。同じものを設定すると考えておけばいい
			commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST); // トポロジの設定
			//マテリアルCBufferの場所を設定
			commandList->SetGraphicsRootConstantBufferView(0, materialResource->GetGPUVirtualAddress()); // Materialリソースの設定。RootParameterのShaderRegisterと合わせること
			//WVP行列CBufferの場所を設定
			commandList->SetGraphicsRootConstantBufferView(1, wvpResource->GetGPUVirtualAddress()); // WVPリソースの設定。RootParameterのShaderRegisterと合わせること
			//DirectionalLight用の定数バッファ(CBV)をRootParameter[3]にセットする
			commandList->SetGraphicsRootConstantBufferView(3, directionalLightResource->GetGPUVirtualAddress());
			//SRVのDescriptorTableの先頭を設定。2はrootParamater[2]である
			commandList->SetGraphicsRootDescriptorTable(2, useMonsterBall ? textureSrvHandleGPU2 : textureSrvHandleGPU); // SRVの設定。RootParameterのShaderRegisterと合わせること
			//描画！　(DrawCall/ドローコール)。　3頂点で一つのインスタンス。インスタンスについては今後
			// 球生成
			// commandList->DrawInstanced(kVertexCount, 1, 0, 0);

			// ModelData
			commandList->DrawInstanced(UINT(modelData.vertices.size()),1,0,0 );

			/*
			commandList->SetGraphicsRootDescriptorTable(2, textureSrvHandleGPU);
			//Spriteの描画。変更が必要なものだけ変更する
			commandList->IASetVertexBuffers(0, 1, &vertexBufferViewSprite);			// VBVを設定
			//TransformationMatrixCBufferの場所を設定
			commandList->SetGraphicsRootConstantBufferView(
				0,
				materialResourceSprite->GetGPUVirtualAddress()
			);

			commandList->SetGraphicsRootConstantBufferView(
				1,
				transformationMatrixResourceSprite->GetGPUVirtualAddress()
			);
			//描画
			commandList->DrawIndexedInstanced(6, 1, 0, 0, 0);
			*/

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

	fenceValue++;

	hr = commandQueue->Signal(
		fence.Get(),
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

	Input::Finalize();
	CoUninitialize();

	return 0;
}