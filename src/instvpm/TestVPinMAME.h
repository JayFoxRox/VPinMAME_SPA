#ifndef TESTVPinSPA_H
#define TESTVPinSPA_H
#if !defined(__GNUC__) || (__GNUC__ == 3 && __GNUC_MINOR__ >= 4) || (__GNUC__ >= 4)	// GCC supports "pragma once" correctly since 3.4
#pragma once
#endif

const char* GetInstalledVersion(char* szVersion, int iSize);
void RunTest(HWND hWnd);

#endif // TESTVPinSPA_H
