#include "SoyDecoder.h"


BufferString<1000> GetAVError(int Error)
{
	char Buffer[1000];

	if ( av_strerror( Error, Buffer, sizeof(Buffer)/sizeof(Buffer[0]) ) != 0 )
	{
		BufferString<1000> Debug;
		Debug << "Unknown error: " << Error;
		return Debug;
	}

	return Buffer;
}

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


TDecodeThread::TDecodeThread(TDecodeParams& Params,TFramePool& FramePool) :
	SoyThread			( "TDecodeThread" ),
	mFrameCount			( 0 ),
	mFramePool			( FramePool ),
	mParams				( Params ),
	mFinishedDecoding	( false )
{
}

TDecodeThread::~TDecodeThread()
{
	waitForThread();
}

bool TDecodeThread::Init()
{
	//	init decoder
	if ( !mDecoder.Init(mParams.mFilename.c_str()) )
		return false;

	//	todo: check video params are valid

	//	start thread
	startThread(true,true);
	return true;
}

bool TDecodeThread::HasVideoToPop()
{
	ofMutex::ScopedLock lock( mFrameMutex );
	return !mFrameBuffers.IsEmpty();
}


TFramePixels* TDecodeThread::PopFrame()
{
	ofMutex::ScopedLock lock( mFrameMutex );

	TFramePixels* PoppedFrame = NULL;
	if ( !mFrameBuffers.IsEmpty() )
	{
		PoppedFrame = mFrameBuffers.PopAt(0);
	}

	return PoppedFrame;
}


void TDecodeThread::threadedFunction()
{
	while ( isThreadRunning() )
	{
		Sleep(1);

		//	if buffer is filled, stop (don't buffer too many frames)
		bool DoDecode = false;
		mFrameMutex.lock();
		DoDecode = (mFrameBuffers.GetSize() < mParams.mMaxFrameBuffers);
		mFrameMutex.unlock();

		if ( !DoDecode )
			continue;
		
		DecodeNextFrame();

		if ( IsFinishedDecoding() )
			break;
	}
}

void TDecodeThread::PushFrame(TFramePixels* pFrame)
{
	ofMutex::ScopedLock lock( mFrameMutex );
	mFrameBuffers.PushBack( pFrame );
	mFrameCount++;
}

TFrameMeta TDecodeThread::GetDecodedFrameMeta()
{
	ofMutex::ScopedLock lock( mDecodeFormat );
	return mDecodeFormat;
}
	
	
void TDecodeThread::SetDecodedFrameMeta(TFrameMeta Format)
{
	ofMutex::ScopedLock lock( mDecodeFormat );
	mDecodeFormat.Get() = Format;
}

bool TDecodeThread::DecodeNextFrame()
{
	//	alloc a frame
	TFramePixels* Frame = mFramePool.Alloc( GetDecodedFrameMeta() );

	//	out of memory/pool, or non-valid format (ie. dont know output dimensions yet)
	if ( !Frame )
		return false;

	if ( !mDecoder.DecodeNextFrame( *Frame ) )
	{
		mFinishedDecoding = true;
		mFramePool.Free( Frame );
		return false;
	}

	PushFrame( Frame );

	return true;
}


TDecoder::TDecoder() :
	mVideoStream	( NULL ),
	mDataOffset		( 0 )
{
}


TDecoder::~TDecoder()
{
}

