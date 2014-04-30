#pragma once

#include <ofxSoylent.h>
#include "SoyDecoder.h"
#include "UnityDevice.h"


namespace TFastVideoState
{
	enum Type
	{
		FirstFrame=0,
		Playing,
		Paused,
	};

	const char*	ToString(Type State);
};

class TFastTextureUploadThread : public SoyThread
{
public:
	TFastTextureUploadThread(TFastTexture& Parent,TUnityDevice& Device);
	~TFastTextureUploadThread();

	virtual void			threadedFunction();
	void					Update();

	bool					IsValid();				//	check was setup okay
	bool					CopyToTarget(Unity::TTexture TargetTexture,SoyTime& TargetTextureFrame);
	
private:
	bool					CreateDynamicTexture();
	void					DeleteDynamicTexture();

	TUnityDevice&			GetDevice()		{	return mDevice;	}

public:
	TFastTexture&			mParent;
	TUnityDevice&			mDevice;

	ofMutexTimed			mDynamicTextureLock;
	Unity::TDynamicTexture	mDynamicTexture;
	SoyTime					mDynamicTextureFrame;	//	frame in current dynamic texture
	bool					mDynamicTextureChanged;	//	locked via mDynamicTextureLock
};

//	instance of a video texture
class TFastTexture : public SoyThread
{
public:
	TFastTexture(SoyRef Ref,TFramePool& FramePool);
	~TFastTexture();

	SoyRef				GetRef() const			{	return mRef;	}

	void				OnTargetTextureChanged();
	void				OnPostRender();	//	callback from unity render thread

	bool				SetTexture(Unity::TTexture TargetTexture);
	bool				SetVideo(const std::wstring& Filename);
	void				SetState(TFastVideoState::Type State);
	void				SetDevice(ofPtr<TUnityDevice> Device);
	void				SetLooping(bool EnableLooping);
   
	SoyTime				GetFrameTime();
	void				InitFrameTime();
	void				SetFrameTime(SoyTime Time);

	bool				UpdateFrameTexture(Unity::TTexture Texture,SoyTime& FrameCopied);			//	copy latest frame to texture. returns if changed
	bool				UpdateFrameTexture(Unity::TDynamicTexture Texture,SoyTime& FrameCopied);	//	copy latest frame to texture. returns if changed

	Unity::TTexture		GetTargetTexture()		{	return mTargetTexture;	}

private:
	void				Update();
	void				UpdateFrameTime();
	SoyTime				GetNowTime();
	virtual void		threadedFunction();

	bool				CreateUploadThread(bool IsRenderThread);

	void				DeleteTargetTexture();
	void				DeleteDecoderThread();
	void				WaitForAllDeadDecoderThreads();
	bool				WaitForLastDeadDecoderThread();
	void				DeleteUploadThread();
	void				OnDecoderInitFailed(FastVideoError Error);
  
    TUnityDevice&       GetDevice();

public:
	TFramePool&				mFramePool;

private:
	ofMutex					mRenderLock;		//	lock while rendering (from a different thread) so we don't deallocate mid-render
	TFastVideoState::Type	mState;
	bool					mLooping;
	ofMutexT<SoyTime>		mFrame;
	ofMutexT<SoyTime>		mLastUpdateTime;
	ofMutexT<SoyTime>		mStartTime;
	TFrameBuffer			mFrameBuffer;
	SoyRef					mRef;

	ofPtr<TUnityDevice>		mDevice;


	Unity::TTexture					mTargetTexture;
	SoyTime							mTargetTextureFrame;	//	frame of the contents of target texture
	ofMutexM<TDecodeThread*>		mDecoderThread;
	ofMutexT<Array<TDecodeThread*>>	mDeadDecoderThreads;	//	waiting to kill these off when we can
	ofPtr<TFastTextureUploadThread>	mUploadThread;
};

