// stdafx.h : include file for standard system include files,
// or project specific include files that are used frequently, but
// are changed infrequently
//

/// @file STDAFX.H
/// @brief Общий заголовок файлов библиотеки TMCIndan: подключает `afx.h`/`afxwin.h`.
///
/// В Qt6-версии `afx.h` и `afxwin.h` предоставляет слой совместимости `compat/`
/// (MFC поверх Qt): `CString`, `CWnd`, функции Win32. Подключается первым
/// в каждом `.cpp` библиотеки.
/// @see https://www.tamic.ru/docs/code/compat.html

#pragma once

#define _ATL_CSTRING_EXPLICIT_CONSTRUCTORS	// some CString constructors will be explicit

#ifndef VC_EXTRALEAN
#endif

#include <afx.h>
#include <afxwin.h>         // MFC core and standard components

// TODO: reference additional headers your program requires here
