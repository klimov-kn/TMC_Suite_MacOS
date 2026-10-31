// TMCGROUTDIALOGView.cpp : implementation file
//

/// @file TMCGROUTDIALOGView.cpp
/// @brief Окно Viewport TMC_DN: обмен полей, доступность полей по флажкам, кнопки шрифта и цветов.
///
/// Часть модуля «Вьювер TMC_DN». Описание методов — в `tmcgroutdialogview.h`.
/// @see https://www.tamic.ru/docs/code/tmc-dn.html#axes
#include "StdAfx.h"
#include "TMCGROUT.H"
#include "TMCGROUTDIALOGView.h"


/////////////////////////////////////////////////////////////////////////////
// CTMCGROUTDIALOGView dialog


CTMCGROUTDIALOGView::CTMCGROUTDIALOGView(CWnd* pParent /*=NULL*/)
	: CDialog(CTMCGROUTDIALOGView::IDD, pParent)
{
	m_ColorGraphics = FALSE;
	m_ColorBackground = FALSE;
	m_ColorAxies = FALSE;
	m_ColorGrid = FALSE;
	m_Color = FALSE;
	m_Font  = FALSE;
	m_aXSFlag = FALSE;
	m_aYSFlag = FALSE;
	m_szDatFileName = _T("");
	m_XMax = 0.0;
	m_XMin = 0.0;
	m_YMax = 0.0;
	m_YMin = 0.0;
	m_XTypeHz1 = FALSE;
	m_XTypeKHz1 = FALSE;
	m_XTypeMHz1 = FALSE;
	m_DrawPoinFlag = FALSE;
	m_XUnit = -1;
	m_YUnit = -1;
	m_csXFormat = _T("");
	m_csYFormat = _T("");
	m_dPointSize = 0.0;
	m_LossPoint = FALSE;
	m_XTypeGHz1 = FALSE;
	m_YNormirovkaFlag = FALSE;
}


void CTMCGROUTDIALOGView::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	DDX_Check(pDX, IDC_GRPAR_AXSFLAG, m_aXSFlag);
	DDX_Check(pDX, IDC_GRPAR_AYSFLAG, m_aYSFlag);
	DDX_Text(pDX, IDC_GRPAR_DATFILENAME, m_szDatFileName);
	DDX_Text(pDX, IDC_GRPAR_XMAX, m_XMax);
	DDX_Text(pDX, IDC_GRPAR_XMIN, m_XMin);
	DDX_Text(pDX, IDC_GRPAR_YMAX, m_YMax);
	DDX_Text(pDX, IDC_GRPAR_YMIN, m_YMin);
	DDX_Check(pDX, IDC_GRPAR_POINTFLAG, m_DrawPoinFlag);
	DDX_Radio(pDX, IDC_GRPAR_XINGHZ, m_XUnit);
	DDX_Radio(pDX, IDC_GRPAR_YSKSV1, m_YUnit);
	DDX_Text(pDX, IDC_GRPAR_XFORMAT, m_csXFormat);
	DDV_MaxChars(pDX, m_csXFormat, 300);
	DDX_Text(pDX, IDC_GRPAR_YFORMAT, m_csYFormat);
	DDV_MaxChars(pDX, m_csYFormat, 300);
	DDX_Text(pDX, IDC_GRPAR_POINTSIZE, m_dPointSize);
	DDV_MinMaxDouble(pDX, m_dPointSize, 0., 100.);
	DDX_Check(pDX, IDC_GRPAR_POINTFLAG2, m_LossPoint);
	DDX_Check(pDX, IDC_GRPAR_NORMIROVYFLAG3, m_YNormirovkaFlag);
}


BEGIN_MESSAGE_MAP(CTMCGROUTDIALOGView, CDialog)
	ON_BN_CLICKED(IDC_GRPAR_XINGHZ, OnGrparXinghz)
	ON_BN_CLICKED(IDC_GRPAR_XINHZ, OnGrparXinhz)
	ON_BN_CLICKED(IDC_GRPAR_XINKHZ, OnGrparXinkhz)
	ON_BN_CLICKED(IDC_GRPAR_XINMHZ, OnGrparXinmhz)
	ON_BN_CLICKED(IDOK2, OnOk2)
	ON_BN_CLICKED(IDOK3, OnOk3)
	ON_BN_CLICKED(IDOK4, OnOk4)
	ON_BN_CLICKED(IDOK5, OnOk5)
	ON_BN_CLICKED(IDOK6, OnOk6)
	ON_BN_CLICKED(ID_FORMATDEFAULTBUTTON, OnFormatdefaultbutton)
	ON_BN_CLICKED(IDOK7, OnOk7)
	ON_BN_CLICKED(IDC_GRPAR_AXSFLAG, OnGrparAxsflag)
	ON_BN_CLICKED(IDC_GRPAR_AYSFLAG, OnGrparAysflag)
	ON_BN_CLICKED(IDC_GRPAR_POINTFLAG, OnGrparPointflag)
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CTMCGROUTDIALOGView message handlers

