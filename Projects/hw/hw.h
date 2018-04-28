#include "windows.h"
#include "stdio.h"
#include <iostream>
using namespace std;

#ifdef HW_API
#else
#define HW_API extern "C" _declspec(dllimport)
#endif

HW_API  BOOL OpenConnection(int port);
HW_API	BOOL ConfigConnection(int iBaud);
HW_API	BOOL StartThread();
HW_API	void SetReceiveHWnd(HWND hwnd);
HW_API	DWORD WINAPI CommProc(LPVOID pParam);
HW_API	DWORD ReadComm(char *buf,DWORD dwLength);
HW_API	DWORD WriteComm(char *buf,DWORD dwLength);
HW_API  BOOL CloseConnection();
HW_API	string ConvertCharBufToString(char *buf,int nLength);
HW_API	void SerialCommandFx(string strReadSerialBuff,int nHeight,int nWeight,int nFoot,int nBmi);


