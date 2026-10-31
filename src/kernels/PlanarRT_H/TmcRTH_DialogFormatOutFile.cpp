// TmcRTH_DialogFormatOutFile.cpp : implementation file
//

/// @file TmcRTH_DialogFormatOutFile.cpp
/// @brief Окно форматов чисел в выходных файлах: обмен данными и кнопка «Set default».
///
/// Часть модуля «Оболочка счётного ядра». Реализация `CTmcRTH_DialogFormatOutFile`.
/// @see https://www.tamic.ru/docs/code/kernel-shell.html#settings
#include "StdAfx.h"
#include "PlanRT_H.h"
#include "TmcRTH_DialogFormatOutFile.h"


/////////////////////////////////////////////////////////////////////////////
// CTmcRTH_DialogFormatOutFile dialog


CTmcRTH_DialogFormatOutFile::CTmcRTH_DialogFormatOutFile(CWnd* pParent /*=NULL*/)
	: CDialog(CTmcRTH_DialogFormatOutFile::IDD, pParent)
{
	IsSetDefaultFormat = FALSE;
}


void CTmcRTH_DialogFormatOutFile::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	DDX_Text(pDX, IDC_TMCRTHFORMUOTF_NT,  m_csnT);
	DDX_Text(pDX, IDC_TMCRTHFORMUOTF_DT,  m_csdTcurrent);
	DDX_Text(pDX, IDC_TMCRTHFORMUOTF_NBL,  m_csnBlock);
	DDX_Text(pDX, IDC_TMCRTHFORMUOTF_INP,  m_csdInp);
	DDX_Text(pDX, IDC_TMCRTHFORMUOTF_OUT,  m_csdOut);
	DDX_Text(pDX, IDC_TMCRTHFORMUOTFIELD,  m_csdOutField);
}


BEGIN_MESSAGE_MAP(CTmcRTH_DialogFormatOutFile, CDialog)
	ON_BN_CLICKED(IDOKDefaultFormat, OnSetDefaultFormat)
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CTmcRTH_DialogFormatOutFile message handlers

void CTmcRTH_DialogFormatOutFile::OnSetDefaultFormat() 
{
	IsSetDefaultFormat = TRUE;
	CDialog::OnOK();
	return;
}
