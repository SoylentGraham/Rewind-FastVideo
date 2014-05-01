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
	TFrameMeta		mTargetTextureMeta;
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

namespace TDecodeState
{
	enum Type
	{
		NoThread = 0,
		Constructed,
		Initialising,
		FailedInit_Unknown,
		FailedInit_FileNotFound,
		FailedInit_CodecError,
		Decoding,
		FinishedDecoding,
	};

	FastVideoError	GetFastVideoError(Type State);
};

namespace TDecodeInitResult
{
	enum Type
	{
		Success			= 0,
		UnknownError,
		FileNotFound,
		CodecError,
	};

	FastVideoError	GetFastVideoError(Type Result);
};

class TDecoder
{
public:
	virtual ~TDecoder()	{}

	virtual TDecodeInitResult::Type	Init(const TDecodeParams& Params)=0;
	TVideoMeta						GetVideoMeta()		{	return mVideoMeta;	}
	TFrameMeta						GetFrameMeta()		{	return GetVideoMeta().mFrameMeta;	}
	virtual bool					PeekNextFrame(TFrameMeta& FrameMeta)=0;
	virtual bool					DecodeNextFrame(TFramePixels& OutFrame,SoyTime MinTimestamp,bool& TryAgain)=0;

public:
	TVideoMeta			mVideoMeta;
	SoyTime				mLastDecodedTimestamp;
	SoyTime				mFakeRunningTimestamp;
};


#if defined(ENABLE_DECODER_LIBAV)
class TDecoder_Libav : public TDecoder
{
public:
	TDecoder_Libav();
	virtual ~TDecoder_Libav();
	
	virtual TDecodeInitResult::Type	Init(const TDecodeParams& Params);
	virtual bool					PeekNextFrame(TFrameMeta& FrameMeta);
	virtual bool					DecodeNextFrame(TFramePixels& OutFrame,SoyTime MinTimestamp,bool& TryAgain);
	
private:
	bool			DecodeNextFrame(TFrameMeta& FrameMeta,TPacket& Packet,std::shared_ptr<AVFrame>& Frame,int& DataOffset,int SkipFrames,int64_t MinPts);
	static void		LogCallback(void *ptr, int level, const char *fmt, va_list vargs);
	static int		LockManagerCallback(void** ppMutex,enum AVLockOp op);

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
	
	virtual TDecodeInitResult::Type	Init(const TDecodeParams& Params);
	bool							PeekNextFrame(TFrameMeta& FrameMeta);
	bool							DecodeNextFrame(TFramePixels& OutFrame,SoyTime MinTimestamp,bool& TryAgain);
	
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
	
	virtual TDecodeInitResult::Type	Init(const TDecodeParams& Params);
	virtual bool					PeekNextFrame(TFrameMeta& FrameMeta);
	virtual bool					DecodeNextFrame(TFramePixels& OutFrame,SoyTime MinTimestamp,bool& TryAgain);
	
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

	TDecodeInitResult::Type		Init();					//	make decoder and start thread
	TFrameMeta					GetVideoFrameMeta()		{	return mDecoder ? mDecoder->GetFrameMeta() : TFrameMeta();	}
	TFrameMeta					GetDecodedFrameMeta();
	void						SetDecodedFrameMeta(TFrameMeta Format);
	SoyTime						GetMinTimestamp();
	void						SetMinTimestamp(SoyTime Timestamp);
	bool						HasFinishedDecoding() const;
	bool						HasFailedInitialisation() const;

protected:
	virtual void				threadedFunction();
	bool						DecodeNextFrame();
	void						PushInitFrame();

public:
	TDecodeParams				mParams;
	TDecodeState::Type			mState;

protected:
	ofMutexT<TFrameMeta>		mDecodeFormat;
	TFramePool&					mFramePool;
	ofPtr<TDecoder>				mDecoder;
	TFrameBuffer&				mFrameBuffer;

	ofMutexT<SoyTime>			mMinTimestamp;	//	skip decoding frames before this time
};




