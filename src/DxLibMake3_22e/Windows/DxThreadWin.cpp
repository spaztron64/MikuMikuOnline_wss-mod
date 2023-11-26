//-----------------------------------------------------------------------------
// 
// 		�c�w���C�u����		Windows�p�X���b�h�֌W�v���O����
// 
//  	Ver 3.22e
// 
//-----------------------------------------------------------------------------

// �c�w���C�u�����쐬���p��`
#define DX_MAKE

// �C���N���[�h ---------------------------------------------------------------
#include "DxThreadWin.h"
#include "DxWinAPI.h"
#include "../DxMemory.h"

#if !defined( BC_COMPILER ) || defined( _DEBUG )
#include "../DxBaseFunc.h"
#endif

#ifndef DX_NON_NAMESPACE

namespace DxLib
{

#endif // DX_NON_NAMESPACE

// �}�N����` -----------------------------------------------------------------

// �\���̐錾 -----------------------------------------------------------------

// �f�[�^��` -----------------------------------------------------------------

// �֐��錾 -------------------------------------------------------------------

// �v���O���� -----------------------------------------------------------------


// �X���b�h�̏���������������
extern void Thread_Initialize( void )
{
	// Windows �ł͓��ɉ������Ȃ�
	return ;
}

// �X���b�h�����s����֐�
DWORD WINAPI ThreadRunFunction( void *pParam )
{
	THREAD_INFO *pInfo = ( THREAD_INFO * )pParam ;

	SETUP_WIN_API

	Thread_Suspend( pInfo ) ;

	pInfo->pFunction( pInfo, pInfo->pParam ) ;

	WinAPIData.Win32Func.ExitThreadFunc( 0 ) ;

	return 0 ;
}

// �X���b�h���쐬����
extern int Thread_Create( THREAD_INFO *pThreadInfo, void ( *pFunction )( THREAD_INFO *, void * ), void *pParam )
{
	SETUP_WIN_API

	pThreadInfo->pFunction = pFunction ;
	pThreadInfo->pParam = pParam ;
	pThreadInfo->ThreadHandle = WinAPIData.Win32Func.CreateThreadFunc( NULL, 0, ThreadRunFunction, pThreadInfo, 0, &pThreadInfo->ThreadID ) ;

	return pThreadInfo->ThreadHandle == NULL ? -1 : 0 ;
}

// �X���b�h�̌�n�����s��
extern void Thread_Delete( THREAD_INFO *pThreadInfo )
{
	SETUP_WIN_API

	WinAPIData.Win32Func.CloseHandleFunc( pThreadInfo->ThreadHandle ) ;
	pThreadInfo->ThreadHandle = NULL ;
}

// �X���b�h���L�����ǂ������擾����( 1:�L��  0:���� )
extern int Thread_IsValid( THREAD_INFO *pThreadInfo )
{
	return pThreadInfo->ThreadHandle != NULL ? 1 : 0 ;
}


// �X���b�h�̎��s�D�揇�ʂ�ݒ肷��
extern void Thread_SetPriority( THREAD_INFO *pThreadInfo, int Priority /* DX_THREAD_PRIORITY_LOWEST �Ȃ� */ )
{
	int winPriority = -1 ;

	SETUP_WIN_API

	switch( Priority )
	{
	case DX_THREAD_PRIORITY_LOWEST :
		winPriority = THREAD_PRIORITY_LOWEST ;
		break ;

	case DX_THREAD_PRIORITY_BELOW_NORMAL :
		winPriority = THREAD_PRIORITY_BELOW_NORMAL ;
		break ;

	case DX_THREAD_PRIORITY_NORMAL :
		winPriority = THREAD_PRIORITY_NORMAL ;
		break ;

	case DX_THREAD_PRIORITY_HIGHEST :
		winPriority = THREAD_PRIORITY_HIGHEST ;
		break ;

	default :
		return ;
	}

	WinAPIData.Win32Func.SetThreadPriorityFunc( pThreadInfo->ThreadHandle, winPriority ) ;
}

// �J�����g�X���b�h�̂h�c���擾����
extern DWORD_PTR Thread_GetCurrentId( void )
{
	SETUP_WIN_API

	return WinAPIData.Win32Func.GetCurrentThreadIdFunc() ;
}

// �X���b�h�̂h�c���擾����
extern DWORD_PTR Thread_GetId( THREAD_INFO *pThreadInfo )
{
	return pThreadInfo->ThreadID ;
}

// �X���b�h���x�~��Ԃɂ���
extern void Thread_Suspend( THREAD_INFO * /*pThreadInfo*/ )
{
	SETUP_WIN_API

	WinAPIData.Win32Func.SuspendThreadFunc( WinAPIData.Win32Func.GetCurrentThreadFunc() ) ;
}

// �X���b�h�̋x�~��Ԃ���������( 0:�x�~��Ԃ���Ȃ�����  1:�x�~��Ԃ����� )
extern int Thread_Resume( THREAD_INFO *pThreadInfo )
{
	SETUP_WIN_API

	return WinAPIData.Win32Func.ResumeThreadFunc( pThreadInfo->ThreadHandle ) == 0 ? 0 : 1 ;
}



// �w�莞�ԃX���b�h���~����
extern void Thread_Sleep( DWORD MiliSecond )
{
	SETUP_WIN_API

	WinAPIData.Win32Func.SleepFunc( MiliSecond ) ;
}





// �N���e�B�J���Z�N�V�����̏�����
extern int CriticalSection_Initialize( DX_CRITICAL_SECTION *pCSection )
{
	SETUP_WIN_API

	WinAPIData.Win32Func.InitializeCriticalSectionFunc( &pCSection->CriticalSection ) ;

#if !defined( BC_COMPILER ) || defined( _DEBUG )
	int i ;
	for( i = 0 ; i < 512 ; i ++ )
	{
		pCSection->FilePath[ i ] = '\0' ;
	}
#endif

	return 0 ;
}

// �N���e�B�J���Z�N�V�����̍폜
extern int CriticalSection_Delete( DX_CRITICAL_SECTION *pCSection )
{
	SETUP_WIN_API

	WinAPIData.Win32Func.DeleteCriticalSectionFunc( &pCSection->CriticalSection ) ;

	return 0 ;
}

// �N���e�B�J���Z�N�V�����̃��b�N�̎擾
#if !defined( BC_COMPILER ) || defined( _DEBUG )
extern int CriticalSection_Lock( DX_CRITICAL_SECTION *pCSection, const char *FilePath, int LineNo )
#else
extern int CriticalSection_Lock( DX_CRITICAL_SECTION *pCSection )
#endif
{
	SETUP_WIN_API

#if !defined( BC_COMPILER ) || defined( _DEBUG )
	DWORD_PTR ThreadID = WinAPIData.Win32Func.GetCurrentThreadIdFunc() ;
#endif

	WinAPIData.Win32Func.EnterCriticalSectionFunc( &pCSection->CriticalSection ) ;

#if !defined( BC_COMPILER ) || defined( _DEBUG )
	int Length = ( int )_STRLEN( FilePath ) ;
	if( Length >= 512 ) Length = 511 ;
	_MEMCPY( pCSection->FilePath, FilePath, ( size_t )Length ) ;
	pCSection->FilePath[ Length ] = '\0' ;
	pCSection->LineNo = LineNo ;
	pCSection->ThreadID = ( DWORD )ThreadID ;
#endif

	return 0 ;
}

// �N���e�B�J���Z�N�V�����̃��b�N���������
extern int CriticalSection_Unlock( DX_CRITICAL_SECTION *pCSection )
{
	SETUP_WIN_API

	WinAPIData.Win32Func.LeaveCriticalSectionFunc( &pCSection->CriticalSection ) ;

	return 0 ;
}




#ifndef DX_NON_NAMESPACE

}

#endif // DX_NON_NAMESPACE

