#pragma once
#include "FastVideo.h"
#include <SoyThread.h>



//	link to ffmpeg static lib (requires DLL)
#define ENABLE_DECODER
#if defined(ENABLE_DECODER)
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

};
#pragma comment(lib,"avcodec.lib")
#pragma comment(lib,"avfilter.lib")
#pragma comment(lib,"avformat.lib")
#pragma comment(lib,"avutil.lib")
#pragma comment(lib,"swscale.lib")
#endif



class TFramePool;

#if defined(ENABLE_DECODER)

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
	TDecodeParams() :
		mMaxFrameBuffers	( 1 )
	{
	}

public:
	std::wstring	mFilename;
	int				mMaxFrameBuffers;
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
	bool			DecodeNextFrame(TFramePixels& OutFrame);

private:
	bool			DecodeNextFrame(TFrameMeta& FrameMeta,TPacket& Packet,std::shared_ptr<AVFrame>& Frame,int& DataOffset);

public:
	TVideoMeta							mVideoMeta;

	std::shared_ptr<AVFormatContext>	mContext;
	std::shared_ptr<AVCodecContext>		mCodec;
	std::vector<uint8_t>				mCodecContextExtraData;
	TPacket								mCurrentPacket;
	std::shared_ptr<AVFrame>			mFrame;			//	currently decoding to this frame
	AVStream*							mVideoStream;	//	gr: change to index!
	int									mDataOffset;
};


class TDecodeThread : public SoyThread
{
public:
	static const int		INVALID_FRAME = -1;

public:
	TDecodeThread(TDecodeParams& Params,TFramePool& FramePool);
	~TDecodeThread();

	bool						Init();				//	do initial load and start thread
	TFrameMeta					GetVideoFrameMeta()		{	return mDecoder.GetFrameMeta();	}
	TFrameMeta					GetDecodedFrameMeta();
	void						SetDecodedFrameMeta(TFrameMeta Format);
	TFramePixels*				PopFrame();
	bool						HasVideoToPop();
	bool						IsFinishedDecoding()	{	return mFinishedDecoding;	}

protected:
	virtual void				threadedFunction();
	bool						DecodeNextFrame();
	void						PushFrame(TFramePixels* pFrame);

protected:
	ofMutexT<TFrameMeta>		mDecodeFormat;
	bool						mFinishedDecoding;
	TFramePool&					mFramePool;
	TDecoder					mDecoder;
	ofMutex						mFrameMutex;
	int							mFrameCount;
	Array<TFramePixels*>		mFrameBuffers;	//	frame buffer
	TDecodeParams				mParams;
};




