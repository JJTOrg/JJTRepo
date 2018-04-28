#define HW_API _declspec(dllexport)
#include "hw.h"

#define XON 0x11
#define XOFF 0x13
#define MAXBLOCK 2048
OVERLAPPED g_osRead, g_osWrite;		// 用于重叠读/写
HANDLE g_hCom;				// 串行口句柄
HANDLE g_thread;				// 代表辅助线程
HANDLE g_hPostMsgEvent;	// 用于WM_COMMNOTIFY消息的事件对象，时间处于有信号状态时才可以发送消息
BOOL g_bConnected;			// 标志和串口的连接状态
HWND g_hTermWnd;			// 接受WM_COMMNOTIFY消息的视图窗口

// 功能：打开串口
// 输入：端口号
// 输出：FALSE:失败 TRUE:成功
BOOL OpenConnection(int port)
{
	COMMTIMEOUTS TimeOuts;
	char szMsg[255];
	sprintf( szMsg, "\\\\.\\COM%d", port );
	g_hCom = CreateFile(szMsg, GENERIC_READ | GENERIC_WRITE, 0, NULL,
			OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,NULL); // 重叠方式
	if(g_hCom == INVALID_HANDLE_VALUE)
		return FALSE;

		// 重新设置设备参数，读、写缓冲区大小均为MAXBLOK
	SetupComm(g_hCom, MAXBLOCK, MAXBLOCK);

	// 设置监视事件EV_RXCHAR，该时间表示接受到任何字符放入接受缓冲区中
	SetCommMask(g_hCom, EV_RXCHAR);

    // 把间隔超时设为最大，把总超时设为0将导致ReadFile立即返回并完成操作
	TimeOuts.ReadIntervalTimeout=MAXDWORD; 
	TimeOuts.ReadTotalTimeoutMultiplier=0; 
	TimeOuts.ReadTotalTimeoutConstant=0; 
    /* 设置写超时以指定WriteComm成员函数中的GetOverlappedResult函数的等待时间*/
	TimeOuts.WriteTotalTimeoutMultiplier=50;
	TimeOuts.WriteTotalTimeoutConstant=2000;
    SetCommTimeouts(g_hCom, &TimeOuts);


	if((g_hPostMsgEvent=CreateEvent(NULL, TRUE, TRUE, NULL)) == NULL)
		return FALSE;
	
	// 初始化用于重叠读/写的OVERLAPPED结构
	memset(&g_osRead, 0, sizeof(OVERLAPPED));
	memset(&g_osWrite, 0, sizeof(OVERLAPPED));
	
	// 为重叠读创建事件对象，手工重置，初始化为无信号的
	if((g_osRead.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL)) == NULL)
		return FALSE;
	
	// 为重叠写创建事件对象，手工重置，初始化为无信号的
	if((g_osWrite.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL)) == NULL)
		return FALSE;

	return TRUE;
}

// 功能：设置串口设备参数
// 输入：波特率
// 输出：FALSE:失败 TRUE:成功
BOOL ConfigConnection(int iBaud)
{
	DCB dcb;
	int m_nFlowCtrl=0;
	if(!GetCommState(g_hCom, &dcb))
		return FALSE;
	
	dcb.fBinary = TRUE;
	dcb.BaudRate = iBaud;		// 波特率
	dcb.ByteSize = 8;	// 每字节位数
	dcb.fParity = TRUE;
	dcb.Parity = NOPARITY;		// 无校验
	dcb.StopBits = ONESTOPBIT;	// 停止位
	
	// 硬件流控制设置
	dcb.fOutxCtsFlow = m_nFlowCtrl==1;
	dcb.fRtsControl = m_nFlowCtrl==1?
		RTS_CONTROL_HANDSHAKE:RTS_CONTROL_ENABLE;
	
	// XON/XOFF流控制设置
	dcb.fInX = dcb.fOutX = m_nFlowCtrl == 2;
	dcb.XonChar = XON;
	dcb.XoffChar = XOFF;
	dcb.XonLim = 50;
	dcb.XoffLim = 50;
	return SetCommState(g_hCom, &dcb);
}

void SetReceiveHWnd(HWND hwnd)
{
	g_hTermWnd=hwnd;
}

DWORD WINAPI CommProc(LPVOID pParam)
{
	OVERLAPPED os;
	DWORD dwMask, dwTrans;
	COMSTAT ComStat;
	DWORD dwErrorFlags;
	
	memset(&os, 0, sizeof(OVERLAPPED));
	os.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	if(os.hEvent == NULL){
	/*	MessageBox("Can't create event object!");*/
		return (UINT)-1;
	}
	while(g_bConnected){
		// 读当前通讯设备状态
		ClearCommError(g_hCom,&dwErrorFlags,&ComStat);
		if(ComStat.cbInQue){					// 有接收字符
			// 无限等待WM_COMMNOTIFY消息被处理完
			WaitForSingleObject(g_hPostMsgEvent, INFINITE);
			ResetEvent(g_hPostMsgEvent);
			// 通知视图
			PostMessage(g_hTermWnd, WM_COMMNOTIFY, EV_RXCHAR, 0);
			continue;
		}
		dwMask = 0;
		if(!WaitCommEvent(g_hCom, &dwMask, &os)){ // 重叠操作
			if(GetLastError() == ERROR_IO_PENDING)
				// 无限等待重叠操作结果 
				GetOverlappedResult(g_hCom, &os, &dwTrans, TRUE);
			else{
				CloseHandle(os.hEvent);
				return (UINT)-1;
				}
		}
	}
	CloseHandle(os.hEvent);
	return 0;
}

