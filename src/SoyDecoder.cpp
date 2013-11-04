#include "SoyDecoder.h"
#include <SortArray.h>



	
class TSortPolicy_TFramePixelsByTimestamp
{
public:
	static int		Compare(const TFramePixels* a,const TFramePixels* b)
	{
		auto Framea = a->mTimestamp;
		auto Frameb = b->mTimestamp;

		if ( a < b ) return -1;
		if ( a > b ) return 1;
		return 0;
	}
};



BufferString<1000> GetAVError(int Error)
{
#if defined(ENABLE_DECODER)
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

#if defined(ENABLE_DECODER)
bool TPacket::reset(AVFormatContext* ctxt)
{
	if (packet.data)
	{
		av_free_packet(&packet);
		packet.data = nullptr;
	}

	auto err = av_read_frame(ctxt, &packet);
	if ( err < 0 )
	{
		BufferString<1000> Debug;
		Debug << "Error reading next packet; " << GetAVError( err );
		Unity::DebugLog( Debug );

		packet.data = nullptr;
		return false;
	}
	return true;
}
#endif


#if defined(ENABLE_DECODER)
int GetChannelCount(enum AVPixelFormat Format)
{
	switch ( Format )
	{
	case AV_PIX_FMT_RGB24:
		return 3;
	case PIX_FMT_RGBA:
		return 4;
	};

	return 0;
}
#endif


#if defined(ENABLE_DECODER)
enum AVPixelFormat GetFormat(const TFrameMeta& FrameMeta)
{
	if ( FrameMeta.mChannels == 3 )
	{
		return PIX_FMT_RGB24;
	}

	if ( FrameMeta.mChannels == 4 )
	{
		return PIX_FMT_RGBA;
	}

	return PIX_FMT_NONE;
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
	mFinishedDecoding	( false )
{
	Unity::DebugLog(__FUNCTION__);
}

TDecodeThread::~TDecodeThread()
{
	ofScopeTimerWarning Timer( __FUNCTION__, 1 );

	Unity::DebugLog("~TDecodeThread release frames");
	mFrameBuffer.ReleaseFrames();

	Unity::DebugLog("~TDecodeThread WaitForThread");
	waitForThread();

	Unity::DebugLog("~TDecodeThread finished");
}


bool TDecodeThread::Init()
{
	Unity::DebugLog( __FUNCTION__ );

	//	init decoder
	if ( !mDecoder.Init(mParams.mFilename.c_str()) )
		return false;

	Unity::DebugLog("TDecodeThread - starting thread");
	startThread( true, true );
	Unity::DebugLog("TDecodeThread - starting thread OK");
	return true;
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
	ofScopeTimerWarning Timer(__FUNCTION__,2);
	ofMutex::ScopedLock lock( mFrameMutex );

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
		Unity::DebugLog( Debug );

		//	pop & release the first X frames we're going to skip
		for ( int i=0;	i<SkipCount;	i++ )
		{
			auto* FrameBuffer = mFrameBuffers.PopAt(0);
			mFramePool.Free( FrameBuffer );
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
	while ( isThreadRunning() )
	{
		Sleep(1);

		//	if buffer is filled, stop (don't buffer too many frames)
		bool DoDecode = !mFrameBuffer.IsFull();

		if ( !DoDecode )
			continue;
		
		DecodeNextFrame();

		if ( IsFinishedDecoding() )
			break;
	}
}

void TFrameBuffer::PushFrame(TFramePixels* pFrame)
{
	ofScopeTimerWarning Timer(__FUNCTION__,2);
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

#if defined(ENABLE_DECODER_INIT_SIZE_FRAME)
	//	push a green "OK" frame
	TFramePixels* Frame = mFramePool.Alloc( GetDecodedFrameMeta(), "Debug init" );
	if ( Frame )
	{
		Unity::DebugLog( BufferString<100>()<<"Pushing Debug Frame; " << __FUNCTION__ );
		Frame->SetColour( ENABLE_DECODER_INIT_SIZE_FRAME );
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

bool TDecodeThread::DecodeNextFrame()
{
	ofScopeTimerWarning Timer( "thread DecodeNextFrame", 1 );

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
		if ( mDecoder.DecodeNextFrame( *Frame, MinTimestamp, TryAgain ) )
			break;
		
		//	failed, and failed hard
		if ( !TryAgain )
		{
			mFinishedDecoding = true;
			mFramePool.Free( Frame );
			return false;
		}
	}

	mFrameBuffer.PushFrame( Frame );

	return true;
}


TDecoder::TDecoder()
#if defined(ENABLE_DECODER)
	:
	mScaleContext	( nullptr ),
	mVideoStream	( nullptr ),
	mDataOffset		( 0 )
#endif
{
}


TDecoder::~TDecoder()
{
#if defined(ENABLE_DECODER)
	sws_freeContext( mScaleContext );
	mScaleContext = nullptr;
#endif
}

#if defined(ENABLE_DECODER)
bool TDecoder::DecodeNextFrame(TFrameMeta& FrameMeta,TPacket& CurrentPacket,std::shared_ptr<AVFrame>& Frame,int& DataOffset)
{
	ofScopeTimerWarning Timer( __FUNCTION__, 1 );

	//	not setup right??
	if ( !mVideoStream )
		return false;

	//	keep processing until a frame is loaded
	int isFrameAvailable = false;
	while ( !isFrameAvailable )
	{
		// reading a packet using libavformat
		if ( DataOffset >= CurrentPacket.packet.size) 
		{
			//	keep fetching [valid] packets until we find our stream
			while ( true )
			{
				//	if we failed to read next packet, we're out of frames
				if ( !CurrentPacket.reset( mContext.get() ) )
					return false;
				
				if ( CurrentPacket.packet.stream_index == mVideoStream->index )
					break;
			}
		}

		auto& packetToSend = CurrentPacket.packet;
	
		// sending data to libavcodec
		const auto processedLength = avcodec_decode_video2( mCodec.get(), Frame.get(), &isFrameAvailable, &packetToSend );
		if (processedLength < 0) 
		{
			//av_free_packet(&packet);
			Unity::DebugLog("Error while processing the data");
			return false;
		}
		DataOffset += processedLength;

		//	keep processing if not availible yet
		if ( !isFrameAvailable )
			continue;
	
		//	frame is availible, return it's info
		FrameMeta = TFrameMeta( Frame->width, Frame->height, GetChannelCount(static_cast<AVPixelFormat>(Frame->format) ) );
		return true;
	}

	//	kept processing until we ran out of data without a result
	return false;
}
#endif

bool TDecoder::PeekNextFrame(TFrameMeta& FrameMeta)
{
#if defined(ENABLE_DECODER)
	//	not setup right??
	if ( !mVideoStream )
		return false;

	int peekDataOffset = mDataOffset;
	TPacket PeekCurrentPacket;
	std::shared_ptr<AVFrame> peekFrame = std::shared_ptr<AVFrame>(avcodec_alloc_frame(), &av_free);

	if ( !DecodeNextFrame( FrameMeta, PeekCurrentPacket, peekFrame, peekDataOffset ) )
		return false;

	return true;
#else
	return false;
#endif
}


bool TDecoder::DecodeNextFrame(TFramePixels& OutputFrame,SoyTime MinTimestamp,bool& TryAgain)
{
	TryAgain = false;

#if defined(ENABLE_DECODER)
	ofScopeTimerWarning Timer( "DecodeNextFrame TOTAL", 1 );

	TFrameMeta FrameMeta;
	if ( !DecodeNextFrame( FrameMeta, mCurrentPacket, mFrame, mDataOffset ) )
		return false;
	
	//	work out timestamp
	double FrameRate = av_q2d( mVideoStream->r_frame_rate );
	//	avoid /zero
	FrameRate = ofMax(1.0/60.0,1.0/FrameRate);
	double TimeBase = av_q2d( mVideoStream->time_base );
	auto PresentationTimestamp = mFrame->pts;
	double Timestamp = mFrame->pts * TimeBase;
	double TimeSecs = Timestamp * FrameRate;
	double TimeMs = TimeSecs * 1000.f;
	OutputFrame.mTimestamp = SoyTime( static_cast<uint64>(TimeMs) );

	//	too far behind, skip it
	if ( OutputFrame.mTimestamp < MinTimestamp && MinTimestamp.IsValid() && !STORE_PAST_FRAMES )
	{
		BufferString<100> Debug;
		Debug << "Decoded frame " << OutputFrame.mTimestamp << " too far behind " << MinTimestamp;
		Unity::DebugLog( Debug );
		TryAgain = true;
		return true;
	}

	//	gr: avpicture takes no time (just filling a struct?)
	AVPicture pict;
	memset(&pict, 0, sizeof(pict));
	avpicture_fill(&pict, OutputFrame.GetData(), GetFormat( OutputFrame.mMeta ), OutputFrame.GetWidth(), OutputFrame.GetHeight() );

	
	ofScopeTimerWarning sws_getContext_Timer( "DecodeFrame - sws_getContext", 1 );
	static int ScaleMode = SWS_POINT;
//	static int ScaleMode = SWS_FAST_BILINEAR;
//	static int ScaleMode = SWS_BILINEAR;
//	static int ScaleMode = SWS_BICUBIC;
	auto ScaleContext = sws_getCachedContext( mScaleContext, mFrame->width, mFrame->height, static_cast<PixelFormat>(mFrame->format), OutputFrame.GetWidth(), OutputFrame.GetHeight(), GetFormat( OutputFrame.mMeta ), ScaleMode, nullptr, nullptr, nullptr);
	sws_getContext_Timer.Stop();

	if ( !ScaleContext )
	{
		Unity::DebugLog("Failed to get converter");
		return false;
	}

	ofScopeTimerWarning sws_scale_Timer( "DecodeFrame - sws_scale", 1 );
	sws_scale( ScaleContext, mFrame->data, mFrame->linesize, 0, mFrame->height, pict.data, pict.linesize);
	sws_scale_Timer.Stop();

	return true;
#else
	return false;
#endif
}



bool TDecoder::Init(const std::wstring& Filename)
{
	ofScopeTimerWarning Timer(__FUNCTION__,2);

#if defined(ENABLE_DECODER)
	std::string Filenamea( Filename.begin(), Filename.end() );

	//	check file exists
	if ( !PathFileExists(Filename.c_str()) )
	{
		BufferString<1000> Debug;
		Debug << Filenamea << " doesn't exist";
		Unity::DebugLog( Debug );
		return false;
	}

	//	init lib av
	av_register_all();

	mContext = std::shared_ptr<AVFormatContext>(avformat_alloc_context(), &avformat_free_context);
	auto avFormatPtr = mContext.get();
	int err = avformat_open_input( &avFormatPtr, Filenamea.c_str(), nullptr, nullptr );
	if ( err != 0 )
	{
		Unity::DebugLog( GetAVError(err) );
		return false;
	}

	//	get streams 
	err = avformat_find_stream_info( mContext.get(), nullptr );
	if ( err < 0)
	{
		Unity::DebugLog( GetAVError(err) );
		return false;
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
		Unity::DebugLog("failed to find a video stream");
		return false;
	}
    
	// getting the required codec structure
	const auto codec = avcodec_find_decoder( mVideoStream->codec->codec_id );
	if ( codec == nullptr )
	{
		Unity::DebugLog("Failed to find codec for video");
		return false;
	}

	// allocating a structure
	mCodec = std::shared_ptr<AVCodecContext>(avcodec_alloc_context3(codec), [](AVCodecContext* c) { avcodec_close(c); av_free(c); });

	// we need to make a copy of videoStream->codec->extradata and give it to the context
	// make sure that this vector exists as long as the avVideoCodec exists
	mCodecContextExtraData = std::vector<uint8_t>( mVideoStream->codec->extradata, mVideoStream->codec->extradata + mVideoStream->codec->extradata_size);
	mCodec->extradata = reinterpret_cast<uint8_t*>(mCodecContextExtraData.data());
	mCodec->extradata_size = mCodecContextExtraData.size();

	// initializing the structure by opening the codec
	err = avcodec_open2(mCodec.get(), codec, nullptr);
	if ( err < 0)
	{
		Unity::DebugLog( GetAVError(err) );
		return false;
	}

	//	alloc our buffer-frame
	mFrame = std::shared_ptr<AVFrame>(avcodec_alloc_frame(), &av_free);

	//	start streaming at the start
	mDataOffset = 0;
	
	//	peek at first frame to get video frame dimensions & inital test
	mVideoMeta.mFramesPerSecond = 1.f;
	if ( !PeekNextFrame( mVideoMeta.mFrameMeta ) )
		return false;

	return true;

#endif
	
	return false;
}