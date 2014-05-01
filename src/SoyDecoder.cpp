#include "SoyDecoder.h"
#include <SortArray.h>

#define MAX_DECODER_FRAMESKIP	25
#define MIN_DECODER_FRAMESKIP	0	//	debug, shouldnt ever have a min frameskip!



FastVideoError TDecodeInitResult::GetFastVideoError(TDecodeInitResult::Type Result)
{
	switch ( Result )
	{
	case TDecodeInitResult::UnknownError:	return FastVideoError::DecoderError;
	case TDecodeInitResult::FileNotFound:	return FastVideoError::FileNotFound;
	case TDecodeInitResult::CodecError:		return FastVideoError::CodecError;

	case TDecodeInitResult::Success:	
	default:
		//	not an error!
		assert( false );
		return FastVideoError::UknownError;
	}
}


FastVideoError TDecodeState::GetFastVideoError(TDecodeState::Type State)
{
	switch ( State )
	{
	case TDecodeState::FailedInit_Unknown:		return FastVideoError::DecoderError;
	case TDecodeState::FailedInit_FileNotFound:	return FastVideoError::FileNotFound;
	case TDecodeState::FailedInit_CodecError:	return FastVideoError::CodecError;

	default:
		//	not an error!
		assert( false );
		return FastVideoError::UknownError;
	}
}



#if defined(ENABLE_DECODER_TEST)
TDecoder_Test::TDecoder_Test() :
	mCurrentColour(0)
{
	mColours.PushBack( TColour(255,0,0) );
	mColours.PushBack( TColour(255,255,0) );
	mColours.PushBack( TColour(255,0,255) );

	mColours.PushBack( TColour(0,255,0) );
	mColours.PushBack( TColour(0,255,255) );

	mColours.PushBack( TColour(0,0,255) );
}
#endif


#if defined(ENABLE_DECODER_TEST)
TDecodeInitResult::Type TDecoder_Test::Init(const TDecodeParams& Params)
{
	//	debug test to see if a big init pause locks up unity
	//Sleep(4000);

	//	delayed failure test
	return TDecodeInitResult::UnknownError;
}
#endif


#if defined(ENABLE_DECODER_TEST)
bool TDecoder_Test::PeekNextFrame(TFrameMeta& FrameMeta)
{
	FrameMeta = TFrameMeta( 4096, 2048, TFrameFormat::BGRA );
	return true;
}
#endif


#if defined(ENABLE_DECODER_TEST)
bool TDecoder_Test::DecodeNextFrame(TFramePixels& OutFrame,SoyTime MinTimestamp,bool& TryAgain)
{
	OutFrame.SetColour( mColours[mCurrentColour%mColours.GetSize()] );
	mCurrentColour++;
	return true;
}
#endif




	
class TSortPolicy_TFramePixelsByTimestamp
{
public:
	static int		Compare(const TFramePixels* a,const TFramePixels* b)
	{
		auto Framea = a->mTimestamp;
		auto Frameb = b->mTimestamp;

		if ( Framea < Frameb ) return -1;
		if ( Framea > Frameb ) return 1;
		return 0;
	}
};



BufferString<1000> GetAVError(int Error)
{
#if defined(ENABLE_DECODER_LIBAV)
	char Buffer[1000];
	if ( av_strerror( Error, Buffer, sizeof(Buffer)/sizeof(Buffer[0]) ) != 0 )
	{
		BufferString<1000> Debug;
		Debug << "Unknown error: " << Error;
		return Debug;
	}
	return Buffer;
#else
	return "AV not enabled";
#endif
}

#if defined(ENABLE_DECODER_LIBAV)
bool TPacket::reset(AVFormatContext* ctxt)
{
	//	gr: av_read_frame free's...
	/*
	if (packet.data)
	{
		av_free_packet(&packet);
		packet.data = nullptr;
	}
	*/
	auto err = av_read_frame(ctxt, &packet);
	if ( err < 0 )
	{
		BufferString<1000> Debug;
		Debug << "Error reading next packet; " << GetAVError( err );
		Unity::DebugError(Debug);

		packet.data = nullptr;
		return false;
	}
	return true;
}
#endif


#if defined(ENABLE_DECODER_LIBAV)
TFrameFormat::Type GetFormat(enum AVPixelFormat Format)
{
	switch ( Format )
	{
	case AV_PIX_FMT_RGB24:		return TFrameFormat::RGB;	
	case AV_PIX_FMT_RGBA:		return TFrameFormat::RGBA;
	case AV_PIX_FMT_YUYV422:	return TFrameFormat::YUV;
	default:					return TFrameFormat::Invalid;
	};
}
#endif


