#pragma once

#if defined(TARGET_WINDOWS)
	#define ENABLE_DIRECTX11
#elif defined(TARGET_OSX)
	#define ENABLE_OPENGL
#endif

#include <ofxSoylent.h>
#include "TFrame.h"

#if defined(ENABLE_DIRECTX11)
	#include <d3d11.h>
#endif

#if defined(ENABLE_OPENGL)
	#if defined(TARGET_WINDOWS)
		#include <gl/GL.h>
	#else
		#include <Opengl/gl.h>
		#include <OpenGL/OpenGL.h>
	#endif
#endif


class TUnityDevice;
class TUnityDevice_Dummy;
#if defined(ENABLE_DIRECTX11)
class TUnityDevice_DX11;
#endif
#if defined(ENABLE_OPENGL)
class TUnityDevice_Opengl;
#endif





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
    
	ofPtr<TUnityDevice> AllocDevice(Unity::TGfxDevice::Type Type,void* Device);
    
    class TTexture;
    class TTexture_DX11;
    class TTexture_Opengl;
};

class Unity::TTexture
{
public:
    TTexture() :
		mObject     (nullptr),
		mId			( 0 )
    {
    }
    explicit TTexture(void* Object) :
		mObject     (Object),
		mId			( 0 )
    {
    }
	explicit TTexture(uint32 Id) :
		mObject     (nullptr),
		mId			( Id )
    {
    }
    virtual bool        IsValid() const {   return (mObject != nullptr) || (mId!=0);  }
	operator            bool()			{	return IsValid();	}

protected:
    void*               mObject;	//	pointer to type in dx
	uint32				mId;		//	id for opengl
};

template<class STRING>
inline STRING& operator<<(STRING& str,const Unity::TGfxDevice::Type& Value)
{
	str << Unity::TGfxDevice::ToString( Value );
	return str;
}


#if defined(ENABLE_DX11)
class Unity::TTexture_DX11 : public Unity::TTexture
{
public:
    TTexture_DX11(ID3D11Texture2D* Texture=nullptr) :
        TTexture    ( Texture )
    {
    }
    
    ID3D11Texture2D*    GetTexture()    {   return reinterpret_cast<ID3D11Texture2D*>( mObject );   }
};
#endif


#if defined(ENABLE_OPENGL)
class Unity::TTexture_Opengl : public Unity::TTexture
{
public:
    TTexture_Opengl(GLuint Texture=0) :
		TTexture    ( Texture )
    {
    }
    
    GLuint			GetTexture()    {   return static_cast<GLuint>( mId );   }
    bool			BindTexture();
};
#endif



//	interface to unity gfx system
class TUnityDevice
{
public:
	TUnityDevice()				{}
	virtual ~TUnityDevice()		{}

	TFrameMeta              GetTextureMeta(Unity::TTexture* Texture)    {   return Texture ? GetTextureMeta(*Texture) : TFrameMeta();   }
    
	virtual bool            IsValid()=0;
    virtual Unity::TTexture AllocTexture(TFrameMeta FrameMeta)=0;
    virtual bool            DeleteTexture(Unity::TTexture Texture)=0;
	virtual TFrameMeta      GetTextureMeta(Unity::TTexture Texture)=0;
	virtual bool            CopyTexture(Unity::TTexture Texture,const TFramePixels& Frame,bool Blocking)=0;
	virtual bool            CopyTexture(Unity::TTexture DstTexture,const Unity::TTexture SrcTexture)=0;
};



#if defined(ENABLE_DX11)
class TUnityDevice_DX11 : public TUnityDevice
{
public:
	TUnityDevice_DX11(ID3D11Device* Device);
    
	virtual bool            IsValid()	{	return true;	}
    virtual Unity::TTexture AllocTexture(TFrameMeta FrameMeta);
    virtual bool            DeleteTexture(Unity::TTexture Texture);
	virtual TFrameMeta      GetTextureMeta(Unity::TTexture Texture);
	virtual bool            CopyTexture(Unity::TTexture Texture,const TFramePixels& Frame,bool Blocking);
	virtual bool            CopyTexture(Unity::TTexture DstTexture,const Unity::TTexture SrcTexture);
    
	ID3D11Device&           GetDevice()		{	assert( mDevice );	return *mDevice;	}
    
private:
	ofMutex						mContextLock;	//	DX11 context is not threadsafe
	TAutoRelease<ID3D11Device>	mDevice;
};
#endif



class TUnityDevice_Dummy : public TUnityDevice
{
public:
	TUnityDevice_Dummy()    {}
    
	virtual bool            IsValid()	{	return false;	}
    virtual Unity::TTexture AllocTexture(TFrameMeta FrameMeta)          {   return Unity::TTexture();   }
    virtual bool            DeleteTexture(Unity::TTexture Texture)      {   return false;   }
	virtual TFrameMeta      GetTextureMeta(Unity::TTexture Texture)     {   return TFrameMeta();    }
	virtual bool            CopyTexture(Unity::TTexture Texture,const TFramePixels& Frame,bool Blocking)    {   return false;   }
    virtual bool            CopyTexture(Unity::TTexture DstTexture,const Unity::TTexture SrcTexture)        {   return false;   }
};




#if defined(ENABLE_OPENGL)
class TUnityDevice_Opengl : public TUnityDevice
{
public:
	TUnityDevice_Opengl();
    
	virtual bool            IsValid()	{	return true;	}
    virtual Unity::TTexture AllocTexture(TFrameMeta FrameMeta);
    virtual bool            DeleteTexture(Unity::TTexture Texture);
	virtual TFrameMeta      GetTextureMeta(Unity::TTexture Texture);
	virtual bool            CopyTexture(Unity::TTexture Texture,const TFramePixels& Frame,bool Blocking);
	virtual bool            CopyTexture(Unity::TTexture DstTexture,const Unity::TTexture SrcTexture);
 
private:
};
#endif










