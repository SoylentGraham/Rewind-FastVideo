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
bool TUnityDevice_DX11::CopyTexture(TAutoRelease<ID3D11Texture2D>& Texture,const TFramePixels& Frame,bool Blocking)
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
bool TUnityDevice_DX11::CopyTexture(TAutoRelease<ID3D11Texture2D>& DstTextureU,TAutoRelease<ID3D11Texture2D>& SrcTextureU)
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