#if defined(ENABLE_DECODER_LIBAV)
enum AVPixelFormat GetFormat(const TFrameMeta& FrameMeta)
{
	switch ( FrameMeta.mFormat )
	{
	case TFrameFormat::RGB:		return AV_PIX_FMT_RGB24;
	case TFrameFormat::RGBA:	return AV_PIX_FMT_RGBA;
	case TFrameFormat::YUV:		return AV_PIX_FMT_YUYV422;
	default:
		return PIX_FMT_NONE;
	}
}
#endif

TFrameBuffer::TFrameBuffer(int MaxFrameBufferSize,TFramePool& FramePool) :
	mMaxFrameBufferSize	( ofMax(MaxFrameBufferSize,1) ),
	mFramePool			( FramePool )
{
}
	
TFrameBuffer::~TFrameBuffer()
{
	ReleaseFrames();
}

bool TFrameBuffer::IsFull()
{
	ofMutex::ScopedLock Lock( mFrameMutex );
	if ( mFrameBuffers.GetSize() >= mMaxFrameBufferSize )
		return true;
	return false;
}


TDecodeThread::TDecodeThread(TDecodeParams& Params,TFrameBuffer& FrameBuffer,TFramePool& FramePool) :
	SoyThread			( "TDecodeThread" ),
	mFrameBuffer		( FrameBuffer ),
	mFramePool			( FramePool ),
	mParams				( Params ),
	mState				( TDecodeState::NoThread )
{
	Unity::Debug(__FUNCTION__);
}

TDecodeThread::~TDecodeThread()
{
	Unity::TScopeTimerWarning Timer( __FUNCTION__, 1 );

	Unity::Debug("~TDecodeThread release frames");
	mFrameBuffer.ReleaseFrames();

	Unity::Debug("~TDecodeThread WaitForThread");
	waitForThread();

	Unity::Debug("~TDecodeThread finished");
}


TDecodeInitResult::Type TDecodeThread::Init()
{
	Unity::TScopeTimerWarning Timer(__FUNCTION__, 4);

	//	alloc decoder
#if defined(ENABLE_DECODER_TEST)
	if (!mDecoder && USE_TEST_DECODER )
		mDecoder = ofPtr<TDecoder>( new TDecoder_Test() );
#endif

#if defined(ENABLE_DECODER_LIBAV)
	if ( !mDecoder )
		mDecoder = ofPtr<TDecoder>( new TDecoder_Libav() );
#endif
	
#if defined(ENABLE_DECODER_QTKIT)
	if ( !mDecoder )
		mDecoder = ofPtr<TDecoder>( new TDecoder_Qtkit() );
#endif
	
#if defined(ENABLE_DECODER_TEST)
	if ( !mDecoder )
		mDecoder = ofPtr<TDecoder>( new TDecoder_Test() );
#endif
	
	if ( !mDecoder )
	{
		mState = TDecodeState::NoThread;
		return TDecodeInitResult::UnknownError;
	}
	mState = TDecodeState::Constructed;

	//	push a clean-frame before we start the thread
	PushInitFrame();

	Unity::Debug("TDecodeThread - starting thread");
	startThread( true, true );
	Unity::Debug("TDecodeThread - starting thread OK");
	return TDecodeInitResult::Success;
}

bool TDecodeThread::HasFinishedDecoding() const
{
	switch ( mState )
	{
	case TDecodeState::FinishedDecoding:
	case TDecodeState::FailedInit_FileNotFound:
	case TDecodeState::FailedInit_Unknown:
	case TDecodeState::FailedInit_CodecError:
		return true;
	}
	return false;
}


bool TDecodeThread::HasFailedInitialisation() const
{
	switch ( mState )
	{
	case TDecodeState::FailedInit_FileNotFound:
	case TDecodeState::FailedInit_Unknown:
	case TDecodeState::FailedInit_CodecError:
		return true;
	}
	return false;
}


void TFrameBuffer::ReleaseFrames()
{
	ofMutex::ScopedLock lock( mFrameMutex );

	//	free frames back to pool
	for ( int i=mFrameBuffers.GetSize()-1;	i>=0;	i-- )
	{
		auto Frame = mFrameBuffers.PopAt(i);
		mFramePool.Free( Frame );
	}
}

