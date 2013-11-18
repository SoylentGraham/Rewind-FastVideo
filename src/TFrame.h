#pragma once

#include <ofxSoylent.h>
#include <SoyThread.h>

namespace TFrameFormat
{
	enum Type
	{
		Invalid,
		RGB,
		RGBA,
		BGRA,
		YUV,	//	4:2:2
	};

	int			GetChannels(Type Format);
	const char*	ToString(Type Format);
};

class TFrameMeta
{
public:
	TFrameMeta() :
		mWidth		( 0 ),
		mHeight		( 0 ),
		mFormat	( TFrameFormat::Invalid )
	{
	}

	TFrameMeta(int Width,int Height,TFrameFormat::Type Format) :
		mWidth		( ofMax(0,Width) ),
		mHeight		( ofMax(0,Height) ),
		mFormat		( Format )
	{
	}

	int			GetDataSize() const			{	return mWidth * mHeight * GetChannels();	}
	bool		IsEqualSize(const TFrameMeta& that) const;
	int			GetChannels() const			{	return TFrameFormat::GetChannels( mFormat );	}
	bool		IsValid() const				{	return mWidth>0 && mHeight>0 && mFormat!=TFrameFormat::Invalid;	}
	bool		operator==(const TFrameMeta& that) const;
	bool		operator!=(const TFrameMeta& that) const;

public:
	int					mWidth;
	int					mHeight;
	TFrameFormat::Type	mFormat;
};
DECLARE_NONCOMPLEX_TYPE(TFrameMeta);


class TColour
{
public:
	TColour() :
		mRed	( 255 ),
		mGreen	( 255 ),
		mBlue	( 255 ),
		mAlpha	( 255 )
	{
	}
	TColour(unsigned char r,unsigned char g,unsigned char b,unsigned char a=255) :
		mRed	( r ),
		mGreen	( g ),
		mBlue	( b ),
		mAlpha	( a )
	{
	}

	inline bool		operator!=(const TColour& that) const	{	return (this->mRed!=that.mRed) || (this->mGreen!=that.mGreen) || (this->mBlue!=that.mBlue) || (this->mAlpha!=that.mAlpha);	}

public:
	unsigned char	mRed;
	unsigned char	mGreen;
	unsigned char	mBlue;
	unsigned char	mAlpha;
};
DECLARE_NONCOMPLEX_NO_CONSTRUCT_TYPE(TColour);


class TFramePixels
{
public:
	TFramePixels(TFrameMeta Meta=TFrameMeta(),const char* Owner=nullptr);
	explicit TFramePixels(const TFramePixels& Other);

	void					SetColour(const TColour& Colour);
	unsigned char*			GetData()			{	return mPixels.GetArray();	}
	const unsigned char*	GetData() const		{	return mPixels.GetArray();	}
	int						GetDataSize() const	{	return mPixels.GetDataSize();	}
	int						GetPitch() const	{	return sizeof(uint8) * mMeta.mWidth * mMeta.GetChannels();	}
	int						GetWidth() const	{	return mMeta.mWidth;	}
	int						GetHeight() const	{	return mMeta.mHeight;	}
	void					SetOwner(const char* Owner)	{	mDebugOwner = Owner;	}
	
public:
	BufferString<100>	mDebugOwner;		//	current owner
	TFrameMeta			mMeta;
	Array<uint8>		mPixels;
	SoyTime				mTimestamp;	//	frame since 0 
};




class TFramePool
{
public:
	TFramePool(int MaxPoolSize);

	TFramePixels*	Alloc(TFrameMeta FrameMeta,const char* Owner);	//	increase pool size
	bool			Free(TFramePixels* pFrame);
	bool			IsEmpty();						//	no used slots

	void			DebugUsedFrames();

	void			PreAlloc(TFrameMeta FrameMeta);	//	allocate all the frames if we havent already

private:
	int				GetAllocatedCount();

protected:
	int							mPoolMaxSize;
	ofMutex						mPoolLock;
	Array<TFramePixels*>		mUsedPool;
	Array<TFramePixels*>		mFreePool;
};
DECLARE_NONCOMPLEX_NO_CONSTRUCT_TYPE( TFramePixels* );
