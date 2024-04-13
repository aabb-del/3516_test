#include "expand.h"


extern VENC_STREAM_CALLBACK_FUNC_T gVencStreamCallback;
void VENC_SetStreamCallback(VENC_STREAM_CALLBACK_FUNC_T Callback)
{
    gVencStreamCallback = Callback;
}
