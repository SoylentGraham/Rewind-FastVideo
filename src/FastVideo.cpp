// Example low level rendering Unity plugin
#include "FastVideo.h"
#include "SoyDecoder.h"
#include "SoyThread.h"
#include <sstream>
#include "TFastTexture.h"



bool USE_TEST_DECODER = false;
bool ENABLE_TIMER_DEBUG_LOG = false;		//	shows timer messages to unity
bool ENABLE_ERROR_LOG = true;			//	big-error debug
bool ENABLE_FULL_DEBUG_LOG = false;		//	coder-only debug
bool ENABLE_LAG_DEBUG_LOG = false;		//	decoder lag
bool ENABLE_DECODER_DEBUG_LOG = false;		//	libav output



namespace Unity
{
	namespace Private
	{
#if defined(SINGLETON_THREADSAFE)
		ofMutex			gFastVideoLock;
#endif
		TFastVideo*		gFastVideo = nullptr;
	};

	TFastVideo&			GetFastVideo();			//	allocate singleton if it hasn't been constructed. This avoids us competeting for crt construction order
};


TFastVideo& Unity::GetFastVideo()
{
#if defined(SINGLETON_THREADSAFE)
	ofMutex::ScopedLock lock( Unity::Private::gFastVideoLock );
#endif
	if ( !Private::gFastVideo )
	{
		Private::gFastVideo = prcore::Heap.Alloc<TFastVideo>();
	}
	
	return *Private::gFastVideo;
}




TFastVideo::TFastVideo() :
	mDebugFunc			( nullptr ),
	mOnErrorFunc		( nullptr ),
	mFramePool			( DEFAULT_MAX_POOL_SIZE ),
	mNextInstanceRef	( "FastTxture" )
{
}

TFastVideo::~TFastVideo()
{
}

SoyRef TFastVideo::AllocInstance()
{
	ofMutex::ScopedLock Lock( mInstancesLock );
	auto* pInstance = new TFastTexture( mNextInstanceRef, mFramePool );
	if ( !pInstance )
		return SoyRef();
	pInstance->SetDevice( mDevice );
	mNextInstanceRef++;
	
	mInstances.PushBack( pInstance );

	Unity::Debug( BufferString<100>() << "Allocated instance: " << pInstance->GetRef() );

	return pInstance->GetRef();
}

bool TFastVideo::FreeInstance(SoyRef InstanceRef)
{
	ofMutex::ScopedLock Lock( mInstancesLock );
	int Index = FindInstanceIndex( InstanceRef );
	if ( Index < 0 )
	{
		Unity::DebugError( BufferString<100>() << "Failed to find instance: " << InstanceRef );
		return false;
	}

	auto* pInstance = mInstances[Index];
	mInstances.RemoveBlock( Index, 1 );
	delete pInstance;
	Unity::Debug( BufferString<100>() << "Free'd instance: " << InstanceRef );

	//	if we have no more instances, the frame pool should be empty
	if ( mInstances.IsEmpty() )
	{
		mFramePool.DebugUsedFrames();
		assert( mFramePool.IsEmpty() );
	}

	return true;
}

TFastTexture* TFastVideo::FindInstance(SoyRef Ref)
{
	ofMutex::ScopedLock Lock( mInstancesLock );
	int Index = FindInstanceIndex( Ref );
	if ( Index < 0 )
		return NULL;
	return mInstances[Index];
}

int TFastVideo::FindInstanceIndex(SoyRef Ref)
{
	ofMutex::ScopedLock Lock( mInstancesLock );
	for ( int i=0;	i<mInstances.GetSize();	i++ )
	{
		auto& Instance = *mInstances[i];
		if ( Instance.GetRef() != Ref )
			continue;
		return i;
	}
	return -1;
}

