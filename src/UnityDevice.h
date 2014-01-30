#pragma once

#include <ofxSoylent.h>
#include "TFrame.h"


#if defined(TARGET_WINDOWS)
	#define ENABLE_DX11
	#define ENABLE_OPENGL
#elif defined(TARGET_OSX)
	#define ENABLE_OPENGL
#endif



#if defined(ENABLE_DX11)
	//	something is including d3d10.h (from dx sdk) and some errors have different export types from winerror.h (winsdk)
	#pragma warning( push )
	#pragma warning( disable : 4005 )

	#include <d3d11.h>

	#pragma warning( pop )
#endif



#if defined(ENABLE_OPENGL)


	#if defined(TARGET_WINDOWS)
#define GLEW_STATIC	//	need to add to pre-processor if we need this
		#include <gl/glew.h>
//		#include <gl/GL.h>	//	included by glew
		#pragma comment(lib,"opengl32.lib")
	#else
		#include <gl/glew.h>
		#include <Opengl/gl.h>
		#include <OpenGL/OpenGL.h>
	#endif

#define GL_INVALID_FORMAT		0
#define GL_INVALID_TEXTURE_NAME	0u
#define GL_INVALID_BUFFER_NAME	0u
#endif



class TUnityDevice;
class TUnityDevice_Dummy;
#if defined(ENABLE_DX11)
class TUnityDevice_Dx11;
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
	class TDynamicTexture;

	class TTexture_Dx11;
	class TDynamicTexture_Dx11;

    class TTexture_Opengl;
	class TDynamicTexture_Opengl;
};

class Unity::TTexture
{
public:
    TTexture() :
		mObject     (nullptr)
    {
    }
    explicit TTexture(void* Object) :
		mObject     ( Object )
    {
    }
	explicit TTexture(uint32 Id) :
		mObject     ( reinterpret_cast<void*>(Id) )
    {
    }
    virtual bool        IsValid() const {   return (mObject != nullptr);  }	//	nullpointer != 0, so might have issues here in c++11
	operator            bool()			{	return IsValid();	}

	uint32				GetInteger() const	{	return reinterpret_cast<uint32>( mObject );	}
	void*				GetPointer() const	{	return mObject;	}

private:
    void*               mObject;	//	pointer to type in dx, integer in opengl. ptr<>int? erk
};



//	just a dynamic texture in DX11, PBO in opengl
class Unity::TDynamicTexture
{
public:
    TDynamicTexture() :
		mObject     (nullptr)
    {
    }
    explicit TDynamicTexture(void* Object) :
		mObject     ( Object )
    {
    }
	explicit TDynamicTexture(uint32 Id) :
		mObject     ( reinterpret_cast<void*>(Id) )
    {
    }
    virtual bool        IsValid() const {   return (mObject != nullptr);  }	//	nullpointer != 0, so might have issues here in c++11
	operator            bool()			{	return IsValid();	}

	uint32				GetInteger() const	{	return reinterpret_cast<uint32>( mObject );	}
	void*				GetPointer() const	{	return mObject;	}

private:
    void*               mObject;	//	pointer to type in dx, integer in opengl. ptr<>int? erk
};


template<class STRING>
inline STRING& operator<<(STRING& str,const Unity::TGfxDevice::Type& Value)
{
	str << Unity::TGfxDevice::ToString( Value );
	return str;
}


#if defined(ENABLE_DX11)
class Unity::TTexture_Dx11 : public Unity::TTexture
{
public:
    TTexture_Dx11(ID3D11Texture2D* Texture=nullptr) :
        TTexture    ( Texture )
    {
    }
    
    ID3D11Texture2D*    GetTexture()    {   return reinterpret_cast<ID3D11Texture2D*>( GetPointer() );   }
};
#endif

#if defined(ENABLE_DX11)
class Unity::TDynamicTexture_Dx11 : public Unity::TDynamicTexture
{
public:
    TDynamicTexture_Dx11(ID3D11Texture2D* Texture=nullptr) :
        TDynamicTexture    ( Texture )
    {
    }
    
    ID3D11Texture2D*    GetTexture()    {   return reinterpret_cast<ID3D11Texture2D*>( GetPointer() );   }
};
#endif


#if defined(ENABLE_OPENGL)
class TOpenglBufferCache
{
public:
	TOpenglBufferCache() :
		mUnityRef			( 0 ),
		mDataMap			( nullptr ),
		mBufferName			( GL_INVALID_BUFFER_NAME ),
		mDeleteRequested	( false ),
		mMapRequested		( false )
	{
	}

	bool		IsAllocated() const						{	return mBufferName != GL_INVALID_BUFFER_NAME;	}
	bool		IsMapped() const						{	return mDataMap != nullptr;	}
	int			GetSize() const							{	return mBufferMeta.GetDataSize();	}
	inline bool	operator==(const uint32 UnityRef) const	{	return mUnityRef == UnityRef;	}

public:
	uint32		mUnityRef;			//	so that we can allocate a texture out of the render thread, we map to our own ID, not the opengl name
	GLuint		mBufferName;		//	thread safe as only allocated/destroyed during render thread
	TFrameMeta	mBufferMeta;		//	data allocated (inc size)