bool TFrameBuffer::HasVideoToPop()
{
	ofMutex::ScopedLock lock( mFrameMutex );
	return !mFrameBuffers.IsEmpty();
}


TFramePixels* TFrameBuffer::PopFrame(SoyTime Timestamp)
{
	Unity::TScopeTimerWarning Timer(__FUNCTION__,2);
	ofMutex::ScopedLock lock( mFrameMutex );

	if ( mFrameBuffers.GetSize() < FORCE_BUFFER_FRAME_COUNT )
		return nullptr;

	//	work out next frame we want to use
	int PopFrameIndex = -1;

	for ( int i=0;	i<mFrameBuffers.GetSize();	i++ )
	{
		auto& FrameBuffer = *mFrameBuffers[i];
		auto FrameTimestamp = FrameBuffer.mTimestamp;

		//	in the future? break out of the loop and don't use i
		if ( FrameTimestamp > Timestamp && Timestamp.IsValid() )
			break;

		//	past (or equal) so use this one
		PopFrameIndex = i;
		continue;
	}

	//	skipping some frames
	int SkipCount = PopFrameIndex;
	if ( SkipCount > 0 && SKIP_PAST_FRAMES )
	{
		BufferString<100> Debug;
		Debug << "Skipping " << SkipCount << " frames";
		Unity::DebugDecodeLag(Debug);


		//	pop & release the first X frames we're going to skip
		for ( int i=0;	i<SkipCount;	i++ )
		{
			auto* Frame = mFrameBuffers.PopAt(0);
			
			BufferString<100> Debug;
			Debug << "Frame " << Frame->mTimestamp << " skipped";
			ofLogNotice( Debug.c_str() );

			mFramePool.Free( Frame );
			PopFrameIndex--;
		}
		assert( PopFrameIndex == 0 );
	}

	//	nothing to use
	if ( PopFrameIndex == -1 )
		return NULL;

	TFramePixels* PoppedFrame = mFrameBuffers.PopAt(0);
	return PoppedFrame;
}


void TDecodeThread::threadedFunction()
{
	assert( mState == TDecodeState::Constructed );
	
	//	init
	TDecodeInitResult::Type InitResult = TDecodeInitResult::UnknownError;
	if ( mDecoder )
	{
		InitResult = mDecoder->Init( mParams );
	}

	//	set state
	switch ( InitResult )
	{
	case TDecodeInitResult::Success:		mState = TDecodeState::Decoding;				break;
	case TDecodeInitResult::FileNotFound:	mState = TDecodeState::FailedInit_FileNotFound;	break;
	case TDecodeInitResult::CodecError:		mState = TDecodeState::FailedInit_CodecError;	break;

	case TDecodeInitResult::UnknownError:
	default:
		mState = TDecodeState::FailedInit_Unknown;
		break;
	}


	while ( isThreadRunning() && mState == TDecodeState::Decoding )
	{
        ofThread::sleep(1);

		//	if buffer is filled, stop (don't buffer too many frames)
		bool DoDecode = !mFrameBuffer.IsFull();

		if ( !DoDecode )
			continue;
		
		DecodeNextFrame();
	}
}

void TFrameBuffer::PushFrame(TFramePixels* pFrame)
{
	Unity::TScopeTimerWarning Timer(__FUNCTION__,2);
	ofMutex::ScopedLock lock( mFrameMutex );

	//	sort by time, it's possible we get frames out of order due to encoding
	auto SortedFrameBuffers = GetSortArray( mFrameBuffers, TSortPolicy_TFramePixelsByTimestamp() );
	SortedFrameBuffers.Push( pFrame );
}

TFrameMeta TDecodeThread::GetDecodedFrameMeta()
{
	ofMutex::ScopedLock lock( mDecodeFormat );
	return mDecodeFormat;
}
	
	
void TDecodeThread::SetDecodedFrameMeta(TFrameMeta Format)
{
	ofMutex::ScopedLock lock( mDecodeFormat );

	//	no change
	if ( mDecodeFormat.Get() == Format )
		return;

	//	update
	mDecodeFormat.Get() = Format;

#if defined(ENABLE_DECODER_LIBAV_INIT_SIZE_FRAME)
	//	push a green "OK" frame
	TFramePixels* Frame = mFramePool.Alloc( GetDecodedFrameMeta(), "Debug init" );
	if ( Frame )
	{
		Unity::DebugLog( BufferString<100>()<<"Pushing Debug Frame; " << __FUNCTION__ );
		Frame->SetColour( ENABLE_DECODER_LIBAV_INIT_SIZE_FRAME );
		mFrameBuffer.PushFrame( Frame );	
	}
	else
	{
		BufferString<100> Debug;
		Debug << "failed to alloc debug Init frame";
		Unity::DebugLog( Debug );
	}
#endif

}

