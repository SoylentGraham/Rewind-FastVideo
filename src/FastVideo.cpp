// Example low level rendering Unity plugin
#include "FastVideo.h"
#include <sstream>


//	globals in a namespace please
namespace TVideoStreamer
{
	UnityDebugLogFunc UnityDebugFunc = NULL;
};


extern "C" void EXPORT_API SetDebugLogFunction(UnityDebugLogFunc pFunc)
{
	TVideoStreamer::UnityDebugFunc = pFunc;
}

// Prints a string
void DebugLog (const char* str)
{
	//	print out to visual studio debugger
	OutputDebugStringA(str);
	OutputDebugStringA("\n");

	//	print to stdout
	//printf ("%s\n", str);

	//	print to unity if we have a function set
	if ( TVideoStreamer::UnityDebugFunc )
	{
		(*TVideoStreamer::UnityDebugFunc)( str );
	}
}

void DebugLog (const std::string& String)
{
	DebugLog( String.c_str() );
}


std::string ofToString(int Integer)
{
	std::ostringstream s;
	s << Integer;
	//return std::string( s.str() );
	return s.str();
}


TFramePool::TFramePool(int MaxPoolSize) :
	mPoolMaxSize	( min(1,MaxPoolSize) )
{
}

TFramePixels* TFramePool::Alloc()
{
	TFramePixels* FreeBlock = NULL;

	//	any free?
	mFrameMutex.Lock();
	for ( int p=0;	p<(int)mPool.size();	p++ )
	{
		if ( mPoolUsed[p] )
			continue;

		FreeBlock = mPool[p];
		mPoolUsed[p] = true;
	}

	//	alloc a new one if we're not at our limit
	if ( !FreeBlock )
	{
		if ( (int)mPool.size() < mPoolMaxSize )
		{
			FreeBlock = new TFramePixels( mParams.mWidth, mParams.mHeight );
			std::string Debug = std::string() + "Allocating new block; " + ofToString( PtrToInt(FreeBlock) ) + " pool size; " + ofToString(mPool.size());
			DebugLog( Debug.c_str() );

			if ( FreeBlock )
			{
				mPool.push_back( FreeBlock );
				mPoolUsed.push_back( true );
			}
		}
	}
	
	mFrameMutex.Unlock();

	return FreeBlock;
}

bool TFramePool::Free(TFramePixels* pFrame)
{
	bool Removed = false;

	//	find index and mark as no-longer in use
	mFrameMutex.Lock();
	for ( int p=0;	p<(int)mPool.size();	p++ )
	{
		auto* Entry = mPool[p];
		if ( Entry != pFrame )
			continue;
		assert( mPoolUsed[p] );
		mPoolUsed[p] = false;
		Removed = true;
	}
	mFrameMutex.Unlock();

	assert( Removed );
	return Removed;
}





TThread::TThread()
{
	m_hThread = 0;
	m_threadId = 0;
	m_canRun = true;
	m_suspended = true;
}

TThread::~TThread()
{
	if (m_hThread)
		CloseHandle(m_hThread);
}

bool TThread::canRun()
{
	m_mutex.Lock();
	bool CanRun = m_canRun;
	m_mutex.Unlock();
	return CanRun;
}

bool TThread::create(unsigned int stackSize)
{
	m_hThread = reinterpret_cast<HANDLE>(_beginthreadex(0, stackSize, 
		threadFunc, this, CREATE_SUSPENDED, &m_threadId));
	
	if (m_hThread)
		return true;
	
	return false;
}

void TThread::join()
{
	WaitForSingleObject(m_hThread, INFINITE);
}

void TThread::resume()
{
	if (m_suspended)
	{
		m_mutex.Lock();
		
		if (m_suspended)
		{
			ResumeThread(m_hThread);
			m_suspended = false;
		}

		m_mutex.Unlock();
	}
}

void TThread::shutdown()
{
	if (m_canRun)
	{
		m_mutex.Lock();

		if (m_canRun)
			m_canRun = false;

		resume();
		m_mutex.Unlock();
	}
}

