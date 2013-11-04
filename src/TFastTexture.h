#pragma once

#include <ofxSoylent.h>
#include "SoyDecoder.h"
#include "UnityDevice.h"


interface ID3D11Texture2D;
class TUnityDevice_DX11;


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

//	instance of a video texture
class TFastTexture
{
public:
	TFastTexture(SoyRef Ref,TFramePool& FramePool);
	~TFastTexture();

	SoyRef				GetRef() const			{	return mRef;	}

	void				OnRenderedFrame(SoyTime Timestamp);
	void				OnPostRender(TUnityDevice_DX11& Device);	//	callback from unity render thread

	bool				SetTexture(ID3D11Texture2D* TargetTexture,TUnityDevice_DX11& Device);
	bool				SetVideo(const std::wstring& Filename,TUnityDevice_DX11& Device);
	void				SetState(TFastVideoState::Type State);

	SoyTime				GetFrameTime();
	void				SetFrameTime(SoyTime Time);

private:
	bool				CreateDynamicTexture(TUnityDevice_DX11& Device);

	void				DeleteTargetTexture();
	void				DeleteDynamicTexture();
	void				DeleteDecoderThread();

	void				UpdateFrameTime();

private:
	ofMutex					mRenderLock;		//	lock while rendering (from a different thread) so we don't deallocate mid-render
	TFastVideoState::Type	mState;
	ofMutexT<SoyTime>		mFrame;
	ofMutexT<SoyTime>		mLastUpdateTime;
	TFrameBuffer			mFrameBuffer;
	SoyRef					mRef;

	TFramePool&						mFramePool;
	TAutoRelease<ID3D11Texture2D>	mDynamicTexture;
	TAutoRelease<ID3D11Texture2D>	mTargetTexture;
	ofPtr<TDecodeThread>			mDecoderThread;
};

