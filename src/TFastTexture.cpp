#include "TFastTexture.h"
#include "UnityDevice.h"



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
	mRef					( Ref ),
	mFrameBuffer			( DEFAULT_MAX_FRAME_BUFFERS, FramePool ),
	mFramePool				( FramePool ),
	mState					( TFastVideoState::FirstFrame )
{
}

TFastTexture::~TFastTexture()
{
	//	wait for render to finish
	ofMutex::ScopedLock Lock( mRenderLock );

	DeleteTargetTexture();
	DeleteUploadThread();
	DeleteDecoderThread();
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
	DeleteUploadThread();
	mDevice.reset();

	//	set new device
	mDevice = Device;
	CreateUploadThread(false);	//	maybe true?
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


void TFastTexture::DeleteDecoderThread()
{
	if ( mDecoderThread )
	{
		mDecoderThread->waitForThread();
		mDecoderThread.reset();
	}
}

bool TFastTexture::CreateUploadThread(bool IsRenderThread)
{
	if ( mUploadThread )
		return true;

    auto& Device = GetDevice();
	if ( !Device.IsValid() )
		return false;

	//	need a target texture first
	if ( !mTargetTexture )
		return false;

	//	don't need it if we have super-fast opengl copying
	//	gr: check type on device!?
#if defined(TARGET_OSX) && defined(ENABLE_OPENGL)
	if ( USE_APPLE_CLIENT_STORAGE && glewIsSupported("GL_APPLE_client_storage") )
	{
		return false;
	}
#endif

	mUploadThread = ofPtr<TFastTextureUploadThread>( new TFastTextureUploadThread( *this, Device ) );
	//	something messed up at init
	if ( !mUploadThread->IsValid() )
	{
		DeleteUploadThread();
		return false;
	}

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

	if ( mDecoderThread )
	{
		mDecoderThread->SetDecodedFrameMeta( TFrameMeta() );
	}	
}


bool TFastTexture::SetTexture(Unity::TTexture TargetTexture)
{
    auto& Device = GetDevice();

	//	release existing texture
	DeleteTargetTexture();

	//	free existing dynamic texture if size/format difference
	DeleteUploadThread();

	{
		auto TextureMeta = Device.GetTextureMeta( TargetTexture );
		BufferString<100> Debug;
		Debug << "Assigning target texture; " << TextureMeta.mWidth << "x" << TextureMeta.mHeight << "x" << TextureMeta.GetChannels();
		Unity::DebugLog( Debug );
	}
	mTargetTexture = TargetTexture;

	//	alloc dynamic texture
	CreateUploadThread(false);
	
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
		//	gr: was dynamic texture format
		TFrameMeta TextureFormat = Device.GetTextureMeta( mTargetTexture );
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
	//	gr: was dynamic texture format
	if ( mTargetTexture )
	{
		TFrameMeta TextureFormat = Device.GetTextureMeta( mTargetTexture );
		mDecoderThread->SetDecodedFrameMeta( TextureFormat );
	}

	return true;
}


bool TFastTexture::UpdateFrameTexture(Unity::TTexture Texture,SoyTime& FrameCopied)
{
	Unity::TScopeTimerWarning Timer(__FUNCTION__,2);

	if ( !Texture )
		return false;

    auto& Device = GetDevice();
	if ( !Device.IsValid() )
		return false;
    
	//	pop latest frame (this takes ownership)
	SoyTime FrameTime = GetFrameTime();
	TFramePixels* pFrame = mFrameBuffer.PopFrame( FrameTime );
	if ( !pFrame )
		return false;

	//	copy to texture
	if ( !Device.CopyTexture( Texture, *pFrame, false ) )
	{
		//	put frame back in queue
		mFrameBuffer.PushFrame( pFrame );
		return false;
	}
	FrameCopied = pFrame->mTimestamp;
	pFrame->SetOwner( __FUNCTION__ );
	
	//	free frame
	mFramePool.Free( pFrame );

	return true;
}

bool TFastTexture::UpdateFrameTexture(Unity::TDynamicTexture Texture,SoyTime& FrameCopied)
{
	Unity::TScopeTimerWarning Timer(__FUNCTION__,2);

	if ( !Texture )
		return false;

    auto& Device = GetDevice();
	if ( !Device.IsValid() )
		return false;
    
	//	pop latest frame (this takes ownership)
	SoyTime FrameTime = GetFrameTime();
	TFramePixels* pFrame = mFrameBuffer.PopFrame( FrameTime );
	if ( !pFrame )
		return false;

	//	copy to texture
	if ( !Device.CopyTexture( Texture, *pFrame, false ) )
	{
		//	put frame back in queue
		mFrameBuffer.PushFrame( pFrame );
		return false;
	}
	FrameCopied = pFrame->mTimestamp;
	pFrame->SetOwner( __FUNCTION__ );
	
	//	free frame
	mFramePool.Free( pFrame );

	return true;
}

void TFastTexture::OnPostRender()
{
	Unity::TScopeTimerWarning Timer(__FUNCTION__,4);
	//ofMutex::ScopedLock RenderLock(mRenderLock);

	bool TargetChanged = false;

	//	somtimes need to create upload thread in the render thread
	if ( !mUploadThread )
		CreateUploadThread(true);

	if ( mUploadThread )
	{
#if defined(FORCE_SINGLE_THREAD_UPLOAD)
		mUploadThread->Update();
#endif

		//	get latest dynamic texture
		Unity::TScopeTimerWarning Timerb( BufferString<100>()<<__FUNCTION__<<"mUploadThread->CopyToTarget",4);
		TargetChanged = mUploadThread->CopyToTarget( mTargetTexture, mTargetTextureFrame );
	}
	else
	{
		//	if we have no upload thread, copy straight to target texture
		Unity::TScopeTimerWarning Timerb( BufferString<100>()<<__FUNCTION__<<"UpdateFrameTexture",4);
		TargetChanged = UpdateFrameTexture( mTargetTexture, mTargetTextureFrame );
	}

	if ( TargetChanged )
		OnTargetTextureChanged();

	if ( TargetChanged && DEBUG_RENDER_LAG )
	{
		auto Now = GetFrameTime();
		auto Lag = Now.GetTime() - mTargetTextureFrame.GetTime();
		static int MinLag = 1;
		if ( Now.IsValid() && Lag >= MinLag )
		{
			BufferString<100> Debug;
			Debug << "Rendering " << Lag << "ms behind";
			Unity::DebugLog( Debug );
		}
	}
}

SoyTime TFastTexture::GetFrameTime()
{
	UpdateFrameTime();

	ofMutex::ScopedLock lockb( mFrame );
	return mFrame.Get();
}

void TFastTexture::OnTargetTextureChanged()
{
	if ( mState == TFastVideoState::FirstFrame )
	{
		mState = TFastVideoState::Playing;
		SetFrameTime( mTargetTextureFrame );
	}
}


void TFastTexture::UpdateFrameTime()
{
	Unity::TScopeTimerWarning Timer(__FUNCTION__,2);
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
//	ofMutex::ScopedLock lockc( mDynamicTextureLock );	//	to avoid deadlock this has to be done first
	ofMutex::ScopedLock locka( mLastUpdateTime );
	ofMutex::ScopedLock lockb( mFrame );

	mFrame.Get() = Frame;
	mLastUpdateTime.Get() = SoyTime(true);

	if ( mDecoderThread )
	{
		mDecoderThread->SetMinTimestamp( mFrame );
	}

//	mDynamicTextureFrame = Frame;	//	-1?


	BufferString<100> Debug;
	Debug << "Set frametime: " << mFrame.Get();
	Unity::DebugLog( Debug );
}



void TFastTextureUploadThread::threadedFunction()
{
	while ( isThreadRunning() )
	{
		sleep(1);

#if !defined(FORCE_SINGLE_THREAD_UPLOAD)
		Update();
#endif
	}
}

void TFastTextureUploadThread::Update()
{
	{
		ofMutex::ScopedLock Lock( mDynamicTextureLock );
		//	last one hasnt been used yet
		if ( mDynamicTextureChanged )
			return;
	}

	//	copy latest
	if ( mParent.UpdateFrameTexture( mDynamicTexture, mDynamicTextureFrame ) )
	{
		//ofMutex::ScopedLock Lock( mDynamicTextureLock );
		mDynamicTextureChanged = true;
	}
}


bool TFastTextureUploadThread::CopyToTarget(Unity::TTexture TargetTexture,SoyTime& TargetTextureFrame)
{
	//	copy latest dynamic texture
	ofMutex::ScopedLock Lock( mDynamicTextureLock );
		
	//	dont' have a new texture yet
	if ( !mDynamicTextureChanged )
		return false;

	//	copy latest
	if ( !mDevice.CopyTexture( TargetTexture, mDynamicTexture ) )
		return false;
	TargetTextureFrame = mDynamicTextureFrame;

	//	latest has been used
	mDynamicTextureChanged = false;

	return true;
}

TFastTextureUploadThread::TFastTextureUploadThread(TFastTexture& Parent,TUnityDevice& Device) :
	SoyThread				( "TFastTextureUploadThread" ),
	mParent					( Parent ),
	mDevice					( Device ),
	mDynamicTextureChanged	( false )
{
	if ( !CreateDynamicTexture() )
	{
		//	shouldn't create this thread if we can't satisfy the requirements 
		assert( false );
	}
}

TFastTextureUploadThread::~TFastTextureUploadThread()
{
	DeleteDynamicTexture();
}

bool TFastTextureUploadThread::CreateDynamicTexture()
{
	auto& Device = GetDevice();
	auto mTargetTexture = mParent.GetTargetTexture();

	//	dynamic texture needs to be same size as target texture
	if ( !mTargetTexture )
	{
		DeleteDynamicTexture();
		return false;
	}

	//	not using dynamic texture
	//	dont bother with a seperate thread if we don't allow out-of-render-thread operations
	if ( !Device.IsValid() )
		return false;


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
		mDynamicTexture = Device.AllocDynamicTexture( TargetTextureMeta );
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
		auto* Frame = mParent.mFramePool.Alloc( TargetTextureMeta, "ENABLE_DYNAMIC_INIT_TEXTURE_COLOUR" );
		if ( Frame )
		{
			Frame->SetColour( ENABLE_DYNAMIC_INIT_TEXTURE_COLOUR );
			Device.CopyTexture( mDynamicTexture, *Frame, true );
			mParent.mFramePool.Free( Frame );
		}
#endif
	}

	/*
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
	*/

	return true;
}

void TFastTextureUploadThread::DeleteDynamicTexture()
{
	ofMutex::ScopedLock lock( mDynamicTextureLock );
    auto& Device = GetDevice();
	Device.DeleteTexture( mDynamicTexture );
}

bool TFastTextureUploadThread::IsValid()
{
	ofMutex::ScopedLock lock( mDynamicTextureLock );
    return mDynamicTexture.IsValid();
}