SoyTime TDecodeThread::GetMinTimestamp()
{
	ofMutex::ScopedLock Lock( mMinTimestamp );
	return mMinTimestamp;
}

void TDecodeThread::SetMinTimestamp(SoyTime Timestamp)
{
	ofMutex::ScopedLock Lock( mMinTimestamp );
	mMinTimestamp.Get() = Timestamp;
}

void TDecodeThread::PushInitFrame()
{
#if defined(ENABLE_DECODER_INIT_FRAME)
	Unity::TScopeTimerWarning Timer( __FUNCTION__, 2 );

	//	alloc a frame
	//	dont know decoder dimensions yet, so use video meta
	TFramePixels* Frame = mFramePool.Alloc( mParams.mTargetTextureMeta, __FUNCTION__ );

	//	out of memory/pool, or non-valid format (ie. dont know output dimensions yet)
	if ( !Frame )
		return;

	Frame->SetColour( ENABLE_DECODER_INIT_FRAME );
	mFrameBuffer.PushFrame( Frame );
#endif
}

bool TDecodeThread::DecodeNextFrame()
{
	if ( !mDecoder )
		return false;

	Unity::TScopeTimerWarning Timer( "thread DecodeNextFrame", 1 );

	//	alloc a frame
	TFramePixels* Frame = mFramePool.Alloc( GetDecodedFrameMeta(), __FUNCTION__ );

	//	out of memory/pool, or non-valid format (ie. dont know output dimensions yet)
	if ( !Frame )
		return false;

	bool TryAgain = true;
	while ( TryAgain )
	{
		SoyTime MinTimestamp = GetMinTimestamp();

		//	success!
		if ( mDecoder->DecodeNextFrame( *Frame, MinTimestamp, TryAgain ) )
			break;
		
		//	failed, and failed hard
		if ( !TryAgain )
		{
			mState = TDecodeState::FinishedDecoding;
			mFramePool.Free( Frame );
			return false;
		}
	}

	/*
	BufferString<100> Debug;
	Debug << Frame->mTimestamp << " decoded -> framebuffer";
	Unity::DebugLog( Debug );
	*/
	mFrameBuffer.PushFrame( Frame );

	return true;
}


#if defined(ENABLE_DECODER_LIBAV)
TDecoder_Libav::TDecoder_Libav() :
	mScaleContext	( nullptr ),
	mVideoStream	( nullptr ),
	mDataOffset		( 0 )
{
	//	initialise dxvacontext
#if defined(ENABLE_DVXA)
	ZeroMemory( &mDxvaContext, sizeof(mDxvaContext) );
#endif
}
#endif


#if defined(ENABLE_DECODER_LIBAV)
TDecoder_Libav::~TDecoder_Libav()
{
	sws_freeContext( mScaleContext );
	mScaleContext = nullptr;

#if defined(ENABLE_DVXA)
	FreeDxvaContext();
#endif
}
#endif

