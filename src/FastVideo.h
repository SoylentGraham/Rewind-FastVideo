#pragma once

#include <ofxSoylent.h>
#include <SoyThread.h>
#include "UnityDevice.h"
#include "TFrame.h"

#define USE_REAL_TIMESTAMP				0

#define DEFAULT_MAX_POOL_SIZE		30
#define DEFAULT_MAX_FRAME_BUFFERS	(DEFAULT_MAX_POOL_SIZE-1)

#if USE_REAL_TIMESTAMP==1
	#define FORCE_BUFFER_FRAME_COUNT	20	//	hold X frames before popping (must be less than DEFAULT_MAX_FRAME_BUFFERS)
#else
	#define FORCE_BUFFER_FRAME_COUNT	0	//	hold X frames before popping (must be less than DEFAULT_MAX_FRAME_BUFFERS)
#endif

//#define ENABLE_DECODER_LIBAV_INIT_SIZE_FRAME		TColour(0,255,0,255)
#define ENABLE_DECODER_INIT_FRAME			TColour(255,0,255,255)
#define ENABLE_FAILED_DECODER_INIT_FRAME	TColour(255,0,0,255)
//#define ENABLE_DYNAMIC_INIT_TEXTURE_COLOUR	TColour(255,255,0,255)	//	gr: not done during render thread, causes big stall as GPU waits to be idle as dx::Map copy is blocking
#define HARDWARE_INIT_TEXTURE_COLOUR		TColour(0,255,255,255)

static bool SKIP_PAST_FRAMES	= true;
static bool STORE_PAST_FRAMES	= true;
static bool SHOW_POOL_FULL_MESSAGE	=	true;

#define ALWAYS_COPY_DYNAMIC_TO_TARGET	false	//	gr: I think not changing the target causes some double buffer mess
#define DYNAMIC_SKIP_OOO_FRAMES			true
#define DECODER_SKIP_OOO_FRAMES			false

#define REAL_TIME_MODIFIER				1.0f	//	speed up/slow down real life time

static bool FORCE_SINGLE_THREAD_UPLOAD	= true;
static bool	OPENGL_REREADY_MAP			=true;	//	after we copy the dynamic texture, immediately re-open the map
static bool	OPENGL_USE_STREAM_TEXTURE	=true;	//	GL_STREAM_DRAW else GL_DYNAMIC_DRAW

#if defined(TARGET_OSX)// && defined(ENABLE_OPENGL)
static bool USE_APPLE_CLIENT_STORAGE	= true;
#endif

#define ENABLE_DEBUG_LOG

//	buffer output log messages. OSX crashes randomly when we use the debug-console output on other threads (not sure if its just cos the c# funcs aren't thread safe?)
//	other platforms PROBABLY need it if OSX does.
#define BUFFER_DEBUG_LOG

//#define DEBUG_LOG_THREADSAFE
//#define SINGLETON_THREADSAFE
//#define DO_GL_FLUSH						glFlush

extern bool USE_TEST_DECODER;
extern bool ENABLE_TIMER_DEBUG_LOG;
extern bool ENABLE_ERROR_LOG;
extern bool ENABLE_FULL_DEBUG_LOG;
extern bool ENABLE_LAG_DEBUG_LOG;
extern bool ENABLE_DECODER_DEBUG_LOG;

class TFastTexture;





//	note: no namespaces to match c# code
enum UnityEvent
{
    OnPostRender = 0,
};

enum FastVideoError
{
	UknownError			= 0,
	DecoderError		= 1,
	CodecError			= 2,
	FileNotFound		= 3,
};


namespace Unity
{
	typedef unsigned long long	ulong;
	typedef void (*TDebugLogFunc)(const char*);
	typedef void (*TOnErrorFunc)(ulong,ulong);

	void		OnError(TFastTexture& Instance,FastVideoError Error);

	void		ConsoleLog(const char* str);
	inline void	ConsoleLog(const std::string& String)		{ ConsoleLog(String.c_str()); }


	inline void	DebugError(const std::string& String)		{ ENABLE_ERROR_LOG ? ConsoleLog(String) : ofLogNoticeWrapper(String); }
	inline void	DebugError(const char* String)				{ ENABLE_ERROR_LOG ? ConsoleLog(String) : ofLogNoticeWrapper(String); }
	inline void	DebugTimer(const std::string& String)		{ ENABLE_TIMER_DEBUG_LOG ? ConsoleLog(String) : ofLogNoticeWrapper(String); }
	inline void	DebugTimer(const char* String)				{ ENABLE_TIMER_DEBUG_LOG ? ConsoleLog(String) : ofLogNoticeWrapper(String); }
	inline void	DebugDecodeLag(const std::string& String)	{ ENABLE_LAG_DEBUG_LOG ? ConsoleLog(String) : ofLogNoticeWrapper(String); }
	inline void	DebugDecodeLag(const char* String)			{ ENABLE_LAG_DEBUG_LOG ? ConsoleLog(String) : ofLogNoticeWrapper(String); }
	inline void	DebugDecoder(const std::string& String)		{ ENABLE_DECODER_DEBUG_LOG ? ConsoleLog(String) : ofLogNoticeWrapper(String); }
	inline void	DebugDecoder(const char* String)			{ ENABLE_DECODER_DEBUG_LOG ? ConsoleLog(String) : ofLogNoticeWrapper(String); }
	inline void	Debug(const std::string& String)			{ ENABLE_FULL_DEBUG_LOG ? ConsoleLog(String) : ofLogNoticeWrapper(String); }
	inline void	Debug(const char* String)					{ ENABLE_FULL_DEBUG_LOG ? ConsoleLog(String) : ofLogNoticeWrapper(String); }


