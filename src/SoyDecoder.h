#pragma once
#include "FastVideo.h"
#include <SoyThread.h>



#define ENABLE_DECODER_LIBAV
//#define ENABLE_DVXA



//	link to ffmpeg static lib (requires DLL)
#if defined(ENABLE_DECODER_LIBAV)
extern "C"
{
#include <libavutil/opt.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/common.h>
#include <libavutil/imgutils.h>
#include <libavutil/mathematics.h>
#include <libavutil/samplefmt.h>
#include <libavutil/samplefmt.h>
#include <libswscale/swscale.h>
#include <libavcodec/dxva2.h>
};
#pragma comment(lib,"avcodec.lib")
#pragma comment(lib,"avfilter.lib")
#pragma comment(lib,"avformat.lib")
#pragma comment(lib,"avutil.lib")
#pragma comment(lib,"swscale.lib")
#endif



class TFramePool;


#if defined(ENABLE_DECODER_LIBAV)
class TPacket 
{
public:
	explicit TPacket(AVFormatContext* ctxt = nullptr) 
	{
		memset( &packet, 0x00, sizeof(packet) );
		av_init_packet(&packet);
		packet.data = nullptr;
		if (ctxt) 
			reset(ctxt);
	}

	TPacket(TPacket&& other) : 
		packet(std::move(other.packet))
	{
		other.packet.data = nullptr;
	}

	~TPacket() {
		if (packet.data)
			av_free_packet(&packet);
	}

	bool reset(AVFormatContext* ctxt);

	AVPacket	packet;
};
#endif


class TDecodeParams
{
public:
	std::wstring	mFilename;
};


class TFrameBuffer
{
public:
	TFrameBuffer(int MaxFrameBufferSize,TFramePool& FramePool);
	~TFrameBuffer();

	bool						IsFull();
	TFramePixels*				PopFrame(SoyTime Frame);
	bool						HasVideoToPop();
	void						PushFrame(TFramePixels* pFrame);
	void						ReleaseFrames();

public:
	int							mMaxFrameBufferSize;
	TFramePool&					mFramePool;
	ofMutex						mFrameMutex;
	Array<TFramePixels*>		mFrameBuffers;	//	frame's we've read and ready to be popped
};



class TVideoMeta
{
public:
	TFrameMeta	mFrameMeta;
	float		mFramesPerSecond;
};

class TDecoder
{
public:
	TDecoder();
	virtual ~TDecoder();

	bool			Init(const std::wstring& Filename);
	TVideoMeta		GetVideoMeta()		{	return mVideoMeta;	}
	TFrameMeta		GetFrameMeta()		{	return GetVideoMeta().mFrameMeta;	}
	bool			PeekNextFrame(TFrameMeta& FrameMeta);
	bool			DecodeNextFrame(TFramePixels& OutFrame,SoyTime MinTimestamp,bool& TryAgain);

private:
#if defined(ENABLE_DECODER_LIBAV)
	bool			DecodeNextFrame(TFrameMeta& FrameMeta,TPacket& Packet,std::shared_ptr<AVFrame>& Frame,int& DataOffset);
#endif

#if defined(ENABLE_DVXA)
	bool			InitDxvaContext();
	void			FreeDxvaContext();
#endif

public:
	TVideoMeta							mVideoMeta;
	SoyTime								mLastDecodedTimestamp;
#if defined(USE_REAL_TIMESTAMP)
	SoyTime								mFakeRunningTimestamp;
#endif

#if defined(ENABLE_DECODER_LIBAV)
	std::shared_ptr<AVFormatContext>	mContext;
	std::shared_ptr<AVCodecContext>		mCodec;
	std::vector<uint8_t>				mCodecContextExtraData;
	TPacket								mCurrentPacket;
	std::shared_ptr<AVFrame>			mFrame;			//	currently decoding to this frame
	AVStream*							mVideoStream;	//	gr: change to index!
	int									mDataOffset;
	SwsContext*							mScaleContext;
#endif
#if defined(ENABLE_DVXA)
	dxva_context						mDxvaContext;
#endif
};


class TDecodeThread : public SoyThread
{
public:
	static const int		INVALID_FRAME = -1;

public:
	TDecodeThread(TDecodeParams& Params,TFrameBuffer& FrameBuffer,TFramePool& FramePool);
	~TDecodeThread();

	bool						Init();				//	do initial load and start thread
	TFrameMeta					GetVideoFrameMeta()		{	return mDecoder.GetFrameMeta();	}
	TFrameMeta					GetDecodedFrameMeta();
	void						SetDecodedFrameMeta(TFrameMeta Format);
	bool						IsFinishedDecoding()	{	return mFinishedDecoding;	}
	SoyTime						GetMinTimestamp();
	void						SetMinTimestamp(SoyTime Timestamp);

protected:
	virtual void				threadedFunction();
	bool						DecodeNextFrame();

protected:
	ofMutexT<TFrameMeta>		mDecodeFormat;
	bool						mFinishedDecoding;
	TFramePool&					mFramePool;
	TDecoder					mDecoder;
	TFrameBuffer&				mFrameBuffer;
	TDecodeParams				mParams;

	ofMutexT<SoyTime>			mMinTimestamp;	//	skip decoding frames before this time
};