#if defined(ENABLE_DVXA)
bool TDecoder::InitDxvaContext()
{
	//	already initialised
	if ( mDxvaContext.decoder )
		return true;
	DXVA2CreateVideoService (
	GUID* GuidResults = nullptr;
	UINT GuidCount = 0;
	auto Result = IDirectXVideoDecoderService::GetDecoderDeviceGuids( &GuidCount, &GuidResults );
	Array<GUID> Guids = GetRemoteArray( GuidResults, GuidCount, GuidCount );
	CoTaskMemFree( GuidResults );
	if ( FAILED( Result ) )
		return false;
	/*
	REFGUID Guid;
	DXVA2_VideoDesc VideoDesc;
	DXVA2_ConfigPictureDecode DecoderConfig;
	//IDirectXVideoDecoderService::CreateVideoDecoder( Guid, &VideoDesc, &DecoderConfig );
	*/
	return false;
}
#endif


#if defined(ENABLE_DVXA)
void TDecoder::FreeDxvaContext()
{
}
#endif




#if defined(ENABLE_DECODER_LIBAV)
bool TDecoder_Libav::DecodeNextFrame(TFrameMeta& FrameMeta,TPacket& CurrentPacket,std::shared_ptr<AVFrame>& Frame,int& DataOffset,int SkipFrames,int64_t MinPts)
{
	Unity::TScopeTimerWarning Timer( __FUNCTION__, 1 );

	//	not setup right??
	if ( !mVideoStream )
		return false;

	Unity::TScopeTimerWarning DecodeTimer( "avcodec_decode_video2", 1, false );

	//	keep processing until a frame is loaded
	int isFrameAvailable = false;
	while ( !isFrameAvailable )
	{
		bool SkippingThisFrame = (SkipFrames>0);

		// reading a packet using libavformat
		if ( DataOffset >= CurrentPacket.packet.size) 
		{
			//	keep fetching [valid] packets until we find our stream
			while ( true )
			{
				//	if we failed to read next packet, we're out of frames
				if ( !CurrentPacket.reset( mContext.get() ) )
					return false;
				
				//	not our stream, skip
				if ( CurrentPacket.packet.stream_index != mVideoStream->index )
					continue;

				//	looking for min pts
				if ( MinPts != 0 && CurrentPacket.packet.pts < MinPts )
					continue;

				//	found our packet to decode
				break;
			}
		}

		auto& packetToSend = CurrentPacket.packet;
	
		// sending data to libavcodec
		DecodeTimer.Start();
		const auto processedLength = avcodec_decode_video2( mCodec.get(), Frame.get(), &isFrameAvailable, &packetToSend );
		DecodeTimer.Stop();
		if (processedLength < 0) 
		{
			//av_free_packet(&packet);
			Unity::Debug("Error while processing packet data");
			return false;
		}
		DataOffset += processedLength;

		//	keep processing if not availible yet
		if ( !isFrameAvailable )
			continue;
	
		//	frame is availible, return it's info
		FrameMeta = TFrameMeta( Frame->width, Frame->height, GetFormat(static_cast<AVPixelFormat>(Frame->format) ) );
		
		if ( SkippingThisFrame )
		{
			isFrameAvailable = false;
			SkipFrames--;
			continue;
		}
		return true;
	}

	//	kept processing until we ran out of data without a result
	return false;
}
#endif

#if defined(ENABLE_DECODER_LIBAV)
bool TDecoder_Libav::PeekNextFrame(TFrameMeta& FrameMeta)
{
	//	not setup right??
	if ( !mVideoStream )
		return false;

	int peekDataOffset = mDataOffset;
	TPacket PeekCurrentPacket;
	std::shared_ptr<AVFrame> peekFrame = std::shared_ptr<AVFrame>(avcodec_alloc_frame(), &av_free);
	int SkipFrames = 0;
	int64_t MinPts = 0;
	if ( !DecodeNextFrame( FrameMeta, PeekCurrentPacket, peekFrame, peekDataOffset, SkipFrames, MinPts ) )
		return false;

	return true;
}
#endif

int64 MsToPts(SoyTime Time,double TimeBase,double FrameRate)
{
	double TimeMs = Time.GetTime();
	double TimeSecs = TimeMs / 1000.0;
	double Timestamp = TimeSecs / TimeBase;
	return Timestamp;
}

SoyTime PtsToMs(int64 Pts,double TimeBase,double FrameRate)
{
	assert( Pts != AV_NOPTS_VALUE );
	double TimeSecs = TimeBase * Pts;
	double TimeMs = TimeSecs * 1000.0;
	uint64 TimeMs64 = static_cast<uint64>( TimeMs );
	return SoyTime( TimeMs64 );
	/*
	auto PresentationTimestamp = Pts;
	double Timestamp = PresentationTimestamp * TimeBase;
	//double TimeSecs = Timestamp * FrameRate;
	double TimeSecs = Timestamp;
	double TimeMs = TimeSecs * 1000.f;
	return SoyTime( static_cast<uint64>(TimeMs) );
	*/
}


#if defined(ENABLE_DECODER_LIBAV)
bool TDecoder_Libav::DecodeNextFrame(TFramePixels& OutputFrame,SoyTime MinTimestamp,bool& TryAgain)
{
	TryAgain = false;

	Unity::TScopeTimerWarning Timer( "DecodeNextFrame TOTAL", 1 );
	
	//	work out timestamp
	double FrameRate = av_q2d( mVideoStream->r_frame_rate );
	//	avoid /zero
	FrameRate = ofMax(1.0/60.0,1.0/FrameRate);
	float FrameStep = static_cast<float>(FrameRate) * 1000.f;


	int SkipFrames = 0;
	//	if we're using fake-counted timestamps then we can determine how many frames we want to skip to keep up
	#if !defined(USE_REAL_TIMESTAMP)
	{
		//	todo: calc this in reverse instead of looping
		SoyTime NextFrame = mFakeRunningTimestamp;
		NextFrame += static_cast<uint64>(FrameStep);
		while ( NextFrame < MinTimestamp )
		{
			NextFrame += static_cast<uint64>(FrameStep);
			SkipFrames++;
		}
		//	cap this
		SkipFrames = ofMax( SkipFrames, MIN_DECODER_FRAMESKIP );
		SkipFrames = ofMin( SkipFrames, ofMax(MIN_DECODER_FRAMESKIP,MAX_DECODER_FRAMESKIP) );

		BufferString<1000> Debug;
		Debug << "Decoder skipping " << SkipFrames;
		Unity::Debug( Debug.c_str() );
	}
	#endif

	//	if using real video times then we skip packets until we see min PTS (presentation timestamp) in a packet
	int64_t MinPts = 0;
	static bool skipbypts = true;
	if ( skipbypts )
	{
		MinPts = MsToPts( MinTimestamp, av_q2d( mVideoStream->time_base ), av_q2d( mVideoStream->r_frame_rate ) );
		SkipFrames = 0;
	}

	TFrameMeta FrameMeta;
	if ( !DecodeNextFrame( FrameMeta, mCurrentPacket, mFrame, mDataOffset, SkipFrames, MinPts ) )
		return false;
	

	#if defined(USE_REAL_TIMESTAMP)
	{
		auto f_pts = mFrame->pts;
		auto pts = mFrame->pkt_pts;
		auto dts = mFrame->pkt_dts;

		OutputFrame.mTimestamp = PtsToMs( pts, av_q2d( mVideoStream->time_base ), av_q2d( mVideoStream->r_frame_rate ) );

	}
	#else
	{
		//uint64 Step = static_cast<uint64>( 1.f / FrameRate );
		//mFakeRunningTimestamp += Step;
		//	framerate == 0.04 (which is 1/25fps)
		//	therefore frame rate is in seconds
		//	we want timestamp in ms
		int FramesStepped = 1 + SkipFrames;
		mFakeRunningTimestamp += static_cast<uint64>( static_cast<float>(FramesStepped) *  FrameStep);
		OutputFrame.mTimestamp = SoyTime( mFakeRunningTimestamp );
	}
	#endif
	
	//	checking for out-of-order frames
	if ( OutputFrame.mTimestamp < mLastDecodedTimestamp )
	{
		BufferString<100> Debug;
		Debug << (DECODER_SKIP_OOO_FRAMES?"Skipped":"Decoded") << " out-of-order frames " << mLastDecodedTimestamp << " ... " << OutputFrame.mTimestamp;
		Unity::Debug(Debug);

		if ( DECODER_SKIP_OOO_FRAMES )
		{
			TryAgain = true;
			return false;
		}
	}
	mLastDecodedTimestamp = OutputFrame.mTimestamp;
	

	//	too far behind, skip it
	//	gr: with pre-frame-skipping this should never occur any more
	if ( OutputFrame.mTimestamp < MinTimestamp && MinTimestamp.IsValid() && !STORE_PAST_FRAMES )
	{
		BufferString<100> Debug;
		Debug << "Decoded frame " << OutputFrame.mTimestamp << " too far behind " << MinTimestamp << " [skipped]";
		Unity::DebugDecodeLag(Debug);
		TryAgain = true;
		return false;
	}

	//	gr: avpicture takes no time (just filling a struct?)
	AVPicture pict;
	memset(&pict, 0, sizeof(pict));
	avpicture_fill(&pict, OutputFrame.GetData(), GetFormat( OutputFrame.mMeta ), OutputFrame.GetWidth(), OutputFrame.GetHeight() );


	Unity::TScopeTimerWarning sws_getContext_Timer( "DecodeFrame - sws_getContext", 1 );
	static int ScaleMode = SWS_POINT;
//	static int ScaleMode = SWS_FAST_BILINEAR;
//	static int ScaleMode = SWS_BILINEAR;
//	static int ScaleMode = SWS_BICUBIC;
	auto ScaleContext = sws_getCachedContext( mScaleContext, mFrame->width, mFrame->height, static_cast<PixelFormat>(mFrame->format), OutputFrame.GetWidth(), OutputFrame.GetHeight(), GetFormat( OutputFrame.mMeta ), ScaleMode, nullptr, nullptr, nullptr);
	sws_getContext_Timer.Stop();

	if ( !ScaleContext )
	{
		Unity::DebugError("Failed to get converter");
		return false;
	}

	Unity::TScopeTimerWarning sws_scale_Timer( "DecodeFrame - sws_scale", 1 );
	sws_scale( ScaleContext, mFrame->data, mFrame->linesize, 0, mFrame->height, pict.data, pict.linesize);
	sws_scale_Timer.Stop();
	
	return true;
}
#endif

#if defined(ENABLE_DECODER_LIBAV)
void TDecoder_Libav::LogCallback(void *ptr, int level, const char *fmt, va_list vargs)
{
	//	gr: seems to be a lot of problems with this, malformed fmt's, nulls??
	/*
	const char *module = NULL;

	if (ptr)
	{
		AVClass *avc = *(AVClass**)ptr;
		module = avc->item_name(ptr);
	}

	//	gr: this throws too many asserts... so just print something, maybe missing the proper info but who cares for now
	//	Expression : ("Incorrect format specifier", 0)
	//static char message[8192];
	//vsnprintf_s(message, sizeof(message), fmt, vargs);
	//Unity::DebugDecoder(message);
	Unity::DebugDecoder(fmt);
	*/
}
#endif

#if defined(ENABLE_DECODER_LIBAV)
//	http://stackoverflow.com/questions/13888915/thread-safety-of-libav-ffmpeg
int TDecoder_Libav::LockManagerCallback(void** ppMutex,enum AVLockOp op)
{
	//	gr: lock type doesn't seem to make any difference
	//typedef std::recursive_mutex MUTEX_TYPE;
	typedef std::mutex MUTEX_TYPE;
	if ( !ppMutex )
		return -1;

	try
	{

		switch ( op )
		{
			case AV_LOCK_CREATE:
			{
				//	gr: might be uninitlised? in which case we ditch this assert
				assert( *ppMutex == nullptr );
				auto* m = new MUTEX_TYPE();
				if ( !m )
					return -1;
				*ppMutex = static_cast<void*>(m);
			}
			break;

			case AV_LOCK_OBTAIN:
			{
				auto* m = static_cast<MUTEX_TYPE*>(*ppMutex);
				if ( !m )
					return -1;	
				m->lock();
			}
			break;

			case AV_LOCK_RELEASE:
			{
				auto* m = static_cast<MUTEX_TYPE*>(*ppMutex);
				if ( !m )
					return -1;	
				m->unlock();
			}
			break;

			case AV_LOCK_DESTROY:
			{
				auto* m = static_cast<MUTEX_TYPE*>(*ppMutex);
				if ( !m )
					return -1;
				delete m;
				*ppMutex = nullptr;
			}
			break;

			default:
				break;
		}
	}
	catch ( ... )
	{
		Unity::DebugError("Caught error with lock manager!");
		return -1;
	}
	return 0;
}
#endif


#if defined(ENABLE_DECODER_LIBAV)
TDecodeInitResult::Type TDecoder_Libav::Init(const TDecodeParams& Params)
{
	Unity::TScopeTimerWarning Timer(__FUNCTION__,2);

	std::string Filenamea( Params.mFilename.begin(), Params.mFilename.end() );
	bool IsUrl = Soy::StringBeginsWith(Filenamea, "rtsp", false);

	//	check file exists
	if ( !IsUrl && !PathFileExists(Params.mFilename.c_str()) )
	{
		BufferString<1000> Debug;
		Debug << Filenamea << " doesn't exist";
		Unity::DebugError( Debug );
		return TDecodeInitResult::FileNotFound;
	}

	//	init lib av
	av_register_all();
	av_lockmgr_register( &TDecoder_Libav::LockManagerCallback );
	av_log_set_callback( &TDecoder_Libav::LogCallback );
	avformat_network_init();

	mContext = std::shared_ptr<AVFormatContext>(avformat_alloc_context(), &avformat_free_context);
	auto avFormatPtr = mContext.get();
	int err = avformat_open_input( &avFormatPtr, Filenamea.c_str(), nullptr, nullptr );
	if ( err != 0 )
	{
		Unity::DebugError( GetAVError(err) );
		return TDecodeInitResult::CodecError;
	}

	//	get streams 
	err = avformat_find_stream_info( mContext.get(), nullptr );
	if ( err < 0)
	{
		Unity::DebugError( GetAVError(err) );
		return TDecodeInitResult::CodecError;
	}

	assert( !mVideoStream );
	for ( int i = 0; i <(int)mContext->nb_streams; ++i) 
	{
		if (mContext->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) 
		{
			// we've found a video stream!
			mVideoStream = mContext->streams[i];
			break;
		}
	}
	if ( !mVideoStream )
	{
		Unity::DebugError("failed to find a video stream");
		return TDecodeInitResult::CodecError;
	}
	
	auto codecid = mVideoStream->codec->codec_id;

	//	find hardware accell
	AVHWAccel* hwaccel_for_codec = nullptr;
	{
		AVHWAccel* hwaccel = nullptr;
		while( hwaccel = av_hwaccel_next(hwaccel) )
		{
			BufferString<100> Debug;
			Debug << "Found hardware accellerator \"" << hwaccel->name << "\"";
			Unity::Debug(Debug);

			if ( hwaccel->id == codecid	) //CODEC_ID_H264))
			{
				hwaccel_for_codec = hwaccel;
			}
			/*
			{ if((hwaccel->pix_fmt == AV_PIX_FMT_DXVA2_VLD) && (hwaccel->id == CODEC_ID_H264))
			{ h264_dxva2_hwaccel = hwaccel;
			av_register_hwaccel(h264_dxva2_hwaccel);
			printf("dxva2_hwaccel = %s\r\n",h264_dxva2_hwaccel->name);
			}
		av_register_hwaccel( &ff_h264_dxva2_hwaccel );
			*/
		}
	}

	// getting the required codec structure
	const auto codec = avcodec_find_decoder( mVideoStream->codec->codec_id );
	if ( codec == nullptr )
	{
		Unity::DebugError("Failed to find codec for video");
		return TDecodeInitResult::CodecError;
	}

	// allocating a structure
	mCodec = std::shared_ptr<AVCodecContext>(avcodec_alloc_context3(codec), [](AVCodecContext* c) { avcodec_close(c); av_free(c); });
	
	if ( hwaccel_for_codec )
	{
#if defined(ENABLE_DVXA)
		//	init context
		if ( InitDxvaContext() )
		{
			BufferString<100> Debug;
			Debug << "Found hardware accellerator for this codec; \"" << hwaccel_for_codec->name << "\"";
			Unity::DebugLog( Debug );
			av_register_hwaccel( hwaccel_for_codec );
			mCodec->hwaccel = hwaccel_for_codec;
			mCodec->hwaccel_context = &mDxvaContext;
		}
#endif
	}
	
	// we need to make a copy of videoStream->codec->extradata and give it to the context
	// make sure that this vector exists as long as the avVideoCodec exists
	mCodecContextExtraData = std::vector<uint8_t>( mVideoStream->codec->extradata, mVideoStream->codec->extradata + mVideoStream->codec->extradata_size);
	mCodec->extradata = reinterpret_cast<uint8_t*>(mCodecContextExtraData.data());
	mCodec->extradata_size = mCodecContextExtraData.size();


#pragma message("todo: re-launch many times to get this error")
	/*
~TDecodeThread release frames
~TDecodeThread WaitForThread
~TDecodeThread finished
TDecodeThread::~TDecodeThread took 3ms to execute
Insufficient thread locking around avcodec_open/close()

No lock manager is set, please see av_lockmgr_register()
*/
	// initializing the structure by opening the codec
	err = avcodec_open2(mCodec.get(), codec, nullptr);
	if ( err < 0)
	{
		Unity::DebugError( GetAVError(err) );
		return TDecodeInitResult::CodecError;
	}

	//	alloc our buffer-frame
	mFrame = std::shared_ptr<AVFrame>(avcodec_alloc_frame(), &av_free);

	//	start streaming at the start
	mDataOffset = 0;
	
	//	peek at first frame to get video frame dimensions & inital test
	mVideoMeta.mFramesPerSecond = 1.f;
	if ( !PeekNextFrame( mVideoMeta.mFrameMeta ) )
		return TDecodeInitResult::UnknownError;

	return TDecodeInitResult::Success;
}
#endif
