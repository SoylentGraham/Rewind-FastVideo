#pragma once


#include <windows.h>
#include <process.h>
#include <vector>
#include <math.h>
#include <stdio.h>
#include <assert.h>

#include <Shlwapi.h>
#pragma comment(lib,"Shlwapi.lib")


//	link to ffmpeg static lib (requires DLL)
#define ENABLE_DECODER
#if defined(ENABLE_DECODER)
extern "C"
{
#include <libavutil/opt.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/common.h>
#include <libavutil/imgutils.h>
#include <libavutil/mathematics.h>
#include <libavutil/samplefmt.h>
#include <libavutil/samplefmt.h>
#include <libswscale/swscale.h>

};
#pragma comment(lib,"avcodec.lib")
#pragma comment(lib,"avfilter.lib")
#pragma comment(lib,"avformat.lib")
#pragma comment(lib,"avutil.lib")
#pragma comment(lib,"swscale.lib")
#endif


// Which platform we are on?
#if _MSC_VER
#define UNITY_WIN 1
#else
#define UNITY_OSX 1
#endif


// Attribute to make function be exported from a plugin
#if UNITY_WIN
#define EXPORT_API __declspec(dllexport)
#else
#define EXPORT_API
#endif


// Which graphics device APIs we possibly support?
#if UNITY_WIN
#define SUPPORT_D3D11 1 // comment this out if you don't have D3D11 header/library files
#endif


// Graphics device identifiers in Unity
enum GfxDeviceRenderer
{
	kGfxRendererOpenGL = 0,          // OpenGL
	kGfxRendererD3D9,                // Direct3D 9
	kGfxRendererD3D11,               // Direct3D 11
	kGfxRendererGCM,                 // Sony PlayStation 3 GCM
	kGfxRendererNull,                // "null" device (used in batch mode)
	kGfxRendererHollywood,           // Nintendo Wii
	kGfxRendererXenon,               // Xbox 360
	kGfxRendererOpenGLES,            // OpenGL ES 1.1
	kGfxRendererOpenGLES20Mobile,    // OpenGL ES 2.0 mobile variant
	kGfxRendererMolehill,            // Flash 11 Stage3D
	kGfxRendererOpenGLES20Desktop,   // OpenGL ES 2.0 desktop variant (i.e. NaCl)
	kGfxRendererCount
};


// Event types for UnitySetGraphicsDevice
enum GfxDeviceEventType {
	kGfxDeviceEventInitialize = 0,
	kGfxDeviceEventShutdown,
	kGfxDeviceEventBeforeReset,
	kGfxDeviceEventAfterReset,
};


// If exported by a plugin, this function will be called when graphics device is created, destroyed,
// before it's being reset (i.e. resolution changed), after it's being reset, etc.
extern "C" void EXPORT_API UnitySetGraphicsDevice (void* device, int deviceType, int eventType);

// If exported by a plugin, this function will be called for GL.IssuePluginEvent script calls.
// The function will be called on a rendering thread; note that when multithreaded rendering is used,
// the rendering thread WILL BE DIFFERENT from the thread that all scripts & other game logic happens!
// You have to ensure any synchronization with other plugin script calls is properly done by you.
extern "C" void EXPORT_API UnityRenderEvent (int eventID);

extern "C" void EXPORT_API SetTimeFromUnity(float t);
extern "C" void EXPORT_API SetTextureFromUnity(void* texturePtr,const wchar_t* Filename,const int FilenameLength);


//	http://www.gamedev.net/page/resources/_/technical/game-programming/c-plugin-debug-log-with-unity-r3349
//	gr: call this in unity to tell us where to DebugLog() to
typedef void (*UnityDebugLogFunc)(const char*);
extern "C" void EXPORT_API SetDebugLogFunction(UnityDebugLogFunc pFunc);




class TColour
{
public:
	TColour() :
		mRed	( 255 ),
		mGreen	( 255 ),
		mBlue	( 255 ),
		mAlpha	( 255 )
	{
	}
	TColour(unsigned char r,unsigned char g,unsigned char b,unsigned char a) :
		mRed	( r ),
		mGreen	( g ),
		mBlue	( b ),
		mAlpha	( a )
	{
	}

	inline bool		operator!=(const TColour& that) const	{	return (this->mRed!=that.mRed) || (this->mGreen!=that.mGreen) || (this->mBlue!=that.mBlue) || (this->mAlpha!=that.mAlpha);	}

public:
	unsigned char	mRed;
	unsigned char	mGreen;
	unsigned char	mBlue;
	unsigned char	mAlpha;
};

#define PIXEL_BLOCK_SIZE	256

