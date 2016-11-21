/*
    File:       ARTSRTSPModule.h

    Contains:   A module that serves content from ARTS.

    

*/

#ifndef __ARTSRTSPMODULE_H__
#define __ARTSRTSPMODULE_H__

#include "QTSS.h"
#include "QTSS_Private.h"

extern "C"
{
    EXPORT QTSS_Error ARTSRTSPModule_Main(void* inPrivateArgs);
}

#endif //__ARTSRTSPMODULE_H__
