#include "UnityDevice.h"
#include "FastVideo.h"


#if defined(ENABLE_DX11)
DXGI_FORMAT TUnityDevice_Dx11::GetFormat(TFrameFormat::Type Format)
{
	switch ( Format )
	{
	case TFrameFormat::RGBA:
		return DXGI_FORMAT_R8G8B8A8_UNORM;

	case TFrameFormat::RGB:		//	24 bit not supported
	default:
		return DXGI_FORMAT_UNKNOWN;
	}
}
#endif

#if defined(ENABLE_DX11)
TFrameFormat::Type TUnityDevice_Dx11::GetFormat(DXGI_FORMAT Format)
{
	//	return 0 if none supported
	switch ( Format )
	{
	case DXGI_FORMAT_R8G8B8A8_UNORM:
		return TFrameFormat::RGBA;

	default:
		return TFrameFormat::Invalid;
	};
}
#endif


#if defined(ENABLE_OPENGL)
GLint TUnityDevice_Opengl::GetFormat(TFrameFormat::Type Format)
{
	switch ( Format )
	{
		case TFrameFormat::RGBA:	return GL_RGBA;
		case TFrameFormat::RGB:		return GL_RGB;
		default:
			return GL_INVALID_FORMAT;
	}
}
#endif

#if defined(ENABLE_OPENGL)
TFrameFormat::Type TUnityDevice_Opengl::GetFormat(GLint Format)
{
	//	return 0 if none supported
	switch ( Format )
	{
		case GL_RGBA:	return TFrameFormat::RGBA;
		case GL_RGB:	return TFrameFormat::RGB;
		default:
			return TFrameFormat::Invalid;
	};
}
#endif


ofPtr<TUnityDevice> Unity::AllocDevice(Unity::TGfxDevice::Type Type,void* Device)
{
	//	only support one atm
	ofPtr<TUnityDevice> pDevice;

	switch ( Type )
	{
#if defined(ENABLE_DX11)
		case Unity::TGfxDevice::D3D11:
			if ( Device )
				pDevice = ofPtr<TUnityDevice>( new TUnityDevice_Dx11( static_cast<ID3D11Device*>(Device) ) );
			break;
#endif
#if defined(ENABLE_OPENGL)
		case Unity::TGfxDevice::OpenGL:
			pDevice = ofPtr<TUnityDevice>( new TUnityDevice_Opengl() );
			break;
#endif
        default:
            break;
	};

	return pDevice;
}


#if defined(ENABLE_DX11)
TUnityDevice_Dx11::TUnityDevice_Dx11(ID3D11Device* Device) :
	mDevice	( Device, true )
{
}
#endif


#if defined(ENABLE_DX11)
Unity::TTexture TUnityDevice_Dx11::AllocTexture(TFrameMeta FrameMeta)
{
	if ( !FrameMeta.IsValid() )
		return Unity::TTexture();
	if ( !mDevice )
		return Unity::TTexture();

	D3D11_TEXTURE2D_DESC Desc;
	memset(&Desc, 0, sizeof(Desc));

	Desc.Width = FrameMeta.mWidth;
	Desc.Height = FrameMeta.mHeight;
	Desc.MipLevels = 1;
	Desc.ArraySize = 1;

	Desc.SampleDesc.Count = 1;
	Desc.SampleDesc.Quality = 0;
	Desc.Usage = D3D11_USAGE_DYNAMIC;
	Desc.Format = GetFormat(FrameMeta.mFormat);
	Desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	Desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	//Desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		
	ID3D11Texture2D* pTexture = NULL;
	TUnityDeviceContextScope Context( *this );
	if ( !Context )
		return Unity::TTexture();

	auto Result = mDevice->CreateTexture2D( &Desc, NULL, &pTexture );
	if ( Result != S_OK )
	{
		Unity::DebugLog("Failed to create dynamic texture");
	}
	
	//	gr: need to manage textures on device
	//TAutoRelease<ID3D11Texture2D> Texture( pTexture, true );
	pTexture->AddRef();
	return Unity::TTexture( pTexture );
}
#endif


#if defined(ENABLE_DX11)
bool TUnityDevice_Dx11::DeleteTexture(Unity::TTexture& Texture)
{
	//	gr: need to manage textures on device
	auto* TextureDx = static_cast<Unity::TTexture_Dx11&>( Texture).GetTexture();
	if ( !TextureDx )
		return false;

	ID3D11Texture2D* pTexture = NULL;
	TUnityDeviceContextScope Context( *this );
	if ( !Context )
		return Unity::TTexture();

	TextureDx->Release();
	Texture = Unity::TTexture();
	return true;
}
#endif

