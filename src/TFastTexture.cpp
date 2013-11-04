#include "TFastTexture.h"
#include <d3d11.h>





const char* TFastVideoState::ToString(TFastVideoState::Type State)
{
	switch ( State )
	{
	case TFastVideoState::FirstFrame:	return "FirstFrame";
	case TFastVideoState::Playing:		return "Playing";
	case TFastVideoState::Paused:		return "Paused";
	default:							return "Unknown state type";
	}
}



TFastTexture::TFastTexture(SoyRef Ref,TFramePool& FramePool) :
	mRef			( Ref ),
	mFrameBuffer	( DEFAULT_MAX_FRAME_BUFFERS, FramePool ),
	mFramePool		( FramePool ),
	mState			( TFastVideoState::FirstFrame )
{
}

TFastTexture::~TFastTexture()
{
	//	wait for render to finish
	ofMutex::ScopedLock Lock( mRenderLock );

	DeleteTargetTexture();
	DeleteDynamicTexture();
	DeleteDecoderThread();
	DeleteUploadThread();
} 

void TFastTexture::SetState(TFastVideoState::Type State)
{
	mState = State;

	if ( mState == TFastVideoState::FirstFrame )
	{
		SetFrameTime( SoyTime() );
	}

	BufferString<100> Debug;
	Debug << GetRef() << " " << TFastVideoState::ToString(State);
	Unity::DebugLog( Debug );
}

	
void TFastTexture::DeleteTargetTexture()
{
	mTargetTexture.Release();
}

