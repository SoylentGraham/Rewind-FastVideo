// Example low level rendering Unity plugin
#include "FastVideo.h"
#include "SoyDecoder.h"
#include "SoyThread.h"
#include <sstream>
#include "TFastTexture.h"


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

	Unity::DebugLog( BufferString<100>() << "Allocated instance: " << pInstance->GetRef() );

	return pInstance->GetRef();
}

bool TFastVideo::FreeInstance(SoyRef InstanceRef)
{
	ofMutex::ScopedLock Lock( mInstancesLock );
	int Index = FindInstanceIndex( InstanceRef );
	if ( Index < 0 )
	{
		Unity::DebugLog( BufferString<100>() << "Failed to find instance: " << InstanceRef );
		return false;
	}

	auto* pInstance = mInstances[Index];
	mInstances.RemoveBlock( Index, 1 );
	delete pInstance;
	Unity::DebugLog( BufferString<100>() << "Free'd instance: " << InstanceRef );

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
			Unity::DebugLog(BufferString<1000>() <<"Failed to allocated device " << DeviceType );
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
	
	Unity::DebugLog("FastVideo debug-log initialised okay");
}

// Prints a string
void Unity::DebugLog(const char* str)
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

void Unity::DebugError(const char* str)
{
	//	print out to visual studio debugger
	ofLogError(str);
	
	auto& FastVideo = Unity::GetFastVideo();
	
#if defined(BUFFER_DEBUG_LOG)
	FastVideo.BufferDebugLog( str, "Error" );
#else
	FastVideo.DebugLog( str );
#endif
}


void TFastVideo::BufferDebugLog(const char* String,const char* Prefix)
{
	ofMutex::ScopedLock lock( mDebugLogBufferLock );
	
	auto& str = mDebugLogBuffer.PushBack();
	if ( Prefix )
		str << "[" << Prefix << "] ";
	str << String;
}


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
		case Unity::TRenderEvent::OnPostRender:
			Unity::GetFastVideo().OnPostRender();
			break;

		default:
			Unity::DebugLog( BufferString<100>() << "Unknown UnityRenderEvent [" << eventID << "]" );
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

extern "C" EXPORT_API bool SetLooping(Unity::ulong Instance,bool EnableLooping)
{
	auto* pInstance = Unity::GetFastVideo().FindInstance( SoyRef(Instance) );
	if ( !pInstance )
		return false;
	
	pInstance->SetLooping( EnableLooping );
	return true;
}