void TFastVideo::OnPostRender()
{
	//	flush debug log, before & after work so we print even with no device
#if defined(BUFFER_DEBUG_LOG)
	FlushDebugLogBuffer();
#endif

	ofMutex::ScopedLock Lock( mInstancesLock );

	if ( !mDevice )
		return;

	//	device callback
	mDevice->SetRenderThread();
	mDevice->OnRenderThreadUpdate();
	
	for ( int i=0;	i<mInstances.GetSize();	i++ )
	{
		auto& Instance = *mInstances[i];
		Instance.OnPostRender();
	}

	mDevice->OnRenderThreadPostUpdate();

	//	flush debug log
#if defined(BUFFER_DEBUG_LOG)
	FlushDebugLogBuffer();
#endif
}

bool TFastVideo::AllocDevice(Unity::TGfxDevice::Type DeviceType,void* Device)
{
	//	free old device
	mDevice.reset();

	//	alloc new one
	mDevice = Unity::AllocDevice( DeviceType, Device );

	if ( !mDevice )
	{
		//	no warning if explicitly no device
		if ( DeviceType != Unity::TGfxDevice::Invalid )
			Unity::DebugError(BufferString<1000>() <<"Failed to allocated device " << DeviceType );
	}

	//	update device on all instances (remove, or add)
	for ( int i=0;	i<mInstances.GetSize();	i++ )
	{
		auto& Instance = *mInstances[i];
		Instance.SetDevice( mDevice );
	}

	return mDevice!=nullptr;
}

bool TFastVideo::FreeDevice(Unity::TGfxDevice::Type DeviceType)
{
	AllocDevice( Unity::TGfxDevice::Invalid, nullptr );
	return true;
}




extern "C" EXPORT_API Unity::ulong	AllocInstance()
{
	//	alloc a new instance
	SoyRef InstanceRef = Unity::GetFastVideo().AllocInstance();
	Unity::ulong RefInt = static_cast<Unity::ulong>( InstanceRef.GetInt64() );
	return RefInt;
}

extern "C" EXPORT_API bool FreeInstance(Unity::ulong Instance)
{
	return Unity::GetFastVideo().FreeInstance( SoyRef(Instance) );
}

extern "C" EXPORT_API bool SetTexture(Unity::ulong Instance,void* pTexture)
{
	auto* pInstance = Unity::GetFastVideo().FindInstance( SoyRef(Instance) );
	if ( !pInstance )
		return false;

    Unity::TTexture Texture( pTexture );
	return pInstance->SetTexture( Texture );
}

extern "C" EXPORT_API bool SetVideo(Unity::ulong Instance,const wchar_t* pFilename,int Length)
{
	auto* pInstance = Unity::GetFastVideo().FindInstance( SoyRef(Instance) );
	if ( !pInstance )
		return false;

	//	make string
	std::wstring Filename;
	for ( int i=0;	i<Length;	i++ )
		Filename += pFilename[i];

	return pInstance->SetVideo( Filename );
}


extern "C" void EXPORT_API SetDebugLogFunction(Unity::TDebugLogFunc pFunc)
{
	auto& FastVideo = Unity::GetFastVideo();
	
#if defined(DEBUG_LOG_THREADSAFE)
	FastVideo.mDebugFuncLock.lock();
	FastVideo.mDebugFunc = pFunc;
	FastVideo.mDebugFuncLock.unlock();
#else
	FastVideo.mDebugFunc = pFunc;
#endif
	
	BufferString<1000> Debug;
	Debug << "FastVideo debug-log initialised okay - ";
	Debug << "Built on " << __DATE__ << " " << __TIME__;
	Unity::ConsoleLog( Debug );
}

// Prints a string
void Unity::ConsoleLog(const char* str)
{
	//	print out to visual studio debugger
	ofLogNotice(str);
	
	//	print to unity if we have a function set
	auto& FastVideo = Unity::GetFastVideo();
	
#if defined(BUFFER_DEBUG_LOG)
	FastVideo.BufferDebugLog( str );
#else
	FastVideo.DebugLog( str );
#endif
}

#if defined(BUFFER_DEBUG_LOG)
void TFastVideo::BufferDebugLog(const char* String,const char* Prefix)
{
	ofMutex::ScopedLock lock( mDebugLogBufferLock );
	
	auto& str = mDebugLogBuffer.PushBack();
	if ( Prefix )
		str << "[" << Prefix << "] ";
	str << String;
}
#endif