#if defined(ENABLE_DX11)
TFrameMeta TUnityDevice_Dx11::GetTextureMeta(Unity::TTexture Texture)
{
	auto* TextureDx = static_cast<Unity::TTexture_Dx11&>( Texture).GetTexture();
	if ( !TextureDx )
		return TFrameMeta();

	ID3D11Texture2D* pTexture = NULL;
	TUnityDeviceContextScope Context( *this );
	if ( !Context )
		return Unity::TTexture();

	D3D11_TEXTURE2D_DESC Desc;
	TextureDx->GetDesc( &Desc );

	auto Format = GetFormat( Desc.Format );
	TFrameMeta TextureMeta( Desc.Width, Desc.Height, Format );
	return TextureMeta;
}
#endif



BufferString<100> Unity::TGfxDevice::ToString(Unity::TGfxDevice::Type DeviceType)
{
	switch ( DeviceType )
	{
		case Invalid:			return "Invalid";
		case OpenGL:			return "OpenGL";
		case D3D9:				return "D3D9";
		case D3D11:				return "D3D11";
		case GCM:				return "GCM";
		case Null:				return "null";
		case Hollywood:			return "Hollywood/Wii";
		case Xenon:				return "Xenon/xbox 360";
		case OpenGLES:			return "OpenGL ES 1.1";
		case OpenGLES20Mobile:	return "OpenGL ES 2.0 mobile";
		case Molehill:			return "Molehill/Flash 11";
		case OpenGLES20Desktop:	return "OpenGL ES 2.0 desktop/NaCL";
		default:
			return BufferString<100>()<<"Unknown device " << static_cast<int>(DeviceType);
	}
}


#if defined(ENABLE_DX11)
bool TUnityDevice_Dx11::CopyTexture(Unity::TTexture TextureU,const TFramePixels& Frame,bool Blocking)
{
	auto* Texture = static_cast<Unity::TTexture_Dx11&>( TextureU ).GetTexture();
	if ( !Texture )
		return false;

	auto& Device11 = GetDevice();
	ID3D11Texture2D* pTexture = NULL;
	TUnityDeviceContextScope Context( *this );
	if ( !Context )
		return Unity::TTexture();


	TAutoRelease<ID3D11DeviceContext> ctx;
	Device11.GetImmediateContext( &ctx.mObject );
	if ( !ctx )
	{
		Unity::DebugLog("Failed to get device context");
		return false;
	}

	//	update our dynamic texture
	{
		ofScopeTimerWarning MapTimer("DX::Map copy",2);
	
		D3D11_TEXTURE2D_DESC SrcDesc;
		Texture->GetDesc(&SrcDesc);

		D3D11_MAPPED_SUBRESOURCE resource;
		int SubResource = 0;
		int flags = Blocking ? D3D11_MAP_FLAG_DO_NOT_WAIT : 0x0;
		HRESULT hr = ctx->Map( Texture, SubResource, D3D11_MAP_WRITE_DISCARD, flags, &resource);

		//	specified do not block, and GPU is using the texture
		if ( !Blocking && hr == DXGI_ERROR_WAS_STILL_DRAWING )
			return false;

		//	other error
		if ( hr != S_OK )
		{
			BufferString<1000> Debug;
			Debug << "Failed to get Map() for dynamic texture(" << SrcDesc.Width << "," << SrcDesc.Height << "); Error; " << hr;
			Unity::DebugLog( Debug );
			return false;
		}

		int ResourceDataSize = resource.RowPitch * SrcDesc.Height;//	width in bytes
		if ( Frame.GetDataSize() != ResourceDataSize )
		{
			BufferString<1000> Debug;
			Debug << "Warning: resource/texture data size mismatch; " << Frame.GetDataSize() << " (frame) vs " << ResourceDataSize << " (resource)";
			Unity::DebugLog( Debug );
			ResourceDataSize = ofMin( ResourceDataSize, Frame.GetDataSize() );
		}

		//	update contents 
		memcpy( resource.pData, Frame.GetData(), ResourceDataSize );
		ctx->Unmap( Texture, SubResource);
	}

	return true;
}
#endif


