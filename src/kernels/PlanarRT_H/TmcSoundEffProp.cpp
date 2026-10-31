// TmcSoundEffProp.cpp : implementation file
//

/// @file TmcSoundEffProp.cpp
/// @brief Набор страниц «Sound Effects»: конструкторы и добавление страниц.
///
/// Часть модуля «Оболочка счётного ядра». Реализация `CTmcSoundEffProp`.
/// @see https://www.tamic.ru/docs/code/kernel-shell.html#dialogs
#include "StdAfx.h"
#include "PlanRT_H.h"
#include "TmcSoundEffProp.h"


/////////////////////////////////////////////////////////////////////////////
// CTmcSoundEffProp

IMPLEMENT_DYNAMIC(CTmcSoundEffProp, CPropertySheet)

CTmcSoundEffProp::CTmcSoundEffProp(LPCTSTR pszCaption, CWnd* pParentWnd, UINT iSelectPage)
	:CPropertySheet( pszCaption, pParentWnd, iSelectPage)
{
	AddPage( &cMelody1 );
	AddPage( &cMelody2 );
	cMelody1.csMelodyName.Format("Step finish calculation");
	cMelody2.csMelodyName.Format("All finish calculation");
	return;
}

CTmcSoundEffProp::~CTmcSoundEffProp()
{
}


BEGIN_MESSAGE_MAP(CTmcSoundEffProp, CPropertySheet)
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CTmcSoundEffProp message handlers
