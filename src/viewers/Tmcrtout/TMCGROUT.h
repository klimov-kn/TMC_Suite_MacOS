// TMCGROUT.h : main header file for the TMCGROUT application
//

/// @file TMCGROUT.h
/// @brief Класс приложения MFC-версии TMCROS и подключение номеров ресурсов.
///
/// Часть модуля «Вьювер TMCROS». В Qt6-версии класс не используется (его реализация
/// `tmcgrout.cpp` в сборку не входит, приложение — `CWinApp` слоя совместимости,
/// точка входа — `src/viewers/Tmcrtout/qt/main.cpp`); заголовок включается ради `resource.h`.
/// @see https://www.tamic.ru/docs/code/tmcros.html#files
#ifndef __AFXWIN_H__
	#error include 'stdafx.h' before including this file for PCH
#endif

#include "resource.h"       // main symbols

/////////////////////////////////////////////////////////////////////////////
// CTMCGROUTApp:
// See TMCGROUT.cpp for the implementation of this class
//

/// @brief Приложение MFC-версии TMCROS. В Qt6-версии не создаётся.
class CTMCGROUTApp : public CWinApp
{
public:
	/// @brief Создаёт приложение.
	CTMCGROUTApp();

// Overrides
	// ClassWizard generated virtual function overrides
	public:
	/// @brief Запуск MFC-приложения: шаблон документа, главное окно, разбор командной строки.
	/// @return TRUE — приложение запущено.
	virtual BOOL InitInstance();

// Implementation

	/// @brief Команда Help > About.
	afx_msg void OnAppAbout();
	DECLARE_MESSAGE_MAP()
};


/////////////////////////////////////////////////////////////////////////////
