#include <windows.h>
#include "conproc.h"
#include "quakedef.h"

HANDLE heventDone;
HANDLE hfileBuffer;
HANDLE heventChildSend;
HANDLE heventParentSend;
HANDLE hStdout;
HANDLE hStdin;

DWORD RequestProc(DWORD dwNichts);
LPVOID GetMappedBuffer(HANDLE hfileBuffer);
void ReleaseMappedBuffer(LPVOID pBuffer);
BOOL GetScreenBufferLines(int* piLines);
BOOL SetScreenBufferLines(int iLines);
BOOL ReadText(LPTSTR pszText, int iBeginLine, int iEndLine);
BOOL WriteText(LPCTSTR szText);
int CharToCode(char c);
BOOL SetConsoleCXCY(HANDLE hStdout, int cx, int cy);


void InitConProc(HANDLE hFile, HANDLE heventParent, HANDLE heventChild)
{
	DWORD dwID;

	// ignore if we don't have all the events.
	if (!hFile || !heventParent || !heventChild)
		return;

	hfileBuffer = hFile;
	heventParentSend = heventParent;
	heventChildSend = heventChild;

	// so we'll know when to go away.
	heventDone = CreateEvent(nullptr, FALSE, FALSE, nullptr);

	if (!heventDone)
	{
		Con_SafePrintf("Couldn't create heventDone\n");
		return;
	}

	if (!CreateThread(nullptr,
	                  0,
	                  reinterpret_cast<LPTHREAD_START_ROUTINE>(RequestProc),
	                  nullptr,
	                  0,
	                  &dwID))
	{
		CloseHandle(heventDone);
		Con_SafePrintf("Couldn't create QHOST thread\n");
		return;
	}

	// save off the input/output handles.
	hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
	hStdin = GetStdHandle(STD_INPUT_HANDLE);

	// force 80 character width, at least 25 character height
	SetConsoleCXCY(hStdout, 80, 25);
}


void DeinitConProc()
{
	if (heventDone)
		SetEvent(heventDone);
}


DWORD RequestProc(DWORD dwNichts)
{
	HANDLE heventWait[2];

	heventWait[0] = heventParentSend;
	heventWait[1] = heventDone;

	while (true)
	{
		auto dwRet = WaitForMultipleObjects(2, heventWait, FALSE, INFINITE);

		// heventDone fired, so we're exiting.
		if (dwRet == WAIT_OBJECT_0 + 1)
			break;

		auto pBuffer = static_cast<int *>(GetMappedBuffer(hfileBuffer));

		// hfileBuffer is invalid.  Just leave.
		if (!pBuffer)
		{
			Con_SafePrintf("Invalid hfileBuffer\n");
			break;
		}

		switch (pBuffer[0])
		{
		case CCOM_WRITE_TEXT:
			// Param1 : Text
			pBuffer[0] = WriteText(reinterpret_cast<LPCTSTR>(pBuffer + 1));
			break;

		case CCOM_GET_TEXT:
			pBuffer[0] = ReadText(reinterpret_cast<LPTSTR>(pBuffer + 1), pBuffer[1],  pBuffer[2]);
			break;

		case CCOM_GET_SCR_LINES:
			// No params
			pBuffer[0] = GetScreenBufferLines(&pBuffer[1]);
			break;

		case CCOM_SET_SCR_LINES:
			// Param1 : Number of lines
			pBuffer[0] = SetScreenBufferLines(pBuffer[1]);
			break;
		default: break;
		}

		ReleaseMappedBuffer(pBuffer);
		SetEvent(heventChildSend);
	}

	return 0;
}


LPVOID GetMappedBuffer(HANDLE hfileBuffer)
{
	auto pBuffer = MapViewOfFile(hfileBuffer, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, 0);

	return pBuffer;
}


void ReleaseMappedBuffer(LPVOID pBuffer)
{
	UnmapViewOfFile(pBuffer);
}


BOOL GetScreenBufferLines(int* piLines)
{
	CONSOLE_SCREEN_BUFFER_INFO info;

	auto bRet = GetConsoleScreenBufferInfo(hStdout, &info);

	if (bRet)
		*piLines = info.dwSize.Y;

	return bRet;
}


BOOL SetScreenBufferLines(int iLines)
{
	return SetConsoleCXCY(hStdout, 80, iLines);
}


