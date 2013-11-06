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
	
void TFramePool::DebugUsedFrames()
{
	ofMutex::ScopedLock lock( mPoolLock );

	for ( int i=0;	i<mUsedPool.GetSize();	i++ )
	{
		auto& Frame = *mUsedPool[i];
		
		BufferString<1000> Debug;
		Debug << "Frame [" << i << "] allocated; owner: " << Frame.mDebugOwner;
		Unity::DebugLog( Debug );
	}
}


bool TFramePool::IsEmpty()
{
	ofMutex::ScopedLock lock( mPoolLock );

	int UsedSlots = mUsedPool.GetSize();
	return (UsedSlots==0);
}

int TFramePool::GetAllocatedCount()
{
	return mFreePool.GetSize() + mUsedPool.GetSize();
}

void TFramePool::PreAlloc(TFrameMeta FrameMeta)
{
	if ( !FrameMeta.IsValid() )
		return;

	ofScopeTimerWarning Timer(__FUNCTION__,2);
	ofMutex::ScopedLock lock( mPoolLock );

	//	already allocated some
	if ( GetAllocatedCount() > 0 )
		return;

	//	alloc X frames
	mFreePool.Reserve( mPoolMaxSize );
	for ( int i=0;	i<mPoolMaxSize;	i++ )
	{
		TFramePixels* Frame = new TFramePixels( FrameMeta, "TFramePool - pre-alloc" );
		if ( !Frame )
		{
			assert( Frame );
			return;
		}
		mFreePool.PushBack( Frame );
	}
}


TFramePixels* TFramePool::Alloc(TFrameMeta FrameMeta,const char* Owner)
{
	//	can't alloc invalid frame 
	if ( !FrameMeta.IsValid() )
		return nullptr;

	PreAlloc( FrameMeta );

	ofScopeTimerWarning Timer(__FUNCTION__,2);
	ofMutex::ScopedLock lock( mPoolLock );

	//	any free?
	TFramePixels* FreeFrame = NULL;

	if ( !mFreePool.IsEmpty() )
	{
		FreeFrame = mFreePool.PopBack();
		mUsedPool.PushBack( FreeFrame );
	}
	else
	{
		if ( GetAllocatedCount() < mPoolMaxSize )
		{
			FreeFrame = new TFramePixels( FrameMeta, "TFramePool - alloc" );
			BufferString<1000> Debug;
			Debug << "Allocating new block; " << PtrToInt(FreeFrame) << " pool size; " << GetAllocatedCount();
			Unity::DebugLog( Debug );

			if ( FreeFrame )
				mUsedPool.PushBack( FreeFrame );
		}
		else
		{
			if ( SHOW_POOL_FULL_MESSAGE )
			{
				BufferString<1000> Debug;
				Debug << "Frame pool is full (" << GetAllocatedCount() << ")";
				Unity::DebugLog( Debug );
			}
		}
	}
	
	//	set outgoing owner name
	if ( FreeFrame )
		FreeFrame->mDebugOwner = Owner;

	return FreeFrame;
}

bool TFramePool::Free(TFramePixels* pFrame)
{
	if ( !pFrame )
		return false;

	ofScopeTimerWarning Timer(__FUNCTION__,2);
	ofMutex::ScopedLock lock( mPoolLock );

	bool Removed = false;

	//	find in used pool
	int UsedIndex = mUsedPool.FindIndex( pFrame );
	if ( UsedIndex == -1 )
	{
		//	missing from pool
		assert( UsedIndex != -1 );
		return false;
	}
	
	//	put into free pool
	pFrame->mDebugOwner = "TFramePool - free";
	mFreePool.PushBack( pFrame );
	mUsedPool.RemoveBlock( UsedIndex, 1 );

	return true;
}




TFramePixels::TFramePixels(TFrameMeta Meta,const char* Owner) :
	mMeta		( Meta ),
	mDebugOwner	( Owner )
{
	mPixels.SetSize( mMeta.mWidth * mMeta.mHeight * mMeta.mChannels );
}

void TFramePixels::SetColour(const TColour& Colour)
{
	for ( int i=0;	i<mPixels.GetSize();	i+=mMeta.mChannels )
	{
		memcpy( &mPixels[i], &Colour, mMeta.mChannels );
	}
}