BOOL StartThread()
{
	g_thread = CreateThread(NULL,0,CommProc,NULL,0,NULL);// 创建并挂起线程
	if (g_thread==NULL)
	{
		CloseHandle(g_hCom);
		return FALSE;
	}
	else
	{
		g_bConnected=TRUE;
		ResumeThread(g_thread);
	}
	return TRUE;
}



// 功能：读串口数据
// 输入：buf:接受缓冲区指针 dwLenth:缓冲区长度
// 输出：读出的数据长度
DWORD ReadComm(char *buf,DWORD dwLength)
{
	DWORD length = 0;
	COMSTAT ComStat;	
	DWORD dwErrorFlags;
	
	// 清理错误标志并设备当前状态信息
	ClearCommError(g_hCom, &dwErrorFlags, &ComStat);
	
	// 如果指定要读的字符数大于接收缓冲区中实际字符数，取最小值
	length=min(dwLength, ComStat.cbInQue);
	
	ReadFile(g_hCom, buf, length, &length, &g_osRead);

	PurgeComm(g_hCom,PURGE_TXCLEAR);//清除发送缓冲区
    PurgeComm(g_hCom,PURGE_RXCLEAR);//清除接受缓冲区

	return length;
}

// 功能：向串口写入数据
// 输入：buf:数组指针 dwLength:写入长度
// 输出：成功:写入的数据长度  失败:错误代码
DWORD WriteComm(char *buf,DWORD dwLength)
{
	BOOL fState;
	DWORD length = dwLength;
	COMSTAT ComStat;
	DWORD dwErrorFlags;
	
	// 清理错误标志并设备当前状态信息
	ClearCommError(g_hCom, &dwErrorFlags, &ComStat);
	fState=WriteFile(g_hCom, buf, length, &length, &g_osWrite);
	if(!fState){
		// 如果写操作还未完成，继续等待
		if(GetLastError() == ERROR_IO_PENDING)
			GetOverlappedResult(g_hCom, &g_osWrite, &length, TRUE);// 等待
		else
			length = 0;
	}
	return length;
}


//关闭连接
BOOL CloseConnection()
{
	// 如果已经被挂断，返回
	if(!g_bConnected) 
		return FALSE;
	g_bConnected = FALSE;
	
	//结束CommProc线程中WaitSingleObject函数的等待
	SetEvent(g_hPostMsgEvent); 
	
	//结束CommProc线程中WaitCommEvent的等待
	SetCommMask(g_hCom, 0); 
	
	//等待辅助线程终止
	WaitForSingleObject(g_thread, INFINITE);
	g_thread = NULL;
	
	// 删除事件句柄
	if(g_hPostMsgEvent)
		CloseHandle(g_hPostMsgEvent);
	if(g_osRead.hEvent)
		CloseHandle(g_osRead.hEvent);
	if(g_osWrite.hEvent)
		CloseHandle(g_osWrite.hEvent);

	// 关闭串口设备句柄
	return CloseHandle(g_hCom);
}

string ConvertCharBufToString(char *buf,int nLength)
{
	string str;
	string strReadSerialBuff;
	if (nLength)
	{
// 		for (int i=0;i<nLength;i++)
// 		{
// 			str+=buf[i];
// 		}
		str=buf;
		strReadSerialBuff=str;
		str=strReadSerialBuff.substr(strReadSerialBuff.length()-1,1);
		if (str.compare("$"))
			return strReadSerialBuff;
	}
	return strReadSerialBuff;
}

void SerialCommandFx(string strReadSerialBuff,int nHeight,int nWeight,int nFoot,int nBmi)
{
	char ch;
	string str;
	while (strReadSerialBuff.length())
	{
		ch=strReadSerialBuff.at(0);
		if (ch=='0')
		{
			str=strReadSerialBuff.substr(1,4);
			nWeight=atoi(str.c_str());
			strReadSerialBuff.erase(0,6);
		}
		else if (ch=='1')
		{
			str=strReadSerialBuff.substr(1,4);
			nFoot=atoi(str.c_str());
			strReadSerialBuff.erase(0, 6);
		}
		else if (ch=='2')
		{
			str=strReadSerialBuff.substr(1,4);
			nHeight=atoi(str.c_str());
			strReadSerialBuff.erase(0, 6);
		}
		else if (ch=='3')//测量结束，计算体型指数
		{
			if (nHeight!=0)
			{
				double d=(float)nWeight/(nHeight*nHeight);
				nBmi=(int)(d*1000000+0.5);
			}
			strReadSerialBuff.erase(0,2);
		}
		else if (ch=='4')
		{
			strReadSerialBuff.erase(0,2);
		}
		else if (ch=='5')
		{
			strReadSerialBuff.erase(0,2);
		}
		else if (ch=='6')
		{
			strReadSerialBuff.erase(0,2);
		}
		else if (ch=='7')
		{
			strReadSerialBuff.erase(0,2);
		}
	}
}