	//	these might need a mutex
	void*		mDataMap;
	bool		mDeleteRequested;
	bool		mMapRequested;
};
#endif


#if defined(ENABLE_OPENGL)
class Unity::TTexture_Opengl : public Unity::TTexture
{
public:
    TTexture_Opengl(GLuint TextureName=GL_INVALID_TEXTURE_NAME) :
		TTexture    ( TextureName )
    {
    }
    
	GLuint			GetName()    {   return static_cast<GLuint>( GetInteger() );   }
	bool			Bind(TUnityDevice_Opengl& Device);
	bool			Unbind(TUnityDevice_Opengl& Device);
};
#endif

#if defined(ENABLE_OPENGL)
class Unity::TDynamicTexture_Opengl : public Unity::TDynamicTexture
{
public:
    TDynamicTexture_Opengl(uint32 UnityRef=0) :
		TDynamicTexture	( UnityRef )
    {
    }
    TDynamicTexture_Opengl(const TOpenglBufferCache& Buffer) :
		TDynamicTexture	( Buffer.mUnityRef )
    {
    }
};
#endif


//	interface to unity gfx system
class TUnityDevice
{
public:
	TUnityDevice()
	{
	}
	virtual ~TUnityDevice()	
	{
	}

	virtual bool			AllowOperationsOutOfRenderThread() const=0;
	bool					IsRenderThreadActive() const			{	return SoyThread::GetCurrentThreadId() == mRenderThreadId;	}
	void					SetRenderThread()						{	mRenderThreadId = SoyThread::GetCurrentThreadId();	}
	//	callback during render thread in case we can only do certain operations in render thread
	virtual void			OnRenderThreadUpdate()
	{
		assert( IsRenderThreadActive() );
	}
	virtual void			OnRenderThreadPostUpdate()
	{
		assert( IsRenderThreadActive() );
	}
	

	virtual bool            IsValid()=0;
    virtual Unity::TTexture			AllocTexture(TFrameMeta FrameMeta)=0;
    virtual Unity::TDynamicTexture	AllocDynamicTexture(TFrameMeta FrameMeta)=0;
    virtual bool            DeleteTexture(Unity::TTexture& Texture)=0;
    virtual bool            DeleteTexture(Unity::TDynamicTexture& Texture)=0;
	virtual TFrameMeta      GetTextureMeta(Unity::TTexture Texture)=0;
	virtual TFrameMeta      GetTextureMeta(Unity::TDynamicTexture Texture)=0;
	TFrameMeta              GetTextureMeta(Unity::TTexture* Texture)		{   return Texture ? GetTextureMeta(*Texture) : TFrameMeta();   }
	TFrameMeta              GetTextureMeta(Unity::TDynamicTexture* Texture)	{   return Texture ? GetTextureMeta(*Texture) : TFrameMeta();   }
	virtual bool            CopyTexture(Unity::TTexture Texture,const TFramePixels& Frame,bool Blocking)=0;
	virtual bool            CopyTexture(Unity::TDynamicTexture Texture,const TFramePixels& Frame,bool Blocking)=0;
	virtual bool            CopyTexture(Unity::TTexture DstTexture,const Unity::TDynamicTexture SrcTexture)=0;

public:
	ofMutex					mContextLock;	//	contexts are generally not threadsafe (certainly not DX11 or opengl) so make it common
	SoyThreadId				mRenderThreadId;
};


class TUnityDeviceContextScope
{
public:
	TUnityDeviceContextScope(TUnityDevice& Device) :
		mDevice	( Device )
	{
		mDevice.mContextLock.lock();
	}
	~TUnityDeviceContextScope()
	{
		mDevice.mContextLock.unlock();
	}

	bool			IsValid() const
	{
		//	fail it not allowed to do device stuff out of render thread
		if ( !mDevice.AllowOperationsOutOfRenderThread() )
		{
			if ( !mDevice.IsRenderThreadActive() )
				return false;
		}
		return true;
	}

	inline operator		bool() const	{	return IsValid();	}

public:
	TUnityDevice&	mDevice;
};


#if defined(ENABLE_DX11)
class TUnityDevice_Dx11 : public TUnityDevice
{
public:
	TUnityDevice_Dx11(ID3D11Device* Device);
    
