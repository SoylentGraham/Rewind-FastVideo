#include "TFastTexture.h"
#include <d3d11.h>


TFastTexture::TFastTexture(SoyRef Ref,TFramePool& FramePool) :
	mRef			( Ref ),
	mFramePool		( FramePool )
{
}

TFastTexture::~TFastTexture()
{
	DeleteTargetTexture();
	DeleteDynamicTexture();
	DeleteDecoderThread();
} 
	
void TFastTexture::DeleteTargetTexture()
{
	mTargetTexture.Release();
}

void TFastTexture::DeleteDynamicTexture()
{
	mDynamicTexture.Release();

	//	update decode-to-format
	if ( mDecoderThread )
	{
		mDecoderThread->SetDecodedFrameMeta( TFrameMeta() );
	}

}

void TFastTexture::DeleteDecoderThread()
{
	if ( mDecoderThread )
	{
		mDecoderThread->waitForThread();
		mDecoderThread.reset();
	}
}

bool TFastTexture::CreateDynamicTexture(TUnityDevice_DX11& Device)
{
	//	dynamic texture needs to be same size as target texture
	if ( !mTargetTexture )
	{
		DeleteDynamicTexture();
		return false;
	}

	auto TargetTextureMeta = Device.GetTextureMeta( mTargetTexture );

	//	if dimensions are different, delete old one
	if ( mDynamicTexture )
	{
		auto CurrentTextureMeta = Device.GetTextureMeta( mDynamicTexture );
		if ( !CurrentTextureMeta.IsEqualSize(TargetTextureMeta) )
		{
			DeleteDynamicTexture();
		}
	}

	//	need to create a new one
	if ( !mDynamicTexture )
	{
		mDynamicTexture = Device.AllocTexture( TargetTextureMeta );
	}

	if ( !mDynamicTexture )
	{
		DeleteDynamicTexture();
		return false;
	}

	//	update decode-to-format
	if ( mDecoderThread )
	{
		TFrameMeta TextureFormat = Device.GetTextureMeta( mDynamicTexture );
		mDecoderThread->SetDecodedFrameMeta( TextureFormat );
	}

	return true;
}

bool TFastTexture::SetTexture(ID3D11Texture2D* TargetTexture,TUnityDevice_DX11& Device)
{
	//	release existing texture
	DeleteTargetTexture();

	//	free existing dynamic texture if size/format difference
	DeleteDynamicTexture();

	mTargetTexture.Set( TargetTexture, true );

	//	alloc dynamic texture
	CreateDynamicTexture( Device );
	
	return true;
}

bool TFastTexture::SetVideo(const std::wstring& Filename,TUnityDevice_DX11& Device)
{
	DeleteDecoderThread();

	//	 alloc new decoder thread
	TDecodeParams Params;
	Params.mFilename = Filename;
	Params.mMaxFrameBuffers = DEFAULT_MAX_FRAME_BUFFERS;
	mDecoderThread = ofPtr<TDecodeThread>( new TDecodeThread( Params, mFramePool ) );

	//	do initial init, will verify filename, dimensions, etc
	if ( !mDecoderThread->Init() )
	{
		DeleteDecoderThread();
		return false;
	}

	//	if we already have the output texture we should set the decode format
	if ( mDynamicTexture )
	{
		TFrameMeta TextureFormat = Device.GetTextureMeta( mDynamicTexture );
		mDecoderThread->SetDecodedFrameMeta( TextureFormat );
	}

	return true;
}