void TThread::start()
{
	if ( !m_hThread )
		create();
	resume();
}

void TThread::suspend()
{
	if (!m_suspended)
	{
		m_mutex.Lock();

		if (!m_suspended)
		{
			SuspendThread(m_hThread);
			m_suspended = true;
		}
		m_mutex.Unlock();
	}
}

unsigned int TThread::threadId() const
{
	return m_threadId;
}

unsigned int __stdcall TThread::threadFunc(void *args)
{
	TThread *pThread = reinterpret_cast<TThread*>(args);
	
	if (pThread)
		pThread->run();

	_endthreadex(0);
	return 0;
} 



// --------------------------------------------------------------------------
// Include headers for the graphics APIs we support

#if SUPPORT_D3D11
	#include <d3d11.h>
#endif

static ID3D11Device* g_D3D11Device;
static ID3D11Buffer* g_D3D11VB; // vertex buffer
static ID3D11Buffer* g_D3D11CB; // constant buffer
static ID3D11VertexShader* g_D3D11VertexShader;
static ID3D11PixelShader* g_D3D11PixelShader;
static ID3D11InputLayout* g_D3D11InputLayout;
static ID3D11RasterizerState* g_D3D11RasterState;
static ID3D11BlendState* g_D3D11BlendState;
static ID3D11DepthStencilState* g_D3D11DepthState;
static float g_Time;
static ID3D11Texture2D* g_TexturePointer;
ID3D11Texture2D* g_DynamicTexture = NULL;
TDecodeThread* gDecodeThread = NULL;
TFramePool gFramePool;

typedef HRESULT (WINAPI *D3DCompileFunc)(
	const void* pSrcData,
	unsigned long SrcDataSize,
	const char* pFileName,
	const D3D10_SHADER_MACRO* pDefines,
	ID3D10Include* pInclude,
	const char* pEntrypoint,
	const char* pTarget,
	unsigned int Flags1,
	unsigned int Flags2,
	ID3D10Blob** ppCode,
	ID3D10Blob** ppErrorMsgs);

static const char* kD3D11ShaderText =
"cbuffer MyCB : register(b0) {\n"
"	float4x4 worldMatrix;\n"
"}\n"
"void VS (float3 pos : POSITION, float4 color : COLOR, out float4 ocolor : COLOR, out float4 opos : SV_Position) {\n"
"	opos = mul (worldMatrix, float4(pos,1));\n"
"	ocolor = color;\n"
"}\n"
"float4 PS (float4 color : COLOR) : SV_TARGET {\n"
"	return color;\n"
"}\n";



// --------------------------------------------------------------------------
// Helper utilities



// COM-like Release macro
#ifndef SAFE_RELEASE
#define SAFE_RELEASE(a) if (a) { a->Release(); a = NULL; }
#endif



// --------------------------------------------------------------------------
// SetTimeFromUnity, an example function we export which is called by one of the scripts.


extern "C" void EXPORT_API SetTimeFromUnity (float t) { g_Time = t; }



// --------------------------------------------------------------------------
// SetTextureFromUnity, an example function we export which is called by one of the scripts.


extern "C" void EXPORT_API SetTextureFromUnity(void* texturePtr,const wchar_t* Filename,const int FilenameLength)
{
	// A script calls this at initialization time; just remember the texture pointer here.
	// Will update texture pixels each frame from the plugin rendering event (texture update
	// needs to happen on the rendering thread).
	g_TexturePointer = (ID3D11Texture2D*)texturePtr;
	if ( !g_TexturePointer )
		return;

	D3D11_TEXTURE2D_DESC SrcDesc;
	g_TexturePointer->GetDesc (&SrcDesc);

	TDecodeParams Params;
	Params.mWidth = SrcDesc.Width;
	Params.mHeight = SrcDesc.Height;

	for ( int i=0;	i<FilenameLength;	i++ )
		Params.mFilename += Filename[i];

	if ( !g_DynamicTexture )
	{
		//	create dynamic texture
		D3D11_TEXTURE2D_DESC Desc;
		memset (&Desc, 0, sizeof(Desc));


		Desc.Width = SrcDesc.Width;
		Desc.Height = SrcDesc.Height;
		Desc.MipLevels = 1;
		Desc.ArraySize = 1;

		Desc.SampleDesc.Count = 1;
		Desc.SampleDesc.Quality = 0;
		Desc.Usage = D3D11_USAGE_DYNAMIC;
		Desc.Format = SrcDesc.Format;
		Desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		Desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		//Desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		
		auto Result = g_D3D11Device->CreateTexture2D( &Desc, NULL, &g_DynamicTexture );
		if ( Result != S_OK )
		{
			DebugLog("Failed to create dynamic texture");
		}
	}

	//	create decoder thread
	if ( !gDecodeThread )
	{
		gDecodeThread = new TDecodeThread( Params );
		gDecodeThread->start();

		gFramePool.mParams = Params;
	}
}