void TFastTexture::DeleteDynamicTexture()
{
	ofMutex::ScopedLock lock( mDynamicTextureLock );
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

bool TFastTexture::CreateUploadThread(TUnityDevice_DX11& Device)
{
	if ( mUploadThread )
		return true;

	//	alloc new one
	mUploadThread = ofPtr<TFastTextureUploadThread>( new TFastTextureUploadThread( *this, Device ) );
	mUploadThread->startThread( true, true );
	return true;
}

void TFastTexture::DeleteUploadThread()
{
	if ( mUploadThread )
	{
		mUploadThread->waitForThread();
		mUploadThread.reset();
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
	ofMutex::ScopedLock lock( mDynamicTextureLock );
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

	CreateUploadThread( Device );
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
	CreateUploadThread( Device );
	
	return true;
}

bool TFastTexture::SetVideo(const std::wstring& Filename,TUnityDevice_DX11& Device)
{
	//	reset video state
	SetState( TFastVideoState::FirstFrame );
	SetFrameTime( SoyTime() );

	DeleteDecoderThread();

	//	 alloc new decoder thread
	TDecodeParams Params;
	Params.mFilename = Filename;
	mDecoderThread = ofPtr<TDecodeThread>( new TDecodeThread( Params, mFrameBuffer, mFramePool ) );

	//	do initial init, will verify filename, dimensions, etc
	if ( !mDecoderThread->Init() )
	{
		DeleteDecoderThread();
				
#if defined(ENABLE_FAILED_DECODER_INIT_FRAME)
		//	push a red "BAD" frame
		TFrameMeta TextureFormat = Device.GetTextureMeta( mDynamicTexture );
		TFramePixels* Frame = mFramePool.Alloc( TextureFormat, "Debug failed init" );
		if ( Frame )
		{
			Unity::DebugLog( BufferString<100>()<<"Pushing Debug ERROR Frame; " << __FUNCTION__ );
			Frame->SetColour( ENABLE_FAILED_DECODER_INIT_FRAME );
			mFrameBuffer.PushFrame( Frame );	
		}
		else
		{
			BufferString<100> Debug;
			Debug << "failed to alloc debug Init frame";
			Unity::DebugLog( Debug );
		}
#endif

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

bool TFastTexture::UpdateDynamicTexture(TUnityDevice_DX11& Device)
{
	ofScopeTimerWarning Timer(__FUNCTION__,2);

	//	push latest frame to texture
	if ( !mTargetTexture )
		return false;

	//	might need to setup dynamic texture still
	if ( !CreateDynamicTexture(Device) )
		return false;

	//	dynamic texture is in use
	ofMutex::ScopedLock Lock( mDynamicTextureLock );

	//	pop latest frame (this takes ownership)
	SoyTime FrameTime = GetFrameTime();
	TFramePixels* pFrame = mFrameBuffer.PopFrame( FrameTime );
	if ( !pFrame )
		return false;
	
	if ( !Device.CopyTexture( mDynamicTexture, *pFrame, false ) )
	{
		mFrameBuffer.PushFrame( pFrame );
		return false;
	}
	mDynamicTextureChanged = true;

	pFrame->SetOwner( __FUNCTION__ );
	OnDynamicTextureChanged( FrameTime );
	
	//	free frame
	mFramePool.Free( pFrame );


	return true;
}

void TFastTexture::OnPostRender(TUnityDevice_DX11& Device)
{
	ofScopeTimerWarning Timer(__FUNCTION__,2);
	ofMutex::ScopedLock RenderLock(mRenderLock);

	//	skip if dynamic texture is in use
	//	gr: or dont? and stall unity?
	ofMutex::ScopedLock Lock(mDynamicTextureLock);

	//	no changes
	if ( !mDynamicTextureChanged )
		return;

	//	copy to GPU for usage
	if ( !Device.CopyTexture( mTargetTexture, mDynamicTexture ) )
		return;
	mDynamicTextureChanged = false;

	return;
}

SoyTime TFastTexture::GetFrameTime()
{
	UpdateFrameTime();

	ofMutex::ScopedLock lockb( mFrame );
	return mFrame.Get();
}

void TFastTexture::OnDynamicTextureChanged(SoyTime Timestamp)
{
	if ( mState == TFastVideoState::FirstFrame )
	{
		mState = TFastVideoState::Playing;
		SetFrameTime( Timestamp );
	}
}


void TFastTexture::UpdateFrameTime()
{
	ofScopeTimerWarning Timer(__FUNCTION__,2);
	ofMutex::ScopedLock locka( mLastUpdateTime );
	//	get step
	SoyTime Now(true);
	auto Step = Now.GetTime() - mLastUpdateTime.Get().GetTime();
	if ( Step <= 0 )
		return;

	//	set the last-update-time regardless
	mLastUpdateTime.Get() = Now;
	
	//	if we're paused, dont move frame time on
	if ( mState == TFastVideoState::Paused )
		return;

	//	if we're waiting for the first frame, we don't step until we've rendered it
	if ( mState == TFastVideoState::FirstFrame )
		return;
	
	ofMutex::ScopedLock lockb( mFrame );
	mFrame.Get() = SoyTime( mFrame.GetTime() + Step );

	if ( mDecoderThread )
	{
		mDecoderThread->SetMinTimestamp( mFrame );
	}
	/*
	BufferString<100> Debug;
	Debug << "Framestep: " << Step << "ms";
	Unity::DebugLog( Debug );
	*/
}


void TFastTexture::SetFrameTime(SoyTime Frame)
{
	ofMutex::ScopedLock locka( mLastUpdateTime );
	ofMutex::ScopedLock lockb( mFrame );

	mFrame.Get() = Frame;
	mLastUpdateTime.Get() = SoyTime(true);

	if ( mDecoderThread )
	{
		mDecoderThread->SetMinTimestamp( mFrame );
	}

	BufferString<100> Debug;
	Debug << "Set frametime: " << mFrame.Get();
	Unity::DebugLog( Debug );
}



void TFastTextureUploadThread::threadedFunction()
{
	while ( isThreadRunning() )
	{
		sleep(1);

		//	update texture
		mParent.UpdateDynamicTexture( mDevice );
	}
}