/*
extern "C" void EXPORT_API SetTextureFromUnity(void* texturePtr,const wchar_t* Filename,const int FilenameLength)
{
	// A script calls this at initialization time; just remember the texture pointer here.
	// Will update texture pixels each frame from the plugin rendering event (texture update
	// needs to happen on the rendering thread).
	g_TexturePointer = (ID3D11Texture2D*)texturePtr;
	if ( !g_TexturePointer )
		return;

	
	D3D11_TEXTURE2D_DESC SrcDesc;
	g_TexturePointer->GetDesc (&SrcDesc);
	
	TDecodeParams Params;
	Params.mWidth = SrcDesc.Width;
	Params.mHeight = SrcDesc.Height;
	for ( int i=0;	i<FilenameLength;	i++ )
		Params.mFilename += Filename[i];




	if ( !g_DynamicTexture )
	{
		//	create dynamic texture
		D3D11_TEXTURE2D_DESC Desc;
		memset (&Desc, 0, sizeof(Desc));


		Desc.Width = SrcDesc.Width;
		Desc.Height = SrcDesc.Height;
		Desc.MipLevels = 1;
		Desc.ArraySize = 1;

		Desc.SampleDesc.Count = 1;
		Desc.SampleDesc.Quality = 0;
		Desc.Usage = D3D11_USAGE_DYNAMIC;
		Desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		Desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		Desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		//Desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		
		auto Result = g_D3D11Device->CreateTexture2D( &Desc, NULL, &g_DynamicTexture );
		if ( Result != S_OK )
		{
			Unity::DebugLog("Failed to create dynamic texture");
		}
	}

	//	create decoder thread
	if ( !TFastVideo::DecodeThread )
	{
		TFastVideo::DecodeThread = new TDecodeThread( Params );
		TFastVideo::DecodeThread->start();

		mFramePool.mParams = Params;
	}
}
*/

void TFastTexture::OnPostRender(TUnityDevice_DX11& Device)
{
	//	push latest frame to texture
	//	check we're running
	if ( !mDecoderThread || !mTargetTexture )
		return;

	//	peek to see if we have some video
	if ( !mDecoderThread->HasVideoToPop() )
		return;

	//	might need to setup dynamic texture
	if ( !CreateDynamicTexture(Device) )
		return;

	//	get context
	auto& Device11 = Device.GetDevice();
	TAutoRelease<ID3D11DeviceContext> ctx;
	Device11.GetImmediateContext( &ctx.mObject );
	if ( !ctx )
		return;

	D3D11_TEXTURE2D_DESC TargetDesc;
	mTargetTexture->GetDesc(&TargetDesc);
	D3D11_TEXTURE2D_DESC SrcDesc;
	mDynamicTexture->GetDesc(&SrcDesc);

	//	pop latest frame (this takes ownership)
	TFramePixels* pFrame = mDecoderThread->PopFrame();
	if ( !pFrame )
		return;

	//	update our dynamic texture
	{
		D3D11_MAPPED_SUBRESOURCE resource;
		int SubResource = 0;
		int flags = 0;
		HRESULT hr = ctx->Map( mDynamicTexture, SubResource, D3D11_MAP_WRITE_DISCARD, flags, &resource);
		if ( hr != S_OK )
		{
			BufferString<1000> Debug;
			Debug << "Failed to get Map() for dynamic texture(" << SrcDesc.Width << "," << SrcDesc.Height << "); Error; " << hr;
			Unity::DebugLog( Debug );
			mFramePool.Free( pFrame );
			return;
		}

		int ResourceDataSize = resource.RowPitch * SrcDesc.Height;//	width in bytes

		if ( pFrame->GetDataSize() != ResourceDataSize )
		{
			BufferString<1000> Debug;
			Debug << "Warning: resource/texture data size mismatch; " << pFrame->GetDataSize() << " (frame) vs " << ResourceDataSize << " (resource)";
			Unity::DebugLog( Debug );
			ResourceDataSize = ofMin( ResourceDataSize, pFrame->GetDataSize() );
		}

		//	update contents 
		memcpy( resource.pData, pFrame->GetData(), ResourceDataSize );
		ctx->Unmap( mDynamicTexture, SubResource);
	}

	//	copy to real texture (gpu->gpu)
	//	gr: this will fail if dimensions/format different
	ctx->CopyResource( mTargetTexture, mDynamicTexture );

	//	free frame
	mFramePool.Free( pFrame );
}

