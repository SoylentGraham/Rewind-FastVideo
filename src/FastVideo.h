#pragma once

#include <ofxSoylent.h>
#include <SoyThread.h>
#include "UnityDevice.h"
#include "TFrame.h"

#define USE_REAL_TIMESTAMP				false

#define DEFAULT_MAX_POOL_SIZE		30
#define DEFAULT_MAX_FRAME_BUFFERS	(DEFAULT_MAX_POOL_SIZE-1)

#if (USE_REAL_TIMESTAMP==true)
	#define FORCE_BUFFER_FRAME_COUNT	0	//	hold X frames before popping (must be less than DEFAULT_MAX_FRAME_BUFFERS)
#else
	#define FORCE_BUFFER_FRAME_COUNT	20	//	hold X frames before popping (must be less than DEFAULT_MAX_FRAME_BUFFERS)
#endif

//#define ENABLE_DECODER_INIT_SIZE_FRAME		TColour(0,255,0,255)
#define ENABLE_FAILED_DECODER_INIT_FRAME	TColour(255,0,0,255)

static bool SKIP_PAST_FRAMES	= false;
static bool STORE_PAST_FRAMES	= true;
static bool SHOW_POOL_FULL_MESSAGE	=	true;

#define ALWAYS_COPY_DYNAMIC_TO_TARGET	false	//	gr: I think not changing the target causes some double buffer mess
#define DYNAMIC_SKIP_OO_FRAMES			true
#define DECODER_SKIP_OO_FRAMES			false


class TFastTexture;


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
	TUnityDevice_DX11&	GetDevice()				{	return *mDevice;	}

private:
	int					FindInstanceIndex(SoyRef InstanceRef);

private:
	ofMutex						mInstancesLock;
	SoyRef						mNextInstanceRef;
	Array<TFastTexture*>		mInstances;
	ofPtr<TUnityDevice_DX11>	mDevice;
	TFramePool					mFramePool;
};












namespace Unity
{
	typedef unsigned long long	ulong;
	typedef void (*TDebugLogFunc)(const char*);

	void	DebugLog(const char* str);
	void	DebugLog(const std::string& string);
};

extern "C" EXPORT_API Unity::ulong	AllocInstance();
extern "C" EXPORT_API bool			FreeInstance(Unity::ulong Instance);
extern "C" EXPORT_API bool			SetTexture(Unity::ulong Instance,void* Texture);
extern "C" EXPORT_API bool			SetVideo(Unity::ulong Instance,const wchar_t* Filename,int Length);
extern "C" EXPORT_API bool			Pause(Unity::ulong Instance);
extern "C" EXPORT_API bool			Resume(Unity::ulong Instance);

//	http://www.gamedev.net/page/resources/_/technical/game-programming/c-plugin-debug-log-with-unity-r3349
//	gr: call this in unity to tell us where to DebugLog() to
extern "C" EXPORT_API void			SetDebugLogFunction(Unity::TDebugLogFunc FunctionPtr);

// If exported by a plugin, this function will be called for GL.IssuePluginEvent script calls.
// The function will be called on a rendering thread; note that when multithreaded rendering is used,
// the rendering thread WILL BE DIFFERENT from the thread that all scripts & other game logic happens!
// You have to ensure any synchronization with other plugin script calls is properly done by you.
extern "C" void EXPORT_API UnityRenderEvent(int eventID);

// If exported by a plugin, this function will be called when graphics device is created, destroyed,
// before it's being reset (i.e. resolution changed), after it's being reset, etc.
extern "C" void EXPORT_API UnitySetGraphicsDevice(void* device, int deviceType, int eventType);