#if defined(BUFFER_DEBUG_LOG)
void TFastVideo::FlushDebugLogBuffer()
{
	ofMutex::ScopedLock lock( mDebugLogBufferLock );
	
	for ( int i=0;	i<mDebugLogBuffer.GetSize();	i++ )
	{
		auto& Debug = mDebugLogBuffer[i];
		DebugLog( Debug.c_str() );
	}
	mDebugLogBuffer.Clear();
}
#endif


void TFastVideo::DebugLog(const char* String)
{
#if defined(ENABLE_DEBUG_LOG)

#if defined(DEBUG_LOG_THREADSAFE)
	ofMutex::ScopedLock lock( mDebugFuncLock );
#endif

	if ( mDebugFunc )
	{
		(*mDebugFunc)( String );
	}

#endif
}



extern "C" void EXPORT_API SetOnErrorFunction(Unity::TOnErrorFunc pFunc)
{
	auto& FastVideo = Unity::GetFastVideo();
	
	FastVideo.mOnErrorFunc = pFunc;
}

// Prints a string
void Unity::OnError(TFastTexture& Instance,FastVideoError Error)
{
	auto& FastVideo = Unity::GetFastVideo();

	if ( FastVideo.mOnErrorFunc )
	{
		ulong InstanceId = Instance.GetRef().GetInt64();
		ulong ErrorId = static_cast<ulong>( Error );
		(*FastVideo.mOnErrorFunc)( InstanceId, ErrorId );
	}
}




extern "C" void EXPORT_API UnitySetGraphicsDevice(void* device, int deviceType, int eventType)
{
	auto DeviceEvent = static_cast<Unity::TGfxDeviceEvent::Type>( eventType );
	auto DeviceType = static_cast<Unity::TGfxDevice::Type>( deviceType );
	
	switch ( DeviceEvent )
	{
	case Unity::TGfxDeviceEvent::Shutdown:
		Unity::GetFastVideo().FreeDevice( DeviceType );
		break;

	case Unity::TGfxDeviceEvent::Initialize:
		Unity::GetFastVideo().AllocDevice( DeviceType, device );
		break;

    default:
        break;
	};

}





extern "C" void EXPORT_API UnityRenderEvent(int eventID)
{
	switch ( eventID )
	{
		case UnityEvent::OnPostRender:
			Unity::GetFastVideo().OnPostRender();
			break;

		default:
			Unity::DebugError( BufferString<100>() << "Unknown UnityRenderEvent [" << eventID << "]" );
			break;
	}
}

extern "C" EXPORT_API bool Pause(Unity::ulong Instance)
{
	auto* pInstance = Unity::GetFastVideo().FindInstance( SoyRef(Instance) );
	if ( !pInstance )
		return false;
	
	pInstance->SetState( TFastVideoState::Paused );
	return true;
}

extern "C" EXPORT_API bool Resume(Unity::ulong Instance)
{
	auto* pInstance = Unity::GetFastVideo().FindInstance( SoyRef(Instance) );
	if ( !pInstance )
		return false;
	
	pInstance->SetState( TFastVideoState::Playing );
	return true;
}

extern "C" EXPORT_API bool SetLooping(Unity::ulong Instance, bool EnableLooping)
{
	auto* pInstance = Unity::GetFastVideo().FindInstance(SoyRef(Instance));
	if (!pInstance)
		return false;

	pInstance->SetLooping(EnableLooping);
	return true;
}

extern "C" EXPORT_API void EnableTestDecoder(bool Enable)
{
	USE_TEST_DECODER = Enable;
}

extern "C" EXPORT_API void EnableDebugTimers(bool Enable)
{
	ENABLE_TIMER_DEBUG_LOG = Enable;
}

extern "C" EXPORT_API void EnableDebugLag(bool Enable)
{
	ENABLE_LAG_DEBUG_LOG = Enable;
}

extern "C" EXPORT_API void EnableDebugError(bool Enable)
{
	ENABLE_ERROR_LOG = Enable;

}
extern "C" EXPORT_API void EnableDebugFull(bool Enable)
{
	ENABLE_FULL_DEBUG_LOG = true;
}
