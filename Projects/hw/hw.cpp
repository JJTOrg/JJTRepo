#define HW_API _declspec(dllexport)
#include "hw.h"

#define XON 0x11
#define XOFF 0x13
#define MAXBLOCK 2048
OVERLAPPED g_osRead, g_osWrite;		// �����ص���/д
HANDLE g_hCom;				// ���пھ��
HANDLE g_thread;				// �������߳�
HANDLE g_hPostMsgEvent;	// ����WM_COMMNOTIFY��Ϣ���¼�����ʱ�䴦�����ź�״̬ʱ�ſ��Է�����Ϣ
BOOL g_bConnected;			// ��־�ʹ��ڵ�����״̬
HWND g_hTermWnd;			// ����WM_COMMNOTIFY��Ϣ����ͼ����

// ���ܣ��򿪴���
// ���룺�˿ں�
// �����FALSE:ʧ�� TRUE:�ɹ�
BOOL OpenConnection(int port)
{
	COMMTIMEOUTS TimeOuts;
	char szMsg[255];
	sprintf( szMsg, "\\\\.\\COM%d", port );
	g_hCom = CreateFile(szMsg, GENERIC_READ | GENERIC_WRITE, 0, NULL,
			OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,NULL); // �ص���ʽ
	if(g_hCom == INVALID_HANDLE_VALUE)
		return FALSE;

		// ���������豸����������д��������С��ΪMAXBLOK
	SetupComm(g_hCom, MAXBLOCK, MAXBLOCK);

	// ���ü����¼�EV_RXCHAR����ʱ���ʾ���ܵ��κ��ַ�������ܻ�������
	SetCommMask(g_hCom, EV_RXCHAR);

    // �Ѽ����ʱ��Ϊ��󣬰��ܳ�ʱ��Ϊ0������ReadFile�������ز���ɲ���
	TimeOuts.ReadIntervalTimeout=MAXDWORD; 
	TimeOuts.ReadTotalTimeoutMultiplier=0; 
	TimeOuts.ReadTotalTimeoutConstant=0; 
    /* ����д��ʱ��ָ��WriteComm��Ա�����е�GetOverlappedResult�����ĵȴ�ʱ��*/
	TimeOuts.WriteTotalTimeoutMultiplier=50;
	TimeOuts.WriteTotalTimeoutConstant=2000;
    SetCommTimeouts(g_hCom, &TimeOuts);


	if((g_hPostMsgEvent=CreateEvent(NULL, TRUE, TRUE, NULL)) == NULL)
		return FALSE;
	
	// ��ʼ�������ص���/д��OVERLAPPED�ṹ
	memset(&g_osRead, 0, sizeof(OVERLAPPED));
	memset(&g_osWrite, 0, sizeof(OVERLAPPED));
	
	// Ϊ�ص��������¼������ֹ����ã���ʼ��Ϊ���źŵ�
	if((g_osRead.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL)) == NULL)
		return FALSE;
	
	// Ϊ�ص�д�����¼������ֹ����ã���ʼ��Ϊ���źŵ�
	if((g_osWrite.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL)) == NULL)
		return FALSE;

	return TRUE;
}

