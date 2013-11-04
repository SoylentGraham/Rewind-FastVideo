#pragma once

#include <ofxSoylent.h>
#include <d3d11.h>
#include "TFrame.h"


class TUnityDevice_DX11;
class TUnityDevice;






template<typename TYPE>
class TAutoRelease
{
public:
	TAutoRelease() :
		mObject	( nullptr )
	{
	}
	TAutoRelease(TYPE* Object,bool AddRef) :
		mObject	( nullptr )
	{
		Set( Object, AddRef );
	}
	TAutoRelease(const TAutoRelease& that) :
		mObject	( nullptr )
	{
		Set( that.mObject, true );
	}
	~TAutoRelease()		{	Release();	}

	TYPE*		operator ->()	{	return mObject;	}
	operator	TYPE*()			{	return mObject;	}

	void		Set(TYPE* Object,bool AddRef)
	{
		Release();
		mObject = Object;
		if ( AddRef && mObject )
			mObject->AddRef();
	}

	void		Release()		
	{
		if ( mObject )
		{
			mObject->Release();
			mObject = nullptr;
		}
	}

public:
	TYPE*	mObject;
};





//	gr: hardcoded Unity stuff
namespace Unity
{
	namespace TRenderEvent
	{
		enum Type
		{
			OnPostRender	= 0,
		};
	};

	namespace TGfxDevice
	{
		enum Type
		{
			Invalid = -1,
			OpenGL = 0,          // OpenGL
			D3D9,                // Direct3D 9
			D3D11,               // Direct3D 11
			GCM,                 // Sony PlayStation 3 GCM
			Null,                // "null" device (used in batch mode)
			Hollywood,           // Nintendo Wii
			Xenon,               // Xbox 360
			OpenGLES,            // OpenGL ES 1.1
			OpenGLES20Mobile,    // OpenGL ES 2.0 mobile variant
			Molehill,            // Flash 11 Stage3D
			OpenGLES20Desktop,   // OpenGL ES 2.0 desktop variant (i.e. NaCl)
			Count
		};
		BufferString<100>	ToString(Type DeviceType);
	};


	// Event types for UnitySetGraphicsDevice
	namespace TGfxDeviceEvent
	{
		enum Type
		{
			Initialize = 0,
			Shutdown,
			BeforeReset,
			AfterReset,
		};
	};

	ofPtr<TUnityDevice_DX11>	AllocDevice(Unity::TGfxDevice::Type Type,void* Device);
};

template<class STRING>
inline STRING& operator<<(STRING& str,const Unity::TGfxDevice::Type& Value)
{
	str << Unity::TGfxDevice::ToString( Value );
	return str;
}



//	interface to unity gfx system
class TUnityDevice
{
public:
	TUnityDevice()				{}
	virtual ~TUnityDevice()		{}

	virtual bool		IsValid()=0;
	virtual TFrameMeta	GetTextureMeta(ID3D11Texture2D* Texture)=0;
};


class TUnityDevice_DX11 : public TUnityDevice
{
public:
	TUnityDevice_DX11(ID3D11Device* Device);

	virtual bool		IsValid()	{	return true;	}
	virtual TFrameMeta	GetTextureMeta(ID3D11Texture2D* Texture);

	ID3D11Device&		GetDevice()		{	assert( mDevice );	return *mDevice;	}
	
	TAutoRelease<ID3D11Texture2D>	AllocTexture(TFrameMeta FrameMeta);

private:
	TAutoRelease<ID3D11Device>	mDevice;
};

