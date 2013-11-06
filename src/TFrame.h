#pragma once

#include <ofxSoylent.h>
#include <SoyThread.h>


class TFrameMeta
{
public:
	TFrameMeta() :
		mWidth		( 0 ),
		mHeight		( 0 ),
		mChannels	( 0 )
	{
	}

	TFrameMeta(int Width,int Height,int Channels) :
		mWidth		( ofMax(0,Width) ),
		mHeight		( ofMax(0,Height) ),
		mChannels	( ofMax(0,Channels) )
	{
	}

	bool		IsEqualSize(const TFrameMeta& that) const;

	bool		IsValid() const				{	return mWidth>0 && mHeight>0 && mChannels>0;	}
	bool		operator==(const TFrameMeta& that) const;
	bool		operator!=(const TFrameMeta& that) const;

public:
	int		mWidth;
	int		mHeight;
	int		mChannels;
};

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
	TColour(unsigned char r,unsigned char g,unsigned char b,unsigned char a) :
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


class TFramePixels
{
public:
	TFramePixels(TFrameMeta Meta,const char* Owner=nullptr);

	void					SetColour(const TColour& Colour);
	unsigned char*			GetData()			{	return mPixels.GetArray();	}
	const unsigned char*	GetData() const		{	return mPixels.GetArray();	}
	int						GetDataSize() const	{	return mPixels.GetDataSize();	}
	int						GetPitch() const	{	return sizeof(uint8) * mMeta.mWidth * mMeta.mChannels;	}
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
