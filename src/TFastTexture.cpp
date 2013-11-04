#include "TFastTexture.h"
#include <d3d11.h>


TFastTexture::TFastTexture(SoyRef Ref,TFramePool& FramePool) :
	mRef			( Ref ),
	mFramePool		( FramePool ),
	mState			( TFastVideoState::Playing )
{
}

TFastTexture::~TFastTexture()
{
	DeleteTargetTexture();
	DeleteDynamicTexture();
	DeleteDecoderThread();
} 

void TFastTexture::SetState(TFastVideoState::Type State)
{
	mState = State;
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
	bool HadTexure = (mDynamicTexture!=nullptr);
	if ( !mDynamicTexture )
	{
		mDynamicTexture = Device.AllocTexture( TargetTextureMeta );
	}

	if ( !mDynamicTexture )
	{
		BufferString<100> Debug;
		Debug << "Failed to alloc dynamic texture; " << TargetTextureMeta.mWidth << "x" << TargetTextureMeta.mHeight << "x" << TargetTextureMeta.mChannels;
		Unity::DebugLog( Debug );

		DeleteDynamicTexture();
		return false;
	}

	//	update decode-to-format
	if ( mDecoderThread )
	{
		TFrameMeta TextureFormat = Device.GetTextureMeta( mDynamicTexture );
		mDecoderThread->SetDecodedFrameMeta( TextureFormat );

		if ( !HadTexure )
		{
			BufferString<100> Debug;
			Debug << "Allocated dynamic texture, set decoded meta; " << TargetTextureMeta.mWidth << "x" << TargetTextureMeta.mHeight << "x" << TargetTextureMeta.mChannels;
			Unity::DebugLog( Debug );
		}
	}

	return true;
}

bool TFastTexture::SetTexture(ID3D11Texture2D* TargetTexture,TUnityDevice_DX11& Device)
{
	//	release existing texture
	DeleteTargetTexture();

	//	free existing dynamic texture if size/format difference
	DeleteDynamicTexture();

	{
		auto TextureMeta = Device.GetTextureMeta( TargetTexture );
		BufferString<100> Debug;
		Debug << "Assigned target texture; " << TextureMeta.mWidth << "x" << TextureMeta.mHeight << "x" << TextureMeta.mChannels;
		Unity::DebugLog( Debug );
	}

	mTargetTexture.Set( TargetTexture, true );

	//	alloc dynamic texture
	CreateDynamicTexture( Device );
	
	return true;
}

bool TFastTexture::SetVideo(const std::wstring& Filename,TUnityDevice_DX11& Device)
{
	//	resume video if paused
	SetState( TFastVideoState::Playing );

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

void TFastTexture::OnPostRender(TUnityDevice_DX11& Device)
{
	//	push latest frame to texture
	//	check we're running
	if ( !mDecoderThread || !mTargetTexture )
		return;

	//	might need to setup dynamic texture
	if ( !CreateDynamicTexture(Device) )
		return;

	//	if paused, don't push new frames
	if ( mState == TFastVideoState::Paused )
		return;

	//	peek to see if we have some video
	if ( !mDecoderThread->HasVideoToPop() )
	{
		//Unity::DebugLog("Waiting for frame...");
		return;
	}

	//	get context
	auto& Device11 = Device.GetDevice();
	TAutoRelease<ID3D11DeviceContext> ctx;
	Device11.GetImmediateContext( &ctx.mObject );
	if ( !ctx )
	{
		Unity::DebugLog("Failed to get device context");
		return;
	}

	D3D11_TEXTURE2D_DESC TargetDesc;
	mTargetTexture->GetDesc(&TargetDesc);
	D3D11_TEXTURE2D_DESC SrcDesc;
	mDynamicTexture->GetDesc(&SrcDesc);

	//	pop latest frame (this takes ownership)
	TFramePixels* pFrame = mDecoderThread->PopFrame();
	if ( !pFrame )
	{
		Unity::DebugLog("Missing decoded frame");
		return;
	}

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

