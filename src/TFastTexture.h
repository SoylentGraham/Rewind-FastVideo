#pragma once

#include <ofxSoylent.h>
#include "SoyDecoder.h"
#include "UnityDevice.h"


interface ID3D11Texture2D;
class TUnityDevice_DX11;

//	instance of a video texture
class TFastTexture
{
public:
	TFastTexture(SoyRef Ref,TFramePool& FramePool);
	~TFastTexture();

	SoyRef				GetRef() const			{	return mRef;	}

	void				OnPostRender(TUnityDevice_DX11& Device);	//	callback from unity render thread

	bool				SetTexture(ID3D11Texture2D* TargetTexture,TUnityDevice_DX11& Device);
	bool				SetVideo(const std::wstring& Filename,TUnityDevice_DX11& Device);

private:
	bool				CreateDynamicTexture(TUnityDevice_DX11& Device);

	void				DeleteTargetTexture();
	void				DeleteDynamicTexture();
	void				DeleteDecoderThread();

private:
	TFramePool&				mFramePool;
	TAutoRelease<ID3D11Texture2D>	mDynamicTexture;
	TAutoRelease<ID3D11Texture2D>	mTargetTexture;
	SoyRef					mRef;
	ofPtr<TDecodeThread>	mDecoderThread;
};

