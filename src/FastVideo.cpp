// Example low level rendering Unity plugin
#include "FastVideo.h"
#include "SoyDecoder.h"
#include "SoyThread.h"
#include <sstream>
#include "TFastTexture.h"


namespace Unity
{
	TDebugLogFunc	gDebugFunc = NULL;
};


TFastVideo gFastVideo;






TFastVideo::TFastVideo() :
	mFramePool			( DEFAULT_MAX_POOL_SIZE ),
	mNextInstanceRef	( "FastTexture" )
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
	mNextInstanceRef++;
	
	mInstances.PushBack( pInstance );
	return pInstance->GetRef();
}

bool TFastVideo::FreeInstance(SoyRef InstanceRef)
{
	ofMutex::ScopedLock Lock( mInstancesLock );
	int Index = FindInstanceIndex( InstanceRef );
	if ( Index < 0 )
		return false;

	auto* pInstance = mInstances[Index];
	mInstances.RemoveBlock( Index, 1 );
	delete pInstance;
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

	auto& Device = *mDevice;
	
	for ( int i=0;	i<mInstances.GetSize();	i++ )
	{
		auto& Instance = *mInstances[i];
		Instance.OnPostRender(Device);
	}
}

bool TFastVideo::AllocDevice(Unity::TGfxDevice::Type DeviceType,void* Device)
{
	//	free old device
	mDevice.reset();

	//	alloc new one
	mDevice = Unity::AllocDevice( DeviceType, Device );

	return mDevice!=nullptr;
}

bool TFastVideo::FreeDevice(Unity::TGfxDevice::Type DeviceType)
{
	mDevice.reset();
	return true;
}




extern "C" EXPORT_API Unity::ulong	AllocInstance()
{
	//	alloc a new instance
	SoyRef InstanceRef = gFastVideo.AllocInstance();
	Unity::ulong RefInt = static_cast<Unity::ulong>( InstanceRef.GetInt64() );
	return RefInt;
}

extern "C" EXPORT_API bool FreeInstance(Unity::ulong Instance)
{
	return gFastVideo.FreeInstance( SoyRef(Instance) );
}

extern "C" EXPORT_API bool SetTexture(Unity::ulong Instance,void* pTexture)
{
	auto* pInstance = gFastVideo.FindInstance( SoyRef(Instance) );
	if ( !pInstance )
		return false;

	if ( !gFastVideo.IsDeviceValid() )
		return false;

	auto* Texture = static_cast<ID3D11Texture2D*>( pTexture );
	return pInstance->SetTexture( Texture, gFastVideo.GetDevice() );
}

extern "C" EXPORT_API bool SetVideo(Unity::ulong Instance,const wchar_t* pFilename,int Length)
{
	auto* pInstance = gFastVideo.FindInstance( SoyRef(Instance) );
	if ( !pInstance )
		return false;

	//	make string
	std::wstring Filename;
	for ( int i=0;	i<Length;	i++ )
		Filename += pFilename[i];

	return pInstance->SetVideo( Filename, gFastVideo.GetDevice() );
}


extern "C" void EXPORT_API SetDebugLogFunction(Unity::TDebugLogFunc pFunc)
{
	Unity::gDebugFunc = pFunc;
	Unity::DebugLog("Test");
}

// Prints a string
void Unity::DebugLog(const char* str)
{
	//	print out to visual studio debugger
	ofLogNotice(str);
	ofLogNotice("\n");

	//	print to stdout
	//printf ("%s\n", str);

	//	print to unity if we have a function set
	if ( gDebugFunc )
	{
		(*gDebugFunc)( str );
	}
}

void Unity::DebugLog(const std::string& String)
{
	DebugLog( String.c_str() );
}


extern "C" void EXPORT_API UnitySetGraphicsDevice(void* device, int deviceType, int eventType)
{
	auto DeviceEvent = static_cast<Unity::TGfxDeviceEvent::Type>( eventType );
	auto DeviceType = static_cast<Unity::TGfxDevice::Type>( deviceType );
	
	switch ( DeviceEvent )
	{
	case Unity::TGfxDeviceEvent::Shutdown:
		gFastVideo.FreeDevice( DeviceType );
		break;

	case Unity::TGfxDeviceEvent::Initialize:
		gFastVideo.AllocDevice( DeviceType, device );
		break;
	};

}





extern "C" void EXPORT_API UnityRenderEvent(int eventID)
{
	switch ( eventID )
	{
		case Unity::TRenderEvent::OnPostRender:
			gFastVideo.OnPostRender();
			break;

		default:
			Unity::DebugLog( BufferString<100>() << "Unknown UnityRenderEvent [" << eventID << "]" );
			break;
	}

}

