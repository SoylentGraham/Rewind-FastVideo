#include "TFrame.h"
#include "FastVideo.h"


bool TFrameMeta::IsEqualSize(const TFrameMeta& that) const
{
	return (mWidth == that.mWidth) &&
			(mHeight == that.mHeight);
}

bool TFrameMeta::operator==(const TFrameMeta& that) const
{
	return IsEqualSize(that) && (mChannels == that.mChannels);
}

bool TFrameMeta::operator!=(const TFrameMeta& that) const
{
	return !IsEqualSize(that) || (mChannels != that.mChannels);
}




TFramePool::TFramePool(int MaxPoolSize) :
	mPoolMaxSize	( ofMax(1,MaxPoolSize) )
{
}

TFramePixels* TFramePool::Alloc(TFrameMeta FrameMeta)
{
	//	can't alloc invalid frame 
	if ( !FrameMeta.IsValid() )
		return nullptr;

	ofMutex::ScopedLock lock( mFrameMutex );

	//	any free?
	TFramePixels* FreeFrame = NULL;

	for ( int p=0;	p<(int)mPool.size();	p++ )
	{
		if ( mPoolUsed[p] )
			continue;

		auto* PoolFrame = mPool[p];

		//	cant use one with different format
		if ( PoolFrame->mMeta != FrameMeta )
			continue;

		FreeFrame = mPool[p];
		mPoolUsed[p] = true;
	}

	//	alloc a new one if we're not at our limit
	if ( !FreeFrame )
	{
		if ( (int)mPool.size() < mPoolMaxSize )
		{
			FreeFrame = new TFramePixels( FrameMeta );
			BufferString<1000> Debug;
			Debug << "Allocating new block; " << PtrToInt(FreeFrame) << " pool size; " << mPool.size();
			Unity::DebugLog( Debug );

			if ( FreeFrame )
			{
				mPool.push_back( FreeFrame );
				mPoolUsed.push_back( true );
			}
		}
		else
		{
			BufferString<1000> Debug;
			Debug << "Frame pool is full (" << mPool.size() << "/; " << mPool.size() << ")";
			Unity::DebugLog( Debug );
		}
	}
	

	return FreeFrame;
}

bool TFramePool::Free(TFramePixels* pFrame)
{
	ofMutex::ScopedLock lock( mFrameMutex );

	bool Removed = false;

	//	find index and mark as no-longer in use
	for ( int p=0;	p<(int)mPool.size();	p++ )
	{
		auto* Entry = mPool[p];
		if ( Entry != pFrame )
			continue;
		assert( mPoolUsed[p] );
		mPoolUsed[p] = false;
		Removed = true;
	}

	assert( Removed );
	return Removed;
}




TFramePixels::TFramePixels(TFrameMeta Meta) :
	mMeta	( Meta )
{
	mPixels.SetSize( mMeta.mWidth * mMeta.mHeight * mMeta.mChannels );
}

