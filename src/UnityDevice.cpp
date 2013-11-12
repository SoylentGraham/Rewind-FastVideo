#include "UnityDevice.h"
#include "FastVideo.h"


#if defined(ENABLE_DX11)
DXGI_FORMAT GetFormat(TFrameMeta Meta)
{
	switch ( Meta.mFormat )
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
TFrameFormat::Type GetFormat(DXGI_FORMAT Format)
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
GLint GetFormat(TFrameMeta Meta)
{
	switch ( Meta.mFormat )
	{
		case TFrameFormat::RGBA:	return GL_RGBA4;
		case TFrameFormat::RGB:		return GL_RGB4;
		default:
			return 0;
	}
}
#endif

#if defined(ENABLE_OPENGL)
TFrameFormat::Type GetFormat(GLint Format)
{
	//	return 0 if none supported
	switch ( Format )
	{
		case GL_RGBA4:	return TFrameFormat::RGBA;
		case GL_RGB4:	return TFrameFormat::RGB;
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
				pDevice = ofPtr<TUnityDevice>( new TUnityDevice_DX11( static_cast<ID3D11Device*>(Device) ) );
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
TUnityDevice_DX11::TUnityDevice_DX11(ID3D11Device* Device) :
	mDevice	( Device, true )
{
}
#endif


#if defined(ENABLE_DX11)
TAutoRelease<ID3D11Texture2D> TUnityDevice_DX11::AllocTexture(TFrameMeta FrameMeta)
{
	if ( !FrameMeta.IsValid() )
		return TAutoRelease<ID3D11Texture2D>();
	if ( !mDevice )
		return TAutoRelease<ID3D11Texture2D>();

	D3D11_TEXTURE2D_DESC Desc;
	memset(&Desc, 0, sizeof(Desc));

	Desc.Width = FrameMeta.mWidth;
	Desc.Height = FrameMeta.mHeight;
	Desc.MipLevels = 1;
	Desc.ArraySize = 1;

	Desc.SampleDesc.Count = 1;
	Desc.SampleDesc.Quality = 0;
	Desc.Usage = D3D11_USAGE_DYNAMIC;
	Desc.Format = GetFormat(FrameMeta);
	Desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	Desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	//Desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		
	ID3D11Texture2D* pTexture = NULL;
	auto Result = mDevice->CreateTexture2D( &Desc, NULL, &pTexture );
	if ( Result != S_OK )
	{
		Unity::DebugLog("Failed to create dynamic texture");
	}
	
	TAutoRelease<ID3D11Texture2D> Texture( pTexture, true );
	return Texture;
}
#endif


#if defined(ENABLE_DX11)
TFrameMeta TUnityDevice_DX11::GetTextureMeta(ID3D11Texture2D* Texture)
{
	if ( !Texture )
		return TFrameMeta();

	D3D11_TEXTURE2D_DESC Desc;
	Texture->GetDesc( &Desc );

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
bool TUnityDevice_DX11::CopyTexture(Unity::TTexture Texture,const TFramePixels& Frame,bool Blocking)
{
	if ( !Texture )
		return false;

	auto& Device11 = GetDevice();

	ofMutex::ScopedLock ContextLock( mContextLock );
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
bool TUnityDevice_DX11::CopyTexture(Unity::TTexture DstTextureU,Unity::TTexture SrcTextureU)
{
    auto* DstTexture = static_cast<Unity::TTexture_DX11&>( DstTexutreU ).GetTexture();
    auto* SrcTexture = static_cast<Unity::TTexture_DX11&>( SrcTexutreU ).GetTexture();
	if ( !DstTexture || !SrcTexture )
		return false;

	auto& Device11 = GetDevice();

	ofMutex::ScopedLock ContextLock( mContextLock );
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
bool Unity::TTexture_Opengl::BindTexture()
{
	if ( !IsValid() )
		return false;
	
	glBindTexture( GL_TEXTURE_2D, mId );
	return true;
}
#endif



#if defined(ENABLE_OPENGL)
TUnityDevice_Opengl::TUnityDevice_Opengl()
{
}
#endif


#if defined(ENABLE_OPENGL)
Unity::TTexture TUnityDevice_Opengl::AllocTexture(TFrameMeta FrameMeta)
{
	if ( !FrameMeta.IsValid() )
		return Unity::TTexture();

	/*
	D3D11_TEXTURE2D_DESC Desc;
	memset(&Desc, 0, sizeof(Desc));
	
	Desc.Width = FrameMeta.mWidth;
	Desc.Height = FrameMeta.mHeight;
	Desc.MipLevels = 1;
	Desc.ArraySize = 1;
	
	Desc.SampleDesc.Count = 1;
	Desc.SampleDesc.Quality = 0;
	Desc.Usage = D3D11_USAGE_DYNAMIC;
	Desc.Format = GetFormat(FrameMeta);
	Desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	Desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	//Desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	
	ID3D11Texture2D* pTexture = NULL;
	auto Result = mDevice->CreateTexture2D( &Desc, NULL, &pTexture );
	if ( Result != S_OK )
	{
		Unity::DebugLog("Failed to create dynamic texture");
	}
	
	TAutoRelease<ID3D11Texture2D> Texture( pTexture, true );
	return Texture;
	 */
	return Unity::TTexture();
}
#endif


#if defined(ENABLE_OPENGL)
TFrameMeta TUnityDevice_Opengl::GetTextureMeta(Unity::TTexture Texture)
{
	auto& TextureGl = static_cast<Unity::TTexture_Opengl&>( Texture );
	if ( !TextureGl.BindTexture() )
		return TFrameMeta();
	
	GLint Width,Height,Formatgl;
	int Lod = 0;
	glGetTexLevelParameteriv( GL_TEXTURE_2D, Lod, GL_TEXTURE_WIDTH, &Width );
	glGetTexLevelParameteriv( GL_TEXTURE_2D, Lod, GL_TEXTURE_HEIGHT, &Height );
	glGetTexLevelParameteriv( GL_TEXTURE_2D, Lod, GL_TEXTURE_INTERNAL_FORMAT, &Formatgl );
	
	auto Format = GetFormat( Formatgl );
	TFrameMeta TextureMeta( Width, Height, Format );
	return TextureMeta;
}
#endif


#if defined(ENABLE_OPENGL)
bool TUnityDevice_Opengl::CopyTexture(Unity::TTexture Texture,const TFramePixels& Frame,bool Blocking)
{
	/*
	GLuint gltex = (GLuint)(size_t)(g_TexturePointer);
	glBindTexture (GL_TEXTURE_2D, gltex);
	int texWidth, texHeight;
	glGetTexLevelParameteriv (GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &texWidth);
	glGetTexLevelParameteriv (GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &texHeight);
	
	unsigned char* data = new unsigned char[texWidth*texHeight*4];
	FillTextureFromCode (texWidth, texHeight, texHeight*4, data);
	glTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0, texWidth, texHeight, GL_RGBA, GL_UNSIGNED_BYTE, data);
	delete[] data;
	/*
	if ( !Texture )
		return false;
	
	auto& Device11 = GetDevice();
	
	ofMutex::ScopedLock ContextLock( mContextLock );
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
	 */
	return false;
}
#endif


#if defined(ENABLE_OPENGL)
bool TUnityDevice_Opengl::CopyTexture(Unity::TTexture DstTextureU,Unity::TTexture SrcTextureU)
{
	/*
	 auto* DstTexture = static_cast<Unity::TTexture_DX11&>( DstTexutreU ).GetTexture();
	 auto* SrcTexture = static_cast<Unity::TTexture_DX11&>( SrcTexutreU ).GetTexture();
	 if ( !DstTexture || !SrcTexture )
	 return false;
	 
	 auto& Device11 = GetDevice();
	 
	 ofMutex::ScopedLock ContextLock( mContextLock );
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
	 */
	return false;
}
#endif


#if defined(ENABLE_OPENGL)
bool TUnityDevice_Opengl::DeleteTexture(Unity::TTexture TextureU)
{
	/*
	 auto* DstTexture = static_cast<Unity::TTexture_DX11&>( DstTexutreU ).GetTexture();
	 auto* SrcTexture = static_cast<Unity::TTexture_DX11&>( SrcTexutreU ).GetTexture();
	 if ( !DstTexture || !SrcTexture )
	 return false;
	 
	 auto& Device11 = GetDevice();
	 
	 ofMutex::ScopedLock ContextLock( mContextLock );
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
	 */
	return false;
}
#endif