void CTMCGROUTDIALOGView::OnGrparXinghz() 
{
	m_XTypeHz1  = FALSE;	
	m_XTypeKHz1 = FALSE;	
	m_XTypeMHz1 = FALSE;	
	m_XTypeGHz1 = FALSE;
	m_XTypeGHz1 = TRUE;
	return;
}

void CTMCGROUTDIALOGView::OnGrparXinhz() 
{
	m_XTypeHz1  = FALSE;	
	m_XTypeKHz1 = FALSE;	
	m_XTypeMHz1 = FALSE;	
	m_XTypeGHz1 = FALSE;
	m_XTypeHz1 = TRUE;	
}

void CTMCGROUTDIALOGView::OnGrparXinkhz() 
{
	m_XTypeHz1  = FALSE;	
	m_XTypeKHz1 = FALSE;	
	m_XTypeMHz1 = FALSE;	
	m_XTypeGHz1 = FALSE;
	m_XTypeKHz1 = TRUE;	
}

void CTMCGROUTDIALOGView::OnGrparXinmhz() 
{
	m_XTypeHz1  = FALSE;	
	m_XTypeKHz1 = FALSE;	
	m_XTypeMHz1 = FALSE;	
	m_XTypeGHz1 = FALSE;
	m_XTypeMHz1 = TRUE;	
}


void CTMCGROUTDIALOGView::OnOk2() 
{
	m_Font = TRUE;
	CDialog::OnOK();
}

void CTMCGROUTDIALOGView::OnOk3() 
{
	m_Color = TRUE;
	CDialog::OnOK();
}

void CTMCGROUTDIALOGView::OnOk4() 
{
	m_ColorGrid = TRUE;
	CDialog::OnOK();
}

void CTMCGROUTDIALOGView::OnOk5() 
{
	m_ColorAxies = TRUE;
	CDialog::OnOK();
}

void CTMCGROUTDIALOGView::OnOk6() 
{
	m_ColorBackground = TRUE;
	CDialog::OnOK();
}


void CTMCGROUTDIALOGView::OnFormatdefaultbutton() 
{
	m_csXFormat.Format("%s", "%4.3lg");
	m_csYFormat.Format("%s", "%4.3lg");
	UpdateData(FALSE);
	return;
}

void CTMCGROUTDIALOGView::OnOk7() 
{
	m_ColorGraphics = TRUE;
	CDialog::OnOK();
	return;
}


BOOL CTMCGROUTDIALOGView::OnInitDialog() 
{
	CDialog::OnInitDialog();
	

	GetDlgItem( IDC_GRPAR_XMAX )->EnableWindow( !m_aXSFlag );
	GetDlgItem( IDC_GRPAR_XMIN )->EnableWindow( !m_aXSFlag );
	
	GetDlgItem( IDC_GRPAR_YMAX )->EnableWindow( !m_aYSFlag );
	GetDlgItem( IDC_GRPAR_YMIN )->EnableWindow( !m_aYSFlag );

	GetDlgItem( IDC_GRPAR_POINTFLAG2  )->EnableWindow( m_DrawPoinFlag );
	GetDlgItem( IDC_GRPAR_POINTSIZE )->EnableWindow( m_DrawPoinFlag );
	
	return TRUE;  // return TRUE unless you set the focus to a control
	              // EXCEPTION: OCX Property Pages should return FALSE
}

void CTMCGROUTDIALOGView::OnGrparAxsflag() 
{
 	UpdateData();


	// Enable/disable edit controls
	GetDlgItem( IDC_GRPAR_XMAX )->EnableWindow( !m_aXSFlag );
	GetDlgItem( IDC_GRPAR_XMIN )->EnableWindow( !m_aXSFlag );

	return;	
}

void CTMCGROUTDIALOGView::OnGrparAysflag() 
{
 	UpdateData();

	
	// Enable/disable edit controls
	GetDlgItem( IDC_GRPAR_YMAX )->EnableWindow( !m_aYSFlag );
	GetDlgItem( IDC_GRPAR_YMIN )->EnableWindow( !m_aYSFlag );


	return;	
}

void CTMCGROUTDIALOGView::OnGrparPointflag() 
{
 	UpdateData();

	
	// Enable/disable edit controls
	GetDlgItem( IDC_GRPAR_POINTFLAG2  )->EnableWindow( m_DrawPoinFlag );
	GetDlgItem( IDC_GRPAR_POINTSIZE )->EnableWindow( m_DrawPoinFlag );
	
	return;	
}
