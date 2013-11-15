#include "SoyDecoder.h"


#if defined(ENABLE_DECODER_QTKIT)
TDecoder_Qtkit::TDecoder_Qtkit() :
	moviePlayer	( nullptr )
{
}
#endif


#if defined(ENABLE_DECODER_QTKIT)
TDecoder_Qtkit::~TDecoder_Qtkit()
{
}
#endif


#if defined(ENABLE_DECODER_QTKIT)
bool TDecoder_Qtkit::Init(const std::wstring& Filename)
{
}
#endif


#if defined(ENABLE_DECODER_QTKIT)
bool TDecoder_Qtkit::PeekNextFrame(TFrameMeta& FrameMeta)
{
	
}
#endif


#if defined(ENABLE_DECODER_QTKIT)
bool TDecoder_Qtkit::DecodeNextFrame(TFramePixels& OutFrame,SoyTime MinTimestamp,bool& TryAgain)
{
	
}
#endif