BOOL ReadText(LPTSTR pszText, int iBeginLine, int iEndLine)
{
	COORD coord;
	DWORD dwRead;

	coord.X = 0;
	coord.Y = iBeginLine;

	auto bRet = ReadConsoleOutputCharacter(
		hStdout,
		pszText,
		80 * (iEndLine - iBeginLine + 1),
		coord,
		&dwRead);

	// Make sure it's nullptr terminated.
	if (bRet)
		pszText[dwRead] = '\0';

	return bRet;
}


BOOL WriteText(LPCTSTR szText)
{
	DWORD dwWritten;
	INPUT_RECORD rec;

	auto sz = const_cast<LPTSTR>(szText);

	while (*sz)
	{
		// 13 is the code for a carriage return (\n) instead of 10.
		if (*sz == 10)
			*sz = 13;

		char upper = toupper(*sz);

		rec.EventType = KEY_EVENT;
		rec.Event.KeyEvent.bKeyDown = TRUE;
		rec.Event.KeyEvent.wRepeatCount = 1;
		rec.Event.KeyEvent.wVirtualKeyCode = upper;
		rec.Event.KeyEvent.wVirtualScanCode = CharToCode(*sz);
		rec.Event.KeyEvent.uChar.AsciiChar = *sz;
		rec.Event.KeyEvent.uChar.UnicodeChar = *sz;
		rec.Event.KeyEvent.dwControlKeyState = isupper(*sz) ? 0x80 : 0x0;

		WriteConsoleInput(
			hStdin,
			&rec,
			1,
			&dwWritten);

		rec.Event.KeyEvent.bKeyDown = FALSE;

		WriteConsoleInput(
			hStdin,
			&rec,
			1,
			&dwWritten);

		sz++;
	}

	return TRUE;
}


int CharToCode(char c)
{
	char upper = toupper(c);

	switch (c)
	{
	case 13:
		return 28;

	default:
		break;
	}

	if (isalpha(c))
		return 30 + upper - 65;

	if (isdigit(c))
		return 1 + upper - 47;

	return c;
}


BOOL SetConsoleCXCY(HANDLE hStdout, int cx, int cy)
{
	CONSOLE_SCREEN_BUFFER_INFO info;

	auto coordMax = GetLargestConsoleWindowSize(hStdout);

	if (cy > coordMax.Y)
		cy = coordMax.Y;

	if (cx > coordMax.X)
		cx = coordMax.X;

	if (!GetConsoleScreenBufferInfo(hStdout, &info))
		return FALSE;

	// height
	info.srWindow.Left = 0;
	info.srWindow.Right = info.dwSize.X - 1;
	info.srWindow.Top = 0;
	info.srWindow.Bottom = cy - 1;

	if (cy < info.dwSize.Y)
	{
		if (!SetConsoleWindowInfo(hStdout, TRUE, &info.srWindow))
			return FALSE;

		info.dwSize.Y = cy;

		if (!SetConsoleScreenBufferSize(hStdout, info.dwSize))
			return FALSE;
	}
	else if (cy > info.dwSize.Y)
	{
		info.dwSize.Y = cy;

		if (!SetConsoleScreenBufferSize(hStdout, info.dwSize))
			return FALSE;

		if (!SetConsoleWindowInfo(hStdout, TRUE, &info.srWindow))
			return FALSE;
	}

	if (!GetConsoleScreenBufferInfo(hStdout, &info))
		return FALSE;

	// width
	info.srWindow.Left = 0;
	info.srWindow.Right = cx - 1;
	info.srWindow.Top = 0;
	info.srWindow.Bottom = info.dwSize.Y - 1;

	if (cx < info.dwSize.X)
	{
		if (!SetConsoleWindowInfo(hStdout, TRUE, &info.srWindow))
			return FALSE;

		info.dwSize.X = cx;

		if (!SetConsoleScreenBufferSize(hStdout, info.dwSize))
			return FALSE;
	}
	else if (cx > info.dwSize.X)
	{
		info.dwSize.X = cx;

		if (!SetConsoleScreenBufferSize(hStdout, info.dwSize))
			return FALSE;

		if (!SetConsoleWindowInfo(hStdout, TRUE, &info.srWindow))
			return FALSE;
	}

	return TRUE;
}