// ���ܣ����ô����豸����
// ���룺������
// �����FALSE:ʧ�� TRUE:�ɹ�
BOOL ConfigConnection(int iBaud)
{
	DCB dcb;
	int m_nFlowCtrl=0;
	if(!GetCommState(g_hCom, &dcb))
		return FALSE;
	
	dcb.fBinary = TRUE;
	dcb.BaudRate = iBaud;		// ������
	dcb.ByteSize = 8;	// ÿ�ֽ�λ��
	dcb.fParity = TRUE;
	dcb.Parity = NOPARITY;		// ��У��
	dcb.StopBits = ONESTOPBIT;	// ֹͣλ
	
	// Ӳ������������
	dcb.fOutxCtsFlow = m_nFlowCtrl==1;
	dcb.fRtsControl = m_nFlowCtrl==1?
		RTS_CONTROL_HANDSHAKE:RTS_CONTROL_ENABLE;
	
	// XON/XOFF����������
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
		// ����ǰͨѶ�豸״̬
		ClearCommError(g_hCom,&dwErrorFlags,&ComStat);
		if(ComStat.cbInQue){					// �н����ַ�
			// ���޵ȴ�WM_COMMNOTIFY��Ϣ��������
			WaitForSingleObject(g_hPostMsgEvent, INFINITE);
			ResetEvent(g_hPostMsgEvent);
			// ֪ͨ��ͼ
			PostMessage(g_hTermWnd, WM_COMMNOTIFY, EV_RXCHAR, 0);
			continue;
		}
		dwMask = 0;
		if(!WaitCommEvent(g_hCom, &dwMask, &os)){ // �ص�����
			if(GetLastError() == ERROR_IO_PENDING)
				// ���޵ȴ��ص�������� 
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
	g_thread = CreateThread(NULL,0,CommProc,NULL,0,NULL);// �����������߳�
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



// ���ܣ�����������
// ���룺buf:���ܻ�����ָ�� dwLenth:����������
// ��������������ݳ���
DWORD ReadComm(char *buf,DWORD dwLength)
{
	DWORD length = 0;
	COMSTAT ComStat;	
	DWORD dwErrorFlags;
	
	// ��������־���豸��ǰ״̬��Ϣ
	ClearCommError(g_hCom, &dwErrorFlags, &ComStat);
	
	// ���ָ��Ҫ�����ַ������ڽ��ջ�������ʵ���ַ�����ȡ��Сֵ
	length=min(dwLength, ComStat.cbInQue);
	
	ReadFile(g_hCom, buf, length, &length, &g_osRead);

	PurgeComm(g_hCom,PURGE_TXCLEAR);//������ͻ�����
    PurgeComm(g_hCom,PURGE_RXCLEAR);//������ܻ�����

	return length;
}

// ���ܣ��򴮿�д������
// ���룺buf:����ָ�� dwLength:д�볤��
// ������ɹ�:д������ݳ���  ʧ��:�������
DWORD WriteComm(char *buf,DWORD dwLength)
{
	BOOL fState;
	DWORD length = dwLength;
	COMSTAT ComStat;
	DWORD dwErrorFlags;
	
	// ��������־���豸��ǰ״̬��Ϣ
	ClearCommError(g_hCom, &dwErrorFlags, &ComStat);
	fState=WriteFile(g_hCom, buf, length, &length, &g_osWrite);
	if(!fState){
		// ���д������δ��ɣ������ȴ�
		if(GetLastError() == ERROR_IO_PENDING)
			GetOverlappedResult(g_hCom, &g_osWrite, &length, TRUE);// �ȴ�
		else
			length = 0;
	}
	return length;
}


//�ر�����
BOOL CloseConnection()
{
	// ����Ѿ����Ҷϣ�����
	if(!g_bConnected) 
		return FALSE;
	g_bConnected = FALSE;
	
	//����CommProc�߳���WaitSingleObject�����ĵȴ�
	SetEvent(g_hPostMsgEvent); 
	
	//����CommProc�߳���WaitCommEvent�ĵȴ�
	SetCommMask(g_hCom, 0); 
	
	//�ȴ������߳���ֹ
	WaitForSingleObject(g_thread, INFINITE);
	g_thread = NULL;
	
	// ɾ���¼����
	if(g_hPostMsgEvent)
		CloseHandle(g_hPostMsgEvent);
	if(g_osRead.hEvent)
		CloseHandle(g_osRead.hEvent);
	if(g_osWrite.hEvent)
		CloseHandle(g_osWrite.hEvent);

	// �رմ����豸���
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
		else if (ch=='3')//������������������ָ��
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