#if defined(ENABLE_DX11)
bool TUnityDevice_Dx11::CopyTexture(Unity::TTexture DstTextureU,Unity::TTexture SrcTextureU)
{
	auto* DstTexture = static_cast<Unity::TTexture_Dx11&>( DstTextureU ).GetTexture();
	auto* SrcTexture = static_cast<Unity::TTexture_Dx11&>( SrcTextureU ).GetTexture();
	if ( !DstTexture || !SrcTexture )
		return false;

	auto& Device11 = GetDevice();
	ID3D11Texture2D* pTexture = NULL;
	TUnityDeviceContextScope Context( *this );
	if ( !Context )
		return Unity::TTexture();


	TAutoRelease<ID3D11DeviceContext> ctx;
	Device11.GetImmediateContext( &ctx.mObject );
	if ( !ctx )
	{
		Unity::DebugLog("Failed to get device context");
		return false;
	}	
	
	//	copy to real texture (gpu->gpu)
	//	gr: this will fail silently if dimensions/format different
	{
		//ofScopeTimerWarning MapTimer("DX::copy resource",2);
		ctx->CopyResource( DstTexture, SrcTexture );
	}

	return true;
}
#endif



#if defined(ENABLE_OPENGL)
bool Unity::TTexture_Opengl::Bind(TUnityDevice_Opengl& Device)
{
	if ( !IsValid() )
		return false;
	
	auto TextureName = GetName();
	glBindTexture( GL_TEXTURE_2D, TextureName );
	if ( TUnityDevice_Opengl::HasError() )
		return false;

	//	gr: this only works AFTER glBindTexture: http://www.opengl.org/sdk/docs/man/xhtml/glIsTexture.xml
	if ( !glIsTexture(TextureName) )
	{
		BufferString<200> Debug;
		Debug << "Bound invalid texture name [" << TextureName << "]";
		Unity::DebugLog( Debug );
		return false;
	}

	return true;
}
#endif



#if defined(ENABLE_OPENGL)
bool Unity::TTexture_Opengl::Unbind(TUnityDevice_Opengl& Device)
{
	glBindTexture( GL_TEXTURE_2D, GL_INVALID_TEXTURE_NAME );
	return true;
}
#endif

#if defined(ENABLE_OPENGL)
bool Unity::TDynamicTexture_Opengl::Unbind(TUnityDevice_Opengl& Device)
{
	glBindBuffer( GL_PIXEL_UNPACK_BUFFER, GL_INVALID_BUFFER_NAME );
	return true;
}
#endif




#if defined(ENABLE_OPENGL)
Unity::TTexture TUnityDevice_Opengl::AllocTexture(TFrameMeta FrameMeta)
{
	if ( !FrameMeta.IsValid() )
		return Unity::TTexture();

	auto Format = GetFormat( FrameMeta.mFormat );
	if ( Format == GL_INVALID_FORMAT )
	{
		BufferString<200> Debug;
		Debug << "Failed to create texture; unsupported format " << TFrameFormat::ToString( FrameMeta.mFormat );
		Unity::DebugLog( Debug );
		return Unity::TTexture();
	}

	//	alloc
	TUnityDeviceContextScope Context( *this );
	if ( !Context )
		return Unity::TTexture();

	GLuint TextureName = GL_INVALID_TEXTURE_NAME;
	glGenTextures( 1, &TextureName );
	if ( HasError() || TextureName == GL_INVALID_TEXTURE_NAME )
		return Unity::TTexture();
	Unity::TTexture NewTexture( TextureName );
	auto& Texture = static_cast<Unity::TTexture_Opengl&>( NewTexture );

	//	bind so we can initialise
	if ( !Texture.Bind(*this) )
	{
		DeleteTexture( Texture );
		return Texture;
	}

	//	initialise to set dimensions
	TFramePixels InitFramePixels( FrameMeta );
	InitFramePixels.SetColour( HARDWARE_INIT_TEXTURE_COLOUR );
	glTexImage2D( GL_TEXTURE_2D, 0, Format, FrameMeta.mWidth, FrameMeta.mHeight, 0, Format, GL_UNSIGNED_BYTE, InitFramePixels.GetData() );
	if ( HasError() )
	{
		DeleteTexture( Texture );
		return Texture;
	}

	return Texture;
}
#endif


#if defined(ENABLE_OPENGL)
Unity::TDynamicTexture TUnityDevice_Opengl::AllocDynamicTexture(TFrameMeta FrameMeta)
{
	if ( !FrameMeta.IsValid() )
		return Unity::TDynamicTexture();

	if ( !glewIsSupported( "GL_ARB_pixel_buffer_object" ) )
		return Unity::TDynamicTexture();

	//	https://developer.apple.com/library/mac/documentation/graphicsimaging/conceptual/opengl-macprogguide/opengl_texturedata/opengl_texturedata.html
	GLuint BufferName = GL_INVALID_BUFFER_NAME;
	glGenBuffersARB( 1, &BufferName );
	if ( HasError() || BufferName == GL_INVALID_BUFFER_NAME )
		return Unity::TDynamicTexture();
	
	//	initialise/validate name with a bind
	Unity::TDynamicTexture_Opengl NewTexture( BufferName );
	if ( !NewTexture.Bind(*this) )
	{
		DeleteTexture( NewTexture );
		return Unity::TDynamicTexture();
	}

	//	init buffer storage
	static bool UseStreamUsage = false;
	auto Usage = UseStreamUsage ? GL_STREAM_DRAW : GL_DYNAMIC_DRAW;
	glBufferData( GL_PIXEL_UNPACK_BUFFER, FrameMeta.GetDataSize(), nullptr, Usage );
	if ( HasError() )
	{
		DeleteTexture( NewTexture );
		return Unity::TDynamicTexture();
	}

	NewTexture.Unbind(*this);

	return NewTexture;
}
#endif

