#pragma once

#include <ofxSoylent.h>
#include "SoyDecoder.h"
#include "UnityDevice.h"


#if defined(ENABLE_DX11)
interface ID3D11Texture2D;
class TUnityDevice_DX11;
#endif


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
	TFastTextureUploadThread(TFastTexture& Parent,TUnityDevice_DX11& Device) :
		SoyThread	( "TFastTextureUploadThread" ),
		mParent		( Parent ),
		mDevice		( Device )
	{
	}
	
	virtual void	threadedFunction();

public:
	TFastTexture&		mParent;
	TUnityDevice_DX11&	mDevice;
};

//	instance of a video texture
class TFastTexture
{
public:
	TFastTexture(SoyRef Ref,TFramePool& FramePool);
	~TFastTexture();

	SoyRef				GetRef() const			{	return mRef;	}

	void				OnDynamicTextureChanged(SoyTime Timestamp);
	void				OnPostRender(TUnityDevice_DX11& Device);	//	callback from unity render thread

	bool				SetTexture(Unity::TTexture TargetTexture,TUnityDevice& Device);
	bool				SetVideo(const std::wstring& Filename,TUnityDevice& Device);
	void				SetState(TFastVideoState::Type State);

	SoyTime				GetFrameTime();
	void				SetFrameTime(SoyTime Time);

	bool				UpdateDynamicTexture(TUnityDevice_DX11& Device);

private:
	bool				CreateDynamicTexture(TUnityDevice_DX11& Device);
	bool				CreateUploadThread(TUnityDevice_DX11& Device);

	void				DeleteTargetTexture();
	void				DeleteDynamicTexture();
	void				DeleteDecoderThread();
	void				DeleteUploadThread();

	void				UpdateFrameTime();

private:
	ofMutex					mRenderLock;		//	lock while rendering (from a different thread) so we don't deallocate mid-render
	TFastVideoState::Type	mState;
	ofMutexT<SoyTime>		mFrame;
	ofMutexT<SoyTime>		mLastUpdateTime;
	TFrameBuffer			mFrameBuffer;
	SoyRef					mRef;

	TFramePool&						mFramePool;

	ofMutex							mDynamicTextureLock;
	Unity::TTexture                 mDynamicTexture;
	SoyTime							mDynamicTextureFrame;	//	frame in current dynamic texture
	bool							mDynamicTextureChanged;	//	locked via mDynamicTextureLock

	Unity::TTexture                 mTargetTexture;
	ofPtr<TDecodeThread>			mDecoderThread;
	ofPtr<TFastTextureUploadThread>	mUploadThread;
};