// --------------------------------------------------------------------------
// UnitySetGraphicsDevice

static int g_DeviceType = -1;


// Actual setup/teardown functions defined below
#if SUPPORT_D3D11
static void SetGraphicsDeviceD3D11 (ID3D11Device* device, GfxDeviceEventType eventType);
#endif


extern "C" void EXPORT_API UnitySetGraphicsDevice (void* device, int deviceType, int eventType)
{
	// Set device type to -1, i.e. "not recognized by our plugin"
	g_DeviceType = -1;
	
	#if SUPPORT_D3D11
	// D3D11 device, remember device pointer and device type.
	// The pointer we get is ID3D11Device.
	if (deviceType == kGfxRendererD3D11)
	{
		DebugLog ("Set D3D11 graphics device\n");
		g_DeviceType = deviceType;
		SetGraphicsDeviceD3D11 ((ID3D11Device*)device, (GfxDeviceEventType)eventType);
	}
	#endif

}


// -------------------------------------------------------------------
//  Direct3D 11 setup/teardown code


#if SUPPORT_D3D11


static void CreateD3D11Resources()
{

	D3D11_BUFFER_DESC desc;
	memset (&desc, 0, sizeof(desc));

	// vertex buffer
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.ByteWidth = 1024;
	desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	g_D3D11Device->CreateBuffer (&desc, NULL, &g_D3D11VB);

	// constant buffer
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.ByteWidth = 64; // hold 1 matrix
	desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	desc.CPUAccessFlags = 0;
	g_D3D11Device->CreateBuffer (&desc, NULL, &g_D3D11CB);



	// shaders
	HMODULE compiler = LoadLibraryA("D3DCompiler_43.dll");

	if (compiler == NULL)
	{
		// Try compiler from Windows 8 SDK
		compiler = LoadLibraryA("D3DCompiler_46.dll");
	}
	if (compiler)
	{
		ID3D10Blob* vsBlob = NULL;
		ID3D10Blob* psBlob = NULL;

		D3DCompileFunc compileFunc = (D3DCompileFunc)GetProcAddress (compiler, "D3DCompile");
		if (compileFunc)
		{
			HRESULT hr;
			hr = compileFunc(kD3D11ShaderText, strlen(kD3D11ShaderText), NULL, NULL, NULL, "VS", "vs_4_0", 0, 0, &vsBlob, NULL);
			if (SUCCEEDED(hr))
			{
				g_D3D11Device->CreateVertexShader (vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), NULL, &g_D3D11VertexShader);
			}

			hr = compileFunc(kD3D11ShaderText, strlen(kD3D11ShaderText), NULL, NULL, NULL, "PS", "ps_4_0", 0, 0, &psBlob, NULL);
			if (SUCCEEDED(hr))
			{
				g_D3D11Device->CreatePixelShader (psBlob->GetBufferPointer(), psBlob->GetBufferSize(), NULL, &g_D3D11PixelShader);
			}
		}

		// input layout
		if (g_D3D11VertexShader && vsBlob)
		{
			D3D11_INPUT_ELEMENT_DESC layout[] = {
				{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
				{ "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			};

			g_D3D11Device->CreateInputLayout (layout, 2, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &g_D3D11InputLayout);
		}

		SAFE_RELEASE(vsBlob);
		SAFE_RELEASE(psBlob);

		FreeLibrary (compiler);
	}
	else
	{
		DebugLog ("D3D11: HLSL shader compiler not found, will not render anything\n");
	}

	// render states
	D3D11_RASTERIZER_DESC rsdesc;
	memset (&rsdesc, 0, sizeof(rsdesc));
	rsdesc.FillMode = D3D11_FILL_SOLID;
	rsdesc.CullMode = D3D11_CULL_NONE;
	rsdesc.DepthClipEnable = TRUE;
	g_D3D11Device->CreateRasterizerState (&rsdesc, &g_D3D11RasterState);

	D3D11_DEPTH_STENCIL_DESC dsdesc;
	memset (&dsdesc, 0, sizeof(dsdesc));
	dsdesc.DepthEnable = TRUE;
	dsdesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
	dsdesc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
	g_D3D11Device->CreateDepthStencilState (&dsdesc, &g_D3D11DepthState);

	D3D11_BLEND_DESC bdesc;
	memset (&bdesc, 0, sizeof(bdesc));
	bdesc.RenderTarget[0].BlendEnable = FALSE;
	bdesc.RenderTarget[0].RenderTargetWriteMask = 0xF;
	g_D3D11Device->CreateBlendState (&bdesc, &g_D3D11BlendState);
}

static void ReleaseD3D11Resources()
{
	SAFE_RELEASE(g_D3D11VB);
	SAFE_RELEASE(g_D3D11CB);
	SAFE_RELEASE(g_D3D11VertexShader);
	SAFE_RELEASE(g_D3D11PixelShader);
	SAFE_RELEASE(g_D3D11InputLayout);
	SAFE_RELEASE(g_D3D11RasterState);
	SAFE_RELEASE(g_D3D11BlendState);
	SAFE_RELEASE(g_D3D11DepthState);
}

static void SetGraphicsDeviceD3D11 (ID3D11Device* device, GfxDeviceEventType eventType)
{
	g_D3D11Device = device;

	if (eventType == kGfxDeviceEventInitialize)
		CreateD3D11Resources();
	if (eventType == kGfxDeviceEventShutdown)
		ReleaseD3D11Resources();
}

#endif // #if SUPPORT_D3D11



// --------------------------------------------------------------------------
// SetDefaultGraphicsState
//
// Helper function to setup some "sane" graphics state. Rendering state
// upon call into our plugin can be almost completely arbitrary depending
// on what was rendered in Unity before.
// Before calling into the plugin, Unity will set shaders to null,
// and will unbind most of "current" objects (e.g. VBOs in OpenGL case).
//
// Here, we set culling off, lighting off, alpha blend & test off, Z
// comparison to less equal, and Z writes off.

static void SetDefaultGraphicsState ()
{

	#if SUPPORT_D3D11
	// D3D11 case
	if (g_DeviceType == kGfxRendererD3D11)
	{
		ID3D11DeviceContext* ctx = NULL;
		g_D3D11Device->GetImmediateContext (&ctx);
		ctx->OMSetDepthStencilState (g_D3D11DepthState, 0);
		ctx->RSSetState (g_D3D11RasterState);
		ctx->OMSetBlendState (g_D3D11BlendState, NULL, 0xFFFFFFFF);
		ctx->Release();
	}
	#endif


}


TFramePixels::TFramePixels(int Width,int Height) :
	mHeight	( Height ),
	mWidth	( Width ),
	mPixels	( NULL )
{
	mPixels = new TColour[mHeight * mWidth];
}

TFramePixels::~TFramePixels()
{
	delete[] mPixels;
}

void TFramePixels::SetColour(TColour Colour)
{
	for ( int i=0;	i<mHeight * mWidth;	i++ )
	{
		mPixels[i] = Colour;
	}
}


std::vector<TFramePixels*> gBlocks;

const TFramePixels* GeTFramePixels(const D3D11_TEXTURE2D_DESC& Desc)
{
	if ( Desc.Format != DXGI_FORMAT_R8G8B8A8_UNORM )
		return NULL;

	//	
	int Width = Desc.Width;
	int Height = Desc.Height;
	/*
	std::string Debug;
	Debug += "Texture format: ";
	Debug << static_cast<int>(Desc.Format);
	DebugLog( Debug.c_str() );
	*/
	
	//	pick colour for the frame
	std::vector<TColour> Colours;
	Colours.push_back( TColour(255,0,0,255) );
	Colours.push_back( TColour(0,255,0,255) );
	Colours.push_back( TColour(0,0,255,255) );
	//int Index = static_cast<int>(g_Time * 1.f) % Colours.size();
	static int Index = 0;
	Index ++;
	auto& Colour = Colours[Index% Colours.size()];

	//	find matching block
	const TFramePixels* pBlock = NULL;
	for ( int i=0;	i<(int)gBlocks.size();	i++ )
	{
		auto& Block = *gBlocks[i];
		if ( Block.mPixels[0] != Colour )
			continue;
		if ( Block.mHeight != Desc.Height || Block.mWidth != Desc.Width )
			continue;
		pBlock = &Block;
	}

	//	no match, create one
	if ( !pBlock )
	{
		auto* NewBlock = gFramePool.Alloc();
		NewBlock->SetColour( Colour );
		gBlocks.push_back( NewBlock );
		pBlock = NewBlock;
	}

	return pBlock;
	
}



// --------------------------------------------------------------------------
// UnityRenderEvent
// This will be called for GL.IssuePluginEvent script calls; eventID will
// be the integer passed to IssuePluginEvent. In this example, we just ignore
// that value.

static void SetDefaultGraphicsState ();


extern "C" void EXPORT_API UnityRenderEvent (int eventID)
{
	// Unknown graphics device type? Do nothing.
	if (g_DeviceType == -1)
		return;
	
	if (g_DeviceType == kGfxRendererD3D11 )
	{
		ID3D11DeviceContext* ctx = NULL;
		g_D3D11Device->GetImmediateContext (&ctx);

		// update native texture from code
		if (g_TexturePointer)
		{
			ID3D11Texture2D* d3dtex = g_TexturePointer;
			D3D11_TEXTURE2D_DESC desc;
			d3dtex->GetDesc (&desc);
			TFramePixels* pFrame = gDecodeThread ? gDecodeThread->PopFrame() : NULL;
			if ( pFrame )
			{
				//	update texture
				D3D11_MAPPED_SUBRESOURCE resource;
				int SubResource = 0;
				int flags = 0;
				HRESULT hr = ctx->Map( g_DynamicTexture, 0, D3D11_MAP_WRITE_DISCARD, flags, &resource);
				if ( hr == S_OK )
				{
					auto* TextureBytes = (unsigned char*)resource.pData;
					memcpy( TextureBytes, pFrame->GetData(), pFrame->GetDataSize() );
					ctx->Unmap( g_DynamicTexture, SubResource);

					//	copy to real texture
					ctx->CopyResource( d3dtex, g_DynamicTexture );
				}
				
				gFramePool.Free( pFrame );
			}
		}

		ctx->Release();
	}

}




TDecodeThread::TDecodeThread(const TDecodeParams& Params) :
	mFrameCount	( 0 ),
	mParams		( Params )
{
	mParams.mMaxFrameBuffer = max( 1, mParams.mMaxFrameBuffer );
	mDecoder.Init( Params.mFilename.c_str() );
}

TDecodeThread::~TDecodeThread()
{

}

TFramePixels* TDecodeThread::PopFrame()
{
	TFramePixels* PoppedFrame = NULL;

	mFrameMutex.Lock();
	if ( mFrameBuffers.size() )
	{
		PoppedFrame = mFrameBuffers[0];
		mFrameBuffers.erase( mFrameBuffers.begin() );
	}
	mFrameMutex.Unlock();

	return PoppedFrame;
}


void TDecodeThread::run()
{
	while ( canRun() )
	{
		Sleep(1);

		//	if buffer is filled, stop (don't buffer too many frames)
		bool DoDecode = false;
		mFrameMutex.Lock();
		DoDecode = ((int)mFrameBuffers.size() < mParams.mMaxFrameBuffer);
		mFrameMutex.Unlock();

		if ( !DoDecode )
			continue;
		
		#if defined(ENABLE_DECODER)
			DecodeNextFrame();
		#else
			DecodeColourFrame();
		#endif
	}
}

void TDecodeThread::PushFrame(TFramePixels* pFrame)
{
	mFrameMutex.Lock();
	mFrameBuffers.push_back( pFrame );
	mFrameCount++;
	mFrameMutex.Unlock();
}


bool TDecodeThread::DecodeColourFrame()
{
	std::vector<TColour> Colours;
	Colours.push_back( TColour(255,0,0,255) );
	Colours.push_back( TColour(0,255,0,255) );
	Colours.push_back( TColour(0,0,255,255) );
	Colours.push_back( TColour(255,0,255,255) );
	Colours.push_back( TColour(0,255,255,255) );
	Colours.push_back( TColour(255,255,0,255) );
	TColour Colour( Colours[ mFrameCount % Colours.size() ] );

	//	make next frame
	TFramePixels* pFrame = gFramePool.Alloc();
	if ( !pFrame )
		return false;
	pFrame->SetColour( Colour );
	
	PushFrame( pFrame );

	return true;
}

bool TDecodeThread::DecodeNextFrame()
{
	//	make next frame
	TFramePixels* Frame = gFramePool.Alloc();
	if ( !Frame )
		return false;
	
	if ( !mDecoder.DecodeNextFrame( Frame ) )
	{
		gFramePool.Free( Frame );
		return false;
	}

	PushFrame( Frame );

	return true;
}


TDecoder::TDecoder() :
	mVideoStream	( NULL ),
	mDataOffset		( 0 ),
	mFirstDecode	( true )
{
}


TDecoder::~TDecoder()
{
}

bool TDecoder::DecodeNextFrame(TFramePixels* OutputFrame)
{
#if defined(ENABLE_DECODER)
	//	not setup right??
	if ( !mVideoStream )
		return false;

	//	keep processing until a frame is loaded
	int isFrameAvailable = false;
	while ( !isFrameAvailable )
	{
		// reading a packet using libavformat
		if (mDataOffset >= mCurrentPacket.packet.size) 
		{
			do 
			{
				mCurrentPacket.reset( mContext.get());
				if (mCurrentPacket.packet.stream_index != mVideoStream->index)
					continue;
			}
			while(0);
		}

		// preparing the packet that we will send to libavcodec
		/*
		AVPacket packetToSend;
		av_init_packet( &packetToSend );
		packetToSend.data = mCurrentPacket.packet.data + mDataOffset;
		packetToSend.size = mCurrentPacket.packet.size - mDataOffset;
		*/
		auto& packetToSend = mCurrentPacket.packet;
	
		// sending data to libavcodec
		const auto processedLength = avcodec_decode_video2(mCodec.get(), mFrame.get(), &isFrameAvailable, &packetToSend);
		if (processedLength < 0) 
		{
			//av_free_packet(&packet);
			DebugLog("Error while processing the data");
			return false;
		}
		mDataOffset += processedLength;

		// processing the image if available
		if (isFrameAvailable) 
		{
			//	debug first time
			if ( mFirstDecode )
			{
				std::string Debug = std::string() + "Decoded frame " + ofToString(mFrame->width) + " x " + ofToString(mFrame->height) + " format: " + ofToString( mFrame->format );
				DebugLog( Debug );
				mFirstDecode = false;
			}

			int CopyWidth = min( mFrame->width, OutputFrame->mWidth );
			int CopyHeight = min( mFrame->height, OutputFrame->mHeight );
			AVPicture pict;
			
			//avpicture_alloc( &pict, PIX_FMT_RGBA, OutputFrame->mWidth, OutputFrame->mHeight );
			memset(&pict, 0, sizeof(pict));
			avpicture_fill(&pict, reinterpret_cast<unsigned char*>(OutputFrame->mPixels), PIX_FMT_RGBA, OutputFrame->mWidth, OutputFrame->mHeight );

			auto ctxt = sws_getContext( mFrame->width, mFrame->height, static_cast<PixelFormat>(mFrame->format), OutputFrame->mWidth, OutputFrame->mHeight, PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);
			if ( !ctxt )
			{
				DebugLog("Failed to get converter");
				return false;
			}
			sws_scale(ctxt, mFrame->data, mFrame->linesize, 0, mFrame->height, pict.data, pict.linesize);

			// pic.data[0] now contains the image data in RGB format (3 bytes)
			// and pic.linesize[0] is the pitch of the data (ie. size of a row in memory, which can be larger than width*sizeof(pixel))

			// we can for example upload it to an OpenGL texture (note: untested code)
			// glBindTexture(GL_TEXTURE_2D, myTex);
			// for (int i = 0; i < avFrame->height; ++i) {
			// 	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, i, avFrame->width, 1, GL_RGB, GL_UNSIGNED_BYTE, avFrame->data[0] + (i * pic.linesize[0]));
			// }

			//avpicture_free(&pict);
			
		}
	}

	return true;
#endif
	return false;
}


std::string GetAVError(int Error)
{
	char Buffer[1000];

	if ( av_strerror( Error, Buffer, sizeof(Buffer)/sizeof(Buffer[0]) ) != 0 )
	{
		std::string out = std::string() + "Unknown error: " + ofToString( Error );
		return out;
	}

	return Buffer;
}

bool TDecoder::Init(const std::wstring& Filename)
{
#if defined(ENABLE_DECODER)
	std::string Filenamea( Filename.begin(), Filename.end() );

	//	check file exists
	if ( !PathFileExists(Filename.c_str()) )
	{
		std::string Debug = std::string() + Filenamea + " doesn't exist";
		DebugLog( Debug );
		return false;
	}

	//	init lib av
	av_register_all();

	mContext = std::shared_ptr<AVFormatContext>(avformat_alloc_context(), &avformat_free_context);
	auto avFormatPtr = mContext.get();
	int err = avformat_open_input( &avFormatPtr, Filenamea.c_str(), nullptr, nullptr );
	if ( err != 0 )
	{
		DebugLog( GetAVError(err) );
		return false;
	}

	//	get streams 
	err = avformat_find_stream_info( mContext.get(), nullptr );
	if ( err < 0)
	{
		DebugLog( GetAVError(err) );
		return false;
	}

	assert( !mVideoStream );
	for ( int i = 0; i <(int)mContext->nb_streams; ++i) 
	{
		if (mContext->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) 
		{
			// we've found a video stream!
			mVideoStream = mContext->streams[i];
			break;
		}
	}
	if ( !mVideoStream )
	{
		DebugLog("failed to find a video stream");
		return false;
	}
    
	// getting the required codec structure
	const auto codec = avcodec_find_decoder( mVideoStream->codec->codec_id );
	if ( codec == nullptr )
	{
		DebugLog("Failed to find codec for video");
		return false;
	}

	// allocating a structure
	mCodec = std::shared_ptr<AVCodecContext>(avcodec_alloc_context3(codec), [](AVCodecContext* c) { avcodec_close(c); av_free(c); });

	// we need to make a copy of videoStream->codec->extradata and give it to the context
	// make sure that this vector exists as long as the avVideoCodec exists
	mCodecContextExtraData = std::vector<uint8_t>( mVideoStream->codec->extradata, mVideoStream->codec->extradata + mVideoStream->codec->extradata_size);
	mCodec->extradata = reinterpret_cast<uint8_t*>(mCodecContextExtraData.data());
	mCodec->extradata_size = mCodecContextExtraData.size();

	// initializing the structure by opening the codec
	err = avcodec_open2(mCodec.get(), codec, nullptr);
	if ( err < 0)
	{
		DebugLog( GetAVError(err) );
		return false;
	}

	//	alloc our buffer-frame
	mFrame = std::shared_ptr<AVFrame>(avcodec_alloc_frame(), &av_free);

	//	start streaming at the start
	mDataOffset = 0;
	
	return true;

#endif
	
	return false;
}