#if defined(ENABLE_OPENGL)
bool Unity::TDynamicTexture_Opengl::Bind(TUnityDevice_Opengl& Device)
{
	TUnityDeviceContextScope Context( Device );
	if ( !Context )
		return false;

	auto Name = GetName();
	glBindBuffer( GL_PIXEL_UNPACK_BUFFER, Name );
	if ( TUnityDevice_Opengl::HasError() )
		return false;
	return true;
}
#endif


#if defined(ENABLE_OPENGL)
TFrameMeta TUnityDevice_Opengl::GetTextureMeta(Unity::TTexture Texture)
{
	/*
	TUnityDeviceContextScope Context( *this );
	if ( !Context )
		return TFrameMeta();
		*/
	auto& TextureGl = static_cast<Unity::TTexture_Opengl&>( Texture );
	if ( !TextureGl.Bind(*this) )
		return TFrameMeta();
	
	GLint Width,Height,Formatgl;
	int Lod = 0;
	glGetTexLevelParameteriv( GL_TEXTURE_2D, Lod, GL_TEXTURE_WIDTH, &Width );
	glGetTexLevelParameteriv( GL_TEXTURE_2D, Lod, GL_TEXTURE_HEIGHT, &Height );
	glGetTexLevelParameteriv( GL_TEXTURE_2D, Lod, GL_TEXTURE_INTERNAL_FORMAT, &Formatgl );
	
	if ( HasError() )
		return TFrameMeta();

	auto Format = GetFormat( Formatgl );
	TFrameMeta TextureMeta( Width, Height, Format );
	return TextureMeta;
}
#endif


#if defined(ENABLE_OPENGL)
TFrameMeta TUnityDevice_Opengl::GetTextureMeta(Unity::TDynamicTexture Texture)
{
	return TFrameMeta();
}
#endif

#if defined(ENABLE_OPENGL)
bool TUnityDevice_Opengl::CopyTexture(Unity::TTexture Texture,const TFramePixels& Frame,bool Blocking)
{
	TUnityDeviceContextScope Context( *this );
	if ( !Context )
		return Unity::TTexture();

	auto& TextureGl = static_cast<Unity::TTexture_Opengl&>( Texture );
	if ( !TextureGl.Bind(*this) )
		return false;

	GLint Format = GetFormat( Frame.mMeta.mFormat );
	glTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, Frame.GetWidth(), Frame.GetHeight(), Format, GL_UNSIGNED_BYTE, Frame.GetData() );
	if ( HasError() )
		return false;

	return true;
}
#endif


#if defined(ENABLE_OPENGL)
bool TUnityDevice_Opengl::CopyTexture(Unity::TDynamicTexture Texture,const TFramePixels& Frame,bool Blocking)
{
	TUnityDeviceContextScope Context( *this );
	if ( !Context )
		return Unity::TTexture();

	auto& DynamicTexture = static_cast<Unity::TDynamicTexture_Opengl&>( Texture );
	if ( !DynamicTexture.Bind(*this) )
		return false;

	//	get data to write to
	//	gr: safety here? eg, data length & mutex?
	auto* DstData = glMapBuffer( GL_PIXEL_UNPACK_BUFFER, GL_WRITE_ONLY );
	if ( !DstData )
		return false;
	memcpy( DstData, Frame.GetData(), Frame.GetDataSize() );
	auto Error = glUnmapBuffer( GL_PIXEL_UNPACK_BUFFER );

	//	gr: error, may not be an error...
	//	http://stackoverflow.com/questions/19544691/glunmapbuffer-return-value-and-error-code
	//	http://www.opengl.org/wiki/Buffer_Object#Mapping
	if ( Error != GL_TRUE )
	{
		BufferString<100> Debug;
		Debug << "glUnmapBuffer returned " << Error << ", may be harmless.";
		Unity::DebugError( Debug );
	}
	

	if ( HasError() )
		return false;

	DynamicTexture.Unbind(*this);

	return true;
}
#endif