class TFramePixels
{
public:
	TFramePixels(int Width,int Height);
	virtual ~TFramePixels();

	void			SetColour(TColour Colour);
	unsigned char*	GetData() const		{	return reinterpret_cast<unsigned char*>( mPixels );	}
	int				GetDataSize() const	{	return sizeof(TColour) * mWidth * mHeight;	}
	int				GetPitch() const	{	return sizeof(TColour) * mWidth;	}
	
public:
	int			mHeight;
	int			mWidth;
	TColour*	mPixels;
	float		mTimestamp;	//	in secs from 0
};


class TMutex
{
public:
	TMutex()		{	InitializeCriticalSection( &mMutex );	}
	~TMutex()		{	DeleteCriticalSection( &mMutex );	}

	void Lock()		{	EnterCriticalSection( &mMutex ); }
	void Unlock()	{	LeaveCriticalSection( &mMutex );	}

private:
	CRITICAL_SECTION	mMutex;
};

class TThread
{
public:
	TThread();
	virtual ~TThread();

	bool create(unsigned int stackSize = 0);
	unsigned int threadId() const;
	void start();
	void join();

	void resume();
	void suspend();
	void shutdown();
	
protected:
	bool canRun();
	virtual void run() = 0;

private:
	static unsigned int __stdcall threadFunc(void *args);
	
	HANDLE m_hThread;
	unsigned int m_threadId;
	volatile bool m_canRun;
	volatile bool m_suspended;
	TMutex			m_mutex;
};


class TDecodeParams
{
public:
	TDecodeParams() :
		mWidth			( 1 ),
		mHeight			( 1 ),
		mMaxFrameBuffer	( 10 )
	{
	}

public:
	int			mWidth;
	int			mHeight;
	int			mMaxFrameBuffer;
	std::wstring	mFilename;
};


#if defined(ENABLE_DECODER)

class TPacket 
{
public:
	explicit TPacket(AVFormatContext* ctxt = nullptr) 
	{
		memset( &packet, 0x00, sizeof(packet) );
		av_init_packet(&packet);
		packet.data = nullptr;
		if (ctxt) 
			reset(ctxt);
	}

	TPacket(TPacket&& other) : 
		packet(std::move(other.packet))
	{
		other.packet.data = nullptr;
	}

	~TPacket() {
		if (packet.data)
			av_free_packet(&packet);
	}

	void reset(AVFormatContext* ctxt)
	{
		if (packet.data)
			av_free_packet(&packet);
		if (av_read_frame(ctxt, &packet) < 0)
			packet.data = nullptr;
	}

	AVPacket	packet;
};
#endif

class TVideoParams
{
public:
	int		mWidth;
	int		mHeight;
	float	mFramesPerSecond;
};

class TDecoder
{
public:
	TDecoder();
	virtual ~TDecoder();

	bool			Init(const std::wstring& Filename);
	TVideoParams	GetVideoParams();
	bool			DecodeNextFrame(TFramePixels* Frame);

public:
	TVideoParams						mVideoParams;
	bool								mFirstDecode;

	std::shared_ptr<AVFormatContext>	mContext;
	std::shared_ptr<AVCodecContext>		mCodec;
	std::vector<uint8_t>				mCodecContextExtraData;
	TPacket								mCurrentPacket;
	std::shared_ptr<AVFrame>			mFrame;			//	currently decoding to this frame
	AVStream*							mVideoStream;	//	gr: change to index!
	int									mDataOffset;
};


class TDecodeThread : public TThread
{
public:
	static const int		INVALID_FRAME = -1;

public:
	TDecodeThread(const TDecodeParams& Params);
	virtual ~TDecodeThread();

	TFramePixels*				PopFrame();

protected:
	virtual void				run();
	bool						DecodeColourFrame();
	bool						DecodeNextFrame();
	void						PushFrame(TFramePixels* pFrame);

protected:
	TDecoder					mDecoder;
	TDecodeParams				mParams;
	TMutex						mFrameMutex;
	int							mFrameCount;
	std::vector<TFramePixels*>	mFrameBuffers;	//	frame buffer
};


class TFramePool
{
public:
	TFramePool(int MaxPoolSize=30);

	TFramePixels*	Alloc();		//	increase pool size
	bool			Free(TFramePixels* pFrame);

public:
	TDecodeParams				mParams;

protected:
	int							mPoolMaxSize;
	TMutex						mFrameMutex;
	std::vector<TFramePixels*>	mPool;		//	
	std::vector<bool>			mPoolUsed;	//	per-frame buffer to say if entry is in use
};



void DebugLog(const char* str);
void DebugLog(const std::string& string);

std::string ofToString(int Integer);