bool TDecoder::DecodeNextFrame(TFrameMeta& FrameMeta,TPacket& CurrentPacket,std::shared_ptr<AVFrame>& Frame,int& DataOffset)
{
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

bool TDecoder::PeekNextFrame(TFrameMeta& FrameMeta)
{
	//	not setup right??
	if ( !mVideoStream )
		return false;

	int peekDataOffset = mDataOffset;
	TPacket PeekCurrentPacket;
	std::shared_ptr<AVFrame> peekFrame = std::shared_ptr<AVFrame>(avcodec_alloc_frame(), &av_free);

	if ( !DecodeNextFrame( FrameMeta, PeekCurrentPacket, peekFrame, peekDataOffset ) )
		return false;

	return true;
}


bool TDecoder::DecodeNextFrame(TFramePixels& OutputFrame)
{
	TFrameMeta FrameMeta;
	if ( !DecodeNextFrame( FrameMeta, mCurrentPacket, mFrame, mDataOffset ) )
		return false;
	/*
	//	frame mis match
	if ( !OutputFrame.mMeta.IsEqualSize(FrameMeta) )
	{
		BufferString<1000> Debug;
		Debug << "Output size (" << OutputFrame.mMeta.mWidth << "," << OutputFrame.mMeta.mHeight << ") mis match to video size (" << FrameMeta.mWidth << "," << FrameMeta.mHeight << ")";
		Unity::DebugLog( Debug );
		return false;
	}
	*/
	AVPicture pict;
			
	//avpicture_alloc( &pict, PIX_FMT_RGBA, OutputFrame->mWidth, OutputFrame->mHeight );
	memset(&pict, 0, sizeof(pict));
	avpicture_fill(&pict, OutputFrame.GetData(), GetFormat( OutputFrame.mMeta ), OutputFrame.GetWidth(), OutputFrame.GetHeight() );

	auto ctxt = sws_getContext( mFrame->width, mFrame->height, static_cast<PixelFormat>(mFrame->format), OutputFrame.GetWidth(), OutputFrame.GetHeight(), GetFormat( OutputFrame.mMeta ), SWS_BILINEAR, nullptr, nullptr, nullptr);
	if ( !ctxt )
	{
		Unity::DebugLog("Failed to get converter");
		return false;
	}
	sws_scale(ctxt, mFrame->data, mFrame->linesize, 0, mFrame->height, pict.data, pict.linesize);

	// pic.data[0] now contains the image data in RGB format (3 bytes)
	// and pic.linesize[0] is the pitch of the data (ie. size of a row in memory, which can be larger than width*sizeof(pixel))

	// we can for example upload it to an OpenGL texture (note: untested code)
	// glBindTexture(GL_TEXTURE_2D, myTex);
	// for (int i = 0; i < avFrame->height; ++i) {
	// 	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, i, avFrame->width, 1, GL_RGB, GL_UNSIGNED_BYTE, avFrame->data[0] + (i * pic.linesize[0]));
	// }

	//avpicture_free(&pict);
	return true;
	/*
#if defined(ENABLE_DECODER)
	//	not setup right??
	if ( !mVideoStream )
		return false;

	//	keep processing until a frame is loaded
	int isFrameAvailable = false;
	while ( !isFrameAvailable )
	{
		// reading a packet using libavformat
		if (mDataOffset >= mCurrentPacket.packet.size) 
		{
			do 
			{
				mCurrentPacket.reset( mContext.get());
				if (mCurrentPacket.packet.stream_index != mVideoStream->index)
					continue;
			}
			while(0);
		}

		// preparing the packet that we will send to libavcodec
		auto& packetToSend = mCurrentPacket.packet;
	
		// sending data to libavcodec
		const auto processedLength = avcodec_decode_video2(mCodec.get(), mFrame.get(), &isFrameAvailable, &packetToSend);
		if (processedLength < 0) 
		{
			//av_free_packet(&packet);
			Unity::DebugLog("Error while processing the data");
			return false;
		}
		mDataOffset += processedLength;

		// processing the image if available
		if (isFrameAvailable) 
		{
			//	debug first time
			if ( mFirstDecode )
			{
				BufferString<100> Debug;
				Debug << "Decoded frame " << mFrame->width << " x " << mFrame->height << " format: " << mFrame->format;
				Unity::DebugLog( Debug );
				mFirstDecode = false;
			}

			//	frame mis match
			TFrameMeta FrameMeta( mFrame->width, mFrame->height, GetChannelCount(static_cast<AVPixelFormat>(mFrame->format)) );
			if ( OutputFrame.mMeta != FrameMeta )
			{
				Unity::DebugLog("Frame meta mis match");
				return false;
			}

			AVPicture pict;
			
			//avpicture_alloc( &pict, PIX_FMT_RGBA, OutputFrame->mWidth, OutputFrame->mHeight );
			memset(&pict, 0, sizeof(pict));
			avpicture_fill(&pict, OutputFrame.GetData(), GetFormat( OutputFrame.mMeta ), OutputFrame.GetWidth(), OutputFrame.GetHeight() );

			auto ctxt = sws_getContext( mFrame->width, mFrame->height, static_cast<PixelFormat>(mFrame->format), OutputFrame.GetWidth(), OutputFrame.GetHeight(), GetFormat( OutputFrame.mMeta ), SWS_BILINEAR, nullptr, nullptr, nullptr);
			if ( !ctxt )
			{
				Unity::DebugLog("Failed to get converter");
				return false;
			}
			sws_scale(ctxt, mFrame->data, mFrame->linesize, 0, mFrame->height, pict.data, pict.linesize);

			// pic.data[0] now contains the image data in RGB format (3 bytes)
			// and pic.linesize[0] is the pitch of the data (ie. size of a row in memory, which can be larger than width*sizeof(pixel))

			// we can for example upload it to an OpenGL texture (note: untested code)
			// glBindTexture(GL_TEXTURE_2D, myTex);
			// for (int i = 0; i < avFrame->height; ++i) {
			// 	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, i, avFrame->width, 1, GL_RGB, GL_UNSIGNED_BYTE, avFrame->data[0] + (i * pic.linesize[0]));
			// }

			//avpicture_free(&pict);
			
		}
	}

	return true;
#endif
	return false;
	*/
}



bool TDecoder::Init(const std::wstring& Filename)
{
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