#if defined(ENABLE_OPENGL)
bool TUnityDevice_Opengl::CopyTexture(Unity::TTexture DstTextureU,Unity::TDynamicTexture SrcTextureU)
{
	TUnityDeviceContextScope Context( *this );
	if ( !Context )
		return false;

	TFrameMeta TextureMeta = GetTextureMeta( DstTextureU );
	auto& SrcTexture = static_cast<Unity::TDynamicTexture_Opengl&>( SrcTextureU );
	auto& DstTexture = static_cast<Unity::TTexture_Opengl&>( DstTextureU );
	if ( !SrcTexture.Bind(*this) )
		return false;
	if ( !DstTexture.Bind(*this) )
		return false;

	auto Format = GetFormat( TextureMeta.mFormat );
	glTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, TextureMeta.mWidth, TextureMeta.mHeight, Format, GL_UNSIGNED_BYTE, nullptr );
	if ( HasError() )
		return false;

	SrcTexture.Unbind(*this);
	DstTexture.Unbind(*this);

	return true;
}
#endif


#if defined(ENABLE_OPENGL)
bool TUnityDevice_Opengl::DeleteTexture(Unity::TTexture& TextureU)
{
	auto Texture = static_cast<Unity::TTexture_Opengl&>( TextureU );

	TUnityDeviceContextScope Context( *this );
	if ( !Context )
	{
		mDeleteTextureQueue.PushBackUnique( Texture.GetName() );
		TextureU = Unity::TTexture();
		return true;
	}

	//	gr: need to know if we created this texture or not
	auto Name = Texture.GetName();
	//glDeleteTextures( 1, &Name );
	TextureU = Unity::TTexture();

	return true;
}
#endif


#if defined(ENABLE_OPENGL)
bool TUnityDevice_Opengl::DeleteTexture(Unity::TDynamicTexture& TextureU)
{
	auto Texture = static_cast<Unity::TDynamicTexture_Opengl&>( TextureU );

	TUnityDeviceContextScope Context( *this );
	if ( !Context )
	{
		mDeleteBufferQueue.PushBackUnique( Texture.GetName() );
		TextureU = Unity::TDynamicTexture();
		return true;
	}

	//	gr: need to know if we created this texture or not
	auto Name = Texture.GetName();
	glDeleteBuffers( 1, &Name );
	TextureU = Unity::TDynamicTexture();

	return true;
}
#endif

#if defined(ENABLE_OPENGL)
BufferString<100> OpenglError_ToString(GLenum Error)
{
	switch ( Error )
	{
	case GL_NO_ERROR:			return "GL_NO_ERROR";
	case GL_INVALID_ENUM:		return "GL_INVALID_ENUM";
	case GL_INVALID_VALUE:		return "GL_INVALID_VALUE";
	case GL_INVALID_OPERATION:	return "GL_INVALID_OPERATION";
	case GL_STACK_OVERFLOW:		return "GL_STACK_OVERFLOW";
	case GL_STACK_UNDERFLOW:	return "GL_STACK_UNDERFLOW";
	case GL_OUT_OF_MEMORY:		return "GL_OUT_OF_MEMORY";
	default:
		return BufferString<100>() << "Unknown GL error [" << static_cast<int>(Error) << "]";
	}
}
#endif

#if defined(ENABLE_OPENGL)
bool TUnityDevice_Opengl::HasError()
{
	auto Error = glGetError();
	if ( Error == GL_NO_ERROR )
		return false;

	BufferString<200> Debug;
	Debug << "Opengl error; " << OpenglError_ToString( Error );
	Unity::DebugLog( Debug );
	return true;
}
#endif
	


#if defined(ENABLE_OPENGL)
std::string TUnityDevice_Opengl::GetString(GLenum StringId)
{
	auto* Stringgl = glGetString( StringId );
	if ( !Stringgl )
		return "";
	std::string String = reinterpret_cast<const char*>( Stringgl );
	return String;
}
#endif

#if defined(ENABLE_OPENGL)
void TUnityDevice_Opengl::OnRenderThreadUpdate()
{
	if ( mFirstRun )
	{
		mFirstRun = false;
		auto Error = glewInit();
		if ( Error != GLEW_OK )
		{
			BufferString<100> Debug;
			Debug << "Failed to initalise GLEW: " << Error;
			Unity::DebugError( Debug );
		}

		std::string Debug = "Opengl version ";
		Debug += GetString( GL_VERSION );
		Unity::DebugLog( Debug );
	}
}
#endif