	virtual bool			AllowOperationsOutOfRenderThread() const		{	return true;	}
	virtual bool            IsValid()	{	return true;	}
    virtual Unity::TTexture AllocTexture(TFrameMeta FrameMeta);
    virtual Unity::TDynamicTexture	AllocDynamicTexture(TFrameMeta FrameMeta);
    virtual bool            DeleteTexture(Unity::TTexture& Texture);
    virtual bool            DeleteTexture(Unity::TDynamicTexture& Texture);
	virtual TFrameMeta      GetTextureMeta(Unity::TTexture Texture);
	virtual TFrameMeta      GetTextureMeta(Unity::TDynamicTexture Texture);
	virtual bool            CopyTexture(Unity::TTexture Texture,const TFramePixels& Frame,bool Blocking);
	virtual bool            CopyTexture(Unity::TDynamicTexture Texture,const TFramePixels& Frame,bool Blocking);
	virtual bool            CopyTexture(Unity::TTexture DstTexture,const Unity::TDynamicTexture SrcTexture);
    
	ID3D11Device&				GetDevice()		{	assert( mDevice );	return *mDevice;	}
 	static TFrameFormat::Type	GetFormat(DXGI_FORMAT Format);
	static DXGI_FORMAT			GetFormat(TFrameFormat::Type Format);
   
private:
	TAutoRelease<ID3D11Device>	mDevice;
};
#endif



class TUnityDevice_Dummy : public TUnityDevice
{
public:
	TUnityDevice_Dummy()    {}
    
	virtual bool			AllowOperationsOutOfRenderThread() const		{	return true;	}
	virtual bool            IsValid()	{	return false;	}
    virtual Unity::TTexture AllocTexture(TFrameMeta FrameMeta)					{   return Unity::TTexture();   }
    virtual Unity::TDynamicTexture	AllocDynamicTexture(TFrameMeta FrameMeta)	{   return Unity::TDynamicTexture();   }
    virtual bool            DeleteTexture(Unity::TTexture& Texture)      {   Texture = Unity::TTexture();	return false;   }
    virtual bool            DeleteTexture(Unity::TDynamicTexture& Texture)      {   Texture = Unity::TDynamicTexture();	return false;   }
	virtual TFrameMeta      GetTextureMeta(Unity::TTexture Texture)     {   return TFrameMeta();    }
	virtual TFrameMeta      GetTextureMeta(Unity::TDynamicTexture Texture)     {   return TFrameMeta();    }
	virtual bool            CopyTexture(Unity::TTexture Texture,const TFramePixels& Frame,bool Blocking)    {   return false;   }
	virtual bool            CopyTexture(Unity::TDynamicTexture Texture,const TFramePixels& Frame,bool Blocking)    {   return false;   }
    virtual bool            CopyTexture(Unity::TTexture DstTexture,const Unity::TDynamicTexture SrcTexture)       {   return false;   }
};





#if defined(ENABLE_OPENGL)
class TUnityDevice_Opengl : public TUnityDevice
{
public:
	TUnityDevice_Opengl() :
		mFirstRun		( true ),
		mLastBufferRef	( 0 )
	{
	}
    
	virtual bool			AllowOperationsOutOfRenderThread() const		{	return false;	}
	virtual void			OnRenderThreadUpdate();
	virtual void			OnRenderThreadPostUpdate();
	virtual bool            IsValid()	{	return !mFirstRun;	}		//	only valid once we've done first loop
    virtual Unity::TTexture AllocTexture(TFrameMeta FrameMeta);
    virtual Unity::TDynamicTexture	AllocDynamicTexture(TFrameMeta FrameMeta);
    virtual bool            DeleteTexture(Unity::TTexture& Texture);
    virtual bool            DeleteTexture(Unity::TDynamicTexture& Texture);
	virtual TFrameMeta      GetTextureMeta(Unity::TTexture Texture);
	virtual TFrameMeta      GetTextureMeta(Unity::TDynamicTexture Texture);
	virtual bool            CopyTexture(Unity::TTexture Texture,const TFramePixels& Frame,bool Blocking);
	virtual bool            CopyTexture(Unity::TDynamicTexture Texture,const TFramePixels& Frame,bool Blocking);
	virtual bool            CopyTexture(Unity::TTexture DstTexture,const Unity::TDynamicTexture SrcTexture);
 
	static TFrameFormat::Type	GetFormat(GLint Format);
	static GLint				GetFormat(TFrameFormat::Type Format);
	static bool				HasError();	//	note: static so need parent to do a context lock
	std::string				GetString(GLenum StringId);
	
private:
	GLuint					GetName(Unity::TDynamicTexture& Texture);
    bool					AllocDynamicTexture(TOpenglBufferCache& Buffer);
    bool					DeleteTexture(TOpenglBufferCache& Buffer);
	bool					Bind(Unity::TDynamicTexture& Texture);
	bool					Bind(TOpenglBufferCache& Buffer);
	bool					Unbind(Unity::TDynamicTexture& Texture);
	bool					Unbind(TOpenglBufferCache& Buffer);
	bool					AllocMap(TOpenglBufferCache& Buffer);
	bool					FreeMap(TOpenglBufferCache& Buffer);

private:
	bool					mFirstRun;
	Array<GLuint>			mDeleteTextureQueue;

	uint32					mLastBufferRef;
	ofMutexT<Array<TOpenglBufferCache>>	mBufferCache;	//	
};
#endif










