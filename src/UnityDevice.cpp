#include "UnityDevice.h"
#include "FastVideo.h"



DXGI_FORMAT GetFormat(TFrameMeta Meta)
{
	if ( Meta.mChannels == 4 )
		return DXGI_FORMAT_R8G8B8A8_UNORM;

	//	24 bit not supported
	if ( Meta.mChannels == 3 )
		return DXGI_FORMAT_UNKNOWN;

	return DXGI_FORMAT_UNKNOWN;
}

int GetChannels(DXGI_FORMAT Format)
{
	//	return 0 if none supported
	switch ( Format )
	{
	case DXGI_FORMAT_R8G8B8A8_UNORM:
		return 4;
	};

	return 0;
}
	
ofPtr<TUnityDevice_DX11> Unity::AllocDevice(Unity::TGfxDevice::Type Type,void* Device)
{
	//	only support one atm
	ofPtr<TUnityDevice_DX11> pDevice;

	switch ( Type )
	{
		case Unity::TGfxDevice::D3D11:
			if ( Device )
				pDevice = ofPtr<TUnityDevice_DX11>( new TUnityDevice_DX11( static_cast<ID3D11Device*>(Device) ) );
			break;
	};

	return pDevice;
}


TUnityDevice_DX11::TUnityDevice_DX11(ID3D11Device* Device) :
	mDevice	( Device, true )
{
}



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



TFrameMeta TUnityDevice_DX11::GetTextureMeta(ID3D11Texture2D* Texture)
{
	if ( !Texture )
		return TFrameMeta();

	D3D11_TEXTURE2D_DESC Desc;
	Texture->GetDesc( &Desc );

	int Channels = GetChannels( Desc.Format );
	TFrameMeta TextureMeta( Desc.Width, Desc.Height, Channels );
	return TextureMeta;
}

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

