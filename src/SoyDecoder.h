#pragma once
#include "FastVideo.h"
#include <SoyThread.h>


#define ENABLE_DECODER_TEST


#if defined(TARGET_WINDOWS)
#define ENABLE_DECODER_LIBAV
//#define ENABLE_DVXA
#endif

#if defined(TARGET_OSX)
//#define ENABLE_DECODER_QTKIT
#endif



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

#if defined(ENABLE_DVXA)
#include <d3d11.h>
#include <libavcodec/dxva2.h>
#endif
};
#pragma comment(lib,"avcodec.lib")
#pragma comment(lib,"avfilter.lib")
#pragma comment(lib,"avformat.lib")
#pragma comment(lib,"avutil.lib")
#pragma comment(lib,"swscale.lib")
#endif

#if defined(ENABLE_DECODER_QTKIT)
	#ifdef __OBJC__
#import <Cocoa/Cocoa.h>
#import <Quartz/Quartz.h>
#import <QTKit/QTKit.h>
#import <OpenGL/OpenGL.h>
	#endif
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

	virtual bool	Init(const std::wstring& Filename)=0;
	TVideoMeta		GetVideoMeta()		{	return mVideoMeta;	}
	TFrameMeta		GetFrameMeta()		{	return GetVideoMeta().mFrameMeta;	}
	virtual bool	PeekNextFrame(TFrameMeta& FrameMeta)=0;
	virtual bool	DecodeNextFrame(TFramePixels& OutFrame,SoyTime MinTimestamp,bool& TryAgain)=0;

public:
	TVideoMeta							mVideoMeta;
	SoyTime								mLastDecodedTimestamp;
#if defined(USE_REAL_TIMESTAMP)
	SoyTime								mFakeRunningTimestamp;
#endif
};


#if defined(ENABLE_DECODER_LIBAV)
class TDecoder_Libav : public TDecoder
{
public:
	TDecoder_Libav();
	virtual ~TDecoder_Libav();
	
	virtual bool	Init(const std::wstring& Filename);
	bool			PeekNextFrame(TFrameMeta& FrameMeta);
	bool			DecodeNextFrame(TFramePixels& OutFrame,SoyTime MinTimestamp,bool& TryAgain);
	
private:
	bool			DecodeNextFrame(TFrameMeta& FrameMeta,TPacket& Packet,std::shared_ptr<AVFrame>& Frame,int& DataOffset);
	
#if defined(ENABLE_DVXA)
	bool			InitDxvaContext();
	void			FreeDxvaContext();
#endif
	
public:
	std::shared_ptr<AVFormatContext>	mContext;
	std::shared_ptr<AVCodecContext>		mCodec;
	std::vector<uint8_t>				mCodecContextExtraData;
	TPacket								mCurrentPacket;
	std::shared_ptr<AVFrame>			mFrame;			//	currently decoding to this frame
	AVStream*							mVideoStream;	//	gr: change to index!
	int									mDataOffset;
	SwsContext*							mScaleContext;
#if defined(ENABLE_DVXA)
	dxva_context						mDxvaContext;
#endif
};
#endif


#if defined(ENABLE_DECODER_QTKIT)
class TDecoder_Qtkit : public TDecoder
{
public:
	TDecoder_Qtkit();
	virtual ~TDecoder_Qtkit();
	
	virtual bool	Init(const std::wstring& Filename);
	bool			PeekNextFrame(TFrameMeta& FrameMeta);
	bool			DecodeNextFrame(TFramePixels& OutFrame,SoyTime MinTimestamp,bool& TryAgain);
	
public:
#ifdef __OBJC__
	QTKitMovieRenderer * moviePlayer;
#else
	void * moviePlayer;
#endif
};
#endif




#if defined(ENABLE_DECODER_TEST)
class TDecoder_Test : public TDecoder
{
public:
	TDecoder_Test();
	
	virtual bool	Init(const std::wstring& Filename);
	virtual bool	PeekNextFrame(TFrameMeta& FrameMeta);
	virtual bool	DecodeNextFrame(TFramePixels& OutFrame,SoyTime MinTimestamp,bool& TryAgain);
	
public:
	BufferArray<TColour,10>	mColours;
	int						mCurrentColour;
};
#endif


class TDecodeThread : public SoyThread
{
public:
	static const int		INVALID_FRAME = -1;

public:
	TDecodeThread(TDecodeParams& Params,TFrameBuffer& FrameBuffer,TFramePool& FramePool);
	~TDecodeThread();

	bool						Init();				//	do initial load and start thread
	TFrameMeta					GetVideoFrameMeta()		{	return mDecoder ? mDecoder->GetFrameMeta() : TFrameMeta();	}
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
	ofPtr<TDecoder>				mDecoder;
	TFrameBuffer&				mFrameBuffer;
	TDecodeParams				mParams;

	ofMutexT<SoyTime>			mMinTimestamp;	//	skip decoding frames before this time
};




