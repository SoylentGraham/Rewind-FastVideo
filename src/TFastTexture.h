#pragma once

#include <ofxSoylent.h>
#include "SoyDecoder.h"
#include "UnityDevice.h"


#if defined(ENABLE_DX11)
interface ID3D11Texture2D;
class TUnityDevice_DX11;
#endif

//	instance of a video texture
class TFastTexture
{
public:
	TFastTexture(SoyRef Ref,TFramePool& FramePool);
	~TFastTexture();

	SoyRef				GetRef() const			{	return mRef;	}

	void				OnPostRender(TUnityDevice_DX11& Device);	//	callback from unity render thread

	bool				SetTexture(Unity::TTexture TargetTexture,TUnityDevice& Device);
	bool				SetVideo(const std::wstring& Filename);

private:
	bool				CreateDynamicTexture(TUnityDevice_DX11& Device);

	void				DeleteTargetTexture();
	void				DeleteDynamicTexture();
	void				DeleteDecoderThread();

private:
	TFramePool&				mFramePool;
    Unity::TTexture         mDynamicTexture;
    Unity::TTexture         mTargetTexture;
	SoyRef					mRef;
	ofPtr<TDecodeThread>	mDecoderThread;
};