	class TScopeTimerWarning : public ofScopeTimerWarning
	{
	public:
		TScopeTimerWarning(const char* Name,uint64 WarningTimeMs,bool AutoStart=true) :
			ofScopeTimerWarning(Name, WarningTimeMs, AutoStart, DebugTimer )
		{
		};
	};
	
	
};

extern "C" EXPORT_API Unity::ulong	AllocInstance();
extern "C" EXPORT_API bool			FreeInstance(Unity::ulong Instance);
extern "C" EXPORT_API bool			SetTexture(Unity::ulong Instance,void* Texture);
extern "C" EXPORT_API bool			SetVideo(Unity::ulong Instance,const wchar_t* Filename,int Length);
extern "C" EXPORT_API bool			Pause(Unity::ulong Instance);
extern "C" EXPORT_API bool			Resume(Unity::ulong Instance);
extern "C" EXPORT_API bool			SetLooping(Unity::ulong Instance, bool EnableLooping);
extern "C" EXPORT_API void			EnableTestDecoder(bool Enable);
extern "C" EXPORT_API void			EnableDebugTimers(bool Enable);
extern "C" EXPORT_API void			EnableDebugLag(bool Enable);
extern "C" EXPORT_API void			EnableDebugError(bool Enable);
extern "C" EXPORT_API void			EnableDebugFull(bool Enable);

//	http://www.gamedev.net/page/resources/_/technical/game-programming/c-plugin-debug-log-with-unity-r3349
//	gr: call this in unity to tell us where to DebugLog() to
extern "C" EXPORT_API void			SetDebugLogFunction(Unity::TDebugLogFunc FunctionPtr);
extern "C" EXPORT_API void			SetOnErrorFunction(Unity::TOnErrorFunc FunctionPtr);

// If exported by a plugin, this function will be called for GL.IssuePluginEvent script calls.
// The function will be called on a rendering thread; note that when multithreaded rendering is used,
// the rendering thread WILL BE DIFFERENT from the thread that all scripts & other game logic happens!
// You have to ensure any synchronization with other plugin script calls is properly done by you.
extern "C" void EXPORT_API UnityRenderEvent(int eventID);

// If exported by a plugin, this function will be called when graphics device is created, destroyed,
// before it's being reset (i.e. resolution changed), after it's being reset, etc.
extern "C" void EXPORT_API UnitySetGraphicsDevice(void* device, int deviceType, int eventType);










class TFastVideo
{
public:
	TFastVideo();
	~TFastVideo();
	
	SoyRef				AllocInstance();
	bool				FreeInstance(SoyRef InstanceRef);
	TFastTexture*		FindInstance(SoyRef InstanceRef);
	
	void				OnPostRender();
	
	bool				AllocDevice(Unity::TGfxDevice::Type DeviceType,void* Device);
	bool				FreeDevice(Unity::TGfxDevice::Type DeviceType);
	bool				IsDeviceValid()			{	return mDevice!=nullptr;	}
	TUnityDevice&       GetDevice()				{	return *mDevice;	}
	
#if defined(BUFFER_DEBUG_LOG)
	void				FlushDebugLogBuffer();
	void				BufferDebugLog(const char* String,const char* Prefix=nullptr);
#endif
	void				DebugLog(const char* String);
	
private:
	int					FindInstanceIndex(SoyRef InstanceRef);
	
private:
	ofMutex						mInstancesLock;
	SoyRef						mNextInstanceRef;
	Array<TFastTexture*>		mInstances;
	ofPtr<TUnityDevice>         mDevice;
	TFramePool					mFramePool;
	
public:
	Unity::TOnErrorFunc			mOnErrorFunc;
#if defined(DEBUG_LOG_THREADSAFE)
	ofMutex						mDebugFuncLock;
#endif
	Unity::TDebugLogFunc		mDebugFunc;
#if defined(BUFFER_DEBUG_LOG)
	ofMutex						mDebugLogBufferLock;
	Array<BufferString<200>>	mDebugLogBuffer;
#endif
};





