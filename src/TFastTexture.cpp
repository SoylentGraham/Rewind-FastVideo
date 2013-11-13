#include "TFastTexture.h"

#if defined(ENABLE_DX11)
#include <d3d11.h>
#endif




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

TUnityDevice& TFastTexture::GetDevice()
{
    static TUnityDevice_Dummy DummyDevice;
    if ( !mDevice )
    {
        Unity::DebugLog("Device expected");
        return DummyDevice;
    }
    return *mDevice;
}

void TFastTexture::SetDevice(ofPtr<TUnityDevice> Device)
{
	//	unset old device
	DeleteTargetTexture();
	DeleteDynamicTexture();
	mDevice.reset();

	//	set new device
	mDevice = Device;
	CreateDynamicTexture();
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
    auto& Device = GetDevice();
	Device.DeleteTexture( mTargetTexture );
}

void TFastTexture::DeleteDynamicTexture()
{
	ofMutex::ScopedLock lock( mDynamicTextureLock );
    auto& Device = GetDevice();
	Device.DeleteTexture( mDynamicTexture );

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

bool TFastTexture::CreateUploadThread()
{
	if ( mUploadThread )
		return true;

	//	alloc new one
    auto& Device = GetDevice();
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


bool TFastTexture::CreateDynamicTexture()
{
    auto& Device = GetDevice();
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
	bool HadTexure = (mDynamicTexture);
	if ( !mDynamicTexture )
	{
		mDynamicTexture = Device.AllocTexture( TargetTextureMeta );
		if ( !mDynamicTexture )
		{
			BufferString<100> Debug;
			Debug << "Failed to alloc dynamic texture; " << TargetTextureMeta.mWidth << "x" << TargetTextureMeta.mHeight << "x" << TargetTextureMeta.GetChannels();
			Unity::DebugLog( Debug );

			DeleteDynamicTexture();
			return false;
		}

		//	initialise texture contents
#if defined(ENABLE_DYNAMIC_INIT_TEXTURE_COLOUR)
		auto* Frame = mFramePool.Alloc( TargetTextureMeta, "ENABLE_DYNAMIC_INIT_TEXTURE_COLOUR" );
		if ( Frame )
		{
			Frame->SetColour( ENABLE_DYNAMIC_INIT_TEXTURE_COLOUR );
			Device.CopyTexture( mDynamicTexture, *Frame, true );
			mFramePool.Free( Frame );
		}
#endif
	}

	//	update decode-to-format
	if ( mDecoderThread )
	{
		TFrameMeta TextureFormat = Device.GetTextureMeta( mDynamicTexture );
		mDecoderThread->SetDecodedFrameMeta( TextureFormat );

		if ( !HadTexure )
		{
			BufferString<100> Debug;
			Debug << "Allocated dynamic texture, set decoded meta; " << TargetTextureMeta.mWidth << "x" << TargetTextureMeta.mHeight << "x" << TargetTextureMeta.GetChannels();
			Unity::DebugLog( Debug );
		}
	}

	CreateUploadThread();
	return true;
}

bool TFastTexture::SetTexture(Unity::TTexture TargetTexture)
{
    auto& Device = GetDevice();

	//	release existing texture
	DeleteTargetTexture();

	//	free existing dynamic texture if size/format difference
	DeleteDynamicTexture();

	{
		auto TextureMeta = Device.GetTextureMeta( TargetTexture );
		BufferString<100> Debug;
		Debug << "Assigning target texture; " << TextureMeta.mWidth << "x" << TextureMeta.mHeight << "x" << TextureMeta.GetChannels();
		Unity::DebugLog( Debug );
	}

	mTargetTexture = TargetTexture;

	//	alloc dynamic texture
	CreateDynamicTexture();
	CreateUploadThread();
	
	//	pre-alloc pool
	TFrameMeta FrameMeta = Device.GetTextureMeta( mTargetTexture );
	mFramePool.PreAlloc( FrameMeta );

	return true;
}

bool TFastTexture::SetVideo(const std::wstring& Filename)
{
    auto& Device = GetDevice();

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

bool TFastTexture::UpdateDynamicTexture()
{
	ofScopeTimerWarning Timer(__FUNCTION__,2);

    auto& Device = GetDevice();
    
	//	push latest frame to texture
	if ( !mTargetTexture )
		return false;

	//	might need to setup dynamic texture still
	if ( !CreateDynamicTexture() )
		return false;

	{
		ofMutex::ScopedLock Lock( mDynamicTextureLock );
		//	last one hasnt been used yet
		if ( mDynamicTextureChanged )
			return true;
	}

	//	pop latest frame (this takes ownership)
	SoyTime FrameTime = GetFrameTime();
	TFramePixels* pFrame = mFrameBuffer.PopFrame( FrameTime );
	if ( !pFrame )
		return false;
	
	bool Copy = true;

	ofMutex::ScopedLock Lock( mDynamicTextureLock );
	if ( pFrame->mTimestamp < mDynamicTextureFrame )
	{
		BufferString<100> Debug;
		Debug << "New dynamic texture frame ("<< pFrame->mTimestamp << ") BEHIND current frame (" << mDynamicTextureFrame << ")";
		Unity::DebugLog( Debug );

		if ( DYNAMIC_SKIP_OOO_FRAMES )
			Copy = false;
	}

	if ( Copy )
	{
		if ( !Device.CopyTexture( mDynamicTexture, *pFrame, false ) )
		{
			mFrameBuffer.PushFrame( pFrame );
			return false;
		}

		mDynamicTextureChanged = true;
		mDynamicTextureFrame = pFrame->mTimestamp;
	}

	pFrame->SetOwner( __FUNCTION__ );
	OnDynamicTextureChanged( FrameTime );
	
	/*
	BufferString<100> Debug;
	Debug << "Frame " << pFrame->mTimestamp << " Buffer -> Dynamic";
	ofLogNotice( Debug.c_str() );
	*/

	//	free frame
	mFramePool.Free( pFrame );


	return true;
}

void TFastTexture::OnPostRender()
{
	ofScopeTimerWarning Timer(__FUNCTION__,2);
	ofMutex::ScopedLock RenderLock(mRenderLock);

	//	skip if dynamic texture is in use
	//	gr: or dont? and stall unity?
	ofMutex::ScopedLock Lock(mDynamicTextureLock);

	if ( !ALWAYS_COPY_DYNAMIC_TO_TARGET )
	{
		//	no changes
		if ( !mDynamicTextureChanged )
			return;
	}

	if ( DEBUG_RENDER_LAG )
	{
		auto Now = GetFrameTime();
		auto Lag = Now.GetTime() - mDynamicTextureFrame.GetTime();
		static int MinLag = 1;
		if ( Now.IsValid() && Lag >= MinLag )
		{
			BufferString<100> Debug;
			Debug << "Rendering " << Lag << "ms behind";
			Unity::DebugLog( Debug );
		}
	}


	//	copy to GPU for usage
    auto& Device = GetDevice();
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
	float Stepf = static_cast<float>( Step ) * REAL_TIME_MODIFIER;
	Step = static_cast<uint64>( Stepf );
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
	ofMutex::ScopedLock lockc( mDynamicTextureLock );	//	to avoid deadlock this has to be done first
	ofMutex::ScopedLock locka( mLastUpdateTime );
	ofMutex::ScopedLock lockb( mFrame );

	mFrame.Get() = Frame;
	mLastUpdateTime.Get() = SoyTime(true);

	if ( mDecoderThread )
	{
		mDecoderThread->SetMinTimestamp( mFrame );
	}

	mDynamicTextureFrame = Frame;	//	-1?


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
		mParent.UpdateDynamicTexture();
	}
}

