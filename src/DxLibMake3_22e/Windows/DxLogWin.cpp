//-----------------------------------------------------------------------------
// 
// 		�c�w���C�u����		Windows�p���O�v���O����
// 
//  	Ver 3.22e
// 
//-----------------------------------------------------------------------------

// �c�w���C�u�����쐬���p��`
#define DX_MAKE

#include "../DxCompileConfig.h"

// �C���N���[�h ---------------------------------------------------------------
#include "DxLogWin.h"

#ifndef DX_NON_LOG

#include "../DxStatic.h"
#include "../DxLib.h"
#include "../DxLog.h"
#include "../DxBaseFunc.h"
#include "../DxMemory.h"
#include "../DxGraphics.h"
#include "DxWindow.h"
#include "DxWinAPI.h"
#include "DxGraphicsWin.h"

#ifndef DX_NON_NAMESPACE

namespace DxLib
{

#endif // DX_NON_NAMESPACE

// �}�N����` -----------------------------------------------------------------

// �\���̐錾 -----------------------------------------------------------------

// �f�[�^��` -----------------------------------------------------------------

// �֐��錾 -------------------------------------------------------------------

// �v���O���� -----------------------------------------------------------------

// ���O�t�@�C�������������鏈���̊��ˑ�����
extern int LogFileInitialize_PF( const wchar_t *LogFilePath )
{
	HANDLE fp ;

	SETUP_WIN_API

	// �g�p����L�����N�^�[�R�[�h�� UTF8�`���̏ꍇ�̂ݏo�͂���t�@�C���� UTF8�`���ɂ���
	LogData.PF.UTF8Mode = _GET_CHAR_CHARCODEFORMAT() == DX_CHARCODEFORMAT_UTF8 ? TRUE : FALSE ;

	// �G���[���O�t�@�C�����č쐬����
	DeleteFileWFunc( LogFilePath ) ;
	fp = CreateFileWFunc( LogFilePath, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL ) ;

	// UTF8�`���� Unicode�`���� BOM ���قȂ�
	if( LogData.PF.UTF8Mode )
	{
		BYTE BOM_UTF8[ 3 ] = { 0xef, 0xbb, 0xbf } ;
		DWORD WriteSize ;
		WinAPIData.Win32Func.WriteFileFunc( fp, &BOM_UTF8, 3, &WriteSize, NULL ) ;
	}
	else
	{
		BYTE BOM_Unicode[ 2 ] = { 0xff, 0xfe } ;
		DWORD WriteSize ;
		WinAPIData.Win32Func.WriteFileFunc( fp, &BOM_Unicode, 2, &WriteSize, NULL ) ;
	}

	if( fp != NULL )
	{
		WinAPIData.Win32Func.CloseHandleFunc( fp ) ;
	}

	// �I��
	return 0 ;
}

// ���O�t�@�C���̌�n���̊��ˑ�����
extern int LogFileTerminate_PF( void )
{
	return 0 ;
}

// ���O�t�@�C���֕�����������o�������̊��ˑ�����
extern int LogFileAdd_WCHAR_T_PF( const wchar_t *LogFilePath, const wchar_t *ErrorStr )
{
	HANDLE	fp ;
	DWORD	WriteSize ;

	SETUP_WIN_API

	if( WinAPIData.Win32Func.WinMMDLL == NULL )
	{
		return -1 ;
	}

	// �G���[���O�t�@�C�����J��
	fp = CreateFileWFunc( LogFilePath, GENERIC_WRITE, 0, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL ) ;
	if( fp != NULL )
	{
		// �t�@�C���A�N�Z�X�ʒu���t�@�C���̖��[�Ɉړ�
		WinAPIData.Win32Func.SetFilePointerFunc( fp, 0, NULL, FILE_END ) ;

		// �G���[���O�t�@�C���ɏ����o��
		if( LogData.PF.UTF8Mode )
		{
			// UTF8 �ɕϊ�������������i�[���邽�߂̃������̈���m��
			size_t UTF8StringBufferBytes = ( _WCSLEN( ErrorStr ) + 1 ) * 8 ;
			char *UTF8StringBuffer = ( char * )DXALLOC( UTF8StringBufferBytes ) ;
			if( UTF8StringBuffer == NULL )
			{
				return -1 ;
			}

			// ������� UTF8 �`���ɕϊ�
			int UTF8Bytes = ConvString( ( char * )ErrorStr, -1, WCHAR_T_CHARCODEFORMAT, UTF8StringBuffer, UTF8StringBufferBytes, DX_CHARCODEFORMAT_UTF8 ) ;

			// �t�@�C���ɏ����o��
			if( UTF8Bytes > 1 )
			{
				WinAPIData.Win32Func.WriteFileFunc( fp, UTF8StringBuffer, ( DWORD )UTF8Bytes - 1, &WriteSize, NULL ) ;
			}

			// �������̉��
			DXFREE( UTF8StringBuffer ) ;
			UTF8StringBuffer = NULL ;
			UTF8StringBufferBytes = 0 ;
		}
		else
		{
			WinAPIData.Win32Func.WriteFileFunc( fp, ErrorStr, ( DWORD )( _WCSLEN( ErrorStr ) * sizeof( wchar_t ) ), &WriteSize, NULL ) ;
		}

		// �G���[���O���A�E�g�v�b�g�ɏ����o��
		OutputDebugStringWFunc( ErrorStr ) ;

		// �t�@�C�������
		WinAPIData.Win32Func.CloseHandleFunc( fp ) ;
	}
	
	// �I��
	return 0 ;
}

// ���O�@�\�̏��������s�����ǂ������擾����
extern int IsInitializeLog( void )
{
	if( WinData.MainWindow == NULL )
	{
		return FALSE ;
	}

#ifndef DX_NON_GRAPHICS
	// �n�[�h�E�G�A�@�\���g���Ă��邩�ǂ����ŏ����𕪊�
	if( GSYS.Setting.ValidHardware )
	{
		switch( GRAWIN.Setting.UseGraphicsAPI )
		{
#ifndef DX_NON_DIRECT3D11
		case GRAPHICS_API_DIRECT3D11_WIN32 :
			return D3D11Device_IsValid() ? TRUE : FALSE ;
#endif // DX_NON_DIRECT3D11

#ifndef DX_NON_DIRECT3D9
		case GRAPHICS_API_DIRECT3D9_WIN32 :
			return NS_GetUseDDrawObj() == NULL ? FALSE : TRUE ;
#endif // DX_NON_DIRECT3D9

		default :
			return FALSE ;
		}
	}
	else
	{
		// �c�w���C�u�������I����Ԃɓ����Ă����珉�������Ȃ�
		return NS_GetUseDDrawObj() == NULL || WinData.MainWindow == NULL ? FALSE : TRUE ;
	}
#else // DX_NON_GRAPHICS
	// �c�w���C�u�������I����Ԃɓ����Ă����珉�������Ȃ�
	return WinData.MainWindow == NULL ? FALSE : TRUE ;
#endif // DX_NON_GRAPHICS
}


#ifndef DX_NON_NAMESPACE

}

#endif // DX_NON_NAMESPACE

#endif // DX_NON_LOG

