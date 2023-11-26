//-----------------------------------------------------------------------------
// 
// 		�c�w���C�u����		WindowsOS�p�T�E���h�v���O����
// 
//  	Ver 3.22e
// 
//-----------------------------------------------------------------------------

// �c�w���C�u�����쐬���p��`
#define DX_MAKE

#include "../DxCompileConfig.h"

#ifndef DX_NON_SOUND

// �C���N���[�h----------------------------------------------------------------
#include "DxSoundWin.h"
#include "DxGuid.h"
#include "DxWinAPI.h"
#include "../DxFile.h"
#include "../DxSound.h"
#include "../DxSystem.h"
#include "../DxASyncLoad.h"
#include "../DxLog.h"

// #include <mmdeviceapi.h>
// #include <Audioclient.h>
// #include <audiopolicy.h>
// #include <endpointvolume.h>
// #include <FunctionDiscoveryKeys_devpkey.h>

#ifndef DX_NON_NAMESPACE

namespace DxLib
{

#endif // DX_NON_NAMESPACE

// �}�N����`------------------------------------------------------------------

// �l�h�c�h�ŏ��{�����[��
#define DM_MIN_VOLUME						(-10000)

// �^��`----------------------------------------------------------------------

// �f�[�^�錾------------------------------------------------------------------

static const D_X3DAUDIO_DISTANCE_CURVE_POINT D_X3DAudioDefault_LinearCurvePoints[2] = { 0.0f, 1.0f, 1.0f, 0.0f };
static const D_X3DAUDIO_DISTANCE_CURVE       D_X3DAudioDefault_LinearCurve          = { ( D_X3DAUDIO_DISTANCE_CURVE_POINT * )& D_X3DAudioDefault_LinearCurvePoints[ 0 ], 2 } ;

static const D_X3DAUDIO_CONE Listener_DirectionalCone = { DX_PI_F * 5.0f / 6.0f, DX_PI_F * 11.0f / 6.0f, 1.0f, 0.75f, 0.0f, 0.25f, 0.708f, 1.0f } ;

static const D_X3DAUDIO_DISTANCE_CURVE_POINT D_Emitter_LFE_CurvePoints[3]			= { 0.0f, 1.0f, 0.25f, 0.0f, 1.0f, 0.0f } ;
static const D_X3DAUDIO_DISTANCE_CURVE       D_Emitter_LFE_Curve					= { ( D_X3DAUDIO_DISTANCE_CURVE_POINT * )&D_Emitter_LFE_CurvePoints[ 0 ], 3 } ;

static const D_X3DAUDIO_DISTANCE_CURVE_POINT D_Emitter_Reverb_CurvePoints[3]		= { 0.0f, 0.5f, 0.75f, 1.0f, 1.0f, 0.0f } ;
static const D_X3DAUDIO_DISTANCE_CURVE       D_Emitter_Reverb_Curve					= { ( D_X3DAUDIO_DISTANCE_CURVE_POINT * )&D_Emitter_Reverb_CurvePoints[ 0 ], 3 } ;

static const D_XAUDIO2FX_REVERB_I3DL2_PARAMETERS D_PRESET_PARAMS[ D_XAUDIO2FX_PRESET_NUM ] =
{
	D_XAUDIO2FX_I3DL2_PRESET_FOREST,
	D_XAUDIO2FX_I3DL2_PRESET_DEFAULT,
	D_XAUDIO2FX_I3DL2_PRESET_GENERIC,
	D_XAUDIO2FX_I3DL2_PRESET_PADDEDCELL,
	D_XAUDIO2FX_I3DL2_PRESET_ROOM,
	D_XAUDIO2FX_I3DL2_PRESET_BATHROOM,
	D_XAUDIO2FX_I3DL2_PRESET_LIVINGROOM,
	D_XAUDIO2FX_I3DL2_PRESET_STONEROOM,
	D_XAUDIO2FX_I3DL2_PRESET_AUDITORIUM,
	D_XAUDIO2FX_I3DL2_PRESET_CONCERTHALL,
	D_XAUDIO2FX_I3DL2_PRESET_CAVE,
	D_XAUDIO2FX_I3DL2_PRESET_ARENA,
	D_XAUDIO2FX_I3DL2_PRESET_HANGAR,
	D_XAUDIO2FX_I3DL2_PRESET_CARPETEDHALLWAY,
	D_XAUDIO2FX_I3DL2_PRESET_HALLWAY,
	D_XAUDIO2FX_I3DL2_PRESET_STONECORRIDOR,
	D_XAUDIO2FX_I3DL2_PRESET_ALLEY,
	D_XAUDIO2FX_I3DL2_PRESET_CITY,
	D_XAUDIO2FX_I3DL2_PRESET_MOUNTAINS,
	D_XAUDIO2FX_I3DL2_PRESET_QUARRY,
	D_XAUDIO2FX_I3DL2_PRESET_PLAIN,
	D_XAUDIO2FX_I3DL2_PRESET_PARKINGLOT,
	D_XAUDIO2FX_I3DL2_PRESET_SEWERPIPE,
	D_XAUDIO2FX_I3DL2_PRESET_UNDERWATER,
	D_XAUDIO2FX_I3DL2_PRESET_SMALLROOM,
	D_XAUDIO2FX_I3DL2_PRESET_MEDIUMROOM,
	D_XAUDIO2FX_I3DL2_PRESET_LARGEROOM,
	D_XAUDIO2FX_I3DL2_PRESET_MEDIUMHALL,
	D_XAUDIO2FX_I3DL2_PRESET_LARGEHALL,
	D_XAUDIO2FX_I3DL2_PRESET_PLATE,
} ;

static WORD WASAPI_BitPerSample[]      = { 16, 24, 32, 32, 32, 0 } ;
static WORD WASAPI_ValidBitPerSample[] = { 16, 24, 24, 32, 32, 0 } ;
static BYTE WASAPI_IsFloatType[]       = {  0,  0,  0,  0,  1, 0 } ;
static DWORD WASAPI_SamplePerSec[] = { 44100, 48000, 88200, 96000, 176400, 192000, 0 } ;

#ifndef DX_NON_ASIO

struct ASIO_FORMATINFO
{
	const wchar_t *			Name ;
	WORD					BitsPerSample ;
	WORD					ValidBitsPerSample ;
	int						IsFloat ;
	int						IsMSB ;
} ;

static ASIO_FORMATINFO ASIO_FormatInfoTable[] =
{
	{ L"ASIOSTInt16MSB",	16, 16, 0, 1 },	// D_ASIOSTInt16MSB		0
	{ L"ASIOSTInt24MSB",	24, 24, 0, 1 },	// D_ASIOSTInt24MSB		1
	{ L"ASIOSTInt32MSB",	32, 32, 0, 1 },	// D_ASIOSTInt32MSB		2
	{ L"ASIOSTFloat32MSB",	32, 32, 1, 1 },	// D_ASIOSTFloat32MSB	3
	{ L"ASIOSTFloat64MSB",	64, 64, 1, 1 },	// D_ASIOSTFloat64MSB	4
	{ 0 },									//						5
	{ 0 },									//						6
	{ 0 },									//						7
	{ L"ASIOSTInt32MSB16",	32, 16, 0, 1 },	// D_ASIOSTInt32MSB16	8
	{ L"ASIOSTInt32MSB18",	32, 18, 0, 1 },	// D_ASIOSTInt32MSB18	9
	{ L"ASIOSTInt32MSB20",	32, 20, 0, 1 },	// D_ASIOSTInt32MSB20	10
	{ L"ASIOSTInt32MSB24",	32, 24, 0, 1 },	// D_ASIOSTInt32MSB24	11
	{ 0 },									//						12
	{ 0 },									//						13
	{ 0 },									//						14
	{ 0 },									//						15
	{ L"ASIOSTInt16LSB",	16, 16, 0, 0 },	// D_ASIOSTInt16LSB		16
	{ L"ASIOSTInt24LSB",	24, 24, 0, 0 },	// D_ASIOSTInt24LSB		17
	{ L"ASIOSTInt32LSB",	32, 32, 0, 0 },	// D_ASIOSTInt32LSB		18
	{ L"ASIOSTFloat32LSB",	32, 32, 1, 0 },	// D_ASIOSTFloat32LSB	19
	{ L"ASIOSTFloat64LSB",	64, 64, 1, 0 },	// D_ASIOSTFloat64LSB	20
	{ 0 },									//						21
	{ 0 },									//						22
	{ 0 },									//						23
	{ L"ASIOSTInt32LSB16",	32, 16, 0, 0 },	// D_ASIOSTInt32LSB16	24
	{ L"ASIOSTInt32LSB18",	32, 18, 0, 0 },	// D_ASIOSTInt32LSB18	25
	{ L"ASIOSTInt32LSB20",	32, 20, 0, 0 },	// D_ASIOSTInt32LSB20	26
	{ L"ASIOSTInt32LSB24",	32, 24, 0, 0 },	// D_ASIOSTInt32LSB24	27
	{ 0 },									//						28
	{ 0 },									//						29
	{ 0 },									//						30
	{ 0 },									//						31
	{ L"ASIOSTDSDInt8LSB1",	 8,  8, 0, 0 },	// D_ASIOSTDSDInt8LSB1	32
	{ L"ASIOSTDSDInt8MSB1",	 8,  8, 0, 1 },	// D_ASIOSTDSDInt8MSB1	33
	{ 0 },									//						34
	{ 0 },									//						35
	{ 0 },									//						36
	{ 0 },									//						37
	{ 0 },									//						38
	{ 0 },									//						39
	{ L"ASIOSTDSDInt8NER8",	 8,  8, 0, 0 },	// D_ASIOSTDSDInt8NER8	40
} ;

#endif // DX_NON_ASIO

// �֐��v���g�^�C�v�錾 -------------------------------------------------------


static	DWORD WINAPI StreamSoundThreadFunction( void * ) ;											// �X�g���[���T�E���h�����p�X���b�h

// XAudio2�֌W
static HRESULT D_XAudio2CreateReverb( D_IUnknown** ppApo, DWORD Flags = 0 ) ;
static HRESULT D_XAudio2CreateVolumeMeter( D_IUnknown** ppApo, DWORD Flags = 0 ) ;
static void D_ReverbConvertI3DL2ToNative( const D_XAUDIO2FX_REVERB_I3DL2_PARAMETERS* pI3DL2, D_XAUDIO2FX_REVERB_PARAMETERS* pNative ) ;
static void D_ReverbConvertI3DL2ToNative2_8( const D_XAUDIO2FX_REVERB_I3DL2_PARAMETERS* pI3DL2, D_XAUDIO2FX_REVERB_PARAMETERS2_8* pNative ) ;

// �v���O����------------------------------------------------------------------

// XAudio2�֌W
static HRESULT D_XAudio2CreateVolumeMeter( D_IUnknown** ppApo, DWORD Flags )
{
	if( SoundSysData.PF.XAudio2_8DLL != NULL )
	{
		return SoundSysData.PF.CreateAudioVolumeMeterFunc( ppApo ) ;
	}
	else
	{
		SETUP_WIN_API
		return WinAPIData.Win32Func.CoCreateInstanceFunc((Flags & D_XAUDIO2FX_DEBUG) ? CLSID_AUDIOVOLUMEMeter2_7_DEBUG : CLSID_AUDIOVOLUMEMeter2_7, NULL, CLSCTX_INPROC_SERVER, IID_IUNKNOWN, (void**)ppApo ) ;
	}
}

static HRESULT D_XAudio2CreateReverb( D_IUnknown** ppApo, DWORD Flags )
{
	if( SoundSysData.PF.XAudio2_8DLL != NULL )
	{
		return SoundSysData.PF.CreateAudioReverbFunc( ppApo ) ;
	}
	else
	{
		SETUP_WIN_API
	    return WinAPIData.Win32Func.CoCreateInstanceFunc( ( Flags & D_XAUDIO2FX_DEBUG) ? CLSID_AUDIOREVERB2_7_DEBUG : CLSID_AUDIOREVERB2_7, NULL, CLSCTX_INPROC_SERVER, IID_IUNKNOWN, (void**)ppApo ) ;
	}
}

static void D_ReverbConvertI3DL2ToNative( const D_XAUDIO2FX_REVERB_I3DL2_PARAMETERS* pI3DL2, D_XAUDIO2FX_REVERB_PARAMETERS* pNative )
{
    float reflectionsDelay;
    float reverbDelay;

    // RoomRolloffFactor is ignored

    // These parameters have no equivalent in I3DL2
    pNative->RearDelay = D_XAUDIO2FX_REVERB_DEFAULT_REAR_DELAY; // 5
    pNative->PositionLeft = D_XAUDIO2FX_REVERB_DEFAULT_POSITION; // 6
    pNative->PositionRight = D_XAUDIO2FX_REVERB_DEFAULT_POSITION; // 6
    pNative->PositionMatrixLeft = D_XAUDIO2FX_REVERB_DEFAULT_POSITION_MATRIX; // 27
    pNative->PositionMatrixRight = D_XAUDIO2FX_REVERB_DEFAULT_POSITION_MATRIX; // 27
    pNative->RoomSize = D_XAUDIO2FX_REVERB_DEFAULT_ROOM_SIZE; // 100
    pNative->LowEQCutoff = 4;
    pNative->HighEQCutoff = 6;

    // The rest of the I3DL2 parameters map to the native property set
    pNative->RoomFilterMain = (float)pI3DL2->Room / 100.0f;
    pNative->RoomFilterHF = (float)pI3DL2->RoomHF / 100.0f;

    if (pI3DL2->DecayHFRatio >= 1.0f)
    {
        int index = _DTOL(-4.0 * _LOG10(pI3DL2->DecayHFRatio));
        if (index < -8) index = -8;
        pNative->LowEQGain = (BYTE)((index < 0) ? index + 8 : 8);
        pNative->HighEQGain = 8;
        pNative->DecayTime = pI3DL2->DecayTime * pI3DL2->DecayHFRatio;
    }
    else
    {
        int index = _DTOL(4.0 * _LOG10(pI3DL2->DecayHFRatio));
        if (index < -8) index = -8;
        pNative->LowEQGain = 8;
        pNative->HighEQGain = (BYTE)((index < 0) ? index + 8 : 8);
        pNative->DecayTime = pI3DL2->DecayTime;
    }

    reflectionsDelay = pI3DL2->ReflectionsDelay * 1000.0f;
    if (reflectionsDelay >= D_XAUDIO2FX_REVERB_MAX_REFLECTIONS_DELAY) // 300
    {
        reflectionsDelay = (float)(D_XAUDIO2FX_REVERB_MAX_REFLECTIONS_DELAY - 1);
    }
    else if (reflectionsDelay <= 1)
    {
        reflectionsDelay = 1;
    }
    pNative->ReflectionsDelay = (DWORD)reflectionsDelay;

    reverbDelay = pI3DL2->ReverbDelay * 1000.0f;
    if (reverbDelay >= D_XAUDIO2FX_REVERB_MAX_REVERB_DELAY) // 85
    {
        reverbDelay = (float)(D_XAUDIO2FX_REVERB_MAX_REVERB_DELAY - 1);
    }
    pNative->ReverbDelay = (BYTE)reverbDelay;

    pNative->ReflectionsGain = pI3DL2->Reflections / 100.0f;
    pNative->ReverbGain = pI3DL2->Reverb / 100.0f;
    pNative->EarlyDiffusion = (BYTE)(15.0f * pI3DL2->Diffusion / 100.0f);
    pNative->LateDiffusion = pNative->EarlyDiffusion;
    pNative->Density = pI3DL2->Density;
    pNative->RoomFilterFreq = pI3DL2->HFReference;

    pNative->WetDryMix = pI3DL2->WetDryMix;
}

static void D_ReverbConvertI3DL2ToNative2_8( const D_XAUDIO2FX_REVERB_I3DL2_PARAMETERS* pI3DL2, D_XAUDIO2FX_REVERB_PARAMETERS2_8* pNative )
{
    float reflectionsDelay;
    float reverbDelay;

    // RoomRolloffFactor is ignored

    // These parameters have no equivalent in I3DL2
    pNative->RearDelay = D_XAUDIO2FX_REVERB_DEFAULT_REAR_DELAY; // 5
    pNative->PositionLeft = D_XAUDIO2FX_REVERB_DEFAULT_POSITION; // 6
    pNative->PositionRight = D_XAUDIO2FX_REVERB_DEFAULT_POSITION; // 6
    pNative->PositionMatrixLeft = D_XAUDIO2FX_REVERB_DEFAULT_POSITION_MATRIX; // 27
    pNative->PositionMatrixRight = D_XAUDIO2FX_REVERB_DEFAULT_POSITION_MATRIX; // 27
    pNative->RoomSize = D_XAUDIO2FX_REVERB_DEFAULT_ROOM_SIZE; // 100
    pNative->LowEQCutoff = 4;
    pNative->HighEQCutoff = 6;

    // The rest of the I3DL2 parameters map to the native property set
    pNative->RoomFilterMain = (float)pI3DL2->Room / 100.0f;
    pNative->RoomFilterHF = (float)pI3DL2->RoomHF / 100.0f;

    if (pI3DL2->DecayHFRatio >= 1.0f)
    {
        int index = _DTOL(-4.0 * _LOG10(pI3DL2->DecayHFRatio));
        if (index < -8) index = -8;
        pNative->LowEQGain = (BYTE)((index < 0) ? index + 8 : 8);
        pNative->HighEQGain = 8;
        pNative->DecayTime = pI3DL2->DecayTime * pI3DL2->DecayHFRatio;
    }
    else
    {
        int index = _DTOL(4.0 * _LOG10(pI3DL2->DecayHFRatio));
        if (index < -8) index = -8;
        pNative->LowEQGain = 8;
        pNative->HighEQGain = (BYTE)((index < 0) ? index + 8 : 8);
        pNative->DecayTime = pI3DL2->DecayTime;
    }

    reflectionsDelay = pI3DL2->ReflectionsDelay * 1000.0f;
    if (reflectionsDelay >= D_XAUDIO2FX_REVERB_MAX_REFLECTIONS_DELAY) // 300
    {
        reflectionsDelay = (float)(D_XAUDIO2FX_REVERB_MAX_REFLECTIONS_DELAY - 1);
    }
    else if (reflectionsDelay <= 1)
    {
        reflectionsDelay = 1;
    }
    pNative->ReflectionsDelay = (DWORD)reflectionsDelay;

    reverbDelay = pI3DL2->ReverbDelay * 1000.0f;
    if (reverbDelay >= D_XAUDIO2FX_REVERB_MAX_REVERB_DELAY) // 85
    {
        reverbDelay = (float)(D_XAUDIO2FX_REVERB_MAX_REVERB_DELAY - 1);
    }
    pNative->ReverbDelay = (BYTE)reverbDelay;

    pNative->ReflectionsGain = pI3DL2->Reflections / 100.0f;
    pNative->ReverbGain = pI3DL2->Reverb / 100.0f;
    pNative->EarlyDiffusion = (BYTE)(15.0f * pI3DL2->Diffusion / 100.0f);
    pNative->LateDiffusion = pNative->EarlyDiffusion;
    pNative->Density = pI3DL2->Density;
    pNative->RoomFilterFreq = pI3DL2->HFReference;

    pNative->WetDryMix = pI3DL2->WetDryMix;
    pNative->DisableLateField = FALSE;
}
















#ifndef DX_NON_ASIO

static void ASIO_BufferSwitch( long index, long /* processNow */ )
{
	WriteSelfMixingSample(
		( BYTE * )SoundSysData.PF.ASIO_BufferInfo[ 0 ].buffers[ index ],
		( BYTE * )SoundSysData.PF.ASIO_BufferInfo[ 1 ].buffers[ index ],
		SoundSysData.PF.SelfMixingFormatEx.Format.wBitsPerSample >> 3,
		SoundSysData.PF.ASIO_BufferSize
	) ;

	// �o�͊�����ʒm
	if( SoundSysData.PF.ASIO_Driver[ SoundSysData.PF.ASIO_DriverUseIndex ] != NULL )
	{
		SoundSysData.PF.ASIO_Driver[ SoundSysData.PF.ASIO_DriverUseIndex ]->outputReady() ;
	}
}

void ASIO_SampleRateDidChange( double sRate )
{
	SoundSysData.PF.SelfMixingFormatEx.Format.nSamplesPerSec  = _DTOL( sRate ) ;
	SoundSysData.PF.SelfMixingFormatEx.Format.nAvgBytesPerSec = SoundSysData.PF.SelfMixingFormatEx.Format.nSamplesPerSec * SoundSysData.PF.SelfMixingFormatEx.Format.nBlockAlign ;

	SoundSysData.SelfMixingFormat = SoundSysData.PF.SelfMixingFormatEx.Format ;
}

long ASIO_AsioMessage( long /* selector */, long /* value */, void * /* message */, double * /* opt */ )
{
	return 0 ;
}

D_ASIOTime* ASIO_BufferSwitchTimeInfo( D_ASIOTime * params, long /* doubleBufferIndex */, long /* directProcess */ )
{
	return params ;
}

#endif // DX_NON_ASIO

DWORD CALLBACK WASAPI_SoundPlayThread( void * )
{
	// �r�����[�h�Ƌ��L���[�h�ŏ����𕪊�
	if( SoundSysData.PF.WASAPI_IsExclusiveFlag )
	{
		// �r�����[�h�̏ꍇ�̓C�x���g�쓮
		while( SoundSysData.PF.WASAPI_ThreadLoop )
		{
			DWORD FrameCount ;
			HRESULT ret ;
			BYTE *Dest ;

			// ���̃o�b�t�@�擾���K�v�ɂȂ�܂őҋ@
			DWORD retval = WinAPIData.Win32Func.WaitForSingleObjectFunc( SoundSysData.PF.WASAPI_Event, 2000 ) ;
			if( retval != WAIT_OBJECT_0 )
			{
				continue ;
			}

			// ���O�~�L�V���O�̑��Đ����Ԃ̊�l������������Ă��Ȃ������珉��������
//			if( SoundSysData.SelfMixingInitlizeTotalPlayTimeBaseCountFlag == FALSE )
//			{
//				UINT64 Pos, QPCPos, Freq ;
//
//				SoundSysData.PF.AudioClock->GetFrequency( &Freq ) ;
//				SoundSysData.PF.AudioClock->GetPosition( &Pos, &QPCPos ) ;
//				if( Pos > 0 )
//				{
//					SoundSysData.SelfMixingTotalPlayTimeBaseCount = QPCPos - _DTOL64( ( double )Pos * WinData.PerformanceTimerFrequency / Freq ) ;
//					SoundSysData.SelfMixingInitlizeTotalPlayTimeBaseCountFlag = TRUE ;
//				}
//			}

			// ����K�v�ȃt���[�������擾
			ret = SoundSysData.PF.AudioClient->GetBufferSize( &FrameCount ) ;

			// �o�̓o�b�t�@�̃|�C���^���擾
			ret = SoundSysData.PF.AudioRenderClient->GetBuffer( FrameCount, &Dest ) ;
			if( SUCCEEDED( ret ) )
			{
				WriteSelfMixingSample( Dest, ( BYTE * )Dest + ( SoundSysData.PF.SelfMixingFormatEx.Format.wBitsPerSample >> 3 ), SoundSysData.PF.SelfMixingFormatEx.Format.nBlockAlign, FrameCount ) ;

				// �o�b�t�@���J��
				SoundSysData.PF.AudioRenderClient->ReleaseBuffer( FrameCount, 0 ) ;
			}
		}
	}
	else
	{
		// ���L���[�h�̏ꍇ�̓o�b�t�@�Ď�
		DWORD SleepTime = ( DWORD )( SoundSysData.PF.WASAPI_DevicePeriod / 20000 ) ;
		if( SleepTime <= 0 ) SleepTime = 1 ;
		while( SoundSysData.PF.WASAPI_ThreadLoop )
		{
			DWORD FrameCount ;
			HRESULT ret ;
			BYTE *Dest ;
			DWORD Padding ;

			// �f�o�C�X�s���I�h�̔����̎��ԐQ��
			WinAPIData.Win32Func.SleepFunc( SleepTime ) ;

			// �܂��Đ�����Ă��Ȃ��L���ȃf�[�^�ʂ��擾����
			ret = SoundSysData.PF.AudioClient->GetCurrentPadding( &Padding ) ;

			// ����n���ׂ��T���v�������Z�o
			FrameCount = SoundSysData.PF.WASAPI_BufferSamples - Padding ;

			// ���O�~�L�V���O�̑��Đ����Ԃ̊�l������������Ă��Ȃ������珉��������
//			if( SoundSysData.SelfMixingInitlizeTotalPlayTimeBaseCountFlag == FALSE )
//			{
//				UINT64 Pos, QPCPos, Freq ;
//
//				SoundSysData.PF.AudioClock->GetFrequency( &Freq ) ;
//				SoundSysData.PF.AudioClock->GetPosition( &Pos, &QPCPos ) ;
//				if( Pos > 0 )
//				{
//					SoundSysData.SelfMixingTotalPlayTimeBaseCount = QPCPos - _DTOL64( ( double )Pos * WinData.PerformanceTimerFrequency / Freq ) ;
//					SoundSysData.SelfMixingInitlizeTotalPlayTimeBaseCountFlag = TRUE ;
//				}
//			}

			// �o�̓o�b�t�@�̃|�C���^���擾
			if( FrameCount > 0 )
			{
				ret = SoundSysData.PF.AudioRenderClient->GetBuffer( FrameCount, &Dest ) ;
				if( SUCCEEDED( ret ) )
				{
					WriteSelfMixingSample( Dest, ( BYTE * )Dest + ( SoundSysData.PF.SelfMixingFormatEx.Format.wBitsPerSample >> 3 ), SoundSysData.PF.SelfMixingFormatEx.Format.nBlockAlign, FrameCount ) ;

					// �o�b�t�@���J��
					SoundSysData.PF.AudioRenderClient->ReleaseBuffer( FrameCount, 0 ) ;
				}
			}
		}
	}

	return 0 ;
}


// �X�g���[���T�E���h�����p�X���b�h
static	DWORD WINAPI StreamSoundThreadFunction( void * )
{
	SETUP_WIN_API

	for(;;)
	{
		if( SoundSysData.PF.StreamSoundThreadEndFlag == 1 ) break ;
		if( SoundSysData.InitializeFlag == FALSE ) break ;

		// �N���e�B�J���Z�N�V�����̎擾
		CRITICALSECTION_LOCK( &HandleManageArray[ DX_HANDLETYPE_SOUND ].CriticalSection ) ;

		// �X�g���[�~���O����
		NS_ProcessStreamSoundMemAll() ;

		// �Đ����I��������n���h�����폜���鏈�����s��
//		ProcessPlayFinishDeleteSoundMemAll() ;

		// �R�c�T�E���h���Đ����Ă���T�E���h�n���h���ɑ΂��鏈�����s��
		ProcessPlay3DSoundMemAll() ;

		// �N���e�B�J���Z�N�V�����̉��
		CriticalSection_Unlock( &HandleManageArray[ DX_HANDLETYPE_SOUND ].CriticalSection ) ;


		// �N���e�B�J���Z�N�V�����̎擾
		CRITICALSECTION_LOCK( &HandleManageArray[ DX_HANDLETYPE_SOFTSOUND ].CriticalSection ) ;

		// �X�g���[�~���O����
		ST_SoftSoundPlayerProcessAll() ;

		// �N���e�B�J���Z�N�V�����̉��
		CriticalSection_Unlock( &HandleManageArray[ DX_HANDLETYPE_SOFTSOUND ].CriticalSection ) ;

		// �҂�
		WinAPIData.Win32Func.SleepFunc( 10 ) ;
	}

	// �X���b�h�I��
	SoundSysData.PF.StreamSoundThreadEndFlag = 2 ;
	WinAPIData.Win32Func.ExitThreadFunc( 0 ) ;

	return 0 ;
}






BOOL CALLBACK DSEnum( LPGUID /*lpGuid*/, LPCSTR lpcstrDescription, LPCSTR lpcstrModule, LPVOID /*lpContext*/ )
{
	DXST_LOGFILEFMT_ADDW(( L"Module Name : %15s   Description : %s ", lpcstrModule , lpcstrDescription )) ;

	return TRUE ;
}

// �T�E���h�V�X�e��������������֐��̊��ˑ��������s���֐�
extern int InitializeSoundSystem_PF_Timing0( void )
{
	HRESULT hr ;
	WAVEFORMATEX wfmtx ;
	D_DSCAPS caps ;

	SETUP_WIN_API

	if( SoundSysData.PF.DirectSoundObject != NULL )
	{
		return -1 ;
	}

INITSTART:

	// �Đ����[�h�ɂ���ď����𕪊�
	switch( SoundSysData.SoundMode )
	{
	case DX_MIDIMODE_MCI :
#ifndef DX_NON_ASIO
		if( SoundSysData.PF.EnableASIOFlag )
		{
			static D_ASIOCallbacks Callbacks ;
			D_ASIOChannelInfo ChannelInfo ;
			double SampleRate ;
			int i ;

			// ���O�~�L�V���O���s���t���O�𗧂Ă�
			SoundSysData.EnableSelfMixingFlag = TRUE ;

			DXST_LOGFILE_ADDUTF16LE( "\x41\x00\x53\x00\x49\x00\x4f\x00\x20\x00\x6e\x30\x1d\x52\x1f\x67\x16\x53\x92\x30\x4c\x88\x44\x30\x7e\x30\x59\x30\x0a\x00\x00"/*@ L"ASIO �̏��������s���܂�\n" @*/ ) ;

			// �K�v�� API ���g�p�ł��Ȃ���� ASIO �g�p����߂�
			if( WinAPIData.Win32Func.CharLowerBuffWFunc == NULL ||
				WinAPIData.Win32Func.RegOpenKeyWFunc == NULL ||
				WinAPIData.Win32Func.RegEnumKeyWFunc == NULL ||
				WinAPIData.Win32Func.RegOpenKeyExWFunc == NULL ||
				WinAPIData.Win32Func.RegQueryValueExWFunc == NULL ||
				WinAPIData.Win32Func.FindFirstFileWFunc == NULL ||
				WinAPIData.Win32Func.FindCloseFunc == NULL ||
				WinAPIData.Win32Func.CLSIDFromStringFunc == NULL )
			{
				DXST_LOGFILE_TABSUB ;
				DXST_LOGFILE_ADDUTF16LE( "\x41\x00\x53\x00\x49\x00\x4f\x00\x20\x00\x6e\x30\x7f\x4f\x28\x75\x6b\x30\xc5\x5f\x81\x89\x6a\x30\x20\x00\x41\x00\x50\x00\x49\x00\x20\x00\x4c\x30\x7f\x4f\x28\x75\x67\x30\x4d\x30\x7e\x30\x5b\x30\x93\x30\x67\x30\x57\x30\x5f\x30\x0a\x00\x00"/*@ L"ASIO �̎g�p�ɕK�v�� API ���g�p�ł��܂���ł���\n" @*/ ) ;
				SoundSysData.PF.EnableASIOFlag = FALSE ;
				SoundSysData.EnableSelfMixingFlag = FALSE ;
				goto INITSTART ;
			}


			DXST_LOGFILE_TABADD ;

			// ASIO�h���C�o�̗�
			{
				HKEY hkASIO = 0 ;
				LONG cr ;
				DWORD datatype, datasize ;
				DWORD index = 0 ;

				DXST_LOGFILEFMT_ADDUTF16LE(( "\x41\x00\x53\x00\x49\x00\x4f\x00\xc9\x30\xe9\x30\xa4\x30\xd0\x30\x6e\x30\x17\x52\x19\x63\x92\x30\x8b\x95\xcb\x59\x57\x30\x7e\x30\x59\x30\x00"/*@ L"ASIO�h���C�o�̗񋓂��J�n���܂�" @*/ )) ;

				// ASIO�̃��W�X�g���K�w���擾
				cr = RegOpenKeyWFunc( HKEY_LOCAL_MACHINE, ASIO_PATH, &hkASIO ) ;
				while( cr == ERROR_SUCCESS && SoundSysData.PF.ASIO_DriverNum < ASIO_MAX_DRIVER_NUM )
				{
					wchar_t DriverKeyName[ ASIO_MAX_DRVNAME_LEN ] ;

					if( index == 0 )
					{
						DXST_LOGFILEFMT_ADDUTF16LE(( "\x41\x00\x53\x00\x49\x00\x4f\x00\xc9\x30\xe9\x30\xa4\x30\xd0\x30\x6e\x30\xec\x30\xb8\x30\xb9\x30\xc8\x30\xea\x30\x8e\x96\x64\x5c\x92\x30\xd6\x53\x97\x5f\x57\x30\x7e\x30\x57\x30\x5f\x30\x00"/*@ L"ASIO�h���C�o�̃��W�X�g���K�w���擾���܂���" @*/ )) ;
					}

					// ASIO�̃h���C�o�L�[�����擾
					cr = RegEnumKeyWFunc( hkASIO, index, DriverKeyName, ASIO_MAX_DRVNAME_LEN ) ;
					index++ ;
					if( cr == ERROR_SUCCESS )
					{
						HKEY hkDriver = 0 ;

						DXST_LOGFILEFMT_ADDUTF16LE(( "\x41\x00\x53\x00\x49\x00\x4f\x00\xc9\x30\xe9\x30\xa4\x30\xd0\x30\x4e\x00\x6f\x00\x2e\x00\x25\x00\x64\x00\x20\x00\x6e\x30\xc9\x30\xe9\x30\xa4\x30\xd0\x30\xad\x30\xfc\x30\x0d\x54\x6f\x30\x20\x00\x25\x00\x73\x00\x20\x00\x67\x30\x59\x30\x00"/*@ L"ASIO�h���C�oNo.%d �̃h���C�o�L�[���� %s �ł�" @*/, index - 1, DriverKeyName )) ;

						// �h���C�o�L�[�̊K�w���擾
						cr = RegOpenKeyExWFunc( hkASIO, DriverKeyName, 0, KEY_READ, &hkDriver ) ;
						if( cr == ERROR_SUCCESS )
						{
							wchar_t DriverCLSID[ 256 ] ;

							DXST_LOGFILEFMT_ADDUTF16LE(( "\x41\x00\x53\x00\x49\x00\x4f\x00\xc9\x30\xe9\x30\xa4\x30\xd0\x30\x4e\x00\x6f\x00\x2e\x00\x25\x00\x64\x00\x20\x00\x6e\x30\xc9\x30\xe9\x30\xa4\x30\xd0\x30\xad\x30\xfc\x30\x8e\x96\x64\x5c\x6e\x30\xd6\x53\x97\x5f\x6b\x30\x10\x62\x9f\x52\x57\x30\x7e\x30\x57\x30\x5f\x30\x00"/*@ L"ASIO�h���C�oNo.%d �̃h���C�o�L�[�K�w�̎擾�ɐ������܂���" @*/, index - 1 )) ;

							// �h���C�o��CLSID���擾
							datatype = REG_SZ ;
							datasize = 256 ;
							cr = RegQueryValueExWFunc( hkDriver, L"clsid", 0, &datatype, ( BYTE * )DriverCLSID, &datasize ) ;
							if( cr == ERROR_SUCCESS )
							{
								HKEY hkCLSIDEnum = 0 ;

								// �h���C�o�� CLSID ���̑啶�����������ɕϊ�
								CharLowerBuffWFunc( DriverCLSID, ( DWORD )CL_strlen( DX_CHARCODEFORMAT_UTF16LE, ( char * )DriverCLSID ) ) ;

								DXST_LOGFILEFMT_ADDUTF16LE(( "\x41\x00\x53\x00\x49\x00\x4f\x00\xc9\x30\xe9\x30\xa4\x30\xd0\x30\x4e\x00\x6f\x00\x2e\x00\x25\x00\x64\x00\x20\x00\x6e\x30\x43\x00\x4c\x00\x53\x00\x49\x00\x44\x00\x6f\x30\x20\x00\x25\x00\x73\x00\x20\x00\x67\x30\x59\x30\x00"/*@ L"ASIO�h���C�oNo.%d ��CLSID�� %s �ł�" @*/, index - 1, DriverCLSID )) ;

								// �h���C�o�̃p�X���擾���邽�߂� CLSID�K�w���擾
								cr = RegOpenKeyWFunc( HKEY_CLASSES_ROOT, L"clsid", &hkCLSIDEnum ) ;
								if( cr == ERROR_SUCCESS )
								{
									DWORD index2 = 0 ;
									BOOL LoopEnd = FALSE;
									while( cr == ERROR_SUCCESS && LoopEnd == FALSE )
									{
										wchar_t TempCLSID[ 1024 ] ;

										// CLSID���擾
										cr = RegEnumKeyWFunc( hkCLSIDEnum, index2, TempCLSID, 1024 ) ;
										index2++ ;
										if( cr == ERROR_SUCCESS )
										{
											// CLSID���̑啶�����������ɕϊ�
											CharLowerBuffWFunc( TempCLSID, ( DWORD )CL_strlen( DX_CHARCODEFORMAT_UTF16LE, ( char * )TempCLSID ) ) ;

											// �h���C�oCLSID �Ɠ������`�F�b�N
											if( CL_strcmp( DX_CHARCODEFORMAT_UTF16LE, ( char * )TempCLSID, ( char * )DriverCLSID ) == 0 )
											{
												HKEY hkCLSID = 0 ;

												DXST_LOGFILEFMT_ADDUTF16LE(( "\x41\x00\x53\x00\x49\x00\x4f\x00\xc9\x30\xe9\x30\xa4\x30\xd0\x30\x4e\x00\x6f\x00\x2e\x00\x25\x00\x64\x00\x20\x00\x6e\x30\x43\x00\x4c\x00\x53\x00\x49\x00\x44\x00\x6e\x30\xc5\x60\x31\x58\x92\x30\x7a\x76\x8b\x89\x57\x30\x7e\x30\x57\x30\x5f\x30\x00"/*@ L"ASIO�h���C�oNo.%d ��CLSID�̏��𔭌����܂���" @*/, index - 1 )) ;

												// ����CLSID�����������烋�[�v���甲����
												LoopEnd = TRUE ;

												// CLSID�̊K�w���擾
												cr = RegOpenKeyExWFunc( hkCLSIDEnum, TempCLSID, 0, KEY_READ, &hkCLSID ) ;
												if( cr == ERROR_SUCCESS )
												{
													HKEY hkDLLPath = 0 ;

													DXST_LOGFILEFMT_ADDUTF16LE(( "\x41\x00\x53\x00\x49\x00\x4f\x00\xc9\x30\xe9\x30\xa4\x30\xd0\x30\x4e\x00\x6f\x00\x2e\x00\x25\x00\x64\x00\x20\x00\x6e\x30\x43\x00\x4c\x00\x53\x00\x49\x00\x44\x00\x6e\x30\xc5\x60\x31\x58\x8e\x96\x64\x5c\x6e\x30\xd6\x53\x97\x5f\x6b\x30\x10\x62\x9f\x52\x57\x30\x7e\x30\x57\x30\x5f\x30\x00"/*@ L"ASIO�h���C�oNo.%d ��CLSID�̏��K�w�̎擾�ɐ������܂���" @*/, index - 1 )) ;

													// DLL�̃p�X�L�[���擾
													cr = RegOpenKeyExWFunc( hkCLSID, L"InprocServer32", 0, KEY_READ, &hkDLLPath ) ;
													if( cr == ERROR_SUCCESS )
													{
														wchar_t DllPath[ ASIO_MAX_PATH_LEN ] ;

														DXST_LOGFILEFMT_ADDUTF16LE(( "\x41\x00\x53\x00\x49\x00\x4f\x00\xc9\x30\xe9\x30\xa4\x30\xd0\x30\x4e\x00\x6f\x00\x2e\x00\x25\x00\x64\x00\x20\x00\x6e\x30\xc9\x30\xe9\x30\xa4\x30\xd0\x30\x6e\x30\xd1\x30\xb9\x30\xad\x30\xfc\x30\x6e\x30\xd6\x53\x97\x5f\x6b\x30\x10\x62\x9f\x52\x57\x30\x7e\x30\x57\x30\x5f\x30\x00"/*@ L"ASIO�h���C�oNo.%d �̃h���C�o�̃p�X�L�[�̎擾�ɐ������܂���" @*/, index - 1 )) ;

														// DLL �̃p�X���擾
														datatype = REG_SZ ;
														datasize = ( DWORD )ASIO_MAX_PATH_LEN ;
														cr = RegQueryValueExWFunc( hkDLLPath, 0, 0, &datatype, ( BYTE * )DllPath, &datasize ) ;
														if( cr == ERROR_SUCCESS )
														{
															HANDLE hFind = NULL ;
															WIN32_FIND_DATAW FindFileData ;

															DXST_LOGFILEFMT_ADDUTF16LE(( "\x41\x00\x53\x00\x49\x00\x4f\x00\xc9\x30\xe9\x30\xa4\x30\xd0\x30\x4e\x00\x6f\x00\x2e\x00\x25\x00\x64\x00\x20\x00\x6e\x30\xc9\x30\xe9\x30\xa4\x30\xd0\x30\x6e\x30\xd1\x30\xb9\x30\x6f\x30\x20\x00\x25\x00\x73\x00\x20\x00\x67\x30\x59\x30\x00"/*@ L"ASIO�h���C�oNo.%d �̃h���C�o�̃p�X�� %s �ł�" @*/, index - 1, DllPath )) ;

															// DLL ���w��̃p�X�ɑ��݂��邩���m�F
															hFind = FindFirstFileWFunc( DllPath, &FindFileData ) ;
															if( hFind != INVALID_HANDLE_VALUE )
															{
																WinAPIData.Win32Func.FindCloseFunc( hFind ) ;
																if( ( FindFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ) == 0 )
																{
																	CLSID clsid ;

																	DXST_LOGFILEFMT_ADDUTF16LE(( "\x41\x00\x53\x00\x49\x00\x4f\x00\xc9\x30\xe9\x30\xa4\x30\xd0\x30\x4e\x00\x6f\x00\x2e\x00\x25\x00\x64\x00\x20\x00\x6e\x30\xc9\x30\xe9\x30\xa4\x30\xd0\x30\x44\x00\x6c\x00\x6c\x00\x6e\x30\x58\x5b\x28\x57\x4c\x30\xba\x78\x8d\x8a\x67\x30\x4d\x30\x7e\x30\x57\x30\x5f\x30\x00"/*@ L"ASIO�h���C�oNo.%d �̃h���C�oDll�̑��݂��m�F�ł��܂���" @*/, index - 1 )) ;

																	// CLSID�����񂩂� CLSID ���擾
																	cr = WinAPIData.Win32Func.CLSIDFromStringFunc( ( LPOLESTR )DriverCLSID, ( LPCLSID )&clsid ) ;
																	if( cr == S_OK )
																	{
																		wchar_t DriverName[ ASIO_MAX_DRVNAME_LEN ] ;

																		// CLSID ��ۑ�
																		_MEMCPY( &SoundSysData.PF.ASIO_DriverGUID[ SoundSysData.PF.ASIO_DriverNum ], &clsid, sizeof( GUID ) ) ;

																		// �h���C�o�����擾
																		datatype = REG_SZ ;
																		datasize = ASIO_MAX_DRVNAME_LEN ;
																		cr = RegQueryValueExWFunc( hkDriver, ASIO_DRV_DESC, 0, &datatype, ( LPBYTE )DriverName, &datasize ) ;
																		CL_strcpy( DX_CHARCODEFORMAT_UTF16LE, ( char * )SoundSysData.PF.ASIO_DriverNames[ SoundSysData.PF.ASIO_DriverNum ], cr == ERROR_SUCCESS ? ( char * )DriverName : ( char * )DriverKeyName ) ;

																		// �h���C�o�̐��𑝂₷
																		SoundSysData.PF.ASIO_DriverNum ++ ;

																		DXST_LOGFILEFMT_ADDUTF16LE(( "\x41\x00\x53\x00\x49\x00\x4f\x00\xc9\x30\xe9\x30\xa4\x30\xd0\x30\x4e\x00\x6f\x00\x2e\x00\x25\x00\x64\x00\x20\x00\x6e\x30\xc9\x30\xe9\x30\xa4\x30\xd0\x30\x92\x30\xea\x30\xb9\x30\xc8\x30\x6b\x30\xfd\x8f\xa0\x52\x57\x30\x7e\x30\x57\x30\x5f\x30\x00"/*@ L"ASIO�h���C�oNo.%d �̃h���C�o�����X�g�ɒǉ����܂���" @*/, index - 1 )) ;
																	}
																}
															}
															else
															{
																DXST_LOGFILEFMT_ADDUTF16LE(( "\x41\x00\x53\x00\x49\x00\x4f\x00\xc9\x30\xe9\x30\xa4\x30\xd0\x30\x4e\x00\x6f\x00\x2e\x00\x25\x00\x64\x00\x20\x00\x6e\x30\xc9\x30\xe9\x30\xa4\x30\xd0\x30\x44\x00\x6c\x00\x6c\x00\x6e\x30\x58\x5b\x28\x57\x4c\x30\xba\x78\x8d\x8a\x67\x30\x4d\x30\x7e\x30\x5b\x30\x93\x30\x67\x30\x57\x30\x5f\x30\x00"/*@ L"ASIO�h���C�oNo.%d �̃h���C�oDll�̑��݂��m�F�ł��܂���ł���" @*/, index - 1 )) ;
															}
														}
														RegCloseKey( hkDLLPath ) ;
													}
													RegCloseKey( hkCLSID ) ;
												}
											}
										}
									}				
									RegCloseKey( hkCLSIDEnum ) ;
								}
							}
							RegCloseKey( hkDriver ) ;
						}
					}
				}
				if( hkASIO )
				{
					RegCloseKey( hkASIO );
				}

				DXST_LOGFILEFMT_ADDUTF16LE(( "\x41\x00\x53\x00\x49\x00\x4f\x00\xc9\x30\xe9\x30\xa4\x30\xd0\x30\x6e\x30\x17\x52\x19\x63\x4c\x30\x8c\x5b\x86\x4e\x57\x30\x7e\x30\x57\x30\x5f\x30\x00"/*@ L"ASIO�h���C�o�̗񋓂��������܂���" @*/ )) ;

				// �h���C�o�����������Ȃ�������G���[
				if( SoundSysData.PF.ASIO_DriverNum == 0 )
				{
					DXST_LOGFILE_TABSUB ;
					DXST_LOGFILE_ADDUTF16LE( "\x41\x00\x53\x00\x49\x00\x4f\x00\xc9\x30\xe9\x30\xa4\x30\xd0\x30\x4c\x30\x7f\x30\x64\x30\x4b\x30\x8a\x30\x7e\x30\x5b\x30\x93\x30\x67\x30\x57\x30\x5f\x30\x0a\x00\x00"/*@ L"ASIO�h���C�o���݂���܂���ł���\n" @*/ ) ;
					SoundSysData.PF.EnableASIOFlag = FALSE ;
					SoundSysData.EnableSelfMixingFlag = FALSE ;
					goto INITSTART ;
				}
			}

			SoundSysData.PF.ASIO_DriverUseIndex = 0 ;

			DXST_LOGFILEFMT_ADDUTF16LE(( "\x41\x00\x53\x00\x49\x00\x4f\x00\xc9\x30\xe9\x30\xa4\x30\xd0\x30\x92\x30\xed\x30\xfc\x30\xc9\x30\x57\x30\x7e\x30\x59\x30\x00"/*@ L"ASIO�h���C�o�����[�h���܂�" @*/ )) ;

			// ASIO�h���C�o�����[�h
			hr = WinAPIData.Win32Func.CoCreateInstanceFunc( SoundSysData.PF.ASIO_DriverGUID[ SoundSysData.PF.ASIO_DriverUseIndex ], 0, CLSCTX_INPROC_SERVER, SoundSysData.PF.ASIO_DriverGUID[ SoundSysData.PF.ASIO_DriverUseIndex ], ( void ** )&SoundSysData.PF.ASIO_Driver[ SoundSysData.PF.ASIO_DriverUseIndex ] ) ;
			if( FAILED( hr ) )
			{
				DXST_LOGFILE_TABSUB ;
				DXST_LOGFILE_ADDUTF16LE( "\x41\x00\x53\x00\x49\x00\x4f\x00\xc9\x30\xe9\x30\xa4\x30\xd0\x30\x6e\x30\xed\x30\xfc\x30\xc9\x30\x6b\x30\x31\x59\x57\x65\x57\x30\x7e\x30\x57\x30\x5f\x30\x0a\x00\x00"/*@ L"ASIO�h���C�o�̃��[�h�Ɏ��s���܂���\n" @*/ ) ;
				SoundSysData.PF.EnableASIOFlag = FALSE ;
				SoundSysData.EnableSelfMixingFlag = FALSE ;
				goto INITSTART ;
			}

			DXST_LOGFILEFMT_ADDUTF16LE(( "\x41\x00\x53\x00\x49\x00\x4f\x00\xc9\x30\xe9\x30\xa4\x30\xd0\x30\x92\x30\xed\x30\xfc\x30\xc9\x30\x6b\x30\x10\x62\x9f\x52\x57\x30\x7e\x30\x57\x30\x5f\x30\x00"/*@ L"ASIO�h���C�o�����[�h�ɐ������܂���" @*/ )) ;
			DXST_LOGFILEFMT_ADDUTF16LE(( "\x41\x00\x53\x00\x49\x00\x4f\x00\x92\x30\x1d\x52\x1f\x67\x16\x53\x57\x30\x7e\x30\x59\x30\x00"/*@ L"ASIO�����������܂�" @*/ )) ;

			// ASIO��������
			if( SoundSysData.PF.ASIO_Driver[ SoundSysData.PF.ASIO_DriverUseIndex ]->init( GetMainWindowHandle() ) == 0 )
			{
				SoundSysData.PF.ASIO_Driver[ SoundSysData.PF.ASIO_DriverUseIndex ]->Release() ;
				SoundSysData.PF.ASIO_Driver[ SoundSysData.PF.ASIO_DriverUseIndex ] = NULL ;

				DXST_LOGFILE_TABSUB ;
				DXST_LOGFILE_ADDUTF16LE( "\x41\x00\x53\x00\x49\x00\x4f\x00\x6e\x30\x1d\x52\x1f\x67\x16\x53\x6b\x30\x31\x59\x57\x65\x57\x30\x7e\x30\x57\x30\x5f\x30\x0a\x00\x00"/*@ L"ASIO�̏������Ɏ��s���܂���\n" @*/ ) ;
				SoundSysData.PF.EnableASIOFlag = FALSE ;
				SoundSysData.EnableSelfMixingFlag = FALSE ;
				goto INITSTART ;
			}

			DXST_LOGFILEFMT_ADDUTF16LE(( "\x41\x00\x53\x00\x49\x00\x4f\x00\x92\x30\x1d\x52\x1f\x67\x16\x53\x6b\x30\x10\x62\x9f\x52\x57\x30\x7e\x30\x57\x30\x5f\x30\x00"/*@ L"ASIO���������ɐ������܂���" @*/ )) ;
			DXST_LOGFILEFMT_ADDUTF16LE(( "\x41\x00\x53\x00\x49\x00\x4f\x00\x6e\x30\xd0\x30\xc3\x30\xd5\x30\xa1\x30\xb5\x30\xa4\x30\xba\x30\x92\x30\xd6\x53\x97\x5f\x57\x30\x7e\x30\x59\x30\x00"/*@ L"ASIO�̃o�b�t�@�T�C�Y���擾���܂�" @*/ )) ;

			// �쐬�\�ȃo�b�t�@�T�C�Y���擾
			if( SoundSysData.PF.ASIO_Driver[ SoundSysData.PF.ASIO_DriverUseIndex ]->getBufferSize( 
				&SoundSysData.PF.ASIO_BufferMiniNum,
				&SoundSysData.PF.ASIO_BufferMaxiNum,
				&SoundSysData.PF.ASIO_Preferred,
				&SoundSysData.PF.ASIO_Granularity ) != 0 )
			{
				SoundSysData.PF.ASIO_Driver[ SoundSysData.PF.ASIO_DriverUseIndex ]->Release() ;
				SoundSysData.PF.ASIO_Driver[ SoundSysData.PF.ASIO_DriverUseIndex ] = NULL ;

				DXST_LOGFILE_TABSUB ;
				DXST_LOGFILE_ADDUTF16LE( "\x41\x00\x53\x00\x49\x00\x4f\x00\x6e\x30\x5c\x4f\x10\x62\xef\x53\xfd\x80\x6a\x30\xd0\x30\xc3\x30\xd5\x30\xa1\x30\xb5\x30\xa4\x30\xba\x30\x6e\x30\xd6\x53\x97\x5f\x6b\x30\x31\x59\x57\x65\x57\x30\x7e\x30\x57\x30\x5f\x30\x0a\x00\x00"/*@ L"ASIO�̍쐬�\�ȃo�b�t�@�T�C�Y�̎擾�Ɏ��s���܂���\n" @*/ ) ;
				SoundSysData.PF.EnableASIOFlag = FALSE ;
				SoundSysData.EnableSelfMixingFlag = FALSE ;
				goto INITSTART ;
			}

			// �o�b�t�@�T�C�Y�̌���
			if( SoundSysData.PF.ASIO_EnableUserParam && SoundSysData.PF.ASIO_UserParam_BufferSize >= 0 )
			{
				SoundSysData.PF.ASIO_BufferSize = SoundSysData.PF.ASIO_UserParam_BufferSize ;
				if( SoundSysData.PF.ASIO_BufferSize < SoundSysData.PF.ASIO_BufferMiniNum )
				{
					SoundSysData.PF.ASIO_BufferSize = SoundSysData.PF.ASIO_BufferMiniNum ;
				}
				else
				if( SoundSysData.PF.ASIO_BufferSize < SoundSysData.PF.ASIO_BufferMaxiNum )
				{
					SoundSysData.PF.ASIO_BufferSize = SoundSysData.PF.ASIO_BufferMaxiNum ;
				}
			}
			else
			{
				SoundSysData.PF.ASIO_BufferSize = SoundSysData.PF.ASIO_Preferred ;
			}

			DXST_LOGFILEFMT_ADDUTF16LE(( "\x41\x00\x53\x00\x49\x00\x4f\x00\x6e\x30\xd0\x30\xc3\x30\xd5\x30\xa1\x30\xb5\x30\xa4\x30\xba\x30\x6e\x30\xd6\x53\x97\x5f\x6b\x30\x10\x62\x9f\x52\x57\x30\x7e\x30\x57\x30\x5f\x30\x00"/*@ L"ASIO�̃o�b�t�@�T�C�Y�̎擾�ɐ������܂���" @*/ )) ;
			DXST_LOGFILEFMT_ADDUTF16LE(( "\x09\x00\x00\x67\x0f\x5c\xd0\x30\xc3\x30\xd5\x30\xa1\x30\xb5\x30\xa4\x30\xba\x30\x20\x00\x20\x00\x20\x00\x3a\x00\x20\x00\x25\x00\x64\x00\x00"/*@ L"\t�ŏ��o�b�t�@�T�C�Y   : %d" @*/, SoundSysData.PF.ASIO_BufferMiniNum )) ;
			DXST_LOGFILEFMT_ADDUTF16LE(( "\x09\x00\x00\x67\x27\x59\xd0\x30\xc3\x30\xd5\x30\xa1\x30\xb5\x30\xa4\x30\xba\x30\x20\x00\x20\x00\x20\x00\x3a\x00\x20\x00\x25\x00\x64\x00\x00"/*@ L"\t�ő�o�b�t�@�T�C�Y   : %d" @*/, SoundSysData.PF.ASIO_BufferMaxiNum )) ;
			DXST_LOGFILEFMT_ADDUTF16LE(( "\x09\x00\x2a\x51\x48\x51\xd0\x30\xc3\x30\xd5\x30\xa1\x30\xb5\x30\xa4\x30\xba\x30\x20\x00\x20\x00\x20\x00\x3a\x00\x20\x00\x25\x00\x64\x00\x00"/*@ L"\t�D��o�b�t�@�T�C�Y   : %d" @*/, SoundSysData.PF.ASIO_Preferred )) ;
			DXST_LOGFILEFMT_ADDUTF16LE(( "\x09\x00\xd0\x30\xc3\x30\xd5\x30\xa1\x30\xb5\x30\xa4\x30\xba\x30\x6e\x30\x92\x7c\xa6\x5e\x20\x00\x3a\x00\x20\x00\x25\x00\x64\x00\x00"/*@ L"\t�o�b�t�@�T�C�Y�̗��x : %d" @*/, SoundSysData.PF.ASIO_Granularity )) ;
			DXST_LOGFILEFMT_ADDUTF16LE(( "\x09\x00\x7f\x4f\x28\x75\x59\x30\x8b\x30\xd0\x30\xc3\x30\xd5\x30\xa1\x30\xb5\x30\xa4\x30\xba\x30\x20\x00\x3a\x00\x20\x00\x25\x00\x64\x00\x00"/*@ L"\t�g�p����o�b�t�@�T�C�Y : %d" @*/, SoundSysData.PF.ASIO_BufferSize )) ;

			// �o�b�t�@�����i�[����̈���[��������
			_MEMSET( SoundSysData.PF.ASIO_BufferInfo, 0, sizeof( SoundSysData.PF.ASIO_BufferInfo ) ) ;
			for( i = 0 ; i < 2; i++ )
			{
				SoundSysData.PF.ASIO_BufferInfo[ i ].isInput    = FALSE ;
				SoundSysData.PF.ASIO_BufferInfo[ i ].channelNum = ( long )i ;
			}

			// �`�����l�����̎擾
			DXST_LOGFILEFMT_ADDUTF16LE(( "\x41\x00\x53\x00\x49\x00\x4f\x00\x6e\x30\xc1\x30\xe3\x30\xf3\x30\xcd\x30\xeb\x30\xc5\x60\x31\x58\x92\x30\xd6\x53\x97\x5f\x57\x30\x7e\x30\x59\x30\x00"/*@ L"ASIO�̃`�����l�������擾���܂�" @*/ )) ;
			ChannelInfo.channel = SoundSysData.PF.ASIO_BufferInfo[ 0 ].channelNum ;
			ChannelInfo.isInput = SoundSysData.PF.ASIO_BufferInfo[ 0 ].isInput ;
			if( SoundSysData.PF.ASIO_Driver[ SoundSysData.PF.ASIO_DriverUseIndex ]->getChannelInfo( &ChannelInfo ) != 0 )
			{
				SoundSysData.PF.ASIO_Driver[ SoundSysData.PF.ASIO_DriverUseIndex ]->Release() ;
				SoundSysData.PF.ASIO_Driver[ SoundSysData.PF.ASIO_DriverUseIndex ] = NULL ;

				DXST_LOGFILE_TABSUB ;
				DXST_LOGFILE_ADDUTF16LE( "\x41\x00\x53\x00\x49\x00\x4f\x00\x6e\x30\xc1\x30\xe3\x30\xf3\x30\xcd\x30\xeb\x30\xc5\x60\x31\x58\x6e\x30\xd6\x53\x97\x5f\x6b\x30\x31\x59\x57\x65\x57\x30\x7e\x30\x57\x30\x5f\x30\x0a\x00\x00"/*@ L"ASIO�̃`�����l�����̎擾�Ɏ��s���܂���\n" @*/ ) ;
				SoundSysData.PF.EnableASIOFlag = FALSE ;
				SoundSysData.EnableSelfMixingFlag = FALSE ;
				goto INITSTART ;
			}
			DXST_LOGFILEFMT_ADDUTF16LE(( "\x41\x00\x53\x00\x49\x00\x4f\x00\x6e\x30\xc1\x30\xe3\x30\xf3\x30\xcd\x30\xeb\x30\xc5\x60\x31\x58\x6e\x30\xd6\x53\x97\x5f\x6b\x30\x10\x62\x9f\x52\x57\x30\x7e\x30\x57\x30\x5f\x30\x00"/*@ L"ASIO�̃`�����l�����̎擾�ɐ������܂���" @*/ )) ;
			DXST_LOGFILEFMT_ADDUTF16LE(( "\x09\x00\xd5\x30\xa9\x30\xfc\x30\xde\x30\xc3\x30\xc8\x30\xbf\x30\xa4\x30\xd7\x30\x20\x00\x3a\x00\x20\x00\x25\x00\x73\x00\x00"/*@ L"\t�t�H�[�}�b�g�^�C�v : %s" @*/, ASIO_FormatInfoTable[ ChannelInfo.type ].Name )) ;

			// �T���v�����[�g��ݒ肷��
			SampleRate = SoundSysData.PF.ASIO_EnableUserParam ? ( float )SoundSysData.PF.ASIO_UserParam_SamplePerSec : 44100.0 ;
			DXST_LOGFILEFMT_ADDUTF16LE(( "\x41\x00\x53\x00\x49\x00\x4f\x00\x6e\x30\xb5\x30\xf3\x30\xd7\x30\xeb\x30\xec\x30\xfc\x30\xc8\x30\x92\x30\x20\x00\x25\x00\x64\x00\x48\x00\x7a\x00\x20\x00\x6b\x30\x2d\x8a\x9a\x5b\x57\x30\x7e\x30\x59\x30\x00"/*@ L"ASIO�̃T���v�����[�g�� %dHz �ɐݒ肵�܂�" @*/, _DTOL( SampleRate ) )) ;
			if( SoundSysData.PF.ASIO_Driver[ SoundSysData.PF.ASIO_DriverUseIndex ]->setSampleRate( SampleRate ) != 0 )
			{
				DXST_LOGFILEFMT_ADDUTF16LE(( "\x41\x00\x53\x00\x49\x00\x4f\x00\x6e\x30\xb5\x30\xf3\x30\xd7\x30\xeb\x30\xec\x30\xfc\x30\xc8\x30\x6e\x30\x2d\x8a\x9a\x5b\x6b\x30\x31\x59\x57\x65\x57\x30\x7e\x30\x57\x30\x5f\x30\x00"/*@ L"ASIO�̃T���v�����[�g�̐ݒ�Ɏ��s���܂���" @*/ )) ;
			}
			else
			{
				DXST_LOGFILEFMT_ADDUTF16LE(( "\x41\x00\x53\x00\x49\x00\x4f\x00\x6e\x30\xb5\x30\xf3\x30\xd7\x30\xeb\x30\xec\x30\xfc\x30\xc8\x30\x6e\x30\x2d\x8a\x9a\x5b\x6b\x30\x10\x62\x9f\x52\x57\x30\x7e\x30\x57\x30\x5f\x30\x00"/*@ L"ASIO�̃T���v�����[�g�̐ݒ�ɐ������܂���" @*/ )) ;
			}

			// �T���v�����O���[�g���擾����
			DXST_LOGFILEFMT_ADDUTF16LE(( "\x41\x00\x53\x00\x49\x00\x4f\x00\x6e\x30\xb5\x30\xf3\x30\xd7\x30\xeb\x30\xec\x30\xfc\x30\xc8\x30\x92\x30\xd6\x53\x97\x5f\x57\x30\x7e\x30\x59\x30\x00"/*@ L"ASIO�̃T���v�����[�g���擾���܂�" @*/ )) ;
			if( SoundSysData.PF.ASIO_Driver[ SoundSysData.PF.ASIO_DriverUseIndex ]->getSampleRate( &SampleRate ) != 0 )
			{
				SoundSysData.PF.ASIO_Driver[ SoundSysData.PF.ASIO_DriverUseIndex ]->Release() ;
				SoundSysData.PF.ASIO_Driver[ SoundSysData.PF.ASIO_DriverUseIndex ] = NULL ;

				DXST_LOGFILE_TABSUB ;
				DXST_LOGFILE_ADDUTF16LE( "\x41\x00\x53\x00\x49\x00\x4f\x00\x6e\x30\xb5\x30\xf3\x30\xd7\x30\xeb\x30\xec\x30\xfc\x30\xc8\x30\x6e\x30\xd6\x53\x97\x5f\x6b\x30\x31\x59\x57\x65\x57\x30\x7e\x30\x57\x30\x5f\x30\x0a\x00\x00"/*@ L"ASIO�̃T���v�����[�g�̎擾�Ɏ��s���܂���\n" @*/ ) ;
				SoundSysData.PF.EnableASIOFlag = FALSE ;
				SoundSysData.EnableSelfMixingFlag = FALSE ;
				goto INITSTART ;
			}
			DXST_LOGFILEFMT_ADDUTF16LE(( "\x41\x00\x53\x00\x49\x00\x4f\x00\x6e\x30\xb5\x30\xf3\x30\xd7\x30\xeb\x30\xec\x30\xfc\x30\xc8\x30\x6e\x30\xd6\x53\x97\x5f\x6b\x30\x10\x62\x9f\x52\x57\x30\x7e\x30\x57\x30\x5f\x30\x00"/*@ L"ASIO�̃T���v�����[�g�̎擾�ɐ������܂���" @*/ )) ;
			DXST_LOGFILEFMT_ADDUTF16LE(( "\x09\x00\xb5\x30\xf3\x30\xd7\x30\xeb\x30\xec\x30\xfc\x30\xc8\x30\x20\x00\x3a\x00\x20\x00\x25\x00\x2e\x00\x33\x00\x66\x00\x00"/*@ L"\t�T���v�����[�g : %.3f" @*/, SampleRate )) ;

			// ���O�~�L�V���O�̃p�����[�^��ݒ肷��
			{
				ASIO_FORMATINFO *Format = &ASIO_FormatInfoTable[ ChannelInfo.type ] ;
				_MEMSET( &SoundSysData.PF.SelfMixingFormatEx, 0, sizeof( SoundSysData.PF.SelfMixingFormatEx ) ) ;
				SoundSysData.PF.SelfMixingFormatEx.Format.cbSize	            = sizeof( D_WAVEFORMATEXTENSIBLE ) - sizeof( WAVEFORMATEX ) ;
				SoundSysData.PF.SelfMixingFormatEx.Format.wFormatTag            = D_WAVE_FORMAT_EXTENSIBLE ;
				SoundSysData.PF.SelfMixingFormatEx.Format.nChannels             = 2 ;
				SoundSysData.PF.SelfMixingFormatEx.Format.nSamplesPerSec        = _DTOL( SampleRate ) ;
				SoundSysData.PF.SelfMixingFormatEx.Format.wBitsPerSample        = Format->BitsPerSample ;
				SoundSysData.PF.SelfMixingFormatEx.Format.nBlockAlign           = SoundSysData.PF.SelfMixingFormatEx.Format.nChannels * SoundSysData.PF.SelfMixingFormatEx.Format.wBitsPerSample / 8 ;
				SoundSysData.PF.SelfMixingFormatEx.Format.nAvgBytesPerSec       = SoundSysData.PF.SelfMixingFormatEx.Format.nSamplesPerSec * SoundSysData.PF.SelfMixingFormatEx.Format.nBlockAlign ;
				SoundSysData.PF.SelfMixingFormatEx.Samples.wValidBitsPerSample  = Format->ValidBitsPerSample ;
				SoundSysData.PF.SelfMixingFormatEx.dwChannelMask                = D_SPEAKER_FRONT_LEFT | D_SPEAKER_FRONT_RIGHT ;
				SoundSysData.PF.SelfMixingFormatEx.SubFormat                    = Format->IsFloat ? D_KSDATAFORMAT_SUBTYPE_IEEE_FLOAT : D_KSDATAFORMAT_SUBTYPE_PCM ;

				SoundSysData.SelfMixingFormat = SoundSysData.PF.SelfMixingFormatEx.Format ;
				SoundSysData.SelfMixingFormatValidBitsPerSample = Format->ValidBitsPerSample ;
				SoundSysData.SelfMixingFormatIsFloat = Format->IsFloat ;
				SoundSysData.SelfMixingFormatIsMSB   = Format->IsMSB ;
			}

			// �g�p����`�����l���̃f�[�^�o�b�t�@�̊m��
			DXST_LOGFILEFMT_ADDUTF16LE(( "\x41\x00\x53\x00\x49\x00\x4f\x00\x6e\x30\xd0\x30\xc3\x30\xd5\x30\xa1\x30\x92\x30\x5c\x4f\x10\x62\x57\x30\x7e\x30\x59\x30\x00"/*@ L"ASIO�̃o�b�t�@���쐬���܂�" @*/ )) ;
			Callbacks.bufferSwitch         = &ASIO_BufferSwitch ;
			Callbacks.sampleRateDidChange  = ASIO_SampleRateDidChange ;
			Callbacks.asioMessage          = ASIO_AsioMessage ;
			Callbacks.bufferSwitchTimeInfo = NULL /* ASIO_BufferSwitchTimeInfo */ ;
			if( SoundSysData.PF.ASIO_Driver[ SoundSysData.PF.ASIO_DriverUseIndex ]->createBuffers( SoundSysData.PF.ASIO_BufferInfo, 2, SoundSysData.PF.ASIO_BufferSize, &Callbacks ) != 0 )
			{
				SoundSysData.PF.ASIO_Driver[ SoundSysData.PF.ASIO_DriverUseIndex ]->Release() ;
				SoundSysData.PF.ASIO_Driver[ SoundSysData.PF.ASIO_DriverUseIndex ] = NULL ;

				DXST_LOGFILE_TABSUB ;
				DXST_LOGFILE_ADDUTF16LE( "\x41\x00\x53\x00\x49\x00\x4f\x00\x6e\x30\xd0\x30\xc3\x30\xd5\x30\xa1\x30\xfc\x30\x5c\x4f\x10\x62\x6b\x30\x31\x59\x57\x65\x57\x30\x7e\x30\x57\x30\x5f\x30\x0a\x00\x00"/*@ L"ASIO�̃o�b�t�@�[�쐬�Ɏ��s���܂���\n" @*/ ) ;
				SoundSysData.PF.EnableASIOFlag = FALSE ;
				SoundSysData.EnableSelfMixingFlag = FALSE ;
				goto INITSTART ;
			}
			DXST_LOGFILEFMT_ADDUTF16LE(( "\x41\x00\x53\x00\x49\x00\x4f\x00\x6e\x30\xd0\x30\xc3\x30\xd5\x30\xa1\x30\x6e\x30\x5c\x4f\x10\x62\x6b\x30\x10\x62\x9f\x52\x57\x30\x7e\x30\x57\x30\x5f\x30\x00"/*@ L"ASIO�̃o�b�t�@�̍쐬�ɐ������܂���" @*/ )) ;

			// ��Ɨp�o�b�t�@�̏���
			if( SetupSelfMixingWorkBuffer( SoundSysData.SelfMixingFormatIsFloat, SoundSysData.PF.ASIO_BufferSize ) < 0 )
			{
				SoundSysData.PF.ASIO_Driver[ SoundSysData.PF.ASIO_DriverUseIndex ]->disposeBuffers() ;
				SoundSysData.PF.ASIO_Driver[ SoundSysData.PF.ASIO_DriverUseIndex ]->Release() ;
				SoundSysData.PF.ASIO_Driver[ SoundSysData.PF.ASIO_DriverUseIndex ] = NULL ;

				DXST_LOGFILE_TABSUB ;
				DXST_LOGFILE_ADDUTF16LE( "\xea\x81\x4d\x52\xdf\x30\xad\x30\xb7\x30\xf3\x30\xb0\x30\x5c\x4f\x6d\x69\x28\x75\xd0\x30\xc3\x30\xd5\x30\xa1\x30\x6e\x30\xba\x78\xdd\x4f\x6b\x30\x31\x59\x57\x65\x57\x30\x7e\x30\x57\x30\x5f\x30\x0a\x00\x00"/*@ L"���O�~�L�V���O��Ɨp�o�b�t�@�̊m�ۂɎ��s���܂���\n" @*/ ) ;
				SoundSysData.PF.EnableASIOFlag = FALSE ;
				SoundSysData.EnableSelfMixingFlag = FALSE ;
				goto INITSTART ;
			}

			// �������̏؋����c��
			SoundSysData.PF.InitializeFlag = TRUE ;

			// �T�E���h�o�͂��J�n����
			DXST_LOGFILEFMT_ADDUTF16LE(( "\x41\x00\x53\x00\x49\x00\x4f\x00\x6e\x30\xb5\x30\xa6\x30\xf3\x30\xc9\x30\x6e\x30\x8d\x51\x1f\x75\x92\x30\x8b\x95\xcb\x59\x57\x30\x7e\x30\x59\x30\x00"/*@ L"ASIO�̃T�E���h�̍Đ����J�n���܂�" @*/ )) ;
			if( SoundSysData.PF.ASIO_Driver[ SoundSysData.PF.ASIO_DriverUseIndex ]->start() != 0 )
			{
				SoundSysData.PF.ASIO_Driver[ SoundSysData.PF.ASIO_DriverUseIndex ]->disposeBuffers() ;
				SoundSysData.PF.ASIO_Driver[ SoundSysData.PF.ASIO_DriverUseIndex ]->Release() ;
				SoundSysData.PF.ASIO_Driver[ SoundSysData.PF.ASIO_DriverUseIndex ] = NULL ;

				DXST_LOGFILE_TABSUB ;
				DXST_LOGFILE_ADDUTF16LE( "\x41\x00\x53\x00\x49\x00\x4f\x00\x6e\x30\xb5\x30\xa6\x30\xf3\x30\xc9\x30\x6e\x30\x8d\x51\x1f\x75\x6e\x30\x8b\x95\xcb\x59\x6b\x30\x31\x59\x57\x65\x57\x30\x7e\x30\x57\x30\x5f\x30\x0a\x00\x00"/*@ L"ASIO�̃T�E���h�̍Đ��̊J�n�Ɏ��s���܂���\n" @*/ ) ;
				SoundSysData.PF.EnableASIOFlag = FALSE ;
				SoundSysData.EnableSelfMixingFlag = FALSE ;
				goto INITSTART ;
			}
			DXST_LOGFILE_TABSUB ;
			DXST_LOGFILE_ADDUTF16LE( "\x41\x00\x53\x00\x49\x00\x4f\x00\x20\x00\x6e\x30\x1d\x52\x1f\x67\x16\x53\x6b\x30\x10\x62\x9f\x52\x57\x30\x7e\x30\x57\x30\x5f\x30\x0a\x00\x00"/*@ L"ASIO �̏������ɐ������܂���\n" @*/ ) ;
		}
		else
#endif // DX_NON_ASIO
		if( SoundSysData.PF.DisableWASAPIFlag == FALSE )
		{
			HRESULT ret ;
			D_WAVEFORMATEXTENSIBLE *MatchWaveFormat = NULL ;
			int IsExclusive = SoundSysData.PF.WASAPI_IsExclusiveFlag ;
			int ChangeFormat = FALSE ;

			// ���O�~�L�V���O���s���t���O�𗧂Ă�
			SoundSysData.EnableSelfMixingFlag = TRUE ;

			DXST_LOGFILE_ADDUTF16LE( "\x57\x00\x41\x00\x53\x00\x41\x00\x50\x00\x49\x00\x20\x00\x6e\x30\x1d\x52\x1f\x67\x16\x53\x92\x30\x4c\x88\x44\x30\x7e\x30\x59\x30\x0a\x00\x00"/*@ L"WASAPI �̏��������s���܂�\n" @*/ ) ;

			DXST_LOGFILE_TABADD ;

			// �}���`���f�B�A�f�o�C�X�񋓎q
			ret = WinAPIData.Win32Func.CoCreateInstanceFunc( CLSID_MMDEVICEENUMERATOR, NULL, CLSCTX_ALL, IID_IMMDEVICEENUMERATOR, ( void** )&SoundSysData.PF.IMMDeviceEnumerator ) ;
			if( FAILED( ret ) )
			{
				DXST_LOGFILE_TABSUB ;
				DXST_LOGFILE_ADDUTF16LE( "\x49\x00\x4d\x00\x4d\x00\x44\x00\x65\x00\x76\x00\x69\x00\x63\x00\x65\x00\x45\x00\x6e\x00\x75\x00\x6d\x00\x65\x00\x72\x00\x61\x00\x74\x00\x6f\x00\x72\x00\x20\x00\x6e\x30\xd6\x53\x97\x5f\x6b\x30\x31\x59\x57\x65\x57\x30\x7e\x30\x57\x30\x5f\x30\x0a\x00\x00"/*@ L"IMMDeviceEnumerator �̎擾�Ɏ��s���܂���\n" @*/ ) ;
				SoundSysData.PF.DisableWASAPIFlag = TRUE ;
				SoundSysData.EnableSelfMixingFlag = FALSE ;
				goto INITSTART ;
			}

			// �f�t�H���g�̃f�o�C�X��I��
			ret = SoundSysData.PF.IMMDeviceEnumerator->GetDefaultAudioEndpoint( D_eRender, D_eConsole, &SoundSysData.PF.IMMDevice ) ;
			if( FAILED( ret ) )
			{
				SoundSysData.PF.IMMDeviceEnumerator->Release() ;
				SoundSysData.PF.IMMDeviceEnumerator = NULL ;

				DXST_LOGFILE_TABSUB ;
				DXST_LOGFILE_ADDUTF16LE( "\x49\x00\x4d\x00\x4d\x00\x44\x00\x65\x00\x76\x00\x69\x00\x63\x00\x65\x00\x20\x00\x6e\x30\xd6\x53\x97\x5f\x6b\x30\x31\x59\x57\x65\x57\x30\x7e\x30\x57\x30\x5f\x30\x0a\x00\x00"/*@ L"IMMDevice �̎擾�Ɏ��s���܂���\n" @*/ ) ;
				SoundSysData.PF.DisableWASAPIFlag = TRUE ;
				SoundSysData.EnableSelfMixingFlag = FALSE ;
				goto INITSTART ;
			}

			// �I�[�f�B�I�N���C�A���g�̎擾
			ret = SoundSysData.PF.IMMDevice->Activate( IID_IAUDIOCLIENT, CLSCTX_ALL, NULL, ( void** )&SoundSysData.PF.AudioClient ) ;
			if( FAILED( ret ) )
			{
				SoundSysData.PF.IMMDevice->Release() ;
				SoundSysData.PF.IMMDevice = NULL ;

				SoundSysData.PF.IMMDeviceEnumerator->Release() ;
				SoundSysData.PF.IMMDeviceEnumerator = NULL ;

				DXST_LOGFILE_TABSUB ;
				DXST_LOGFILE_ADDUTF16LE( "\xaa\x30\xfc\x30\xc7\x30\xa3\x30\xaa\x30\xaf\x30\xe9\x30\xa4\x30\xa2\x30\xf3\x30\xc8\x30\x6e\x30\xd6\x53\x97\x5f\x6b\x30\x31\x59\x57\x65\x57\x30\x7e\x30\x57\x30\x5f\x30\x0a\x00\x00"/*@ L"�I�[�f�B�I�N���C�A���g�̎擾�Ɏ��s���܂���\n" @*/ ) ;
				SoundSysData.PF.DisableWASAPIFlag = TRUE ;
				SoundSysData.EnableSelfMixingFlag = FALSE ;
				goto INITSTART ;
			}

			// �f�t�H���g�f�o�C�X�s���I�h�ƍŏ��f�o�C�X�s���I�h���擾
			ret = SoundSysData.PF.AudioClient->GetDevicePeriod( &SoundSysData.PF.WASAPI_DefaultDevicePeriod, &SoundSysData.PF.WASAPI_MinimumDevicePeriod ) ;

			DXST_LOGFILEFMT_ADDUTF16LE(( "\xc7\x30\xd5\x30\xa9\x30\xeb\x30\xc8\x30\x45\x90\xf6\x5e\x42\x66\x93\x95\x20\x00\x3a\x00\x20\x00\x25\x00\x2e\x00\x33\x00\x66\x00\x20\x00\x6d\x00\x73\x00\x00"/*@ L"�f�t�H���g�x������ : %.3f ms" @*/, ( float )SoundSysData.PF.WASAPI_DefaultDevicePeriod / 10000.0f )) ;
			DXST_LOGFILEFMT_ADDUTF16LE(( "\x00\x67\x0f\x5c\x45\x90\xf6\x5e\x42\x66\x93\x95\x20\x00\x20\x00\x20\x00\x20\x00\x20\x00\x20\x00\x20\x00\x3a\x00\x20\x00\x25\x00\x2e\x00\x33\x00\x66\x00\x20\x00\x6d\x00\x73\x00\x00"/*@ L"�ŏ��x������       : %.3f ms" @*/, ( float )SoundSysData.PF.WASAPI_MinimumDevicePeriod / 10000.0f )) ;

			// ���ۂɎg�p����f�o�C�X�s���I�h�̎Z�o
			if( SoundSysData.PF.WASAPI_EnableUserParam && SoundSysData.PF.WASAPI_UserParam_DevicePeriod >= 0 )
			{
				SoundSysData.PF.WASAPI_DevicePeriod = SoundSysData.PF.WASAPI_MinimumDevicePeriod > SoundSysData.PF.WASAPI_UserParam_DevicePeriod ? SoundSysData.PF.WASAPI_MinimumDevicePeriod : SoundSysData.PF.WASAPI_UserParam_DevicePeriod ;
			}
			else
			{
				SoundSysData.PF.WASAPI_DevicePeriod = SoundSysData.PF.WASAPI_DefaultDevicePeriod ;
			}

			DXST_LOGFILEFMT_ADDUTF16LE(( "\x45\x90\xf6\x5e\x42\x66\x93\x95\x20\x00\x20\x00\x20\x00\x20\x00\x20\x00\x20\x00\x20\x00\x20\x00\x20\x00\x20\x00\x20\x00\x3a\x00\x20\x00\x25\x00\x2e\x00\x33\x00\x66\x00\x20\x00\x6d\x00\x73\x00\x00"/*@ L"�x������           : %.3f ms" @*/, ( float )SoundSysData.PF.WASAPI_DevicePeriod / 10000.0f )) ;

			// �r�����[�h���ǂ����ŏ����𕪊�
			if( IsExclusive )
			{
				// �t�H�[�}�b�g�̐ݒ�
				D_WAVEFORMATEXTENSIBLE WaveFormat ;
				_MEMSET( &WaveFormat, 0, sizeof( WaveFormat ) ) ;
				WaveFormat.Format.cbSize                = sizeof( D_WAVEFORMATEXTENSIBLE ) - sizeof( WAVEFORMATEX );
				WaveFormat.Format.wFormatTag            = D_WAVE_FORMAT_EXTENSIBLE ;
				WaveFormat.Format.nChannels             = 2 ;
				WaveFormat.Format.nSamplesPerSec        = SoundSysData.PF.WASAPI_EnableUserParam ? ( DWORD )SoundSysData.PF.WASAPI_UserParam_SamplePerSec : 44100 ;
				WaveFormat.Format.wBitsPerSample        = 16 ;
				WaveFormat.Format.nBlockAlign           = WaveFormat.Format.nChannels * WaveFormat.Format.wBitsPerSample / 8 ;
				WaveFormat.Format.nAvgBytesPerSec       = WaveFormat.Format.nSamplesPerSec * WaveFormat.Format.nBlockAlign ;
				WaveFormat.Samples.wValidBitsPerSample  = WaveFormat.Format.wBitsPerSample ;
				WaveFormat.dwChannelMask                = D_SPEAKER_FRONT_LEFT | D_SPEAKER_FRONT_RIGHT ;
				WaveFormat.SubFormat                    = D_KSDATAFORMAT_SUBTYPE_PCM ;

				// �t�H�[�}�b�g�̃T�|�[�g�`�F�b�N
				ret = SoundSysData.PF.AudioClient->IsFormatSupported( D_AUDCLNT_SHAREMODE_EXCLUSIVE, ( WAVEFORMATEX * )&WaveFormat, NULL ) ;
				if( FAILED( ret ) )
				{
					int i, j ;

					DXST_LOGFILEFMT_ADDUTF16LE(( "\xc7\x30\xd0\x30\xa4\x30\xb9\x30\x4c\x30\x07\x63\x9a\x5b\x6e\x30\xd5\x30\xa9\x30\xfc\x30\xde\x30\xc3\x30\xc8\x30\x6b\x30\x5e\x97\xfe\x5b\xdc\x5f\x60\x30\x63\x30\x5f\x30\x5f\x30\x81\x30\xd6\x4e\x6e\x30\xd5\x30\xa9\x30\xfc\x30\xde\x30\xc3\x30\xc8\x30\x67\x30\x6e\x30\x1d\x52\x1f\x67\x16\x53\x92\x30\x66\x8a\x7f\x30\x7e\x30\x59\x30\x00"/*@ L"�f�o�C�X���w��̃t�H�[�}�b�g�ɔ�Ή����������ߑ��̃t�H�[�}�b�g�ł̏����������݂܂�" @*/ )) ;

					// �T�|�[�g�O�������ꍇ�A�Ή����Ă������ȃt�H�[�}�b�g�������ݎ���
					for( i = 0 ; WASAPI_SamplePerSec[ i ] != 0 ; i ++ )
					{
						for( j = 0 ; WASAPI_BitPerSample[ j ] != 0 ; j ++ )
						{
							WaveFormat.Format.nSamplesPerSec        = WASAPI_SamplePerSec[ i ] ;
							WaveFormat.Format.wBitsPerSample        = WASAPI_BitPerSample[ j ] ;
							WaveFormat.Format.nBlockAlign           = WaveFormat.Format.nChannels * WaveFormat.Format.wBitsPerSample / 8 ;
							WaveFormat.Format.nAvgBytesPerSec       = WaveFormat.Format.nSamplesPerSec * WaveFormat.Format.nBlockAlign ;
							WaveFormat.Samples.wValidBitsPerSample  = WASAPI_ValidBitPerSample[ j ] ;
							WaveFormat.SubFormat                    = WASAPI_IsFloatType[ j ] ? D_KSDATAFORMAT_SUBTYPE_IEEE_FLOAT : D_KSDATAFORMAT_SUBTYPE_PCM ;
							ret = SoundSysData.PF.AudioClient->IsFormatSupported( D_AUDCLNT_SHAREMODE_EXCLUSIVE, ( WAVEFORMATEX * )&WaveFormat, NULL ) ;
							if( SUCCEEDED( ret ) )
							{
								break ;
							}
						}
						if( SUCCEEDED( ret ) )
						{
							break ;
						}
					}

					if( FAILED( ret ) )
					{
						SoundSysData.PF.AudioClient->Release() ;
						SoundSysData.PF.AudioClient = NULL ;

						SoundSysData.PF.IMMDevice->Release() ;
						SoundSysData.PF.IMMDevice = NULL ;

						SoundSysData.PF.IMMDeviceEnumerator->Release() ;
						SoundSysData.PF.IMMDeviceEnumerator = NULL ;

						DXST_LOGFILE_TABSUB ;
						DXST_LOGFILE_ADDUTF16LE( "\x57\x00\x41\x00\x53\x00\x41\x00\x50\x00\x49\x00\x20\x00\x6e\x30\x92\x63\xd6\x4e\xe2\x30\xfc\x30\xc9\x30\x4c\x30\xb5\x30\xdd\x30\xfc\x30\xc8\x30\x59\x30\x8b\x30\xd5\x30\xa9\x30\xfc\x30\xde\x30\xc3\x30\xc8\x30\x4c\x30\x8b\x89\x64\x30\x4b\x30\x8a\x30\x7e\x30\x5b\x30\x93\x30\x67\x30\x57\x30\x5f\x30\x0a\x00\x00"/*@ L"WASAPI �̔r�����[�h���T�|�[�g����t�H�[�}�b�g��������܂���ł���\n" @*/ ) ;
						DXST_LOGFILE_ADDUTF16LE( "\x92\x63\xd6\x4e\xe2\x30\xfc\x30\xc9\x30\x6e\x30\x1d\x52\x1f\x67\x16\x53\x6b\x30\x31\x59\x57\x65\x57\x30\x5f\x30\x6e\x30\x67\x30\x71\x51\x09\x67\xe2\x30\xfc\x30\xc9\x30\x6e\x30\x1d\x52\x1f\x67\x16\x53\x92\x30\x66\x8a\x4c\x88\x57\x30\x7e\x30\x59\x30\x0a\x00\x00"/*@ L"�r�����[�h�̏������Ɏ��s�����̂ŋ��L���[�h�̏����������s���܂�\n" @*/ ) ;
						SoundSysData.PF.WASAPI_IsExclusiveFlag = FALSE ;
						goto INITSTART ;
					}
				}

				// �t�H�[�}�b�g��ۑ�
				SoundSysData.PF.SelfMixingFormatEx = WaveFormat ;
			}
			else
			{
				ret = SoundSysData.PF.AudioClient->GetMixFormat( ( WAVEFORMATEX ** )&MatchWaveFormat ) ;
				if( FAILED( ret ) )
				{
					SoundSysData.PF.AudioClient->Release() ;
					SoundSysData.PF.AudioClient = NULL ;

					SoundSysData.PF.IMMDevice->Release() ;
					SoundSysData.PF.IMMDevice = NULL ;

					SoundSysData.PF.IMMDeviceEnumerator->Release() ;
					SoundSysData.PF.IMMDeviceEnumerator = NULL ;

					DXST_LOGFILE_TABSUB ;
					DXST_LOGFILE_ADDUTF16LE( "\x57\x00\x41\x00\x53\x00\x41\x00\x50\x00\x49\x00\x20\x00\x6e\x30\x71\x51\x09\x67\xe2\x30\xfc\x30\xc9\x30\x6e\x30\xb5\x30\xa6\x30\xf3\x30\xc9\x30\xd5\x30\xa9\x30\xfc\x30\xde\x30\xc3\x30\xc8\x30\x6e\x30\xd6\x53\x97\x5f\x6b\x30\x31\x59\x57\x65\x57\x30\x7e\x30\x57\x30\x5f\x30\x0a\x00\x00"/*@ L"WASAPI �̋��L���[�h�̃T�E���h�t�H�[�}�b�g�̎擾�Ɏ��s���܂���\n" @*/ ) ;
					SoundSysData.PF.DisableWASAPIFlag = TRUE ;
					SoundSysData.EnableSelfMixingFlag = FALSE ;
					goto INITSTART ;
				}

				// �t�H�[�}�b�g��ۑ�
				SoundSysData.PF.SelfMixingFormatEx = *MatchWaveFormat ;
			}

			// ���O�~�L�V���O�t�H�[�}�b�g���ɂ����f
			SoundSysData.SelfMixingFormat = SoundSysData.PF.SelfMixingFormatEx.Format ;
			SoundSysData.SelfMixingFormatValidBitsPerSample = SoundSysData.PF.SelfMixingFormatEx.Samples.wValidBitsPerSample ;
			SoundSysData.SelfMixingFormatIsMSB = FALSE ;
			SoundSysData.SelfMixingFormatIsFloat = _MEMCMP( &SoundSysData.PF.SelfMixingFormatEx.SubFormat, &D_KSDATAFORMAT_SUBTYPE_IEEE_FLOAT, sizeof( GUID ) ) == 0 ? TRUE : FALSE ;

			DXST_LOGFILEFMT_ADDUTF16LE(( "\xc1\x30\xe3\x30\xf3\x30\xcd\x30\xeb\x30\x70\x65\x20\x00\x20\x00\x00\x30\x20\x00\x20\x00\x20\x00\x3a\x00\x20\x00\x25\x00\x64\x00\x20\x00\x63\x00\x68\x00\x00"/*@ L"�`�����l����  �@   : %d ch" @*/, SoundSysData.PF.SelfMixingFormatEx.Format.nChannels )) ;
			DXST_LOGFILEFMT_ADDUTF16LE(( "\xcf\x91\x50\x5b\x16\x53\xd3\x30\xc3\x30\xc8\x30\xf1\x6d\xa6\x5e\x20\x00\x20\x00\x20\x00\x3a\x00\x20\x00\x25\x00\x64\x00\x20\x00\x62\x00\x69\x00\x74\x00\x00"/*@ L"�ʎq���r�b�g�[�x   : %d bit" @*/, SoundSysData.PF.SelfMixingFormatEx.Format.wBitsPerSample )) ;
			DXST_LOGFILEFMT_ADDUTF16LE(( "\x09\x67\xb9\x52\xd3\x30\xc3\x30\xc8\x30\xf1\x6d\xa6\x5e\x00\x30\x20\x00\x20\x00\x20\x00\x3a\x00\x20\x00\x25\x00\x64\x00\x20\x00\x62\x00\x69\x00\x74\x00\x00"/*@ L"�L���r�b�g�[�x�@   : %d bit" @*/, SoundSysData.PF.SelfMixingFormatEx.Samples.wValidBitsPerSample )) ;

			DXST_LOGFILEFMT_ADDUTF16LE(( "\xb5\x30\xf3\x30\xd7\x30\xea\x30\xf3\x30\xb0\x30\xec\x30\xfc\x30\xc8\x30\x20\x00\x3a\x00\x20\x00\x25\x00\x64\x00\x20\x00\x48\x00\x7a\x00\x00"/*@ L"�T���v�����O���[�g : %d Hz" @*/, SoundSysData.PF.SelfMixingFormatEx.Format.nSamplesPerSec )) ;
			if( SoundSysData.SelfMixingFormatIsFloat )
			{
				DXST_LOGFILEFMT_ADDUTF16LE(( "\xc7\x30\xfc\x30\xbf\x30\x62\x5f\x0f\x5f\x20\x00\x20\x00\x20\x00\x20\x00\x20\x00\x20\x00\x20\x00\x20\x00\x20\x00\x3a\x00\x20\x00\x6e\x6d\xd5\x52\x0f\x5c\x70\x65\xb9\x70\x8b\x57\x00"/*@ L"�f�[�^�`��         : ���������_�^" @*/ )) ;
			}
			else
			{
				DXST_LOGFILEFMT_ADDUTF16LE(( "\xc7\x30\xfc\x30\xbf\x30\x62\x5f\x0f\x5f\x20\x00\x20\x00\x20\x00\x20\x00\x20\x00\x20\x00\x20\x00\x20\x00\x20\x00\x3a\x00\x20\x00\x74\x65\x70\x65\x8b\x57\x00"/*@ L"�f�[�^�`��         : �����^" @*/ )) ;
			}

			// �I�[�f�B�I�N���C�A���g�̏��������[�v
			{
				int i = 0, j = 0 ;
				int ErrorLoop = FALSE ;

				for(;;)
				{
					// �G���[���[�v���J�n����Ă�����A�����Ńt�H�[�}�b�g����������
					if( ErrorLoop == TRUE )
					{
						SoundSysData.PF.SelfMixingFormatEx.Format.nSamplesPerSec        = WASAPI_SamplePerSec[ i ] ;
						SoundSysData.PF.SelfMixingFormatEx.Format.wBitsPerSample        = WASAPI_BitPerSample[ j ] ;
						SoundSysData.PF.SelfMixingFormatEx.Format.nBlockAlign           = SoundSysData.PF.SelfMixingFormatEx.Format.nChannels * SoundSysData.PF.SelfMixingFormatEx.Format.wBitsPerSample / 8 ;
						SoundSysData.PF.SelfMixingFormatEx.Format.nAvgBytesPerSec       = SoundSysData.PF.SelfMixingFormatEx.Format.nSamplesPerSec * SoundSysData.PF.SelfMixingFormatEx.Format.nBlockAlign ;
						SoundSysData.PF.SelfMixingFormatEx.Samples.wValidBitsPerSample  = WASAPI_ValidBitPerSample[ j ] ;
						SoundSysData.PF.SelfMixingFormatEx.SubFormat                    = WASAPI_IsFloatType[ j ] ? D_KSDATAFORMAT_SUBTYPE_IEEE_FLOAT : D_KSDATAFORMAT_SUBTYPE_PCM ;

						SoundSysData.SelfMixingFormat = SoundSysData.PF.SelfMixingFormatEx.Format ;
						SoundSysData.SelfMixingFormatValidBitsPerSample = SoundSysData.PF.SelfMixingFormatEx.Samples.wValidBitsPerSample ;
						SoundSysData.SelfMixingFormatIsMSB = FALSE ;
						SoundSysData.SelfMixingFormatIsFloat = WASAPI_IsFloatType[ j ] ;
					}

					// �I�[�f�B�I�N���C�A���g�̏�����
					ret = SoundSysData.PF.AudioClient->Initialize(
						IsExclusive ? D_AUDCLNT_SHAREMODE_EXCLUSIVE : D_AUDCLNT_SHAREMODE_SHARED,
						IsExclusive ? D_AUDCLNT_STREAMFLAGS_EVENTCALLBACK : 0, 
//						0,
						SoundSysData.PF.WASAPI_DevicePeriod,
						IsExclusive ? SoundSysData.PF.WASAPI_DevicePeriod : 0,
//						0,
						( WAVEFORMATEX * )&SoundSysData.PF.SelfMixingFormatEx,
						NULL
					) ;
//					HRESULT hrlist[] = { AUDCLNT_E_NOT_INITIALIZED, AUDCLNT_E_ALREADY_INITIALIZED, AUDCLNT_E_WRONG_ENDPOINT_TYPE, AUDCLNT_E_DEVICE_INVALIDATED, AUDCLNT_E_NOT_STOPPED, AUDCLNT_E_BUFFER_TOO_LARGE, AUDCLNT_E_OUT_OF_ORDER, AUDCLNT_E_UNSUPPORTED_FORMAT, AUDCLNT_E_INVALID_SIZE, AUDCLNT_E_DEVICE_IN_USE, AUDCLNT_E_BUFFER_OPERATION_PENDING, AUDCLNT_E_THREAD_NOT_REGISTERED, AUDCLNT_E_EXCLUSIVE_MODE_NOT_ALLOWED, AUDCLNT_E_ENDPOINT_CREATE_FAILED, AUDCLNT_E_SERVICE_NOT_RUNNING, AUDCLNT_E_EVENTHANDLE_NOT_EXPECTED, AUDCLNT_E_EXCLUSIVE_MODE_ONLY, AUDCLNT_E_BUFDURATION_PERIOD_NOT_EQUAL, AUDCLNT_E_EVENTHANDLE_NOT_SET, AUDCLNT_E_INCORRECT_BUFFER_SIZE, AUDCLNT_E_BUFFER_SIZE_ERROR, AUDCLNT_E_CPUUSAGE_EXCEEDED, AUDCLNT_E_BUFFER_ERROR, AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED, AUDCLNT_E_INVALID_DEVICE_PERIOD, AUDCLNT_E_INVALID_STREAM_FLAG, AUDCLNT_E_ENDPOINT_OFFLOAD_NOT_CAPABLE, AUDCLNT_E_OUT_OF_OFFLOAD_RESOURCES, AUDCLNT_E_OFFLOAD_MODE_ONLY, AUDCLNT_E_NONOFFLOAD_MODE_ONLY, AUDCLNT_E_RESOURCES_INVALIDATED, AUDCLNT_E_RAW_MODE_UNSUPPORTED, AUDCLNT_E_ENGINE_PERIODICITY_LOCKED, AUDCLNT_E_ENGINE_FORMAT_LOCKED, AUDCLNT_E_HEADTRACKING_ENABLED, AUDCLNT_E_HEADTRACKING_UNSUPPORTED, };
					if( FAILED( ret ) )
					{
						// �o�b�t�@�T�C�Y�A���C�����g�G���[�����������ꍇ�̓f�o�C�X�s���I�h���C������
						if( ret == D_AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED )
						{
							DXST_LOGFILEFMT_ADDUTF16LE(( "\xaa\x30\xfc\x30\xc7\x30\xa3\x30\xaa\x30\xaf\x30\xe9\x30\xa4\x30\xa2\x30\xf3\x30\xc8\x30\x6e\x30\x1d\x52\x1f\x67\x16\x53\x4c\x30\x20\x00\x41\x00\x55\x00\x44\x00\x43\x00\x4c\x00\x4e\x00\x54\x00\x5f\x00\x45\x00\x5f\x00\x42\x00\x55\x00\x46\x00\x46\x00\x45\x00\x52\x00\x5f\x00\x53\x00\x49\x00\x5a\x00\x45\x00\x5f\x00\x4e\x00\x4f\x00\x54\x00\x5f\x00\x41\x00\x4c\x00\x49\x00\x47\x00\x4e\x00\x45\x00\x44\x00\x20\x00\xa8\x30\xe9\x30\xfc\x30\x67\x30\x31\x59\x57\x65\x57\x30\x7e\x30\x57\x30\x5f\x30\x01\x30\x45\x90\xf6\x5e\x42\x66\x93\x95\x92\x30\xbf\x8a\x74\x65\x57\x30\x7e\x30\x59\x30\x00"/*@ L"�I�[�f�B�I�N���C�A���g�̏������� AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED �G���[�Ŏ��s���܂����A�x�����Ԃ𒲐����܂�" @*/ )) ;

							// �o�b�t�@�̃T���v��������f�o�C�X�s���I�h���Z�o
							{
								LONGLONG MulValue1, MulValue2, DivValue, DivResult ;
								DWORD MulResult[ 4 ] ;

								ret = SoundSysData.PF.AudioClient->GetBufferSize( &SoundSysData.PF.WASAPI_BufferSamples ) ;

								DXST_LOGFILEFMT_ADDUTF16LE(( "\xaa\x30\xfc\x30\xc7\x30\xa3\x30\xaa\x30\xaf\x30\xe9\x30\xa4\x30\xa2\x30\xf3\x30\xc8\x30\x6e\x30\xd0\x30\xc3\x30\xd5\x30\xa1\x30\xb5\x30\xf3\x30\xd7\x30\xeb\x30\x70\x65\x20\x00\x3a\x00\x20\x00\x25\x00\x64\x00\x20\x00\x73\x00\x61\x00\x6d\x00\x70\x00\x6c\x00\x65\x00\x00"/*@ L"�I�[�f�B�I�N���C�A���g�̃o�b�t�@�T���v���� : %d sample" @*/, SoundSysData.PF.WASAPI_BufferSamples )) ;

								MulValue1 = SoundSysData.PF.WASAPI_BufferSamples ;
								MulValue2 = 10000000 ;
								DivValue = SoundSysData.PF.SelfMixingFormatEx.Format.nSamplesPerSec ;
								_MUL128_1( ( DWORD * )&MulValue1, ( DWORD * )&MulValue2, MulResult ) ;
								_DIV128_1( MulResult, ( DWORD * )&DivValue, ( DWORD * )&DivResult ) ;

								SoundSysData.PF.WASAPI_DevicePeriod = DivResult ;

//								SoundSysData.PF.WASAPI_DevicePeriod = ( LONGLONG )_DTOL( 10000000.0 * SoundSysData.PF.WASAPI_BufferSamples / SoundSysData.PF.SelfMixingFormatEx.Format.nSamplesPerSec + 0.5 ) ;

								DXST_LOGFILEFMT_ADDUTF16LE(( "\xbf\x8a\x74\x65\x8c\x5f\x6e\x30\x45\x90\xf6\x5e\x42\x66\x93\x95\x20\x00\x20\x00\x20\x00\x3a\x00\x20\x00\x25\x00\x2e\x00\x31\x00\x66\x00\x20\x00\xbc\x03\x73\x00\x20\x00\x28\x00\x20\x00\x25\x00\x2e\x00\x33\x00\x66\x00\x20\x00\x6d\x00\x73\x00\x20\x00\x29\x00\x00"/*@ L"������̒x������   : %.1f ��s ( %.3f ms )" @*/, ( float )SoundSysData.PF.WASAPI_DevicePeriod / 10.0f, ( float )SoundSysData.PF.WASAPI_DevicePeriod / 10000.0f )) ;
							}

							// �I�[�f�B�I�N���C�A���g���č쐬
							SoundSysData.PF.AudioClient->Release() ;
							SoundSysData.PF.AudioClient = NULL ;
							ret = SoundSysData.PF.IMMDevice->Activate( IID_IAUDIOCLIENT, CLSCTX_ALL, NULL, ( void** )&SoundSysData.PF.AudioClient );
							if( FAILED( ret ) )
							{
								SoundSysData.PF.IMMDevice->Release() ;
								SoundSysData.PF.IMMDevice = NULL ;

								SoundSysData.PF.IMMDeviceEnumerator->Release() ;
								SoundSysData.PF.IMMDeviceEnumerator = NULL ;

								if( MatchWaveFormat != NULL )
								{
									WinAPIData.Win32Func.CoTaskMemFreeFunc( MatchWaveFormat ) ;
									MatchWaveFormat = NULL ;
								}

								DXST_LOGFILE_TABSUB ;
								DXST_LOGFILE_ADDUTF16LE( "\xaa\x30\xfc\x30\xc7\x30\xa3\x30\xaa\x30\xaf\x30\xe9\x30\xa4\x30\xa2\x30\xf3\x30\xc8\x30\x6e\x30\x8d\x51\xd6\x53\x97\x5f\x6b\x30\x31\x59\x57\x65\x57\x30\x7e\x30\x57\x30\x5f\x30\x0a\x00\x00"/*@ L"�I�[�f�B�I�N���C�A���g�̍Ď擾�Ɏ��s���܂���\n" @*/ ) ;
								if( IsExclusive )
								{
									DXST_LOGFILE_ADDUTF16LE( "\x92\x63\xd6\x4e\xe2\x30\xfc\x30\xc9\x30\x67\x30\x6e\x30\x1d\x52\x1f\x67\x16\x53\x6b\x30\x31\x59\x57\x65\x57\x30\x5f\x30\x6e\x30\x67\x30\x71\x51\x09\x67\xe2\x30\xfc\x30\xc9\x30\x67\x30\x6e\x30\x1d\x52\x1f\x67\x16\x53\x92\x30\x66\x8a\x4c\x88\x57\x30\x7e\x30\x59\x30\x0a\x00\x00"/*@ L"�r�����[�h�ł̏������Ɏ��s�����̂ŋ��L���[�h�ł̏����������s���܂�\n" @*/ ) ;
									SoundSysData.PF.WASAPI_IsExclusiveFlag = FALSE ;
								}
								else
								{
									SoundSysData.PF.DisableWASAPIFlag = TRUE ;
									SoundSysData.EnableSelfMixingFlag = FALSE ;
								}
								goto INITSTART ;
							}

							// �ēx������������
							ret = SoundSysData.PF.AudioClient->Initialize(
								IsExclusive ? D_AUDCLNT_SHAREMODE_EXCLUSIVE : D_AUDCLNT_SHAREMODE_SHARED,
								IsExclusive ? D_AUDCLNT_STREAMFLAGS_EVENTCALLBACK : 0,
//								0,
								SoundSysData.PF.WASAPI_DevicePeriod,
								IsExclusive ? SoundSysData.PF.WASAPI_DevicePeriod : 0,
//								0,
								( WAVEFORMATEX * )&SoundSysData.PF.SelfMixingFormatEx,
								NULL
							) ;
						}
						else
						// �r�����[�h�������ꍇ�͑��̃t�H�[�}�b�g������
						if( IsExclusive )
						{
							int IsLoop = TRUE ;

							// �G���[���[�v���n�܂��Ă��邩�ǂ����ŏ����𕪊�
							if( ErrorLoop == FALSE )
							{
								// ����̓t���O�𗧂ĂĖ߂邾��
								ErrorLoop = TRUE ;

								// �t�H�[�}�b�g��ύX�����t���O�𗧂Ă�
								ChangeFormat = TRUE ;
							}
							else
							{
								// �Q��ڈȍ~�̓��[�v����
								j++ ;
								if( WASAPI_BitPerSample[ j ] == 0 )
								{
									j = 0 ;
									i++ ;
									if( WASAPI_SamplePerSec[ i ] == 0 )
									{
										// �S�Ď����Ă��ʖڂ�������I��
										IsLoop = FALSE ;
									}
								}
							}

							// ���[�v����ꍇ�̓I�[�f�B�I�N���C�A���g���Ď擾
							if( IsLoop == TRUE )
							{
								SoundSysData.PF.AudioClient->Release() ;
								SoundSysData.PF.AudioClient = NULL ;
								ret = SoundSysData.PF.IMMDevice->Activate( IID_IAUDIOCLIENT, CLSCTX_ALL, NULL, ( void** )&SoundSysData.PF.AudioClient );
								if( FAILED( ret ) )
								{
									SoundSysData.PF.IMMDevice->Release() ;
									SoundSysData.PF.IMMDevice = NULL ;

									SoundSysData.PF.IMMDeviceEnumerator->Release() ;
									SoundSysData.PF.IMMDeviceEnumerator = NULL ;

									if( MatchWaveFormat != NULL )
									{
										WinAPIData.Win32Func.CoTaskMemFreeFunc( MatchWaveFormat ) ;
										MatchWaveFormat = NULL ;
									}

									DXST_LOGFILE_TABSUB ;
									DXST_LOGFILE_ADDUTF16LE( "\xaa\x30\xfc\x30\xc7\x30\xa3\x30\xaa\x30\xaf\x30\xe9\x30\xa4\x30\xa2\x30\xf3\x30\xc8\x30\x6e\x30\x8d\x51\xd6\x53\x97\x5f\x6b\x30\x31\x59\x57\x65\x57\x30\x7e\x30\x57\x30\x5f\x30\x0a\x00\x00"/*@ L"�I�[�f�B�I�N���C�A���g�̍Ď擾�Ɏ��s���܂���\n" @*/ ) ;
									if( IsExclusive )
									{
										DXST_LOGFILE_ADDUTF16LE( "\x92\x63\xd6\x4e\xe2\x30\xfc\x30\xc9\x30\x67\x30\x6e\x30\x1d\x52\x1f\x67\x16\x53\x6b\x30\x31\x59\x57\x65\x57\x30\x5f\x30\x6e\x30\x67\x30\x71\x51\x09\x67\xe2\x30\xfc\x30\xc9\x30\x67\x30\x6e\x30\x1d\x52\x1f\x67\x16\x53\x92\x30\x66\x8a\x4c\x88\x57\x30\x7e\x30\x59\x30\x0a\x00\x00"/*@ L"�r�����[�h�ł̏������Ɏ��s�����̂ŋ��L���[�h�ł̏����������s���܂�\n" @*/ ) ;
										SoundSysData.PF.WASAPI_IsExclusiveFlag = FALSE ;
									}
									else
									{
										SoundSysData.PF.DisableWASAPIFlag = TRUE ;
										SoundSysData.EnableSelfMixingFlag = FALSE ;
									}
									goto INITSTART ;
								}
								continue ;
							}
						}

						if( FAILED( ret ) )
						{
							SoundSysData.PF.AudioClient->Release() ;
							SoundSysData.PF.AudioClient = NULL ;

							SoundSysData.PF.IMMDevice->Release() ;
							SoundSysData.PF.IMMDevice = NULL ;

							SoundSysData.PF.IMMDeviceEnumerator->Release() ;
							SoundSysData.PF.IMMDeviceEnumerator = NULL ;

							if( MatchWaveFormat != NULL )
							{
								WinAPIData.Win32Func.CoTaskMemFreeFunc( MatchWaveFormat ) ;
								MatchWaveFormat = NULL ;
							}

							DXST_LOGFILE_TABSUB ;
							DXST_LOGFILEFMT_ADDUTF16LE(( "\xaa\x30\xfc\x30\xc7\x30\xa3\x30\xaa\x30\xaf\x30\xe9\x30\xa4\x30\xa2\x30\xf3\x30\xc8\x30\x6e\x30\x1d\x52\x1f\x67\x16\x53\x6b\x30\x31\x59\x57\x65\x57\x30\x7e\x30\x57\x30\x5f\x30\x00\x30\xa8\x30\xe9\x30\xfc\x30\xb3\x30\xfc\x30\xc9\x30\x1a\xff\x25\x00\x30\x00\x38\x00\x78\x00\x00"/*@ L"�I�[�f�B�I�N���C�A���g�̏������Ɏ��s���܂����@�G���[�R�[�h�F%08x" @*/, ret )) ;
							if( IsExclusive )
							{
								DXST_LOGFILE_ADDUTF16LE( "\x92\x63\xd6\x4e\xe2\x30\xfc\x30\xc9\x30\x67\x30\x6e\x30\x1d\x52\x1f\x67\x16\x53\x6b\x30\x31\x59\x57\x65\x57\x30\x5f\x30\x6e\x30\x67\x30\x71\x51\x09\x67\xe2\x30\xfc\x30\xc9\x30\x67\x30\x6e\x30\x1d\x52\x1f\x67\x16\x53\x92\x30\x66\x8a\x4c\x88\x57\x30\x7e\x30\x59\x30\x0a\x00\x00"/*@ L"�r�����[�h�ł̏������Ɏ��s�����̂ŋ��L���[�h�ł̏����������s���܂�\n" @*/ ) ;
								SoundSysData.PF.WASAPI_IsExclusiveFlag = FALSE ;
							}
							else
							{
								SoundSysData.PF.DisableWASAPIFlag = TRUE ;
								SoundSysData.EnableSelfMixingFlag = FALSE ;
							}
							goto INITSTART ;
						}
					}

					break ;
				}
			}

			// �I�[�f�B�I�N���b�N�̎擾
			ret = SoundSysData.PF.AudioClient->GetService( IID_IAUDIOCLOCK, ( void** )&SoundSysData.PF.AudioClock ) ;
			if( FAILED( ret ) )
			{
				SoundSysData.PF.AudioClient->Release() ;
				SoundSysData.PF.AudioClient = NULL ;

				SoundSysData.PF.IMMDevice->Release() ;
				SoundSysData.PF.IMMDevice = NULL ;

				SoundSysData.PF.IMMDeviceEnumerator->Release() ;
				SoundSysData.PF.IMMDeviceEnumerator = NULL ;

				DXST_LOGFILE_TABSUB ;
				DXST_LOGFILE_ADDUTF16LE( "\xaa\x30\xfc\x30\xc7\x30\xa3\x30\xaa\x30\xaf\x30\xed\x30\xc3\x30\xaf\x30\x6e\x30\xd6\x53\x97\x5f\x6b\x30\x31\x59\x57\x65\x57\x30\x7e\x30\x57\x30\x5f\x30\x0a\x00\x00"/*@ L"�I�[�f�B�I�N���b�N�̎擾�Ɏ��s���܂���\n" @*/ ) ;
				SoundSysData.PF.DisableWASAPIFlag = TRUE ;
				SoundSysData.EnableSelfMixingFlag = FALSE ;
				goto INITSTART ;
			}

			// �t�H�[�}�b�g�ɕύX���������ꍇ�͍ďo��
			if( ChangeFormat )
			{
				DXST_LOGFILEFMT_ADDUTF16LE(( "\xd5\x30\xa9\x30\xfc\x30\xde\x30\xc3\x30\xc8\x30\x6b\x30\x09\x59\xf4\x66\x4c\x30\x42\x30\x63\x30\x5f\x30\x5f\x30\x81\x30\xd5\x30\xa9\x30\xfc\x30\xde\x30\xc3\x30\xc8\x30\xc5\x60\x31\x58\x92\x30\x8d\x51\xfa\x51\x9b\x52\x00"/*@ L"�t�H�[�}�b�g�ɕύX�����������߃t�H�[�}�b�g�����ďo��" @*/ )) ;

				DXST_LOGFILEFMT_ADDUTF16LE(( "\xc1\x30\xe3\x30\xf3\x30\xcd\x30\xeb\x30\x70\x65\x20\x00\x20\x00\x00\x30\x20\x00\x20\x00\x20\x00\x3a\x00\x20\x00\x25\x00\x64\x00\x20\x00\x63\x00\x68\x00\x00"/*@ L"�`�����l����  �@   : %d ch" @*/, SoundSysData.PF.SelfMixingFormatEx.Format.nChannels )) ;
				DXST_LOGFILEFMT_ADDUTF16LE(( "\xcf\x91\x50\x5b\x16\x53\xd3\x30\xc3\x30\xc8\x30\xf1\x6d\xa6\x5e\x20\x00\x20\x00\x20\x00\x3a\x00\x20\x00\x25\x00\x64\x00\x20\x00\x62\x00\x69\x00\x74\x00\x00"/*@ L"�ʎq���r�b�g�[�x   : %d bit" @*/, SoundSysData.PF.SelfMixingFormatEx.Format.wBitsPerSample )) ;
				DXST_LOGFILEFMT_ADDUTF16LE(( "\x09\x67\xb9\x52\xd3\x30\xc3\x30\xc8\x30\xf1\x6d\xa6\x5e\x00\x30\x20\x00\x20\x00\x20\x00\x3a\x00\x20\x00\x25\x00\x64\x00\x20\x00\x62\x00\x69\x00\x74\x00\x00"/*@ L"�L���r�b�g�[�x�@   : %d bit" @*/, SoundSysData.PF.SelfMixingFormatEx.Samples.wValidBitsPerSample )) ;

				DXST_LOGFILEFMT_ADDUTF16LE(( "\xb5\x30\xf3\x30\xd7\x30\xea\x30\xf3\x30\xb0\x30\xec\x30\xfc\x30\xc8\x30\x20\x00\x3a\x00\x20\x00\x25\x00\x64\x00\x20\x00\x48\x00\x7a\x00\x00"/*@ L"�T���v�����O���[�g : %d Hz" @*/, SoundSysData.PF.SelfMixingFormatEx.Format.nSamplesPerSec )) ;
				if( SoundSysData.SelfMixingFormatIsFloat )
				{
					DXST_LOGFILEFMT_ADDUTF16LE(( "\xc7\x30\xfc\x30\xbf\x30\x62\x5f\x0f\x5f\x20\x00\x20\x00\x20\x00\x20\x00\x20\x00\x20\x00\x20\x00\x20\x00\x20\x00\x3a\x00\x20\x00\x6e\x6d\xd5\x52\x0f\x5c\x70\x65\xb9\x70\x8b\x57\x00"/*@ L"�f�[�^�`��         : ���������_�^" @*/ )) ;
				}
				else
				{
					DXST_LOGFILEFMT_ADDUTF16LE(( "\xc7\x30\xfc\x30\xbf\x30\x62\x5f\x0f\x5f\x20\x00\x20\x00\x20\x00\x20\x00\x20\x00\x20\x00\x20\x00\x20\x00\x20\x00\x3a\x00\x20\x00\x74\x65\x70\x65\x8b\x57\x00"/*@ L"�f�[�^�`��         : �����^" @*/ )) ;
				}
			}

			// �r�����[�h�̏ꍇ�̂݃C�x���g���g�p����
			if( IsExclusive )
			{
				// �C�x���g����
				SoundSysData.PF.WASAPI_Event = WinAPIData.Win32Func.CreateEventAFunc( NULL, FALSE, FALSE, NULL ) ;
				if( SoundSysData.PF.WASAPI_Event == NULL )
				{
					SoundSysData.PF.AudioClient->Release() ;
					SoundSysData.PF.AudioClient = NULL ;

					SoundSysData.PF.IMMDevice->Release() ;
					SoundSysData.PF.IMMDevice = NULL ;

					SoundSysData.PF.IMMDeviceEnumerator->Release() ;
					SoundSysData.PF.IMMDeviceEnumerator = NULL ;

					if( MatchWaveFormat != NULL )
					{
						WinAPIData.Win32Func.CoTaskMemFreeFunc( MatchWaveFormat ) ;
						MatchWaveFormat = NULL ;
					}

					DXST_LOGFILE_TABSUB ;
					DXST_LOGFILE_ADDUTF16LE( "\xa4\x30\xd9\x30\xf3\x30\xc8\x30\xaa\x30\xd6\x30\xb8\x30\xa7\x30\xaf\x30\xc8\x30\x6e\x30\x5c\x4f\x10\x62\x6b\x30\x31\x59\x57\x65\x57\x30\x7e\x30\x57\x30\x5f\x30\x0a\x00\x00"/*@ L"�C�x���g�I�u�W�F�N�g�̍쐬�Ɏ��s���܂���\n" @*/ ) ;
					if( IsExclusive )
					{
						DXST_LOGFILE_ADDUTF16LE( "\x92\x63\xd6\x4e\xe2\x30\xfc\x30\xc9\x30\x67\x30\x6e\x30\x1d\x52\x1f\x67\x16\x53\x6b\x30\x31\x59\x57\x65\x57\x30\x5f\x30\x6e\x30\x67\x30\x71\x51\x09\x67\xe2\x30\xfc\x30\xc9\x30\x67\x30\x6e\x30\x1d\x52\x1f\x67\x16\x53\x92\x30\x66\x8a\x4c\x88\x57\x30\x7e\x30\x59\x30\x0a\x00\x00"/*@ L"�r�����[�h�ł̏������Ɏ��s�����̂ŋ��L���[�h�ł̏����������s���܂�\n" @*/ ) ;
						SoundSysData.PF.WASAPI_IsExclusiveFlag = FALSE ;
					}
					else
					{
						SoundSysData.PF.DisableWASAPIFlag = TRUE ;
						SoundSysData.EnableSelfMixingFlag = FALSE ;
					}
					goto INITSTART ;
				}

				// �C�x���g�̃Z�b�g
				ret = SoundSysData.PF.AudioClient->SetEventHandle( SoundSysData.PF.WASAPI_Event ) ;
				if( FAILED( ret ) )
				{
					WinAPIData.Win32Func.CloseHandleFunc( SoundSysData.PF.WASAPI_Event ) ;
					SoundSysData.PF.WASAPI_Event = NULL ;

					SoundSysData.PF.AudioClient->Release() ;
					SoundSysData.PF.AudioClient = NULL ;

					SoundSysData.PF.IMMDevice->Release() ;
					SoundSysData.PF.IMMDevice = NULL ;

					SoundSysData.PF.IMMDeviceEnumerator->Release() ;
					SoundSysData.PF.IMMDeviceEnumerator = NULL ;

					if( MatchWaveFormat != NULL )
					{
						WinAPIData.Win32Func.CoTaskMemFreeFunc( MatchWaveFormat ) ;
						MatchWaveFormat = NULL ;
					}

					DXST_LOGFILE_TABSUB ;
					DXST_LOGFILE_ADDUTF16LE( "\xa4\x30\xd9\x30\xf3\x30\xc8\x30\xaa\x30\xd6\x30\xb8\x30\xa7\x30\xaf\x30\xc8\x30\x6e\x30\x2d\x8a\x9a\x5b\x6b\x30\x31\x59\x57\x65\x57\x30\x7e\x30\x57\x30\x5f\x30\x0a\x00\x00"/*@ L"�C�x���g�I�u�W�F�N�g�̐ݒ�Ɏ��s���܂���\n" @*/ ) ;
					if( IsExclusive )
					{
						DXST_LOGFILE_ADDUTF16LE( "\x92\x63\xd6\x4e\xe2\x30\xfc\x30\xc9\x30\x67\x30\x6e\x30\x1d\x52\x1f\x67\x16\x53\x6b\x30\x31\x59\x57\x65\x57\x30\x5f\x30\x6e\x30\x67\x30\x71\x51\x09\x67\xe2\x30\xfc\x30\xc9\x30\x67\x30\x6e\x30\x1d\x52\x1f\x67\x16\x53\x92\x30\x66\x8a\x4c\x88\x57\x30\x7e\x30\x59\x30\x0a\x00\x00"/*@ L"�r�����[�h�ł̏������Ɏ��s�����̂ŋ��L���[�h�ł̏����������s���܂�\n" @*/ ) ;
						SoundSysData.PF.WASAPI_IsExclusiveFlag = FALSE ;
					}
					else
					{
						SoundSysData.PF.DisableWASAPIFlag = TRUE ;
						SoundSysData.EnableSelfMixingFlag = FALSE ;
					}
					goto INITSTART ;
				}
			}

			// �I�[�f�B�I�����_���[�̎擾
			ret = SoundSysData.PF.AudioClient->GetService( IID_IAUDIORENDERCLIENT,( void ** )&SoundSysData.PF.AudioRenderClient ) ;
			if( FAILED( ret ) )
			{
				WinAPIData.Win32Func.CloseHandleFunc( SoundSysData.PF.WASAPI_Event ) ;
				SoundSysData.PF.WASAPI_Event = NULL ;

				SoundSysData.PF.AudioClient->Release() ;
				SoundSysData.PF.AudioClient = NULL ;

				SoundSysData.PF.IMMDevice->Release() ;
				SoundSysData.PF.IMMDevice = NULL ;

				SoundSysData.PF.IMMDeviceEnumerator->Release() ;
				SoundSysData.PF.IMMDeviceEnumerator = NULL ;

				if( MatchWaveFormat != NULL )
				{
					WinAPIData.Win32Func.CoTaskMemFreeFunc( MatchWaveFormat ) ;
					MatchWaveFormat = NULL ;
				}

				DXST_LOGFILE_TABSUB ;
				DXST_LOGFILE_ADDUTF16LE( "\xaa\x30\xfc\x30\xc7\x30\xa3\x30\xaa\x30\xec\x30\xf3\x30\xc0\x30\xe9\x30\xfc\x30\x6e\x30\xd6\x53\x97\x5f\x6b\x30\x31\x59\x57\x65\x57\x30\x7e\x30\x57\x30\x5f\x30\x0a\x00\x00"/*@ L"�I�[�f�B�I�����_���[�̎擾�Ɏ��s���܂���\n" @*/ ) ;
				if( IsExclusive )
				{
					DXST_LOGFILE_ADDUTF16LE( "\x92\x63\xd6\x4e\xe2\x30\xfc\x30\xc9\x30\x67\x30\x6e\x30\x1d\x52\x1f\x67\x16\x53\x6b\x30\x31\x59\x57\x65\x57\x30\x5f\x30\x6e\x30\x67\x30\x71\x51\x09\x67\xe2\x30\xfc\x30\xc9\x30\x67\x30\x6e\x30\x1d\x52\x1f\x67\x16\x53\x92\x30\x66\x8a\x4c\x88\x57\x30\x7e\x30\x59\x30\x0a\x00\x00"/*@ L"�r�����[�h�ł̏������Ɏ��s�����̂ŋ��L���[�h�ł̏����������s���܂�\n" @*/ ) ;
					SoundSysData.PF.WASAPI_IsExclusiveFlag = FALSE ;
				}
				else
				{
					SoundSysData.PF.DisableWASAPIFlag = TRUE ;
					SoundSysData.EnableSelfMixingFlag = FALSE ;
				}
				goto INITSTART ;
			}

			// �o�b�t�@���[���N���A
			{
				LPBYTE pData ;

				SoundSysData.PF.AudioClient->GetBufferSize( &SoundSysData.PF.WASAPI_BufferSamples ) ;
				ret = SoundSysData.PF.AudioRenderClient->GetBuffer( SoundSysData.PF.WASAPI_BufferSamples, &pData ) ;
				if( SUCCEEDED( ret ) )
				{
					_MEMSET( pData, 0, SoundSysData.PF.WASAPI_BufferSamples * SoundSysData.PF.SelfMixingFormatEx.Format.nBlockAlign ) ;
					SoundSysData.PF.AudioRenderClient->ReleaseBuffer( SoundSysData.PF.WASAPI_BufferSamples, 0 ) ;
				}
			}

			// ��Ɨp�o�b�t�@�̏���
			if( SetupSelfMixingWorkBuffer( SoundSysData.SelfMixingFormatIsFloat, SoundSysData.PF.WASAPI_BufferSamples ) < 0 )
			{
				SoundSysData.PF.AudioRenderClient->Release() ;
				SoundSysData.PF.AudioRenderClient = NULL ;

				WinAPIData.Win32Func.CloseHandleFunc( SoundSysData.PF.WASAPI_Event ) ;
				SoundSysData.PF.WASAPI_Event = NULL ;

				SoundSysData.PF.AudioClient->Release() ;
				SoundSysData.PF.AudioClient = NULL ;

				SoundSysData.PF.IMMDevice->Release() ;
				SoundSysData.PF.IMMDevice = NULL ;

				SoundSysData.PF.IMMDeviceEnumerator->Release() ;
				SoundSysData.PF.IMMDeviceEnumerator = NULL ;

				if( MatchWaveFormat != NULL )
				{
					WinAPIData.Win32Func.CoTaskMemFreeFunc( MatchWaveFormat ) ;
					MatchWaveFormat = NULL ;
				}

				DXST_LOGFILE_TABSUB ;
				DXST_LOGFILE_ADDUTF16LE( "\xea\x81\x4d\x52\xdf\x30\xad\x30\xb7\x30\xf3\x30\xb0\x30\x5c\x4f\x6d\x69\x28\x75\xd0\x30\xc3\x30\xd5\x30\xa1\x30\x6e\x30\xba\x78\xdd\x4f\x6b\x30\x31\x59\x57\x65\x57\x30\x7e\x30\x57\x30\x5f\x30\x0a\x00\x00"/*@ L"���O�~�L�V���O��Ɨp�o�b�t�@�̊m�ۂɎ��s���܂���\n" @*/ ) ;
				SoundSysData.PF.DisableWASAPIFlag = TRUE ;
				SoundSysData.EnableSelfMixingFlag = FALSE ;
				goto INITSTART ;
			}

			// �X���b�h���[�v�t���O�𗧂Ă�
			SoundSysData.PF.WASAPI_ThreadLoop = TRUE;

			// �Đ������p�X���b�h�N��
			{
				DWORD dwThread ;
				SoundSysData.PF.WASAPI_ThreadHandle = WinAPIData.Win32Func.CreateThreadFunc( NULL, 0, WASAPI_SoundPlayThread, NULL, 0, &dwThread ) ;
				if( SoundSysData.PF.WASAPI_ThreadHandle == NULL )
				{
					SoundSysData.PF.AudioRenderClient->Release() ;
					SoundSysData.PF.AudioRenderClient = NULL ;

					WinAPIData.Win32Func.CloseHandleFunc( SoundSysData.PF.WASAPI_Event ) ;
					SoundSysData.PF.WASAPI_Event = NULL ;

					SoundSysData.PF.AudioClient->Release() ;
					SoundSysData.PF.AudioClient = NULL ;

					SoundSysData.PF.IMMDevice->Release() ;
					SoundSysData.PF.IMMDevice = NULL ;

					SoundSysData.PF.IMMDeviceEnumerator->Release() ;
					SoundSysData.PF.IMMDeviceEnumerator = NULL ;

					if( MatchWaveFormat != NULL )
					{
						WinAPIData.Win32Func.CoTaskMemFreeFunc( MatchWaveFormat ) ;
						MatchWaveFormat = NULL ;
					}

					DXST_LOGFILE_TABSUB ;
					DXST_LOGFILE_ADDUTF16LE( "\xaa\x30\xfc\x30\xc7\x30\xa3\x30\xaa\x30\x8d\x51\x1f\x75\x28\x75\xb9\x30\xec\x30\xc3\x30\xc9\x30\x6e\x30\x5c\x4f\x10\x62\x6b\x30\x31\x59\x57\x65\x57\x30\x7e\x30\x57\x30\x5f\x30\x0a\x00\x00"/*@ L"�I�[�f�B�I�Đ��p�X���b�h�̍쐬�Ɏ��s���܂���\n" @*/ ) ;
					SoundSysData.PF.DisableWASAPIFlag = TRUE ;
					SoundSysData.EnableSelfMixingFlag = FALSE ;
					goto INITSTART ;
				}
				WinAPIData.Win32Func.SetThreadPriorityFunc( SoundSysData.PF.WASAPI_ThreadHandle, THREAD_PRIORITY_TIME_CRITICAL ) ;
			}

			// �������̊J��
			if( MatchWaveFormat != NULL )
			{
				WinAPIData.Win32Func.CoTaskMemFreeFunc( MatchWaveFormat ) ;
				MatchWaveFormat = NULL ;
			}

			// �������̏؋����c��
			SoundSysData.PF.InitializeFlag = TRUE ;

			// �Đ��J�n
			SoundSysData.PF.AudioClient->Start() ;
		}
		else
		if( SoundSysData.PF.EnableXAudioFlag )
		{
			int i ;
			const wchar_t *X3DAudioDLLName[] =
			{
				L"X3DAudio1_7.dll",
				//L"X3DAudio1_6.dll",
				//L"X3DAudio1_5.dll",
				//L"X3DAudio1_4.dll",
				//L"X3DAudio1_3.dll",
				//L"X3DAudio1_2.dll",
				//L"X3DAudio1_1.dll",
				//L"X3DAudio1_0.dll",
				NULL,
			} ;
			GUID *XAudioGuidList[][ 2 ] = 
			{
				{
					&CLSID_XAUDIO2_7,
					&CLSID_XAUDIO2_7_DEBUG,
				},
				//{
				//	&CLSID_XAUDIO2_6,
				//	&CLSID_XAUDIO2_6_DEBUG,
				//},
				//{
				//	&CLSID_XAUDIO2_5,
				//	&CLSID_XAUDIO2_5_DEBUG,
				//},
				//{
				//	&CLSID_XAUDIO2_4,
				//	&CLSID_XAUDIO2_4_DEBUG,
				//},
				//{
				//	&CLSID_XAUDIO2_3,
				//	&CLSID_XAUDIO2_3_DEBUG,
				//},
				//{
				//	&CLSID_XAUDIO2_2,
				//	&CLSID_XAUDIO2_2_DEBUG,
				//},
				//{
				//	&CLSID_XAUDIO2_1,
				//	&CLSID_XAUDIO2_1_DEBUG,
				//},
				//{
				//	&CLSID_XAUDIO2_0,
				//	&CLSID_XAUDIO2_0_DEBUG,
				//},
				{
					NULL,
					NULL
				},
			} ;
			DXST_LOGFILE_ADDUTF16LE( "\x58\x00\x41\x00\x75\x00\x64\x00\x69\x00\x6f\x00\x32\x00\x20\x00\x6e\x30\x1d\x52\x1f\x67\x16\x53\x92\x30\x4c\x88\x44\x30\x7e\x30\x59\x30\x0a\x00\x00"/*@ L"XAudio2 �̏��������s���܂�\n" @*/ ) ;

			DXST_LOGFILE_TABADD ;

			// �ŏ���XAudio2_8.dll�����������ׂ�
			SoundSysData.PF.XAudio2_8DLL = LoadLibraryW( L"XAudio2_8.dll" ) ;
			if( SoundSysData.PF.XAudio2_8DLL != NULL )
			{
				DXST_LOGFILE_ADDUTF16LE( "\x58\x00\x41\x00\x75\x00\x64\x00\x69\x00\x6f\x00\x32\x00\x5f\x00\x38\x00\x2e\x00\x64\x00\x6c\x00\x6c\x00\x20\x00\x92\x30\x7f\x4f\x28\x75\x57\x30\x7e\x30\x59\x30\x0a\x00\x00"/*@ L"XAudio2_8.dll ���g�p���܂�\n" @*/ ) ;

				// �������ꍇ�̏���
				SoundSysData.PF.XAudio2CreateFunc          = ( HRESULT ( WINAPI * )( D_IXAudio2_8 **, DWORD, D_XAUDIO2_PROCESSOR ) )GetProcAddress( SoundSysData.PF.XAudio2_8DLL, "XAudio2Create" ) ;
				SoundSysData.PF.CreateAudioVolumeMeterFunc = ( HRESULT ( WINAPI * )( D_IUnknown** ppApo ) )GetProcAddress( SoundSysData.PF.XAudio2_8DLL, "CreateAudioVolumeMeter" ) ;
				SoundSysData.PF.CreateAudioReverbFunc      = ( HRESULT ( WINAPI * )( D_IUnknown** ppApo ) )GetProcAddress( SoundSysData.PF.XAudio2_8DLL, "CreateAudioReverb" ) ;
				SoundSysData.PF.X3DAudioInitializeFunc     = ( void ( __cdecl * )( DWORD, float, D_X3DAUDIO_HANDLE ) )GetProcAddress( SoundSysData.PF.XAudio2_8DLL, "X3DAudioInitialize" ) ;
				SoundSysData.PF.X3DAudioCalculateFunc      = ( void ( __cdecl * )( const D_X3DAUDIO_HANDLE, const D_X3DAUDIO_LISTENER *, const D_X3DAUDIO_EMITTER *, DWORD, D_X3DAUDIO_DSP_SETTINGS * ) )GetProcAddress( SoundSysData.PF.XAudio2_8DLL, "X3DAudioCalculate" ) ;

				DXST_LOGFILE_ADDUTF16LE( "\x58\x00\x41\x00\x75\x00\x64\x00\x69\x00\x6f\x00\x32\x00\x20\x00\xa4\x30\xf3\x30\xbf\x30\xfc\x30\xd5\x30\xa7\x30\xfc\x30\xb9\x30\x6e\x30\xd6\x53\x97\x5f\x68\x30\x1d\x52\x1f\x67\x16\x53\x92\x30\x4c\x88\x44\x30\x7e\x30\x59\x30\x2e\x00\x2e\x00\x2e\x00\x2e\x00\x20\x00\x20\x00\x00"/*@ L"XAudio2 �C���^�[�t�F�[�X�̎擾�Ə��������s���܂�....  " @*/ ) ;

				hr = SoundSysData.PF.XAudio2CreateFunc( &SoundSysData.PF.XAudio2_8Object, 0, D_XAUDIO2_DEFAULT_PROCESSOR ) ;
				if( FAILED( hr ) )
				{
					DXST_LOGFILE_ADDUTF16LE( "\x1d\x52\x1f\x67\x16\x53\x6b\x30\x31\x59\x57\x65\x57\x30\x7e\x30\x57\x30\x5f\x30\x0a\x00\x00"/*@ L"�������Ɏ��s���܂���\n" @*/ ) ;
					DXST_LOGFILEFMT_ADDA(( "\x83\x47\x83\x89\x81\x5b\x83\x52\x81\x5b\x83\x68 %x"/*@ "�G���[�R�[�h %x" @*/, hr )) ;
					SoundSysData.PF.XAudio2_8Object->Release() ;
					SoundSysData.PF.XAudio2_8Object = NULL ;

					SoundSysData.PF.EnableXAudioFlag = FALSE ;
					goto INITSTART ;
				}
				DXST_LOGFILE_ADDUTF16LE( "\x10\x62\x9f\x52\x20\x00\x56\x00\x65\x00\x72\x00\x32\x00\x2e\x00\x38\x00\x0a\x00\x00"/*@ L"���� Ver2.8\n" @*/ ) ;

				hr = SoundSysData.PF.XAudio2_8Object->CreateMasteringVoice( &SoundSysData.PF.XAudio2_8MasteringVoiceObject, D_XAUDIO2_DEFAULT_CHANNELS, SoundSysData.OutputSmaplesPerSec ) ;
				if( FAILED( hr ) )
				{
					DXST_LOGFILE_ADDUTF16LE( "\x58\x00\x41\x00\x75\x00\x64\x00\x69\x00\x6f\x00\x32\x00\x4d\x00\x61\x00\x73\x00\x74\x00\x65\x00\x72\x00\x69\x00\x6e\x00\x67\x00\x56\x00\x6f\x00\x69\x00\x63\x00\x65\x00\x6e\x30\x5c\x4f\x10\x62\x6b\x30\x31\x59\x57\x65\x57\x30\x7e\x30\x57\x30\x5f\x30\x0a\x00\x00"/*@ L"XAudio2MasteringVoice�̍쐬�Ɏ��s���܂���\n" @*/ ) ;
					SoundSysData.PF.XAudio2_8Object->Release() ;
					SoundSysData.PF.XAudio2_8Object = NULL ;

					SoundSysData.PF.EnableXAudioFlag = FALSE ;
					goto INITSTART ;
				}

				// ���o�[�u�v�Z�p�p�����[�^����
				for( i = 0 ; i < D_XAUDIO2FX_PRESET_NUM ; i ++ )
				{
					D_ReverbConvertI3DL2ToNative2_8( &D_PRESET_PARAMS[ i ], &SoundSysData.PF.XAudio2_8ReverbParameters[ i ] ) ;
				}
			}
			else
			{
				DXST_LOGFILE_ADDUTF16LE( "\x58\x00\x41\x00\x75\x00\x64\x00\x69\x00\x6f\x00\x32\x00\x20\x00\xa4\x30\xf3\x30\xbf\x30\xfc\x30\xd5\x30\xa7\x30\xfc\x30\xb9\x30\x6e\x30\xd6\x53\x97\x5f\x92\x30\x4c\x88\x44\x30\x7e\x30\x59\x30\x2e\x00\x2e\x00\x2e\x00\x2e\x00\x20\x00\x20\x00\x00"/*@ L"XAudio2 �C���^�[�t�F�[�X�̎擾���s���܂�....  " @*/ ) ;
				for( i = 0 ; XAudioGuidList[ i ][ 0 ] != NULL ; i ++ )
				{
					hr = WinAPIData.Win32Func.CoCreateInstanceFunc( *XAudioGuidList[ i ][ 0 ], NULL, CLSCTX_ALL, IID_IXAUDIO2, ( LPVOID *)&SoundSysData.PF.XAudio2Object );
					if( !FAILED( hr ) )
					{
						break ;
					}
				}
				if( XAudioGuidList[ i ][ 0 ] != NULL )
				{
					DXST_LOGFILEFMT_ADDUTF16LE(( "\x10\x62\x9f\x52\x20\x00\x56\x00\x65\x00\x72\x00\x32\x00\x2e\x00\x25\x00\x64\x00\x0a\x00\x00"/*@ L"���� Ver2.%d\n" @*/, 7 - i )) ;
					DXST_LOGFILE_ADDUTF16LE(( "\x15\x5f\x4d\x30\x9a\x7d\x4d\x30\xa4\x30\xf3\x30\xbf\x30\xfc\x30\xd5\x30\xa7\x30\xfc\x30\xb9\x30\x6e\x30\x1d\x52\x1f\x67\x16\x53\xe6\x51\x06\x74\x2e\x00\x2e\x00\x2e\x00\x20\x00\x20\x00\x00"/*@ L"���������C���^�[�t�F�[�X�̏���������...  " @*/ )) ;
					hr = SoundSysData.PF.XAudio2Object->Initialize( 0 ) ;
					if( FAILED( hr ) )
					{
						DXST_LOGFILE_ADDUTF16LE( "\x1d\x52\x1f\x67\x16\x53\x6b\x30\x31\x59\x57\x65\x57\x30\x7e\x30\x57\x30\x5f\x30\x0a\x00\x00"/*@ L"�������Ɏ��s���܂���\n" @*/ ) ;
						DXST_LOGFILEFMT_ADDA(( "\x83\x47\x83\x89\x81\x5b\x83\x52\x81\x5b\x83\x68 %x"/*@ "�G���[�R�[�h %x" @*/, hr )) ;
						SoundSysData.PF.XAudio2Object->Release() ;
						SoundSysData.PF.XAudio2Object = NULL ;

						SoundSysData.PF.EnableXAudioFlag = FALSE ;
						goto INITSTART ;
					}
					DXST_LOGFILE_ADDUTF16LE( "\x10\x62\x9f\x52\x0a\x00\x00"/*@ L"����\n" @*/ ) ;

					DXST_LOGFILE_ADDUTF16LE( "\x58\x00\x33\x00\x44\x00\x41\x00\x75\x00\x64\x00\x69\x00\x6f\x00\x2e\x00\x44\x00\x4c\x00\x4c\x00\x20\x00\x6e\x30\xad\x8a\x7f\x30\xbc\x8f\x7f\x30\x2d\x4e\x2e\x00\x2e\x00\x2e\x00\x20\x00\x00"/*@ L"X3DAudio.DLL �̓ǂݍ��ݒ�... " @*/ ) ;
					for( i = 0 ; X3DAudioDLLName[ i ] != NULL ; i ++ )
					{
						SoundSysData.PF.X3DAudioDLL = LoadLibraryW( X3DAudioDLLName[ i ] ) ;
						if( SoundSysData.PF.X3DAudioDLL != NULL )
							break ;
					}
					if( SoundSysData.PF.X3DAudioDLL == NULL )
					{
						DXST_LOGFILE_ADDUTF16LE( "\x31\x59\x57\x65\x01\x30\x58\x00\x33\x00\x44\x00\x41\x00\x75\x00\x64\x00\x69\x00\x6f\x00\x01\x30\x58\x00\x41\x00\x75\x00\x64\x00\x69\x00\x6f\x00\x32\x00\x6f\x30\x7f\x4f\x28\x75\x57\x30\x7e\x30\x5b\x30\x93\x30\x0a\x00\x00"/*@ L"���s�AX3DAudio�AXAudio2�͎g�p���܂���\n" @*/ ) ;
						SoundSysData.PF.XAudio2Object->Release() ;
						SoundSysData.PF.XAudio2Object = NULL ;

						SoundSysData.PF.EnableXAudioFlag = FALSE ;
						goto INITSTART ;
					}
					else
					{
						DXST_LOGFILE_ADDUTF16LE( "\x10\x62\x9f\x52\x0a\x00\x00"/*@ L"����\n" @*/ ) ;

						SoundSysData.PF.X3DAudioInitializeFunc = ( void ( __cdecl * )( DWORD, float, D_X3DAUDIO_HANDLE ) )GetProcAddress( SoundSysData.PF.X3DAudioDLL, "X3DAudioInitialize" ) ;
						SoundSysData.PF.X3DAudioCalculateFunc  = ( void ( __cdecl * )( const D_X3DAUDIO_HANDLE, const D_X3DAUDIO_LISTENER *, const D_X3DAUDIO_EMITTER *, DWORD, D_X3DAUDIO_DSP_SETTINGS * ) )GetProcAddress( SoundSysData.PF.X3DAudioDLL, "X3DAudioCalculate" ) ;
					}
				}
				else
				{
					SoundSysData.PF.XAudio2Object = NULL ;
					DXST_LOGFILE_ADDUTF16LE( "\x58\x00\x41\x00\x75\x00\x64\x00\x69\x00\x6f\x00\x32\x00\xa4\x30\xf3\x30\xbf\x30\xfc\x30\xd5\x30\xa7\x30\xfc\x30\xb9\x30\x6e\x30\xd6\x53\x97\x5f\x6b\x30\x31\x59\x57\x65\x57\x30\x7e\x30\x57\x30\x5f\x30\x0a\x00\x00"/*@ L"XAudio2�C���^�[�t�F�[�X�̎擾�Ɏ��s���܂���\n" @*/ ) ;

					SoundSysData.PF.EnableXAudioFlag = FALSE ;
					goto INITSTART ;
				}

				hr = SoundSysData.PF.XAudio2Object->CreateMasteringVoice( &SoundSysData.PF.XAudio2MasteringVoiceObject, D_XAUDIO2_DEFAULT_CHANNELS, SoundSysData.OutputSmaplesPerSec ) ;
				if( FAILED( hr ) )
				{
					DXST_LOGFILE_ADDUTF16LE( "\x58\x00\x41\x00\x75\x00\x64\x00\x69\x00\x6f\x00\x32\x00\x4d\x00\x61\x00\x73\x00\x74\x00\x65\x00\x72\x00\x69\x00\x6e\x00\x67\x00\x56\x00\x6f\x00\x69\x00\x63\x00\x65\x00\x6e\x30\x5c\x4f\x10\x62\x6b\x30\x31\x59\x57\x65\x57\x30\x7e\x30\x57\x30\x5f\x30\x0a\x00\x00"/*@ L"XAudio2MasteringVoice�̍쐬�Ɏ��s���܂���\n" @*/ ) ;

					if( SoundSysData.PF.X3DAudioDLL != NULL )
					{
						FreeLibrary( SoundSysData.PF.X3DAudioDLL ) ;
						SoundSysData.PF.X3DAudioInitializeFunc = NULL ;
						SoundSysData.PF.X3DAudioCalculateFunc = NULL ;
						SoundSysData.PF.X3DAudioDLL = NULL ;
					}

					SoundSysData.PF.XAudio2Object->Release() ;
					SoundSysData.PF.XAudio2Object = NULL ;

					SoundSysData.PF.EnableXAudioFlag = FALSE ;
					goto INITSTART ;
				}

				// ���o�[�u�v�Z�p�p�����[�^����
				for( i = 0 ; i < D_XAUDIO2FX_PRESET_NUM ; i ++ )
				{
					D_ReverbConvertI3DL2ToNative( &D_PRESET_PARAMS[ i ], &SoundSysData.PF.XAudio2ReverbParameters[ i ] ) ;
				}
			}
		}
		else
		{
			DXST_LOGFILE_ADDUTF16LE( "\x44\x00\x69\x00\x72\x00\x65\x00\x63\x00\x74\x00\x53\x00\x6f\x00\x75\x00\x6e\x00\x64\x00\x20\x00\x6e\x30\x1d\x52\x1f\x67\x16\x53\x92\x30\x4c\x88\x44\x30\x7e\x30\x59\x30\x0a\x00\x00"/*@ L"DirectSound �̏��������s���܂�\n" @*/ ) ;
			DXST_LOGFILE_ADDUTF16LE( "\x44\x00\x69\x00\x72\x00\x65\x00\x63\x00\x74\x00\x53\x00\x6f\x00\x75\x00\x6e\x00\x64\x00\x20\x00\xa4\x30\xf3\x30\xbf\x30\xfc\x30\xd5\x30\xa7\x30\xfc\x30\xb9\x30\x6e\x30\xd6\x53\x97\x5f\x92\x30\x4c\x88\x44\x30\x7e\x30\x59\x30\x2e\x00\x2e\x00\x2e\x00\x2e\x00\x20\x00\x20\x00\x00"/*@ L"DirectSound �C���^�[�t�F�[�X�̎擾���s���܂�....  " @*/ ) ; 
			hr = WinAPIData.Win32Func.CoCreateInstanceFunc( CLSID_DIRECTSOUND, NULL, CLSCTX_ALL, IID_IDIRECTSOUND, ( LPVOID *)&SoundSysData.PF.DirectSoundObject );
			if( !FAILED( hr ) )
			{
				DXST_LOGFILE_ADDUTF16LE( "\x10\x62\x9f\x52\x0a\x00\x00"/*@ L"����\n" @*/ ) ;
				DXST_LOGFILE_ADDUTF16LE( "\x15\x5f\x4d\x30\x9a\x7d\x4d\x30\xa4\x30\xf3\x30\xbf\x30\xfc\x30\xd5\x30\xa7\x30\xfc\x30\xb9\x30\x6e\x30\x1d\x52\x1f\x67\x16\x53\xe6\x51\x06\x74\x2e\x00\x2e\x00\x2e\x00\x20\x00\x20\x00\x00"/*@ L"���������C���^�[�t�F�[�X�̏���������...  " @*/ ) ;
				hr = SoundSysData.PF.DirectSoundObject->Initialize( NULL ) ;
				if( FAILED( hr ) )
				{
					DXST_LOGFILE_ADDUTF16LE( "\x1d\x52\x1f\x67\x16\x53\x6b\x30\x31\x59\x57\x65\x57\x30\x7e\x30\x57\x30\x5f\x30\x0a\x00\x00"/*@ L"�������Ɏ��s���܂���\n" @*/ ) ;
					DXST_LOGFILEFMT_ADDA(( "\x83\x47\x83\x89\x81\x5b\x83\x52\x81\x5b\x83\x68 %x"/*@ "�G���[�R�[�h %x" @*/, hr )) ;
					SoundSysData.PF.DirectSoundObject->Release() ;
					SoundSysData.PF.DirectSoundObject = NULL ;
					return -1 ;
				}
				DXST_LOGFILE_ADDUTF16LE( "\x10\x62\x9f\x52\x0a\x00\x00"/*@ L"����\n" @*/ ) ;
			}
			else
			{
				SoundSysData.PF.DirectSoundObject = NULL ;
				DXST_LOGFILE_ADDUTF16LE( "\x44\x00\x69\x00\x72\x00\x65\x00\x63\x00\x74\x00\x53\x00\x6f\x00\x75\x00\x6e\x00\x64\x00\xa4\x30\xf3\x30\xbf\x30\xfc\x30\xd5\x30\xa7\x30\xfc\x30\xb9\x30\x6e\x30\xd6\x53\x97\x5f\x6b\x30\x31\x59\x57\x65\x57\x30\x7e\x30\x57\x30\x5f\x30\x0a\x00\x00"/*@ L"DirectSound�C���^�[�t�F�[�X�̎擾�Ɏ��s���܂���\n" @*/ ) ;
				return -1 ;
			}
	/*
			DXST_LOGFILE_ADDUTF16LE( L"DirectSound �̏��������s���܂�\n" ) ; 
			hr = DirectSoundCreate( NULL , &SoundSysData.PF.DirectSoundObject , NULL ) ;
			if( hr != D_DS_OK )
			{
				SoundSysData.PF.DirectSoundObject = NULL ;
				DXST_LOGFILE_ADDUTF16LE( L"DirectSound�̏������Ɏ��s���܂���\n" ) ;
				return -1 ;
			}
	*/

			// �������x�����Z�b�g����
			hr = SoundSysData.PF.DirectSoundObject->SetCooperativeLevel( WinAPIData.Win32Func.GetDesktopWindowFunc(), D_DSSCL_PRIORITY ) ;
			if( hr != D_DS_OK )
			{
				DXST_LOGFILE_ADDUTF16LE( "\x44\x00\x69\x00\x72\x00\x65\x00\x63\x00\x74\x00\x53\x00\x6f\x00\x75\x00\x6e\x00\x64\x00\x6e\x30\x54\x53\xbf\x8a\xec\x30\xd9\x30\xeb\x30\x6e\x30\x2d\x8a\x9a\x5b\x6b\x30\x31\x59\x57\x65\x57\x30\x7e\x30\x57\x30\x5f\x30\x0a\x00\x00"/*@ L"DirectSound�̋������x���̐ݒ�Ɏ��s���܂���\n" @*/ ) ;
				SoundSysData.PF.DirectSoundObject->Release() ;
				SoundSysData.PF.DirectSoundObject = NULL ;
				return -1 ;
			}

			// �v���C�}���T�E���h�o�b�t�@���쐬����
			{
				D_DSBUFFERDESC dsbdesc ;
				DWORD ChNum , Rate , Byte ;

				// �쐬�X�e�[�^�X���Z�b�g����
				_MEMSET( &dsbdesc, 0, sizeof( dsbdesc ) ) ;
				dsbdesc.dwSize = sizeof( dsbdesc ) ;
				dsbdesc.dwFlags = ( DWORD )( D_DSBCAPS_PRIMARYBUFFER | ( SoundSysData.PF.UseSoftwareMixing ? D_DSBCAPS_LOCSOFTWARE : 0 ) ) ;
				hr = SoundSysData.PF.DirectSoundObject->CreateSoundBuffer( &dsbdesc , &SoundSysData.PF.PrimarySoundBuffer , NULL ) ;
				if( hr != D_DS_OK )
				{
					DXST_LOGFILE_ADDUTF16LE( "\x44\x00\x69\x00\x72\x00\x65\x00\x63\x00\x74\x00\x53\x00\x6f\x00\x75\x00\x6e\x00\x64\x00\x6e\x30\xd7\x30\xe9\x30\xa4\x30\xde\x30\xea\x30\xb5\x30\xa6\x30\xf3\x30\xc9\x30\xd0\x30\xc3\x30\xd5\x30\xa1\x30\x6e\x30\x5c\x4f\x10\x62\x6b\x30\x31\x59\x57\x65\x57\x30\x7e\x30\x57\x30\x5f\x30\x0a\x00\x00"/*@ L"DirectSound�̃v���C�}���T�E���h�o�b�t�@�̍쐬�Ɏ��s���܂���\n" @*/ ) ;
					SoundSysData.PF.DirectSoundObject->Release() ;
					SoundSysData.PF.DirectSoundObject = NULL ;
					return -1 ;
				}

				// ���𓾂�
				_MEMSET( &caps, 0, sizeof( caps ) ) ;
				caps.dwSize = sizeof( caps ) ;
				SoundSysData.PF.DirectSoundObject->GetCaps( &caps ) ;


				// �v���C�}���o�b�t�@�̃t�H�[�}�b�g���Z�b�g����
				ChNum = ( DWORD )( caps.dwFlags & D_DSCAPS_PRIMARYSTEREO ? 2 : 1 ) ;
				Rate  = caps.dwMaxSecondarySampleRate < 44100 ? caps.dwMaxSecondarySampleRate : 44100  ;
				Byte  = ( DWORD )( caps.dwFlags & D_DSCAPS_PRIMARY16BIT  ? 2 : 1 ) ;

				_MEMSET( &wfmtx, 0, sizeof( wfmtx ) ) ;
				wfmtx.wFormatTag		= WAVE_FORMAT_PCM ;											// PCM�t�H�[�}�b�g
				wfmtx.nChannels			= ( WORD )ChNum ;											// �`�����l���Q���X�e���I
				wfmtx.nSamplesPerSec	= Rate ;													// �Đ����[�g
				wfmtx.wBitsPerSample	= ( WORD )( Byte * 8 ) ;									// �P���ɂ�����f�[�^�r�b�g��
				wfmtx.nBlockAlign		= ( WORD )( wfmtx.wBitsPerSample / 8 * wfmtx.nChannels ) ;	// �P�w���c�ɂ�����f�[�^�o�C�g��
				wfmtx.nAvgBytesPerSec	= wfmtx.nSamplesPerSec * wfmtx.nBlockAlign ;				// �P�b�ɂ�����f�[�^�o�C�g��
				hr = SoundSysData.PF.PrimarySoundBuffer->SetFormat( &wfmtx ) ;
				if( hr != D_DS_OK )
				{
					DXST_LOGFILE_ADDUTF16LE( "\x44\x00\x69\x00\x72\x00\x65\x00\x63\x00\x74\x00\x53\x00\x6f\x00\x75\x00\x6e\x00\x64\x00\x6e\x30\xd7\x30\xe9\x30\xa4\x30\xde\x30\xea\x30\xb5\x30\xa6\x30\xf3\x30\xc9\x30\xd0\x30\xc3\x30\xd5\x30\xa1\x30\x6e\x30\xd5\x30\xa9\x30\xfc\x30\xde\x30\xc3\x30\xc8\x30\x2d\x8a\x9a\x5b\x6b\x30\x31\x59\x57\x65\x57\x30\x7e\x30\x57\x30\x5f\x30\x0a\x00\x00"/*@ L"DirectSound�̃v���C�}���T�E���h�o�b�t�@�̃t�H�[�}�b�g�ݒ�Ɏ��s���܂���\n" @*/ ) ;
				}

			}
		}
		break ;

	case DX_MIDIMODE_DIRECT_MUSIC_REVERB :
	case DX_MIDIMODE_DIRECT_MUSIC_NORMAL :
		SoundSysData.PF.EnableXAudioFlag = FALSE ;

		// �c�����������l���������ɂ��Đ��̏ꍇ�̏���
		{
			D_IDirectMusic *DMusic ;
			D_IDirectMusic8 *DMusic8 ;
			GUID MidiGuid = {0};
			bool MidiGuidValid ;
				
			DXST_LOGFILE_ADDUTF16LE( "\x44\x00\x69\x00\x72\x00\x65\x00\x63\x00\x74\x00\x53\x00\x6f\x00\x75\x00\x6e\x00\x64\x00\x38\x00\x20\x00\x6e\x30\x1d\x52\x1f\x67\x16\x53\x92\x30\x4c\x88\x44\x30\x7e\x30\x59\x30\x0a\x00\x00"/*@ L"DirectSound8 �̏��������s���܂�\n" @*/ ) ; 

			// DirectMusic �I�u�W�F�N�g�̍쐬
//			if( FAILED(	WinAPIData.Win32Func.CoCreateInstanceFunc( CLSID_DirectMusicLoader , NULL , 
//											CLSCTX_INPROC , IID_IDirectMusicLoader8 , ( void ** )&SoundSysData.PF.DirectMusicLoaderObject ) ) )
			if( FAILED(	WinAPIData.Win32Func.CoCreateInstanceFunc( CLSID_DIRECTMUSICLOADER, NULL , 
											CLSCTX_INPROC , IID_IDIRECTMUSICLOADER8 , ( void ** )&SoundSysData.PF.DirectMusicLoaderObject ) ) )
			{
				DXST_LOGFILE_ADDUTF16LE( "\x44\x00\x69\x00\x72\x00\x65\x00\x63\x00\x74\x00\x4d\x00\x75\x00\x73\x00\x69\x00\x63\x00\x4c\x00\x6f\x00\x61\x00\x64\x00\x65\x00\x72\x00\x38\x00\x20\x00\xaa\x30\xd6\x30\xb8\x30\xa7\x30\xaf\x30\xc8\x30\x6e\x30\x5c\x4f\x10\x62\x6b\x30\x31\x59\x57\x65\x57\x30\x7e\x30\x57\x30\x5f\x30\x0a\x00\x00"/*@ L"DirectMusicLoader8 �I�u�W�F�N�g�̍쐬�Ɏ��s���܂���\n" @*/ ) ;
				DXST_LOGFILE_ADDUTF16LE( "\x4d\x52\xd0\x30\xfc\x30\xb8\x30\xe7\x30\xf3\x30\x67\x30\x6e\x30\x1d\x52\x1f\x67\x16\x53\xe6\x51\x06\x74\x6b\x30\xfb\x79\x8a\x30\x7e\x30\x59\x30\x0a\x00\x00"/*@ L"�O�o�[�W�����ł̏����������Ɉڂ�܂�\n" @*/ ) ;
				SoundSysData.SoundMode = DX_MIDIMODE_MCI ;
				goto INITSTART ;
			}

//			if( FAILED( WinAPIData.Win32Func.CoCreateInstanceFunc( CLSID_DirectMusicPerformance , NULL , 
//											CLSCTX_INPROC , IID_IDirectMusicPerformance8 , ( void ** )&SoundSysData.PF.DirectMusicPerformanceObject ) ) )
			if( FAILED( WinAPIData.Win32Func.CoCreateInstanceFunc( CLSID_DIRECTMUSICPERFORMANCE, NULL , 
											CLSCTX_INPROC , IID_IDIRECTMUSICPERFORMANCE8 , ( void ** )&SoundSysData.PF.DirectMusicPerformanceObject ) ) )
			{
				// ���[�_�[���������
				if( SoundSysData.PF.DirectMusicLoaderObject )
				{
					SoundSysData.PF.DirectMusicLoaderObject->Release() ; 
					SoundSysData.PF.DirectMusicLoaderObject = NULL ;
				}

				DXST_LOGFILE_ADDUTF16LE( "\x44\x00\x69\x00\x72\x00\x65\x00\x63\x00\x74\x00\x4d\x00\x75\x00\x73\x00\x69\x00\x63\x00\x50\x00\x65\x00\x72\x00\x66\x00\x6f\x00\x72\x00\x6d\x00\x61\x00\x6e\x00\x63\x00\x65\x00\x20\x00\xaa\x30\xd6\x30\xb8\x30\xa7\x30\xaf\x30\xc8\x30\x6e\x30\x5c\x4f\x10\x62\x6b\x30\x31\x59\x57\x65\x57\x30\x7e\x30\x57\x30\x5f\x30\x0a\x00\x00"/*@ L"DirectMusicPerformance �I�u�W�F�N�g�̍쐬�Ɏ��s���܂���\n" @*/ ) ;
				SoundSysData.SoundMode = DX_MIDIMODE_MCI ;
				goto INITSTART ;
//				TerminateSoundSystem() ;
//				return -1 ;
			}

			MidiGuidValid = false ;
			// MidiSystemData�����̗�
			{
				D_DMUS_PORTCAPS Param ;
				WCHAR wStr[D_DMUS_MAX_DESCRIPTION+3] ;
				int i ;

				if( FAILED( WinAPIData.Win32Func.CoCreateInstanceFunc( CLSID_DIRECTMUSIC, NULL,
												CLSCTX_INPROC_SERVER, IID_IDIRECTMUSIC, (LPVOID*)&DMusic ) ) )
				{
					DXST_LOGFILE_ADDUTF16LE( "\x44\x00\x69\x00\x72\x00\x65\x00\x63\x00\x74\x00\x4d\x00\x75\x00\x73\x00\x69\x00\x63\x00\x20\x00\xaa\x30\xd6\x30\xb8\x30\xa7\x30\xaf\x30\xc8\x30\x6e\x30\xd6\x53\x97\x5f\x6b\x30\x31\x59\x57\x65\x57\x30\x7e\x30\x57\x30\x5f\x30\x0a\x00\x00"/*@ L"DirectMusic �I�u�W�F�N�g�̎擾�Ɏ��s���܂���\n" @*/ ) ;
					TerminateSoundSystem() ;
					return -1 ;
				}
			
//				if( FAILED( DMusic->QueryInterface( IID_IDirectMusic8, (void **)&DMusic8 ) ) )
				if( FAILED( DMusic->QueryInterface( IID_IDIRECTMUSIC8, (void **)&DMusic8 ) ) )
				{
					DMusic->Release() ;
					DXST_LOGFILE_ADDUTF16LE( "\x44\x00\x69\x00\x72\x00\x65\x00\x63\x00\x74\x00\x4d\x00\x75\x00\x73\x00\x69\x00\x63\x00\x38\x00\x20\x00\xaa\x30\xd6\x30\xb8\x30\xa7\x30\xaf\x30\xc8\x30\x6e\x30\xd6\x53\x97\x5f\x6b\x30\x31\x59\x57\x65\x57\x30\x7e\x30\x57\x30\x5f\x30\x0a\x00\x00"/*@ L"DirectMusic8 �I�u�W�F�N�g�̎擾�Ɏ��s���܂���\n" @*/ ) ;
					TerminateSoundSystem() ;
					return -1 ;
				}
				
				_MEMSET( &Param, 0, sizeof( Param ) ) ;
				Param.dwSize = sizeof(D_DMUS_PORTCAPS) ;
				
				for( i = 0 ; DMusic8->EnumPort( ( DWORD )i, &Param ) != S_FALSE ; i ++ )
				{
					_WCSCPY_S( wStr, sizeof( wStr ), Param.wszDescription ) ;
					wStr[D_DMUS_MAX_DESCRIPTION] = L'\0' ;
					DXST_LOGFILEFMT_ADDW(( L"%s  Device%d:%s ", Param.dwClass == D_DMUS_PC_OUTPUTCLASS ? L"Output port" : L"Input port", i, wStr )) ;
				}
				
				DMusic8->Release() ;
				DMusic->Release() ;
			}
	
			// DirectMusicPerformance�I�u�W�F�N�g�̏�����
			{
				D_DMUS_AUDIOPARAMS AudioParams ;

				_MEMSET( &AudioParams, 0, sizeof( AudioParams ) ) ;
				AudioParams.dwSize = sizeof( AudioParams ) ;
				AudioParams.fInitNow = TRUE ;
				AudioParams.dwValidData = D_DMUS_AUDIOPARAMS_SAMPLERATE | D_DMUS_AUDIOPARAMS_FEATURES ;
				AudioParams.dwSampleRate = 44100 ;
				AudioParams.dwFeatures = D_DMUS_AUDIOF_ALL ;
				if( MidiGuidValid == true )
				{
					AudioParams.dwValidData |= D_DMUS_AUDIOPARAMS_DEFAULTSYNTH  ;
					AudioParams.clsidDefaultSynth = MidiGuid ;
				}

				SoundSysData.PF.DirectSoundObject = NULL ;
				DMusic = NULL ;
				if( SoundSysData.PF.DirectMusicPerformanceObject->InitAudio(
											&DMusic ,									// IDirectMusic�C���^�[�t�F�C�X�͕s�v�B
											&SoundSysData.PF.DirectSoundObject,			// IDirectSound�C���^�[�t�F�C�X�|�C���^��n���B
											NULL ,										// �E�B���h�E�̃n���h���B
											SoundSysData.SoundMode == DX_MIDIMODE_DIRECT_MUSIC_NORMAL ? D_DMUS_APATH_DYNAMIC_STEREO : D_DMUS_APATH_SHARED_STEREOPLUSREVERB,		// �f�t�H���g�̃I�[�f�B�I�p�X�^�C�v
											64 ,										// �p�t�H�[�}���X�`�����l���̐��B
											D_DMUS_AUDIOF_ALL ,							// �V���Z�T�C�U�̋@�\�B
											&AudioParams								// �I�[�f�B�I�p�����[�^�ɂ̓f�t�H���g���g�p�B
										) != S_OK )
				{
					DXST_LOGFILE_ADDUTF16LE( "\x44\x00\x69\x00\x72\x00\x65\x00\x63\x00\x74\x00\x4d\x00\x75\x00\x73\x00\x69\x00\x63\x00\x50\x00\x65\x00\x72\x00\x66\x00\x6f\x00\x72\x00\x6d\x00\x61\x00\x6e\x00\x63\x00\x65\x00\xaa\x30\xd6\x30\xb8\x30\xa7\x30\xaf\x30\xc8\x30\x6e\x30\x1d\x52\x1f\x67\x16\x53\x6b\x30\x31\x59\x57\x65\x57\x30\x7e\x30\x57\x30\x5f\x30\x0a\x00\x00"/*@ L"DirectMusicPerformance�I�u�W�F�N�g�̏������Ɏ��s���܂���\n" @*/ ) ;
					TerminateSoundSystem() ;
					return -1 ;
				}
			}

			// �ő剹�ʂ̎擾
			SoundSysData.PF.DirectMusicPerformanceObject->GetGlobalParam( GUID_PERFMASTERVOLUME , ( void * )&SoundSysData.MaxVolume , sizeof( long ) ) ;
		}
		break ;
	}

#ifndef DX_NON_ASIO
	if( SoundSysData.PF.EnableASIOFlag )
	{
	}
	else
#endif // DX_NON_ASIO
	if( SoundSysData.PF.DisableWASAPIFlag == FALSE )
	{
		if( SoundSysData.PF.WASAPI_IsExclusiveFlag )
		{
			DXST_LOGFILEFMT_ADDUTF16LE(( "\xd5\x52\x5c\x4f\xe2\x30\xfc\x30\xc9\x30\x20\x00\x20\x00\x20\x00\x20\x00\x20\x00\x20\x00\x20\x00\x20\x00\x20\x00\x3a\x00\x20\x00\x92\x63\xd6\x4e\xe2\x30\xfc\x30\xc9\x30\x00"/*@ L"���샂�[�h         : �r�����[�h" @*/ )) ;
		}
		else
		{
			DXST_LOGFILEFMT_ADDUTF16LE(( "\xd5\x52\x5c\x4f\xe2\x30\xfc\x30\xc9\x30\x20\x00\x20\x00\x20\x00\x20\x00\x20\x00\x20\x00\x20\x00\x20\x00\x20\x00\x3a\x00\x20\x00\x71\x51\x09\x67\xe2\x30\xfc\x30\xc9\x30\x00"/*@ L"���샂�[�h         : ���L���[�h" @*/ )) ;
		}

		DXST_LOGFILE_TABSUB ;
	}
	else
	if( SoundSysData.PF.EnableXAudioFlag )
	{
		// ���o��
		if( SoundSysData.PF.XAudio2_8DLL == NULL )
		{
			D_XAUDIO2_DEVICE_DETAILS Details ;

			SoundSysData.PF.XAudio2Object->GetDeviceDetails( 0, &Details ) ;

			DXST_LOGFILE_TABADD ;

#ifndef DX_GCC_COMPILE
			DXST_LOGFILEFMT_ADDW(( L"Device Name : %s", Details.DisplayName )) ;
#endif
			DXST_LOGFILEFMT_ADDUTF16LE(( "\xc1\x30\xe3\x30\xf3\x30\xcd\x30\xeb\x30\x70\x65\x3a\x00\x25\x00\x64\x00\x00"/*@ L"�`�����l����:%d" @*/, Details.OutputFormat.Format.nChannels )) ;
			DXST_LOGFILEFMT_ADDUTF16LE(( "\xd3\x30\xc3\x30\xc8\x30\xbe\x7c\xa6\x5e\x3a\x00\x25\x00\x64\x00\x62\x00\x69\x00\x74\x00\x00"/*@ L"�r�b�g���x:%dbit" @*/, Details.OutputFormat.Format.wBitsPerSample )) ;
			DXST_LOGFILEFMT_ADDUTF16LE(( "\xb5\x30\xf3\x30\xd7\x30\xea\x30\xf3\x30\xb0\x30\xec\x30\xfc\x30\xc8\x30\x3a\x00\x25\x00\x64\x00\x48\x00\x7a\x00\x00"/*@ L"�T���v�����O���[�g:%dHz" @*/, Details.OutputFormat.Format.nSamplesPerSec )) ;

			DXST_LOGFILE_TABSUB ;

			SoundSysData.PF.XAudio2OutputChannelMask = Details.OutputFormat.dwChannelMask ;
			SoundSysData.OutputChannels = Details.OutputFormat.Format.nChannels ;
		}
		else
		{
			int i ;

			SoundSysData.PF.XAudio2_8MasteringVoiceObject->GetChannelMask( &SoundSysData.PF.XAudio2OutputChannelMask ) ;
			SoundSysData.OutputChannels = 0 ;
			for( i = 0 ; i < 32 ; i ++ )
			{
				if( ( SoundSysData.PF.XAudio2OutputChannelMask & ( 1 << i ) ) != 0 )
					SoundSysData.OutputChannels ++ ;
			}
		}

		// X3DAudio �̏�����
		SoundSysData.PF.X3DAudioInitializeFunc( SoundSysData.PF.XAudio2OutputChannelMask, D_X3DAUDIO_SPEED_OF_SOUND, SoundSysData.PF.X3DAudioInstance ) ;
	}
	else
	{
		// �����o�b�t�@�̏�����
		{
			D_DSBUFFERDESC dsbdesc ;
			WAVEFORMATEX wfmtex ;

			// �o�b�t�@�����X�e�[�^�X�̃Z�b�g
			_MEMSET( &wfmtex, 0, sizeof( wfmtex ) ) ;
			wfmtex.wFormatTag		= WAVE_FORMAT_PCM ;								// PCM�t�H�[�}�b�g
			wfmtex.nChannels			= 1 ;										// �`�����l���P�����m����
			wfmtex.nSamplesPerSec	= 22050 ;										// �T���v�����O���[�g
			wfmtex.wBitsPerSample	= 16 ;											// �P�T���v���ɂ�����r�b�g��
			wfmtex.nBlockAlign		= 2 ;											// �P�T���v���ɂ�����o�C�g���@�~�@�`�����l����
			wfmtex.nAvgBytesPerSec	= wfmtex.nSamplesPerSec * wfmtex.nBlockAlign ;	// �P�b���̃f�[�^�o�C�g��

			_MEMSET( &dsbdesc, 0, sizeof( dsbdesc ) ) ;
			dsbdesc.dwSize			= sizeof( dsbdesc ) ;
			dsbdesc.dwFlags			= ( DWORD )( D_DSBCAPS_GLOBALFOCUS | D_DSBCAPS_CTRLPAN | D_DSBCAPS_CTRLVOLUME | D_DSBCAPS_CTRLFREQUENCY | D_DSBCAPS_GETCURRENTPOSITION2 | ( SoundSysData.PF.UseSoftwareMixing ? D_DSBCAPS_LOCSOFTWARE : D_DSBCAPS_STATIC ) ) ;
			dsbdesc.dwBufferBytes	= ( DWORD )( wfmtex.nBlockAlign * 9000 ) ;
			dsbdesc.lpwfxFormat		= &wfmtex ;

			if( SoundSysData.PF.DirectSoundObject->CreateSoundBuffer( &dsbdesc , &SoundSysData.PF.NoSoundBuffer , NULL ) != D_DS_OK )
			{
				// �쐬�Ɏ��s�����ꍇ�́A�܂߂�@�\�����炵�čēx�쐬����
				_MEMSET( &dsbdesc, 0, sizeof( dsbdesc ) ) ;
				dsbdesc.dwSize			= sizeof( dsbdesc ) ;
				dsbdesc.dwFlags			= ( DWORD )( D_DSBCAPS_CTRLPAN | D_DSBCAPS_CTRLVOLUME | ( SoundSysData.PF.UseSoftwareMixing ? D_DSBCAPS_LOCSOFTWARE : D_DSBCAPS_STATIC ) ) ;
				dsbdesc.dwBufferBytes	= ( DWORD )( wfmtex.nBlockAlign * 9000 ) ;
				dsbdesc.lpwfxFormat		= &wfmtex ;
				if( SoundSysData.PF.DirectSoundObject->CreateSoundBuffer( &dsbdesc , &SoundSysData.PF.NoSoundBuffer , NULL ) != D_DS_OK )
				{
					DXST_LOGFILE_ADDUTF16LE( "\x21\x71\xf3\x97\xd0\x30\xc3\x30\xd5\x30\xa1\x30\x6e\x30\x5c\x4f\x10\x62\x6b\x30\x31\x59\x57\x65\x57\x30\x7e\x30\x57\x30\x5f\x30\x00"/*@ L"�����o�b�t�@�̍쐬�Ɏ��s���܂���" @*/ ) ;
					SoundSysData.PF.NoSoundBuffer = NULL ;
					goto R1 ;
				}
			}

			// �����Ŗ��߂�
			{
				LPVOID write1 ;
				DWORD length1 ;
				LPVOID write2 ;
				DWORD length2 ;

				hr = SoundSysData.PF.NoSoundBuffer->Lock( 0 , dsbdesc.dwBufferBytes, &write1 , &length1 , &write2 , &length2 , 0 ) ;		// �o�b�t�@�̃��b�N
				if( hr != D_DS_OK )
				{
					DXST_LOGFILE_ADDUTF16LE( "\x21\x71\xf3\x97\xd0\x30\xc3\x30\xd5\x30\xa1\x30\x6e\x30\xed\x30\xc3\x30\xaf\x30\x6b\x30\x31\x59\x57\x65\x57\x30\x7e\x30\x57\x30\x5f\x30\x00"/*@ L"�����o�b�t�@�̃��b�N�Ɏ��s���܂���" @*/ ) ;
					SoundSysData.PF.NoSoundBuffer->Release() ;
					SoundSysData.PF.NoSoundBuffer = NULL ;
				}

				_MEMSET( write1, 0, length1 ) ;
				if( write2 != 0 )
				{
					_MEMSET( write2, 0, length2 ) ;
				}

				hr = SoundSysData.PF.NoSoundBuffer->Unlock( write1, length1, write2, length2 ) ;								// �o�b�t�@�̃��b�N����
			}

			// �����o�b�t�@�̍Đ�
			SoundSysData.PF.NoSoundBuffer->Play( 0, 0, D_DSBPLAY_LOOPING ) ;
		}

R1 :
		// ���\�\��
		if( SoundSysData.PF.DirectSoundObject )
		{
			typedef HRESULT ( WINAPI *DIRECTSOUNDENUMERATEFUNC )( LPD_DSENUMCALLBACKA, LPVOID lpContext ) ;
			DIRECTSOUNDENUMERATEFUNC DirectSoundEnumerateFunc = NULL ;
			HINSTANCE DSoundDLL = NULL ;
	 
			DXST_LOGFILE_TABADD ;
			_MEMSET( &caps, 0, sizeof( caps ) ) ;
			caps.dwSize = sizeof( caps ) ;
			SoundSysData.PF.DirectSoundObject->GetCaps( &caps ) ;

			DXST_LOGFILE_ADDUTF16LE( "\x44\x00\x69\x00\x72\x00\x65\x00\x63\x00\x74\x00\x53\x00\x6f\x00\x75\x00\x6e\x00\x64\x00\x20\x00\xc7\x30\xd0\x30\xa4\x30\xb9\x30\x92\x30\x17\x52\x19\x63\x57\x30\x7e\x30\x59\x30\x0a\x00\x00"/*@ L"DirectSound �f�o�C�X��񋓂��܂�\n" @*/ ) ;
			DXST_LOGFILE_TABADD ;
			
			// DirectSound DLL �����[�h����
			if( ( DSoundDLL = LoadLibraryA( "DSound.DLL" ) ) == NULL )
			{
				DXST_LOGFILE_ADDUTF16LE( "\x44\x00\x53\x00\x4f\x00\x55\x00\x4e\x00\x44\x00\x2e\x00\x44\x00\x4c\x00\x4c\x00\x20\x00\x6e\x30\xed\x30\xfc\x30\xc9\x30\x6b\x30\x31\x59\x57\x65\x57\x30\x7e\x30\x57\x30\x5f\x30\x0a\x00\x00"/*@ L"SoundSysData.DLL �̃��[�h�Ɏ��s���܂���\n" @*/ ) ;
				DXST_LOGFILE_TABSUB ;
				DXST_LOGFILE_TABSUB ;
				goto ENUMEND ;
			}
		
			// �񋓗p�֐��̎擾
			if( ( DirectSoundEnumerateFunc = ( DIRECTSOUNDENUMERATEFUNC )GetProcAddress( DSoundDLL, "DirectSoundEnumerateW" ) ) == NULL )
			{
				FreeLibrary( DSoundDLL );
				DXST_LOGFILE_ADDUTF16LE( "\x44\x00\x69\x00\x72\x00\x65\x00\x63\x00\x74\x00\x53\x00\x6f\x00\x75\x00\x6e\x00\x64\x00\x20\x00\xc7\x30\xd0\x30\xa4\x30\xb9\x30\x6e\x30\x17\x52\x19\x63\x28\x75\xa2\x95\x70\x65\x6e\x30\xdd\x30\xa4\x30\xf3\x30\xbf\x30\xd6\x53\x97\x5f\x6b\x30\x31\x59\x57\x65\x57\x30\x7e\x30\x57\x30\x5f\x30\x0a\x00\x00"/*@ L"DirectSound �f�o�C�X�̗񋓗p�֐��̃|�C���^�擾�Ɏ��s���܂���\n" @*/ ) ;
				DXST_LOGFILE_TABSUB ;
				DXST_LOGFILE_TABSUB ;
				goto ENUMEND ;
			}

			// ��
			DirectSoundEnumerateFunc( DSEnum , NULL ) ;

			DXST_LOGFILE_TABSUB ;

			DXST_LOGFILEFMT_ADDUTF16LE(( "\x00\x67\x27\x59\xb5\x30\xf3\x30\xd7\x30\xea\x30\xf3\x30\xb0\x30\xec\x30\xfc\x30\xc8\x30\x3a\x00\x25\x00\x2e\x00\x32\x00\x66\x00\x4b\x00\x48\x00\x7a\x00\x20\x00\x00\x67\x0f\x5c\xb5\x30\xf3\x30\xd7\x30\xea\x30\xf3\x30\xb0\x30\xec\x30\xfc\x30\xc8\x30\x3a\x00\x25\x00\x2e\x00\x32\x00\x66\x00\x4b\x00\x48\x00\x7a\x00\x20\x00\x00"/*@ L"�ő�T���v�����O���[�g:%.2fKHz �ŏ��T���v�����O���[�g:%.2fKHz " @*/,
						( double )caps.dwMaxSecondarySampleRate / 1000 ,
						( double )caps.dwMinSecondarySampleRate / 1000 )) ;

			DXST_LOGFILEFMT_ADDUTF16LE(( "\xcf\x7d\xb5\x30\xa6\x30\xf3\x30\xc9\x30\xe1\x30\xe2\x30\xea\x30\x18\x98\xdf\x57\x3a\x00\x25\x00\x2e\x00\x32\x00\x66\x00\x4b\x00\x42\x00\x20\x00\x7a\x7a\x4d\x30\xb5\x30\xa6\x30\xf3\x30\xc9\x30\xe1\x30\xe2\x30\xea\x30\x18\x98\xdf\x57\x3a\x00\x25\x00\x2e\x00\x32\x00\x66\x00\x4b\x00\x42\x00\x20\x00\x0a\x00\x00"/*@ L"���T�E���h�������̈�:%.2fKB �󂫃T�E���h�������̈�:%.2fKB \n" @*/,
						( double )caps.dwTotalHwMemBytes / 0x100 ,
						( double )caps.dwFreeHwMemBytes / 0x100 )) ;

			DXST_LOGFILE_ADDUTF16LE( "\x29\x52\x28\x75\xef\x53\xfd\x80\xb5\x30\xf3\x30\xd7\x30\xea\x30\xf3\x30\xb0\x30\xbe\x7c\xa6\x5e\x0a\x00\x00"/*@ L"���p�\�T���v�����O���x\n" @*/ ) ;
			DXST_LOGFILEFMT_ADDW(( L"  Primary    16bit = %s  8bit = %s ",
				( caps.dwFlags & D_DSCAPS_PRIMARY16BIT ? L"OK" : L"NO" ) ,
				( caps.dwFlags & D_DSCAPS_PRIMARY8BIT  ? L"OK" : L"NO" ) )) ;

			DXST_LOGFILEFMT_ADDW(( L"  Secondary  16bit = %s  8bit = %s \n",
				( caps.dwFlags & D_DSCAPS_SECONDARY16BIT ? L"OK" : L"NO" ) ,
				( caps.dwFlags & D_DSCAPS_SECONDARY8BIT  ? L"OK" : L"NO" ) )) ;

			DXST_LOGFILE_ADDUTF16LE( "\x29\x52\x28\x75\xef\x53\xfd\x80\xc1\x30\xe3\x30\xf3\x30\xcd\x30\xeb\x30\x0a\x00\x00"/*@ L"���p�\�`�����l��\n" @*/ ) ;
			DXST_LOGFILEFMT_ADDW(( L"  Primary    MONO = %s   STEREO = %s ",
				( caps.dwFlags & D_DSCAPS_PRIMARYMONO   ? L"OK" : L"NO" ) ,
				( caps.dwFlags & D_DSCAPS_PRIMARYSTEREO ? L"OK" : L"NO" ) )) ;

			DXST_LOGFILEFMT_ADDW(( L"  Secondary  MONO = %s   STEREO = %s \n",
				( caps.dwFlags & D_DSCAPS_SECONDARYMONO   ? L"OK" : L"NO" ) ,
				( caps.dwFlags & D_DSCAPS_SECONDARYSTEREO ? L"OK" : L"NO" ) )) ;

			DXST_LOGFILE_TABSUB ;

			// DirectSound DLL ���������
			FreeLibrary( DSoundDLL );
		}
	}

ENUMEND :

	// �R�c�T�E���h�����p�̃��X�i�[����������
	SoundSysData.PF.X3DAudioListenerConeData = Listener_DirectionalCone ;
	SoundSysData.PF.X3DAudioListenerData.Position.x = 0.0f ;
	SoundSysData.PF.X3DAudioListenerData.Position.y = 0.0f ;
	SoundSysData.PF.X3DAudioListenerData.Position.z = 0.0f ;
	SoundSysData.PF.X3DAudioListenerData.Velocity.x = 0.0f ;
	SoundSysData.PF.X3DAudioListenerData.Velocity.y = 0.0f ;
	SoundSysData.PF.X3DAudioListenerData.Velocity.z = 0.0f ;
	SoundSysData.PF.X3DAudioListenerData.OrientFront.x = 0.0f ;
	SoundSysData.PF.X3DAudioListenerData.OrientFront.y = 0.0f ;
	SoundSysData.PF.X3DAudioListenerData.OrientFront.z = 1.0f ;
	SoundSysData.PF.X3DAudioListenerData.OrientTop.x = 0.0f ;
	SoundSysData.PF.X3DAudioListenerData.OrientTop.y = 1.0f ;
	SoundSysData.PF.X3DAudioListenerData.OrientTop.z = 0.0f ;
	SoundSysData.PF.X3DAudioListenerData.pCone = &SoundSysData.PF.X3DAudioListenerConeData ;

	// �I��
	return 0 ;
}


// �T�E���h�V�X�e��������������֐��̊��ˑ��������s���֐�( ���s�ӏ���ʂP )
extern	int		InitializeSoundSystem_PF_Timing1( void )
{
#ifndef DX_NON_MULTITHREAD
	SETUP_WIN_API

	// �X�g���[���T�E���h�Đ��p�̏������s���X���b�h�̍쐬
	SoundSysData.PF.StreamSoundThreadHandle = NULL ;
	if( WinData.ProcessorNum > 1 )
	{
		SoundSysData.PF.StreamSoundThreadHandle = WinAPIData.Win32Func.CreateThreadFunc( NULL, 0, StreamSoundThreadFunction, NULL, CREATE_SUSPENDED, &SoundSysData.PF.StreamSoundThreadID ) ;
		WinAPIData.Win32Func.SetThreadPriorityFunc( SoundSysData.PF.StreamSoundThreadHandle, THREAD_PRIORITY_TIME_CRITICAL ) ;
		WinAPIData.Win32Func.ResumeThreadFunc( SoundSysData.PF.StreamSoundThreadHandle ) ;
	}

#endif // DX_NON_MULTITHREAD

	if( SoundSysData.PF.EnableASIOFlag )
	{
	}
	else
	if( SoundSysData.PF.DisableWASAPIFlag == FALSE )
	{
		DXST_LOGFILE_ADDUTF16LE( "\x57\x00\x41\x00\x53\x00\x41\x00\x50\x00\x49\x00\x20\x00\x6e\x30\x1d\x52\x1f\x67\x16\x53\x6f\x30\x63\x6b\x38\x5e\x6b\x30\x42\x7d\x86\x4e\x57\x30\x7e\x30\x57\x30\x5f\x30\x0a\x00\x00"/*@ L"WASAPI �̏������͐���ɏI�����܂���\n" @*/ ) ;
	}
	else
	if( SoundSysData.PF.EnableXAudioFlag )
	{
		DXST_LOGFILE_TABSUB ;
		DXST_LOGFILE_ADDUTF16LE( "\x58\x00\x41\x00\x75\x00\x64\x00\x69\x00\x6f\x00\x32\x00\x20\x00\x6e\x30\x1d\x52\x1f\x67\x16\x53\x6f\x30\x63\x6b\x38\x5e\x6b\x30\x42\x7d\x86\x4e\x57\x30\x7e\x30\x57\x30\x5f\x30\x0a\x00\x00"/*@ L"XAudio2 �̏������͐���ɏI�����܂���\n" @*/ ) ;
	}
	else
	{
		DXST_LOGFILE_ADDUTF16LE( "\x44\x00\x69\x00\x72\x00\x65\x00\x63\x00\x74\x00\x53\x00\x6f\x00\x75\x00\x6e\x00\x64\x00\x20\x00\x6e\x30\x1d\x52\x1f\x67\x16\x53\x6f\x30\x63\x6b\x38\x5e\x6b\x30\x42\x7d\x86\x4e\x57\x30\x7e\x30\x57\x30\x5f\x30\x0a\x00\x00"/*@ L"DirectSound �̏������͐���ɏI�����܂���\n" @*/ ) ;
	}

	// ����I��
	return 0 ;
}




// �T�E���h�V�X�e���̌�n��������֐��̊��ˑ��������s���֐�( ���s�ӏ���ʂO )
extern	int		TerminateSoundSystem_PF_Timing0( void )
{
#ifndef DX_NON_MULTITHREAD
	SETUP_WIN_API

	// �X�g���[���T�E���h�Đ��p�X���b�h���I������
	if( SoundSysData.PF.StreamSoundThreadHandle != NULL )
	{
		SoundSysData.PF.StreamSoundThreadEndFlag = 1 ;
		while( NS_ProcessMessage() == 0 && SoundSysData.PF.StreamSoundThreadEndFlag == 1 ) WinAPIData.Win32Func.SleepFunc( 2 ) ;
	}

	// WASAPI�p�X���b�h���I������
	if( SoundSysData.PF.WASAPI_ThreadHandle != NULL )
	{
		SoundSysData.PF.WASAPI_ThreadLoop = FALSE ;
		WinAPIData.Win32Func.WaitForSingleObjectFunc( SoundSysData.PF.WASAPI_ThreadHandle, INFINITE ) ;
		WinAPIData.Win32Func.CloseHandleFunc( SoundSysData.PF.WASAPI_ThreadHandle ) ;
		SoundSysData.PF.WASAPI_ThreadHandle = NULL ;
	}

#endif // DX_NON_MULTITHREAD

	// ����I��
	return 0 ;
}


// �T�E���h�V�X�e���̌�n��������֐��̊��ˑ��������s���֐�( ���s�ӏ���ʂP )
extern	int		TerminateSoundSystem_PF_Timing1( void )
{
	SETUP_WIN_API

	// �e���|�����t�@�C���̍폜
	if( MidiSystemData.FileName[ 0 ] != L'\0' )
	{
		DeleteFileWFunc( MidiSystemData.FileName ) ;
	}

	// �Đ����[�h�ɂ���ď����𕪊�
	switch( SoundSysData.SoundMode )
	{
	case DX_MIDIMODE_MCI :
		// �ʏ�̍Đ����[�h�̎��̏���

		// PlayWav�֐��ōĐ�����Ă���T�E���h�͂Ȃ��̂�-1���Z�b�g���Ă���
		SoundSysData.PlayWavSoundHandle = -1 ;

		break ;

	case DX_MIDIMODE_DIRECT_MUSIC_REVERB :
	case DX_MIDIMODE_DIRECT_MUSIC_NORMAL :
		// DirectMusic �ɂ�鉉�t���s���Ă����ꍇ�̏���

		// ���ׂẴT�E���h�̍Đ����~�߂�
		if( SoundSysData.PF.DirectMusicPerformanceObject )
		{
			SoundSysData.PF.DirectMusicPerformanceObject->Stop( NULL , NULL , 0 , 0 ) ;
			SoundSysData.PF.DirectMusicPerformanceObject->CloseDown() ;
			SoundSysData.PF.DirectMusicPerformanceObject->Release() ;

			SoundSysData.PF.DirectMusicPerformanceObject = NULL ;
		}

		// ���[�_�[���������
		if( SoundSysData.PF.DirectMusicLoaderObject )
		{
			SoundSysData.PF.DirectMusicLoaderObject->Release() ; 
			SoundSysData.PF.DirectMusicLoaderObject = NULL ;
		}

		break ;
	}

#ifndef DX_NON_ASIO
	// ASIO ���g�p���Ă��邩�ǂ����ŏ����𕪊�
	if( SoundSysData.PF.EnableASIOFlag )
	{
		if( SoundSysData.PF.ASIO_Driver[ SoundSysData.PF.ASIO_DriverUseIndex ] != NULL )
		{
			SoundSysData.PF.ASIO_Driver[ SoundSysData.PF.ASIO_DriverUseIndex ]->stop() ;
			SoundSysData.PF.ASIO_Driver[ SoundSysData.PF.ASIO_DriverUseIndex ]->disposeBuffers() ;
			SoundSysData.PF.ASIO_Driver[ SoundSysData.PF.ASIO_DriverUseIndex ]->Release() ;
			SoundSysData.PF.ASIO_Driver[ SoundSysData.PF.ASIO_DriverUseIndex ] = NULL ;
		}
	}
#endif // DX_NON_ASIO
	// WASAPI ���g�p���Ă��邩�ǂ����ŏ����𕪊�
	if( SoundSysData.PF.DisableWASAPIFlag == FALSE )
	{
		if( SoundSysData.PF.AudioRenderClient != NULL )
		{
			SoundSysData.PF.AudioRenderClient->Release() ;
			SoundSysData.PF.AudioRenderClient = NULL ;
		}

		if( SoundSysData.PF.WASAPI_Event != NULL )
		{
			WinAPIData.Win32Func.CloseHandleFunc( SoundSysData.PF.WASAPI_Event ) ;
			SoundSysData.PF.WASAPI_Event = NULL ;
		}

		if( SoundSysData.PF.AudioClient != NULL )
		{
			SoundSysData.PF.AudioClient->Release() ;
			SoundSysData.PF.AudioClient = NULL ;
		}

		if( SoundSysData.PF.IMMDevice != NULL )
		{
			SoundSysData.PF.IMMDevice->Release() ;
			SoundSysData.PF.IMMDevice = NULL ;
		}

		if( SoundSysData.PF.IMMDeviceEnumerator != NULL )
		{
			SoundSysData.PF.IMMDeviceEnumerator->Release() ;
			SoundSysData.PF.IMMDeviceEnumerator = NULL ;
		}
	}
	else
	// XAudio2 ���g�p���Ă��邩�ǂ����ŏ����𕪊�
	if( SoundSysData.PF.EnableXAudioFlag )
	{
		// �}�X�^�����O�{�C�X�̉��
		if( SoundSysData.PF.XAudio2MasteringVoiceObject != NULL )
		{
			SoundSysData.PF.XAudio2MasteringVoiceObject->DestroyVoice() ;
			SoundSysData.PF.XAudio2MasteringVoiceObject = NULL ;
		}
		if( SoundSysData.PF.XAudio2_8MasteringVoiceObject != NULL )
		{
			SoundSysData.PF.XAudio2_8MasteringVoiceObject->DestroyVoice() ;
			SoundSysData.PF.XAudio2_8MasteringVoiceObject = NULL ;
		}

		// XAudio2�̉��
		if( SoundSysData.PF.XAudio2Object != NULL )
		{
			SoundSysData.PF.XAudio2Object->Release() ;
			SoundSysData.PF.XAudio2Object = NULL ;
		}

		// XAudio2.dll�̌�n������
		if( SoundSysData.PF.XAudio2_8DLL != NULL )
		{
			FreeLibrary( SoundSysData.PF.XAudio2_8DLL ) ;
			SoundSysData.PF.XAudio2_8DLL = NULL ;
			SoundSysData.PF.XAudio2CreateFunc = NULL ;
			SoundSysData.PF.CreateAudioVolumeMeterFunc = NULL ;
			SoundSysData.PF.CreateAudioReverbFunc = NULL ;
			SoundSysData.PF.X3DAudioInitializeFunc = NULL ;
			SoundSysData.PF.X3DAudioCalculateFunc = NULL ;
		}

		// X3DAudio�̌�n������
		if( SoundSysData.PF.X3DAudioDLL != NULL )
		{
			FreeLibrary( SoundSysData.PF.X3DAudioDLL ) ;
			SoundSysData.PF.X3DAudioDLL = NULL ;
			SoundSysData.PF.X3DAudioInitializeFunc = NULL ;
			SoundSysData.PF.X3DAudioCalculateFunc = NULL ;
		}
	}
	else
	{
		// �v���C�}���T�E���h�o�b�t�@�̍Đ���~�A�j��
		if( SoundSysData.PF.PrimarySoundBuffer )
		{
			SoundSysData.PF.PrimarySoundBuffer->Stop() ;
			SoundSysData.PF.PrimarySoundBuffer->Release() ;
			SoundSysData.PF.PrimarySoundBuffer = NULL ;
		}

		// �����o�b�t�@�̍Đ���~�A�j��
		if( SoundSysData.PF.NoSoundBuffer )
		{
			SoundSysData.PF.NoSoundBuffer->Stop() ;
			SoundSysData.PF.NoSoundBuffer->Release() ;
			SoundSysData.PF.NoSoundBuffer = NULL ;
		}

		// �c�����������r���������I�u�W�F�N�g�̉��
		if( SoundSysData.PF.DirectSoundObject > (D_IDirectSound *)1 )
		{
			SoundSysData.PF.DirectSoundObject->Release() ;
		}
		SoundSysData.PF.DirectSoundObject = NULL ;


		DXST_LOGFILE_ADDUTF16LE( "\x44\x00\x69\x00\x72\x00\x65\x00\x63\x00\x74\x00\x53\x00\x6f\x00\x75\x00\x6e\x00\x64\x00\x20\x00\x6e\x30\x42\x7d\x86\x4e\xe6\x51\x06\x74\x6f\x30\x63\x6b\x38\x5e\x6b\x30\x42\x7d\x86\x4e\x57\x30\x7e\x30\x57\x30\x5f\x30\x0a\x00\x00"/*@ L"DirectSound �̏I�������͐���ɏI�����܂���\n" @*/ ) ;
	}

	// ����I��
	return 0 ;
}

// �T�E���h�V�X�e���̏������`�F�b�N�̊��ˑ��������s���֐�( TRUE:����������Ă���  FALSE:����������Ă��Ȃ� )
extern	int		CheckSoundSystem_Initialize_PF( void )
{
	return SoundSysData.PF.DirectSoundObject == NULL ? FALSE : TRUE ;
}

// �T�E���h�V�X�e���̑��Đ����Ԃ��擾����
extern	int GetSoundSystemTotalPlaySamples_PF( ULONGLONG *TotalPlaySamples, ULONGLONG *Frequency )
{
	if( SoundSysData.PF.AudioClock != NULL )
	{
		UINT64 Pos, Freq ;

		SoundSysData.PF.AudioClock->GetFrequency( &Freq ) ;
		SoundSysData.PF.AudioClock->GetPosition( &Pos, NULL ) ;
		*TotalPlaySamples = ( ULONGLONG )Pos ; // _DTOL64( ( double )Pos * SoundSysData.SelfMixingFormat.nSamplesPerSec / Freq ) ;
		*Frequency = ( ULONGLONG )Freq ;

		return 0 ;
	}

	return -1 ;
}

// �l�h�c�h�n���h���̌�n�����s���֐��̊��ˑ�����
extern	int		TerminateMidiHandle_PF( MIDIHANDLEDATA *MusicData )
{
	// DirectMusicSegment8 �I�u�W�F�N�g�̉��
	if( MusicData->PF.DirectMusicSegmentObject != NULL )
	{
		// ���F�f�[�^�̉��
		MusicData->PF.DirectMusicSegmentObject->Unload( SoundSysData.PF.DirectMusicPerformanceObject ) ;

		// �I�u�W�F�N�g�̉��
		SoundSysData.PF.DirectMusicLoaderObject->ReleaseObjectByUnknown( MusicData->PF.DirectMusicSegmentObject ) ;
		MusicData->PF.DirectMusicSegmentObject->Release() ;
		MusicData->PF.DirectMusicSegmentObject = NULL ;
	}

	// ����I��
	return 0 ;
}

// �v���Z�b�g�̂R�c�T�E���h�p�̃��o�[�u�p�����[�^���擾���鏈���̊��ˑ��������s���֐�
extern	int		Get3DPresetReverbParamSoundMem_PF( SOUND3D_REVERB_PARAM *ParamBuffer, int PresetNo /* DX_REVERB_PRESET_DEFAULT �� */ )
{
	if( SoundSysData.PF.EnableXAudioFlag == FALSE )
		return -1 ;

	// �p�����[�^���R�s�[
	if( ParamBuffer != NULL )
	{
		if( SoundSysData.PF.XAudio2_8DLL != NULL )
		{
			D_XAUDIO2FX_REVERB_PARAMETERS2_8 *Param2_8 ;

			Param2_8 = &SoundSysData.PF.XAudio2_8ReverbParameters[ PresetNo ] ;

			ParamBuffer->WetDryMix           = Param2_8->WetDryMix ;

			ParamBuffer->ReflectionsDelay    = Param2_8->ReflectionsDelay;
			ParamBuffer->ReverbDelay         = Param2_8->ReverbDelay ;
			ParamBuffer->RearDelay           = Param2_8->RearDelay ;

			ParamBuffer->PositionLeft        = Param2_8->PositionLeft ;
			ParamBuffer->PositionRight       = Param2_8->PositionRight ;
			ParamBuffer->PositionMatrixLeft  = Param2_8->PositionMatrixLeft ;
			ParamBuffer->PositionMatrixRight = Param2_8->PositionMatrixRight ;
			ParamBuffer->EarlyDiffusion      = Param2_8->EarlyDiffusion ;
			ParamBuffer->LateDiffusion       = Param2_8->LateDiffusion ;
			ParamBuffer->LowEQGain           = Param2_8->LowEQGain ;
			ParamBuffer->LowEQCutoff         = Param2_8->LowEQCutoff ;
			ParamBuffer->HighEQGain          = Param2_8->HighEQGain ;
			ParamBuffer->HighEQCutoff        = Param2_8->HighEQCutoff ;

			ParamBuffer->RoomFilterFreq      = Param2_8->RoomFilterFreq ;
			ParamBuffer->RoomFilterMain      = Param2_8->RoomFilterMain ;
			ParamBuffer->RoomFilterHF        = Param2_8->RoomFilterHF ;
			ParamBuffer->ReflectionsGain     = Param2_8->ReflectionsGain ;
			ParamBuffer->ReverbGain          = Param2_8->ReverbGain ;
			ParamBuffer->DecayTime           = Param2_8->DecayTime ;
			ParamBuffer->Density             = Param2_8->Density ;
			ParamBuffer->RoomSize            = Param2_8->RoomSize ;
		}
		else
		{
			D_XAUDIO2FX_REVERB_PARAMETERS *Param ;

			Param = &SoundSysData.PF.XAudio2ReverbParameters[ PresetNo ] ;

			ParamBuffer->WetDryMix           = Param->WetDryMix ;

			ParamBuffer->ReflectionsDelay    = Param->ReflectionsDelay;
			ParamBuffer->ReverbDelay         = Param->ReverbDelay ;
			ParamBuffer->RearDelay           = Param->RearDelay ;

			ParamBuffer->PositionLeft        = Param->PositionLeft ;
			ParamBuffer->PositionRight       = Param->PositionRight ;
			ParamBuffer->PositionMatrixLeft  = Param->PositionMatrixLeft ;
			ParamBuffer->PositionMatrixRight = Param->PositionMatrixRight ;
			ParamBuffer->EarlyDiffusion      = Param->EarlyDiffusion ;
			ParamBuffer->LateDiffusion       = Param->LateDiffusion ;
			ParamBuffer->LowEQGain           = Param->LowEQGain ;
			ParamBuffer->LowEQCutoff         = Param->LowEQCutoff ;
			ParamBuffer->HighEQGain          = Param->HighEQGain ;
			ParamBuffer->HighEQCutoff        = Param->HighEQCutoff ;

			ParamBuffer->RoomFilterFreq      = Param->RoomFilterFreq ;
			ParamBuffer->RoomFilterMain      = Param->RoomFilterMain ;
			ParamBuffer->RoomFilterHF        = Param->RoomFilterHF ;
			ParamBuffer->ReflectionsGain     = Param->ReflectionsGain ;
			ParamBuffer->ReverbGain          = Param->ReverbGain ;
			ParamBuffer->DecayTime           = Param->DecayTime ;
			ParamBuffer->Density             = Param->Density ;
			ParamBuffer->RoomSize            = Param->RoomSize ;
		}
	}

	return 0 ;
}

// �R�c�T�E���h�̃��X�i�[�̈ʒu�ƃ��X�i�[�̑O���ʒu�ƃ��X�i�[�̏�����ʒu��ݒ肷�鏈���̊��ˑ��������s���֐�
extern int Set3DSoundListenerPosAndFrontPosAndUpVec_PF( VECTOR /*Position*/, VECTOR /*FrontPosition*/, VECTOR /*UpVector*/ )
{
	SoundSysData.PF.X3DAudioListenerData.Position.x = SoundSysData.ListenerInfo.Position.x ;
	SoundSysData.PF.X3DAudioListenerData.Position.y = SoundSysData.ListenerInfo.Position.y ;
	SoundSysData.PF.X3DAudioListenerData.Position.z = SoundSysData.ListenerInfo.Position.z ;

	SoundSysData.PF.X3DAudioListenerData.OrientFront.x = SoundSysData.ListenerInfo.FrontDirection.x ;
	SoundSysData.PF.X3DAudioListenerData.OrientFront.y = SoundSysData.ListenerInfo.FrontDirection.y ;
	SoundSysData.PF.X3DAudioListenerData.OrientFront.z = SoundSysData.ListenerInfo.FrontDirection.z ;

	SoundSysData.PF.X3DAudioListenerData.OrientTop.x = SoundSysData.ListenerInfo.UpDirection.x ;
	SoundSysData.PF.X3DAudioListenerData.OrientTop.y = SoundSysData.ListenerInfo.UpDirection.y ;
	SoundSysData.PF.X3DAudioListenerData.OrientTop.z = SoundSysData.ListenerInfo.UpDirection.z ;

	return 0 ;
}

// �R�c�T�E���h�̃��X�i�[�̈ړ����x��ݒ肷�鏈���̊��ˑ��������s���֐�
extern int Set3DSoundListenerVelocity_PF( VECTOR Velocity )
{
	SoundSysData.PF.X3DAudioListenerData.Velocity.x = Velocity.x ;
	SoundSysData.PF.X3DAudioListenerData.Velocity.y = Velocity.y ;
	SoundSysData.PF.X3DAudioListenerData.Velocity.z = Velocity.z ;

	return 0 ;
}

// �R�c�T�E���h�̃��X�i�[�̉��p�x�͈͂�ݒ肷�鏈���̊��ˑ��������s���֐�
extern int Set3DSoundListenerConeAngle_PF( float InnerAngle, float OuterAngle )
{
	SoundSysData.PF.X3DAudioListenerConeData.InnerAngle = InnerAngle ;
	SoundSysData.PF.X3DAudioListenerConeData.OuterAngle = OuterAngle ;

	return 0 ;
}

// �R�c�T�E���h�̃��X�i�[�̉��p�x�͈͂̉��ʔ{����ݒ肷�鏈���̊��ˑ��������s���֐�
extern int Set3DSoundListenerConeVolume_PF( float InnerAngleVolume, float OuterAngleVolume )
{
	SoundSysData.PF.X3DAudioListenerConeData.InnerVolume = InnerAngleVolume ;
	SoundSysData.PF.X3DAudioListenerConeData.OuterVolume = OuterAngleVolume ;

	return 0 ;
}

// �T�E���h�����\�[�X����ǂݍ���
extern int NS_LoadSoundMemByResource( const TCHAR *ResourceName, const TCHAR *ResourceType, int BufferNum )
{
#ifdef UNICODE
	return LoadSoundMemByResource_WCHAR_T(
		ResourceName, ResourceType, BufferNum
	) ;
#else
	int Result = -1 ;

	int IntResourceName ;
	int IntResourceType ;
	TCHAR_TO_WCHAR_T_STRING_BEGIN( ResourceName )
	TCHAR_TO_WCHAR_T_STRING_BEGIN( ResourceType )

	IntResourceName = ( ( ( DWORD_PTR )ResourceName ) >> 16 ) == 0 ? TRUE : FALSE ;
	IntResourceType = ( ( ( DWORD_PTR )ResourceType ) >> 16 ) == 0 ? TRUE : FALSE ;

	if( IntResourceName )
	{
		UseResourceNameBuffer = ( wchar_t * )ResourceName ;
	}
	else
	{
		TCHAR_TO_WCHAR_T_STRING_SETUP( ResourceName, goto ERR )
	}

	if( IntResourceType )
	{
		UseResourceTypeBuffer = ( wchar_t * )ResourceType ;
	}
	else
	{
		TCHAR_TO_WCHAR_T_STRING_SETUP( ResourceType, goto ERR )
	}

	Result = LoadSoundMemByResource_WCHAR_T(
		UseResourceNameBuffer, UseResourceTypeBuffer, BufferNum
	) ;

ERR :

	if( IntResourceName == FALSE )
	{
		TCHAR_TO_WCHAR_T_STRING_END( ResourceName )
	}

	if( IntResourceType == FALSE )
	{
		TCHAR_TO_WCHAR_T_STRING_END( ResourceType )
	}

	return Result ;
#endif
}

// �T�E���h���\�[�X����T�E���h�n���h�����쐬����
extern int NS_LoadSoundMemByResourceWithStrLen( const TCHAR *ResourceName, size_t ResourceNameLength, const TCHAR *ResourceType, size_t ResourceTypeLength, int BufferNum )
{
	int Result = -1 ;
	TCHAR_STRING_WITH_STRLEN_TO_TCHAR_STRING_BEGIN( ResourceName )
	TCHAR_STRING_WITH_STRLEN_TO_TCHAR_STRING_BEGIN( ResourceType )
	TCHAR_STRING_WITH_STRLEN_TO_TCHAR_STRING_SETUP( ResourceName, ResourceNameLength, goto ERR )
	TCHAR_STRING_WITH_STRLEN_TO_TCHAR_STRING_SETUP( ResourceType, ResourceTypeLength, goto ERR )

	Result = NS_LoadSoundMemByResource( UseResourceNameBuffer, UseResourceTypeBuffer, BufferNum ) ;

ERR :

	TCHAR_STRING_WITH_STRLEN_TO_TCHAR_STRING_END( ResourceName )
	TCHAR_STRING_WITH_STRLEN_TO_TCHAR_STRING_END( ResourceType )
	return Result ;
}

// �T�E���h�����\�[�X����ǂݍ���
extern int LoadSoundMemByResource_WCHAR_T( const wchar_t *ResourceName, const wchar_t *ResourceType, int BufferNum )
{
	void *Image ;
	size_t ImageSize ;

	// ���\�[�X�̏����擾
	if( GetResourceInfo_WCHAR_T( ResourceName, ResourceType, &Image, &ImageSize ) < 0 )
	{
		return -1 ;
	}

	// �n���h���̍쐬
	return NS_LoadSoundMemByMemImageBase( Image, ImageSize, BufferNum, -1 ) ;
}

// LoadMusicMemByResource �̎������֐�
static int LoadMusicMemByResource_Static(
	int MusicHandle,
	const wchar_t *ResourceName,
	const wchar_t *ResourceType,
	int ASyncThread
)
{
	MIDIHANDLEDATA * MusicData ;
	void *Image ;
	size_t ImageSize ;

	if( ASyncThread )
	{
		if( MIDI_MASKHCHK_ASYNC( MusicHandle, MusicData ) )
			return -1 ;
	}
	else
	{
		if( MIDI_MASKHCHK( MusicHandle, MusicData ) )
			return -1 ;
	}

	// ���\�[�X�̏����擾
	if( GetResourceInfo_WCHAR_T( ResourceName, ResourceType, &Image, &ImageSize ) < 0 )
	{
		return -1 ;
	}

	// �n���h���̃Z�b�g�A�b�v
	return LoadMusicMemByMemImage_Static( MusicHandle, Image, ImageSize, ASyncThread ) ;
}

#ifndef DX_NON_ASYNCLOAD
// LoadMusicMemByResource �̔񓯊��ǂݍ��݃X���b�h����Ă΂��֐�
static void LoadMusicMemByResource_ASync( ASYNCLOADDATA_COMMON *AParam )
{
	int MusicHandle ;
	const wchar_t *ResourceName ;
	const wchar_t *ResourceType ;
	int Addr ;
	int Result ;

	Addr = 0 ;
	MusicHandle = GetASyncLoadParamInt( AParam->Data, &Addr ) ;
	ResourceName = GetASyncLoadParamString( AParam->Data, &Addr ) ;
	ResourceType = GetASyncLoadParamString( AParam->Data, &Addr ) ;

	Result = LoadMusicMemByResource_Static( MusicHandle, ResourceName, ResourceType, TRUE ) ;

	DecASyncLoadCount( MusicHandle ) ;
	if( Result < 0 )
	{
		SubHandle( MusicHandle ) ;
	}
}
#endif // DX_NON_ASYNCLOAD

// LoadMusicMemByResource �̃O���[�o���ϐ��ɃA�N�Z�X���Ȃ��o�[�W����
extern int LoadMusicMemByResource_UseGParam(
	const wchar_t *ResourceName,
	const wchar_t *ResourceType,
	int ASyncLoadFlag
)
{
	int MusicHandle ;

	if( SoundSysData.InitializeFlag == FALSE )
		return -1 ;

	// �\�t�g����A�N�e�B�u�̏ꍇ�̓A�N�e�B�u�ɂȂ�܂ő҂�
	CheckActiveState() ;

	// �n���h���̍쐬
	MusicHandle = AddHandle( DX_HANDLETYPE_MUSIC, FALSE, -1 ) ;
	if( MusicHandle == -1 ) return -1 ;

#ifndef DX_NON_ASYNCLOAD
	if( ASyncLoadFlag )
	{
		ASYNCLOADDATA_COMMON *AParam = NULL ;
		int Addr ;

		// �p�����[�^�ɕK�v�ȃ������̃T�C�Y���Z�o
		Addr = 0 ;
		AddASyncLoadParamInt( NULL, &Addr, MusicHandle ) ;
		AddASyncLoadParamString( NULL, &Addr, ResourceName ) ;
		AddASyncLoadParamString( NULL, &Addr, ResourceType ) ;

		// �������̊m��
		AParam = AllocASyncLoadDataMemory( Addr ) ;
		if( AParam == NULL )
			goto ERR ;

		// �����ɕK�v�ȏ����Z�b�g
		AParam->ProcessFunction = LoadMusicMemByResource_ASync ;
		Addr = 0 ;
		AddASyncLoadParamInt( AParam->Data, &Addr, MusicHandle ) ;
		AddASyncLoadParamString( AParam->Data, &Addr, ResourceName ) ;
		AddASyncLoadParamString( AParam->Data, &Addr, ResourceType ) ;

		// �f�[�^��ǉ�
		if( AddASyncLoadData( AParam ) < 0 )
		{
			DXFREE( AParam ) ;
			AParam = NULL ;
			goto ERR ;
		}

		// �񓯊��ǂݍ��݃J�E���g���C���N�������g
		IncASyncLoadCount( MusicHandle, AParam->Index ) ;
	}
	else
#endif // DX_NON_ASYNCLOAD
	{
		if( LoadMusicMemByResource_Static( MusicHandle, ResourceName, ResourceType, FALSE ) < 0 )
			goto ERR ;
	}

	// �I��
	return MusicHandle ;

ERR :
	SubHandle( MusicHandle ) ;

	return -1 ;
}

// ���\�[�X��̂l�h�c�h�t�@�C����ǂݍ���
extern int NS_LoadMusicMemByResource( const TCHAR *ResourceName, const TCHAR *ResourceType )
{
#ifdef UNICODE
	return LoadMusicMemByResource_WCHAR_T(
		ResourceName, ResourceType
	) ;
#else
	int Result = -1 ;

	int IntResourceName ;
	int IntResourceType ;
	TCHAR_TO_WCHAR_T_STRING_BEGIN( ResourceName )
	TCHAR_TO_WCHAR_T_STRING_BEGIN( ResourceType )

	IntResourceName = ( ( ( DWORD_PTR )ResourceName ) >> 16 ) == 0 ? TRUE : FALSE ;
	IntResourceType = ( ( ( DWORD_PTR )ResourceType ) >> 16 ) == 0 ? TRUE : FALSE ;

	if( IntResourceName )
	{
		UseResourceNameBuffer = ( wchar_t * )ResourceName ;
	}
	else
	{
		TCHAR_TO_WCHAR_T_STRING_SETUP( ResourceName, goto ERR )
	}

	if( IntResourceType )
	{
		UseResourceTypeBuffer = ( wchar_t * )ResourceType ;
	}
	else
	{
		TCHAR_TO_WCHAR_T_STRING_SETUP( ResourceType, goto ERR )
	}

	Result = LoadMusicMemByResource_WCHAR_T(
		UseResourceNameBuffer, UseResourceTypeBuffer
	) ;

ERR :

	if( IntResourceName == FALSE )
	{
		TCHAR_TO_WCHAR_T_STRING_END( ResourceName )
	}

	if( IntResourceType == FALSE )
	{
		TCHAR_TO_WCHAR_T_STRING_END( ResourceType )
	}

	return Result ;
#endif
}

// ���\�[�X��̂l�h�c�h�t�@�C������l�h�c�h�n���h�����쐬����
extern int NS_LoadMusicMemByResourceWithStrLen( const TCHAR *ResourceName, size_t ResourceNameLength, const TCHAR *ResourceType, size_t ResourceTypeLength )
{
	int Result = -1 ;
	TCHAR_STRING_WITH_STRLEN_TO_TCHAR_STRING_BEGIN( ResourceName )
	TCHAR_STRING_WITH_STRLEN_TO_TCHAR_STRING_BEGIN( ResourceType )
	TCHAR_STRING_WITH_STRLEN_TO_TCHAR_STRING_SETUP( ResourceName, ResourceNameLength, goto ERR )
	TCHAR_STRING_WITH_STRLEN_TO_TCHAR_STRING_SETUP( ResourceType, ResourceTypeLength, goto ERR )

	Result = NS_LoadMusicMemByResource( UseResourceNameBuffer, UseResourceTypeBuffer ) ;

ERR :

	TCHAR_STRING_WITH_STRLEN_TO_TCHAR_STRING_END( ResourceName )
	TCHAR_STRING_WITH_STRLEN_TO_TCHAR_STRING_END( ResourceType )
	return Result ;
}

// ���\�[�X��̂l�h�c�h�t�@�C����ǂݍ���
extern int LoadMusicMemByResource_WCHAR_T( const wchar_t *ResourceName, const wchar_t *ResourceType )
{
	return LoadMusicMemByResource_UseGParam( ResourceName, ResourceType, GetASyncLoadFlag() ) ;
}

// LoadMusicMemByMemImage �̎������֐��̊��ˑ��������s���֐�
extern int LoadMusicMemByMemImage_Static_PF( MIDIHANDLEDATA *MusicData, int /*ASyncThread*/ )
{
	switch( SoundSysData.SoundMode )
	{
	case DX_MIDIMODE_MCI :
		break ;

	case DX_MIDIMODE_DIRECT_MUSIC_REVERB :
	case DX_MIDIMODE_DIRECT_MUSIC_NORMAL :
		// DirectMusic ���g�p����ꍇ�̓��������� DirectMusicSegment8 ���쐬����
		{
			D_DMUS_OBJECTDESC ObjDesc ;

			_MEMSET( &ObjDesc, 0, sizeof( ObjDesc ) ) ;
			ObjDesc.dwSize = sizeof( D_DMUS_OBJECTDESC ) ;
			ObjDesc.dwValidData = D_DMUS_OBJ_MEMORY | D_DMUS_OBJ_CLASS ;
			ObjDesc.guidClass = CLSID_DIRECTMUSICSEGMENT ;
			ObjDesc.pbMemData = (BYTE *)MusicData->DataImage ;
			ObjDesc.llMemLength = MusicData->DataSize ;

			SoundSysData.PF.DirectMusicLoaderObject->ClearCache( IID_IDIRECTMUSICSEGMENT8 );
//			if( SoundSysData.PF.DirectMusicLoaderObject->GetObject( &ObjDesc, IID_IDirectMusicSegment8, ( void ** )&MusicData->PF.DirectMusicSegmentObject ) != S_OK )
			if( SoundSysData.PF.DirectMusicLoaderObject->GetObject( &ObjDesc, IID_IDIRECTMUSICSEGMENT8, ( void ** )&MusicData->PF.DirectMusicSegmentObject ) != S_OK )
			{
				DXST_LOGFILE_ADDUTF16LE( "\xdf\x30\xe5\x30\xfc\x30\xb8\x30\xc3\x30\xaf\x30\xc7\x30\xfc\x30\xbf\x30\x4b\x30\x89\x30\x20\x00\x44\x00\x69\x00\x72\x00\x65\x00\x63\x00\x74\x00\x4d\x00\x75\x00\x73\x00\x69\x00\x63\x00\x53\x00\x65\x00\x67\x00\x6d\x00\x65\x00\x6e\x00\x74\x00\x38\x00\x20\x00\x92\x30\xd6\x53\x97\x5f\x59\x30\x8b\x30\xe6\x51\x06\x74\x4c\x30\x31\x59\x57\x65\x57\x30\x7e\x30\x57\x30\x5f\x30\x0a\x00\x00"/*@ L"�~���[�W�b�N�f�[�^���� DirectMusicSegment8 ���擾���鏈�������s���܂���\n" @*/ ) ;
				return -1 ;
			}

			// �f�[�^�`���� MidiSystemData �ɃZ�b�g
			MusicData->PF.DirectMusicSegmentObject->SetParam( GUID_STANDARDMIDIFILE, 0xFFFFFFFF, 0, 0, NULL);

			// ���F�f�[�^�̃_�E�����[�h
			MusicData->PF.DirectMusicSegmentObject->Download( SoundSysData.PF.DirectMusicPerformanceObject ) ;
		}
		break ;
	}

	return 0 ;
}

// ���\�[�X����l�h�c�h�t�@�C����ǂݍ���ŉ��t����
extern int NS_PlayMusicByResource( const TCHAR *ResourceName, const TCHAR *ResourceType, int PlayType )
{
#ifdef UNICODE
	return PlayMusicByResource_WCHAR_T(
		ResourceName, ResourceType, PlayType
	) ;
#else
	int Result = -1 ;

	int IntResourceName ;
	int IntResourceType ;
	TCHAR_TO_WCHAR_T_STRING_BEGIN( ResourceName )
	TCHAR_TO_WCHAR_T_STRING_BEGIN( ResourceType )

	IntResourceName = ( ( ( DWORD_PTR )ResourceName ) >> 16 ) == 0 ? TRUE : FALSE ;
	IntResourceType = ( ( ( DWORD_PTR )ResourceType ) >> 16 ) == 0 ? TRUE : FALSE ;

	if( IntResourceName )
	{
		UseResourceNameBuffer = ( wchar_t * )ResourceName ;
	}
	else
	{
		TCHAR_TO_WCHAR_T_STRING_SETUP( ResourceName, goto ERR )
	}

	if( IntResourceType )
	{
		UseResourceTypeBuffer = ( wchar_t * )ResourceType ;
	}
	else
	{
		TCHAR_TO_WCHAR_T_STRING_SETUP( ResourceType, goto ERR )
	}

	Result = PlayMusicByResource_WCHAR_T(
		UseResourceNameBuffer, UseResourceTypeBuffer, PlayType
	) ;

ERR :

	if( IntResourceName == FALSE )
	{
		TCHAR_TO_WCHAR_T_STRING_END( ResourceName )
	}

	if( IntResourceType == FALSE )
	{
		TCHAR_TO_WCHAR_T_STRING_END( ResourceType )
	}

	return Result ;
#endif
}

// ���\�[�X����l�h�c�h�t�@�C����ǂݍ���ŉ��t����
extern int NS_PlayMusicByResourceWithStrLen( const TCHAR *ResourceName, size_t ResourceNameLength, const TCHAR *ResourceType, size_t ResourceTypeLength, int PlayType )
{
	int Result = -1 ;
	TCHAR_STRING_WITH_STRLEN_TO_TCHAR_STRING_BEGIN( ResourceName )
	TCHAR_STRING_WITH_STRLEN_TO_TCHAR_STRING_BEGIN( ResourceType )
	TCHAR_STRING_WITH_STRLEN_TO_TCHAR_STRING_SETUP( ResourceName, ResourceNameLength, goto ERR )
	TCHAR_STRING_WITH_STRLEN_TO_TCHAR_STRING_SETUP( ResourceType, ResourceTypeLength, goto ERR )

	Result = NS_PlayMusicByResource( UseResourceNameBuffer, UseResourceTypeBuffer, PlayType ) ;

ERR :

	TCHAR_STRING_WITH_STRLEN_TO_TCHAR_STRING_END( ResourceName )
	TCHAR_STRING_WITH_STRLEN_TO_TCHAR_STRING_END( ResourceType )
	return Result ;
}

// ���\�[�X����l�h�c�h�t�@�C����ǂݍ���ŉ��t����
extern int PlayMusicByResource_WCHAR_T( const wchar_t *ResourceName, const wchar_t *ResourceType, int PlayType )
{
	void *Image ;
	size_t ImageSize ;

	// ���\�[�X�̏����擾
	if( GetResourceInfo_WCHAR_T( ResourceName, ResourceType, &Image, &ImageSize ) < 0 )
	{
		return -1 ;
	}

	// ���t�J�n
	return NS_PlayMusicByMemImage( Image, ImageSize, PlayType ) ;
}

// �ǂݍ��񂾂l�h�c�h�f�[�^�̉��t���J�n���鏈���̊��ˑ��������s���֐�
extern int PlayMusicMem_PF( MIDIHANDLEDATA *MusicData, int PlayType )
{
	SETUP_WIN_API

	switch( SoundSysData.SoundMode )
	{
	case DX_MIDIMODE_MCI :
		{
			int Result ;
			MCI_OPEN_PARMSW		mciOpenParms;
			MCI_PLAY_PARMS		mciPlayParms;
			MCI_STATUS_PARMS	mciStatusParms;
			HANDLE FileHandle ;
			DWORD WriteSize ;

			// �ȑO�̃e���|�����t�@�C�����c���Ă��邩������Ȃ��̂ŁA�ꉞ�폜
			DeleteFileWFunc( MidiSystemData.FileName ) ;

			// �e���|�����t�@�C�����J��
			FileHandle = CreateTemporaryFile( MidiSystemData.FileName, sizeof( MidiSystemData.FileName ) ) ;
			if( FileHandle == NULL ) return -1 ;

			// �e���|�����t�@�C���Ƀf�[�^����������
			WinAPIData.Win32Func.WriteFileFunc( FileHandle, MusicData->DataImage, ( DWORD )MusicData->DataSize, &WriteSize, NULL ) ;
			WinAPIData.Win32Func.CloseHandleFunc( FileHandle ) ;

			// �I�[�v���X�e�[�^�X�Z�b�g
			mciOpenParms.lpstrElementName = MidiSystemData.FileName ;
			mciOpenParms.lpstrDeviceType  = L"sequencer" ;

			// �l�b�h�̃I�[�v��
			Result = ( int )WinAPIData.Win32Func.mciSendCommandFunc( 0, MCI_OPEN, MCI_OPEN_TYPE | MCI_OPEN_ELEMENT, ( DWORD_PTR )( LPVOID )&mciOpenParms ) ;
			if( Result != 0 )
			{
				LOADSOUND_GPARAM GParam ;
				int OldHandle ;

	//			DXST_LOGFILE_ADDUTF16LE( L"�l�b�h�̃I�[�v���Ɏ��s���܂���\n" ) ;

				// �G���[���N������ꎞ�t�@�C�����폜
				DeleteFileWFunc( MidiSystemData.FileName ) ;

				// ���t�Ɏ��s�����畁�ʂ̃T�E���h�t�@�C���̉\��������
				OldHandle = MidiSystemData.DefaultHandle ;
				InitLoadSoundGParam( &GParam ) ;
				GParam.CreateSoundDataType = DX_SOUNDDATATYPE_MEMPRESS ;
				MidiSystemData.DefaultHandle = LoadSoundMemByMemImageBase_UseGParam( &GParam, TRUE, -1, MusicData->DataImage, MusicData->DataSize, 1, -1, 0 ) ;
				if( MidiSystemData.DefaultHandle == -1 )
				{
					// ����ł����s������t�@�C�����Ȃ��Ƃ�������
					MidiSystemData.DefaultHandle = 0;
					return -1 ;
				}

				// ���ʐݒ�
				NS_ChangeVolumeSoundMem( MusicData->Volume, MidiSystemData.DefaultHandle ) ;

				// ���܂ł̃n���h���͍폜
				if( OldHandle != 0 )
				{
					if( MidiSystemData.DefaultHandleToSoundHandleFlag == TRUE )
					{
						NS_DeleteSoundMem( OldHandle, FALSE ) ;
					}
					else
					{
						NS_DeleteMusicMem( OldHandle ) ;
					}
					OldHandle = 0 ;
				}
				MidiSystemData.DefaultHandleToSoundHandleFlag = TRUE ;

				// �Đ��J�n
				NS_PlaySoundMem( MidiSystemData.DefaultHandle, PlayType, TRUE ) ;
				return 0 ;
			}

			// �l�h�c�h�̃f�o�C�X�h�c��ۑ�
			MidiSystemData.PF.MidiDeviceID = mciOpenParms.wDeviceID ;

			// �l�h�c�h�}�b�p�[������
			mciStatusParms.dwItem = MCI_SEQ_STATUS_PORT ;
			if( WinAPIData.Win32Func.mciSendCommandFunc( MidiSystemData.PF.MidiDeviceID , MCI_STATUS , MCI_STATUS_ITEM , ( DWORD_PTR )( LPVOID )&mciStatusParms ) )
			{
				WinAPIData.Win32Func.mciSendCommandFunc( MidiSystemData.PF.MidiDeviceID , MCI_CLOSE , 0 , 0 ) ;
				DXST_LOGFILE_ADDUTF16LE( "\x2d\xff\x29\xff\x24\xff\x29\xff\x14\x6f\x4f\x59\x8b\x95\xcb\x59\xe6\x51\x06\x74\x67\x30\xa8\x30\xe9\x30\xfc\x30\x4c\x30\x77\x8d\x4d\x30\x7e\x30\x57\x30\x5f\x30\x11\xff\x0a\x00\x00"/*@ L"�l�h�c�h���t�J�n�����ŃG���[���N���܂����P\n" @*/ ) ;
				goto MCI_ERROR ;
			}
			if( LOWORD( mciStatusParms.dwReturn ) != LOWORD( MIDI_MAPPER ) )
			{
				WinAPIData.Win32Func.mciSendCommandFunc( MidiSystemData.PF.MidiDeviceID , MCI_CLOSE , 0 , 0 ) ;
				DXST_LOGFILE_ADDUTF16LE( "\x2d\xff\x29\xff\x24\xff\x29\xff\x14\x6f\x4f\x59\x8b\x95\xcb\x59\xe6\x51\x06\x74\x67\x30\xa8\x30\xe9\x30\xfc\x30\x4c\x30\x77\x8d\x4d\x30\x7e\x30\x57\x30\x5f\x30\x12\xff\x0a\x00\x00"/*@ L"�l�h�c�h���t�J�n�����ŃG���[���N���܂����Q\n" @*/ ) ;
				goto MCI_ERROR ;
			}

			// �R�[���o�b�N�Ώۂ����C���E�C���h�E�ɐݒ肵�ĉ��t�J�n
			mciPlayParms.dwCallback = ( DWORD_PTR )NS_GetMainWindowHandle() ;
			if( WinAPIData.Win32Func.mciSendCommandFunc( MidiSystemData.PF.MidiDeviceID , MCI_PLAY , MCI_NOTIFY , ( DWORD_PTR )( LPVOID )&mciPlayParms ) )
			{
				WinAPIData.Win32Func.mciSendCommandFunc( MidiSystemData.PF.MidiDeviceID , MCI_CLOSE , 0 , 0 ) ;
				DXST_LOGFILE_ADDUTF16LE( "\x2d\xff\x29\xff\x24\xff\x29\xff\x14\x6f\x4f\x59\x6b\x30\x31\x59\x57\x65\x57\x30\x7e\x30\x57\x30\x5f\x30\x0a\x00\x00"/*@ L"�l�h�c�h���t�Ɏ��s���܂���\n" @*/ ) ;
				goto MCI_ERROR ;
			}

			// �e���|�����t�@�C������Đ����Ă��邱�Ƃ������t���O�𗧂Ă�
			MidiSystemData.MemImagePlayFlag = TRUE ;
		}
		break ;

	case DX_MIDIMODE_DIRECT_MUSIC_REVERB :
	case DX_MIDIMODE_DIRECT_MUSIC_NORMAL :
		{
			D_REFERENCE_TIME RTime ;
			HRESULT hr ;

			// ���[�v�ݒ�
			MusicData->PF.DirectMusicSegmentObject->SetRepeats( PlayType == DX_PLAYTYPE_LOOP ? D_DMUS_SEG_REPEAT_INFINITE : 0 ) ;

			// ���t�J�n
			hr = SoundSysData.PF.DirectMusicPerformanceObject->PlaySegmentEx( 
				MusicData->PF.DirectMusicSegmentObject,	// ���t����Z�O�����g�B
				NULL,									// �\���O�Ɏg�p����p�����[�^�B��������Ă��Ȃ��B
				NULL,									// �g�����W�V�����Ɋւ���p�����[�^�B
				D_DMUS_SEGF_REFTIME,					// �t���O�B
				0,										// �J�n�^�C���B0�͒����ɊJ�n�B
				NULL,									// �Z�O�����g��Ԃ��󂯎��|�C���^�B
				NULL,									// ��~����I�u�W�F�N�g�B
				NULL									// �f�t�H���g�łȂ��ꍇ�̓I�[�f�B�I�p�X�B
			) ;

			// ���t�J�n���Ԃ̕ۑ�
			SoundSysData.PF.DirectMusicPerformanceObject->GetTime( &RTime, NULL ) ;
			MusicData->StartTime = _DTOL( (double)RTime / 10000.0 ) ;
		}
		// �e���|�����t�@�C������Đ����Ă��邱�Ƃ������t���O��|��
		MidiSystemData.MemImagePlayFlag = FALSE ;
		break ;
	}

	return 0 ;

MCI_ERROR:
	// �G���[���N������ꎞ�t�@�C�����폜���ďI��
	DeleteFileWFunc( MidiSystemData.FileName ) ;

	// �G���[�I��
	return -1 ;
}

// �l�h�c�h�f�[�^�̉��t���~���鏈���̊��ˑ��������s��
extern int StopMusicMem_PF( MIDIHANDLEDATA *MusicData )
{
	int i ;

	SETUP_WIN_API

	switch( SoundSysData.SoundMode )
	{
	case DX_MIDIMODE_MCI :
		// ���t�I���֐����Ă�
		MidiCallBackProcess() ;

		// �E�G�C�g
		for( i = 0 ; i <= 4 ; i++ ) NS_ProcessMessage() ;
		break ;

	case DX_MIDIMODE_DIRECT_MUSIC_REVERB :
	case DX_MIDIMODE_DIRECT_MUSIC_NORMAL :
		// ���t���~����
		SoundSysData.PF.DirectMusicPerformanceObject->StopEx( MusicData->PF.DirectMusicSegmentObject, 0, 0 ) ;
		break ;
	}

	// �e���|�����t�@�C������Đ�����Ă����ꍇ�͍폜����
	if( MidiSystemData.MemImagePlayFlag == TRUE )
	{
		DeleteFileWFunc( MidiSystemData.FileName ) ;
		MidiSystemData.MemImagePlayFlag = FALSE ;
	}

	return 0 ;
}

// �l�h�c�h�f�[�^�����t�����ǂ������擾����( TRUE:���t��  FALSE:��~�� )�����̊��ˑ��������s���֐�
extern int CheckMusicMem_PF( MIDIHANDLEDATA *MusicData )
{
	int Result = -1 ;

	switch( SoundSysData.SoundMode )
	{
	case DX_MIDIMODE_MCI :
		Result = MidiSystemData.PlayFlag ;
		break ;

	case DX_MIDIMODE_DIRECT_MUSIC_REVERB :
	case DX_MIDIMODE_DIRECT_MUSIC_NORMAL :
		Result = MusicData->PlayFlag ;
		break ;
	}

	return Result ;
}

// �l�h�c�h�f�[�^�̎����I�����̊��ˑ��������s���֐�
extern int ProcessMusicMem_PF( MIDIHANDLEDATA *MusicData )
{
	int play ;

	switch( SoundSysData.SoundMode )
	{
	case DX_MIDIMODE_MCI :
		break ;

	case DX_MIDIMODE_DIRECT_MUSIC_REVERB :
	case DX_MIDIMODE_DIRECT_MUSIC_NORMAL :
		play = SoundSysData.PF.DirectMusicPerformanceObject->IsPlaying( MusicData->PF.DirectMusicSegmentObject , NULL ) != S_FALSE ;
		if( MusicData->PlayStartFlag == FALSE )
		{
			// �܂����t���n�܂��Ă��Ȃ������ꍇ�͏�Ԃ����t���ɂȂ������Ƃ�
			// ���߂ĉ��t���Ƃ������ƂɂȂ�
			if( play == TRUE ) MusicData->PlayStartFlag = TRUE ;
		}
		else
		{
			// ���t���n�܂������Ƃ͌��݂̏�Ԃ����̂܂ܔ��f�����
			MusicData->PlayFlag = play ;
		}
		break ;
	}

	return 0 ;
}

// �l�h�c�h�f�[�^�̌��݂̍Đ��ʒu���擾���鏈���̊��ˑ��������s���֐�
extern int GetMusicMemPosition_PF( MIDIHANDLEDATA *MusicData )
{
	int Result = -1 ;

	switch( SoundSysData.SoundMode )
	{
	case DX_MIDIMODE_MCI :
		Result = NS_GetMusicPosition() ;
		break ;

	case DX_MIDIMODE_DIRECT_MUSIC_REVERB :
	case DX_MIDIMODE_DIRECT_MUSIC_NORMAL :
		{
			D_REFERENCE_TIME RTime ;
			D_MUSIC_TIME Time ;
			D_IDirectMusicSegmentState *State ;

			SoundSysData.PF.DirectMusicPerformanceObject->GetTime( &RTime, &Time ) ;
			if( SoundSysData.PF.DirectMusicPerformanceObject->GetSegmentState( &State, Time ) != S_OK )
				return -1 ;

			State->GetSeek( &Time ) ;
			State->Release() ;

			// ���Ԃ�Ԃ�
			Result = _DTOL( (double)RTime / 10000.0 ) - MusicData->StartTime ;
		}
		break ;
	}

	return Result ;
}

// �l�h�c�h�̍Đ����ʂ��Z�b�g���鏈���̊��ˑ��������s���֐�
extern int SetVolumeMusic_PF( int Volume )
{
	long V ;

	switch( SoundSysData.SoundMode )
	{
	case DX_MIDIMODE_MCI :
		// �l�b�h�̏ꍇ�͉��ʂ̕ύX�͏o���Ȃ�
		break ;

	case DX_MIDIMODE_DIRECT_MUSIC_REVERB :
	case DX_MIDIMODE_DIRECT_MUSIC_NORMAL :
		// ���ʂ̃Z�b�g
		V = _DTOL( ( double )( SoundSysData.MaxVolume - DM_MIN_VOLUME ) / 256 * Volume ) + DM_MIN_VOLUME ;
		if( Volume == 255 ) V = SoundSysData.MaxVolume ;
		SoundSysData.PF.DirectMusicPerformanceObject->SetGlobalParam( GUID_PERFMASTERVOLUME , &V , sizeof( long ) ) ;
		
		break ;
	}

	return 0 ;
}

// �l�h�c�h�̌��݂̍Đ��ʒu���擾���鏈���̊��ˑ��������s���֐�
extern int GetMusicPosition_PF( void )
{
	SETUP_WIN_API

	switch( SoundSysData.SoundMode )
	{
	case DX_MIDIMODE_MCI :
		{
			MCI_SET_PARMS mciSetParms ;
			MCI_STATUS_PARMS mciStatusParms ;

			// �擾���鎞�Ԃ̒P�ʂ��~���b�P�ʂɂ���
			_MEMSET( &mciSetParms, 0, sizeof( mciSetParms ) ) ;
			mciSetParms.dwTimeFormat = MCI_FORMAT_MILLISECONDS ;
			if( WinAPIData.Win32Func.mciSendCommandFunc( MidiSystemData.PF.MidiDeviceID, MCI_SET, MCI_SET_TIME_FORMAT, (DWORD_PTR)&mciSetParms ) != 0 )
				return -1 ;

			// ���Ԃ��擾����
			_MEMSET( &mciStatusParms, 0, sizeof( mciStatusParms ) ) ;
			mciStatusParms.dwItem = MCI_STATUS_POSITION ;
			if( WinAPIData.Win32Func.mciSendCommandFunc( MidiSystemData.PF.MidiDeviceID, MCI_STATUS, MCI_STATUS_ITEM, (DWORD_PTR)&mciStatusParms ) != 0 )
				return -1 ;

			// ���Ԃ�Ԃ�
			return ( int )mciStatusParms.dwReturn ;
		}
		break ;

	case DX_MIDIMODE_DIRECT_MUSIC_REVERB :
	case DX_MIDIMODE_DIRECT_MUSIC_NORMAL :
		{
			D_REFERENCE_TIME RTime ;
			D_MUSIC_TIME /*StartTime,*/ Time ;
			D_IDirectMusicSegmentState *State ;

			SoundSysData.PF.DirectMusicPerformanceObject->GetTime( &RTime, &Time ) ;
			if( SoundSysData.PF.DirectMusicPerformanceObject->GetSegmentState( &State, Time ) != S_OK )
				return -1 ;

			State->GetSeek( &Time ) ;
			State->Release() ;

//			SoundSysData.PF.DirectMusicPerformanceObject->MusicToReferenceTime( Time, &RTime ) ;

			// ���Ԃ�Ԃ�
//			return (int)Time ;
			return _DTOL( (double)RTime / 10000.0 ) - MidiSystemData.StartTime ;
		}
		break ;
	}

	return -1 ;
}


















void __stdcall SOUNDBUFFER_CALLBACK::OnVoiceProcessingPassStart( DWORD BytesRequired )
{
	int CompPos = Buffer->CompPos ;
	int Loop = Buffer->Loop ;
	D_XAUDIO2_BUFFER XBuffer = { 0 } ;
	DWORD MoveData ;
	DWORD MaxSendSamples ;
	void *SampleBuffer ;
	int BlockAlign ;

	MaxSendSamples = Buffer->Format.nSamplesPerSec / 60 ;
	SampleBuffer = Buffer->Wave->DoubleSizeBuffer ? Buffer->Wave->DoubleSizeBuffer : Buffer->Wave->Buffer ;
	BlockAlign = Buffer->Format.wBitsPerSample * Buffer->PF.XAudioChannels / 8 ;

	MoveData = 0 ;
	while( MoveData < BytesRequired )
	{
		if( CompPos >= Buffer->SampleNum )
		{
			if( Loop )
			{
				CompPos = 0 ;
				goto COPYDATA ;
			}
			else
			{
				SoundBuffer_Stop( Buffer ) ;
				break ;
			}
		}
		else
		{
			DWORD NowBytes ;
			DWORD AddSamples ;

COPYDATA :
			NowBytes           = ( DWORD )( CompPos * BlockAlign ) ;
			XBuffer.pAudioData = ( BYTE * )SampleBuffer + NowBytes ;
			AddSamples         = ( DWORD )( Buffer->SampleNum - CompPos ) ;
			if( AddSamples > MaxSendSamples )
			{
				AddSamples = MaxSendSamples ;
			}
			XBuffer.AudioBytes = AddSamples * BlockAlign ;
			if( XBuffer.AudioBytes > BytesRequired )
			{
				AddSamples = BytesRequired / BlockAlign ;
				XBuffer.AudioBytes = AddSamples * BlockAlign ;
			}
			CompPos += AddSamples ;
			MoveData += XBuffer.AudioBytes ;

			XBuffer.pContext = ( void * )Buffer ;

			if( SoundSysData.PF.XAudio2_8DLL != NULL )
			{
				Buffer->PF.XA2_8SourceVoice->SubmitSourceBuffer( &XBuffer ) ;
			}
			else
			{
				Buffer->PF.XA2SourceVoice->SubmitSourceBuffer( &XBuffer ) ;
			}
		}
	}
	Buffer->CompPos = CompPos ;
}

void __stdcall SOUNDBUFFER_CALLBACK::OnVoiceProcessingPassEnd()
{
}

void __stdcall SOUNDBUFFER_CALLBACK::OnStreamEnd()
{
}

void __stdcall SOUNDBUFFER_CALLBACK::OnBufferStart( void* /*pBufferContext*/ )
{
}

void __stdcall SOUNDBUFFER_CALLBACK::OnBufferEnd( void* pBufferContext )
{
	SOUNDBUFFER *SBuffer = ( SOUNDBUFFER * )pBufferContext ;

	SBuffer->PlayPos = SBuffer->CompPos ;
}

void __stdcall SOUNDBUFFER_CALLBACK::OnLoopEnd( void* /*pBufferContext*/ )
{
}

void __stdcall SOUNDBUFFER_CALLBACK::OnVoiceError( void* /*pBufferContext*/, HRESULT /*Error*/ )
{
}











extern int SoundBuffer_Initialize_Timing0_PF( SOUNDBUFFER *Buffer, DWORD Bytes, WAVEFORMATEX *Format, SOUNDBUFFER *Src, int Is3DSound )
{
	int i ;
	HRESULT hr ;
	WAVEFORMATEX waveformat ;
	int UseDoubleSizeBuffer = FALSE ;

	Buffer->PF.DSBuffer			= NULL ;
	Buffer->PF.XA2SourceVoice	= NULL ;
	Buffer->PF.XA2_8SourceVoice	= NULL ;
	Buffer->PF.XA2SubmixVoice	= NULL ;
	Buffer->PF.XA2_8SubmixVoice	= NULL ;
	Buffer->PF.XA2ReverbEffect	= NULL ;
	Buffer->PF.XA2Callback		= NULL ;

	if( Src != NULL )
	{
		waveformat = Src->Format ;
		Bytes      = Src->SampleNum * Src->Format.nBlockAlign ;
	}
	else
	{
		waveformat = *Format ;
	}

	if( SoundSysData.PF.EnableXAudioFlag )
	{
		// �R�c�T�E���h���ǂ����ŕ���
		if( Is3DSound != FALSE )
		{
			D_XAUDIO2_EFFECT_DESCRIPTOR effects[ 1 ] ;
			D_XAUDIO2_EFFECT_CHAIN effectChain ;

			// �R�c�T�E���h�̏ꍇ�̓T�u�~�b�N�X�{�C�X���쐬����
			hr = D_XAudio2CreateReverb( &Buffer->PF.XA2ReverbEffect, 0 ) ;
			if( FAILED( hr ) )
				goto ERR ;

			effects[ 0 ].pEffect = Buffer->PF.XA2ReverbEffect ;
			effects[ 0 ].InitialState = TRUE ;
			effects[ 0 ].OutputChannels = waveformat.nChannels ;

			effectChain.EffectCount = 1 ;
			effectChain.pEffectDescriptors = effects ;

			if( SoundSysData.PF.XAudio2_8DLL != NULL )
			{
				if( FAILED( SoundSysData.PF.XAudio2_8Object->CreateSubmixVoice(
					&Buffer->PF.XA2_8SubmixVoice,
					waveformat.nChannels,
					SoundSysData.OutputSmaplesPerSec,
					0,
					0,
					NULL,
					&effectChain ) ) )
					goto ERR ;
				_MEMCPY( &Buffer->PF.XAudio2_8ReverbParameter, &SoundSysData.PF.XAudio2_8ReverbParameters[ D_XAUDIO2FX_PRESET_DEFAULT ], sizeof( SoundSysData.PF.XAudio2_8ReverbParameters[ D_XAUDIO2FX_PRESET_DEFAULT ] ) ) ;
				Buffer->PF.XA2_8SubmixVoice->SetEffectParameters( 0, &Buffer->PF.XAudio2_8ReverbParameter, sizeof( Buffer->PF.XAudio2_8ReverbParameter ) ) ;
				Buffer->PF.XA2_8SubmixVoice->DisableEffect( 0 ) ;
			}
			else
			{
				if( FAILED( SoundSysData.PF.XAudio2Object->CreateSubmixVoice(
					&Buffer->PF.XA2SubmixVoice,
					waveformat.nChannels,
					SoundSysData.OutputSmaplesPerSec,
					0,
					0,
					NULL,
					&effectChain ) ) )
					goto ERR ;
				_MEMCPY( &Buffer->PF.XAudio2ReverbParameter, &SoundSysData.PF.XAudio2ReverbParameters[ D_XAUDIO2FX_PRESET_DEFAULT ], sizeof( SoundSysData.PF.XAudio2ReverbParameters[ D_XAUDIO2FX_PRESET_DEFAULT ] ) ) ;
				Buffer->PF.XA2SubmixVoice->SetEffectParameters( 0, &Buffer->PF.XAudio2ReverbParameter, sizeof( Buffer->PF.XAudio2ReverbParameter ) ) ;
				Buffer->PF.XA2SubmixVoice->DisableEffect( 0 ) ;
			}
		}
		else
		{
			// ���m�����T�E���h�ł��R�c�T�E���h�ł͂Ȃ��ꍇ�̓p���̂��߂ɃX�e���I�ɕύX
			if( waveformat.nChannels == 1 )
			{
				waveformat.nChannels = 2 ;
				waveformat.nBlockAlign = ( WORD )( waveformat.nChannels * waveformat.wBitsPerSample / 8 ) ;
				waveformat.nAvgBytesPerSec = waveformat.nBlockAlign * waveformat.nSamplesPerSec ;
				UseDoubleSizeBuffer = TRUE ;
			}
		}
		Buffer->PF.XAudioChannels = waveformat.nChannels ;

		if( Src != NULL )
		{
			Buffer->Wave = DuplicateWaveData( Src->Wave ) ;
		}
		else
		{
			Buffer->Wave = AllocWaveData( ( int )Bytes, UseDoubleSizeBuffer ) ;
		}
		if( Buffer->Wave == NULL )
			goto ERR ;

		Buffer->PF.XA2Callback = new SOUNDBUFFER_CALLBACK ;
		if( Buffer->PF.XA2Callback == NULL )
			goto ERR ;

		Buffer->PF.XA2Callback->Buffer = Buffer ;

		if( SoundSysData.PF.XAudio2_8DLL != NULL )
		{
			if( Is3DSound != FALSE )
			{
				D_XAUDIO2_VOICE_SENDS2_8 sendList ;
				D_XAUDIO2_SEND_DESCRIPTOR2_8 sendDescriptors[ 2 ] ;

				sendDescriptors[0].Flags = D_XAUDIO2_SEND_USEFILTER ;
				sendDescriptors[0].pOutputVoice = SoundSysData.PF.XAudio2_8MasteringVoiceObject ;
				sendDescriptors[1].Flags = D_XAUDIO2_SEND_USEFILTER ;
				sendDescriptors[1].pOutputVoice = Buffer->PF.XA2_8SubmixVoice ;
				sendList.SendCount = 2 ;
				sendList.pSends = sendDescriptors ;

				hr = SoundSysData.PF.XAudio2_8Object->CreateSourceVoice( &Buffer->PF.XA2_8SourceVoice, &waveformat, 0, D_XAUDIO2_DEFAULT_FREQ_RATIO, Buffer->PF.XA2Callback, &sendList ) ;
			}
			else
			{
				hr = SoundSysData.PF.XAudio2_8Object->CreateSourceVoice( &Buffer->PF.XA2_8SourceVoice, &waveformat, 0, D_XAUDIO2_DEFAULT_FREQ_RATIO, Buffer->PF.XA2Callback ) ;
			}
		}
		else
		{
			if( Is3DSound != FALSE )
			{
				D_XAUDIO2_VOICE_SENDS sendList ;
				D_XAUDIO2_SEND_DESCRIPTOR sendDescriptors[ 2 ] ;

				sendDescriptors[0].Flags = D_XAUDIO2_SEND_USEFILTER ;
				sendDescriptors[0].pOutputVoice = SoundSysData.PF.XAudio2MasteringVoiceObject ;
				sendDescriptors[1].Flags = D_XAUDIO2_SEND_USEFILTER ;
				sendDescriptors[1].pOutputVoice = Buffer->PF.XA2SubmixVoice ;
				sendList.SendCount = 2 ;
				sendList.pSends = sendDescriptors ;

				hr = SoundSysData.PF.XAudio2Object->CreateSourceVoice( &Buffer->PF.XA2SourceVoice, &waveformat, 0, D_XAUDIO2_DEFAULT_FREQ_RATIO, Buffer->PF.XA2Callback, &sendList ) ;
			}
			else
			{
				hr = SoundSysData.PF.XAudio2Object->CreateSourceVoice( &Buffer->PF.XA2SourceVoice, &waveformat, 0, D_XAUDIO2_DEFAULT_FREQ_RATIO, Buffer->PF.XA2Callback ) ;
			}
		}
		
		if( FAILED( hr ) )
			goto ERR ;

		if( Src != NULL )
		{
			for( i = 0 ; i < SOUNDBUFFER_MAX_CHANNEL_NUM ; i ++ )
			{
				SoundBuffer_SetVolume( Buffer, i, Src->Volume[ i ] ) ;
			}
			SoundBuffer_SetFrequency( Buffer, ( DWORD )Src->Frequency ) ;
			SoundBuffer_SetPan( Buffer, Src->Pan ) ;
		}
	}
	else
	{
		D_DSBUFFERDESC Desc ;

		_MEMSET( &Desc, 0, sizeof( Desc ) ) ;
		Desc.dwSize			= sizeof( Desc ) ;
		Desc.dwFlags		= ( DWORD )( D_DSBCAPS_GLOBALFOCUS | D_DSBCAPS_CTRLPAN | D_DSBCAPS_CTRLVOLUME | D_DSBCAPS_CTRLFREQUENCY | D_DSBCAPS_GETCURRENTPOSITION2 | ( SoundSysData.PF.UseSoftwareMixing ? D_DSBCAPS_LOCSOFTWARE : D_DSBCAPS_STATIC ) ) ;
		Desc.dwBufferBytes	= Bytes ;
		Desc.lpwfxFormat	= &waveformat ;

		if( SoundSysData.PF.DirectSoundObject->CreateSoundBuffer( &Desc, &Buffer->PF.DSBuffer, NULL ) != D_DS_OK )
		{
			// �쐬�Ɏ��s�����ꍇ�́A�܂߂�@�\�����炵�čēx�쐬����
			_MEMSET( &Desc, 0, sizeof( Desc ) ) ;
			Desc.dwSize			= sizeof( Desc ) ;
			Desc.dwFlags		= ( DWORD )( D_DSBCAPS_CTRLPAN | D_DSBCAPS_CTRLVOLUME | ( SoundSysData.PF.UseSoftwareMixing ? D_DSBCAPS_LOCSOFTWARE : D_DSBCAPS_STATIC ) ) ;
			Desc.dwBufferBytes	= Bytes ;
			Desc.lpwfxFormat	= &waveformat ;
			if( SoundSysData.PF.DirectSoundObject->CreateSoundBuffer( &Desc, &Buffer->PF.DSBuffer, NULL ) != D_DS_OK )
			{
				goto ERR ;
			}
		}
	}
	
	return 0 ;

ERR :
	if( SoundSysData.PF.EnableXAudioFlag )
	{
		if( SoundSysData.PF.XAudio2_8DLL != NULL )
		{
			if( Buffer->PF.XA2_8SourceVoice != NULL )
			{
				Buffer->PF.XA2_8SourceVoice->DestroyVoice() ;
				Buffer->PF.XA2_8SourceVoice = NULL ;
			}

			if( Buffer->PF.XA2_8SubmixVoice != NULL )
			{
				Buffer->PF.XA2_8SubmixVoice->DestroyVoice() ;
				Buffer->PF.XA2_8SubmixVoice = NULL ;
			}
		}
		else
		{
			if( Buffer->PF.XA2SourceVoice != NULL )
			{
				Buffer->PF.XA2SourceVoice->DestroyVoice() ;
				Buffer->PF.XA2SourceVoice = NULL ;
			}

			if( Buffer->PF.XA2SubmixVoice != NULL )
			{
				Buffer->PF.XA2SubmixVoice->DestroyVoice() ;
				Buffer->PF.XA2SubmixVoice = NULL ;
			}
		}

		if( Buffer->PF.XA2Callback != NULL )
		{
			delete Buffer->PF.XA2Callback ;
			Buffer->PF.XA2Callback = NULL ;
		}
	}
	else
	{
		if( Buffer->PF.DSBuffer != NULL )
		{
			Buffer->PF.DSBuffer->Release() ;
			Buffer->PF.DSBuffer = NULL ;
		}
	}

	return -1 ;
}

extern int SoundBuffer_Initialize_Timing1_PF( SOUNDBUFFER *Buffer, SOUNDBUFFER *Src, int Is3DSound )
{
	int i ;

	// �R�c�T�E���h�̏ꍇ�͂R�c�T�E���h�p�����[�^��������
	if( Is3DSound != FALSE )
	{
		// �G�~�b�^�[�̊�{�I�ȏ����Z�b�g����
		if( Src != NULL )
		{
			Buffer->PF.X3DAudioEmitterConeData              = Src->PF.X3DAudioEmitterConeData ;
			Buffer->PF.X3DAudioEmitterData                  = Src->PF.X3DAudioEmitterData ;
			Buffer->PF.X3DAudioEmitterData.pCone            = &Buffer->PF.X3DAudioEmitterConeData ;
			Buffer->PF.X3DAudioEmitterData.pChannelAzimuths = Buffer->PF.X3DAudioEmitterChannelAzimuths ;
		}
		else
		{
			Buffer->PF.X3DAudioEmitterData.pCone = &Buffer->PF.X3DAudioEmitterConeData ;
			Buffer->PF.X3DAudioEmitterData.pCone->InnerAngle = 0.0f ;
			Buffer->PF.X3DAudioEmitterData.pCone->OuterAngle = 0.0f ;
			Buffer->PF.X3DAudioEmitterData.pCone->InnerVolume = 0.0f ;
			Buffer->PF.X3DAudioEmitterData.pCone->OuterVolume = 1.0f ;
			Buffer->PF.X3DAudioEmitterData.pCone->InnerLPF = 0.0f ;
			Buffer->PF.X3DAudioEmitterData.pCone->OuterLPF = 1.0f ;
			Buffer->PF.X3DAudioEmitterData.pCone->InnerReverb = 0.0f ;
			Buffer->PF.X3DAudioEmitterData.pCone->OuterReverb = 1.0f ;

			Buffer->PF.X3DAudioEmitterData.Position.x = 0.0f ;
			Buffer->PF.X3DAudioEmitterData.Position.y = 0.0f ;
			Buffer->PF.X3DAudioEmitterData.Position.z = 0.0f ;
			Buffer->PF.X3DAudioEmitterData.OrientFront.x = 0.0f ;
			Buffer->PF.X3DAudioEmitterData.OrientFront.y = 0.0f ;
			Buffer->PF.X3DAudioEmitterData.OrientFront.z = 1.0f ;
			Buffer->PF.X3DAudioEmitterData.OrientTop.x = 0.0f ;
			Buffer->PF.X3DAudioEmitterData.OrientTop.y = 1.0f ;
			Buffer->PF.X3DAudioEmitterData.OrientTop.z = 0.0f ;
			Buffer->PF.X3DAudioEmitterData.Velocity.x = 0.0f ;
			Buffer->PF.X3DAudioEmitterData.Velocity.y = 0.0f ;
			Buffer->PF.X3DAudioEmitterData.Velocity.z = 0.0f ;
			Buffer->PF.X3DAudioEmitterData.ChannelRadius = 0.1f ;
			Buffer->PF.X3DAudioEmitterData.pChannelAzimuths = Buffer->PF.X3DAudioEmitterChannelAzimuths ;

			Buffer->PF.X3DAudioEmitterData.InnerRadius = 2.0f;
			Buffer->PF.X3DAudioEmitterData.InnerRadiusAngle = DX_PI_F / 4.0f ;

			Buffer->PF.X3DAudioEmitterData.pVolumeCurve = ( D_X3DAUDIO_DISTANCE_CURVE * )&D_X3DAudioDefault_LinearCurve ;
			Buffer->PF.X3DAudioEmitterData.pLFECurve    = ( D_X3DAUDIO_DISTANCE_CURVE * )&D_Emitter_LFE_Curve ;
			Buffer->PF.X3DAudioEmitterData.pLPFDirectCurve = NULL ;
			Buffer->PF.X3DAudioEmitterData.pLPFReverbCurve = NULL ;
			Buffer->PF.X3DAudioEmitterData.pReverbCurve    = ( D_X3DAUDIO_DISTANCE_CURVE * )&D_Emitter_Reverb_Curve ;
			Buffer->PF.X3DAudioEmitterData.CurveDistanceScaler = 14.0f ;
			Buffer->PF.X3DAudioEmitterData.DopplerScaler = 1.0f ;

			Buffer->EmitterInfo.InnerAngle = Buffer->PF.X3DAudioEmitterConeData.InnerAngle ;
			Buffer->EmitterInfo.OuterAngle = Buffer->PF.X3DAudioEmitterConeData.OuterAngle ;
			Buffer->EmitterInfo.InnerVolume = Buffer->PF.X3DAudioEmitterConeData.InnerVolume ;
			Buffer->EmitterInfo.OuterVolume = Buffer->PF.X3DAudioEmitterConeData.OuterVolume ;
			Buffer->EmitterRadius = Buffer->PF.X3DAudioEmitterData.CurveDistanceScaler ;
			Buffer->EmitterInnerRadius = Buffer->PF.X3DAudioEmitterData.InnerRadius ;

			// �`�����l������ۑ�����
			Buffer->PF.X3DAudioEmitterData.ChannelCount = Buffer->Format.nChannels/*D_X3DAUDIO_INPUTCHANNELS*/ ;
			for( i = 0 ; i < Buffer->Format.nChannels ; i ++ )
			{
				Buffer->PF.X3DAudioEmitterChannelAzimuths[ i ] = 0.0f ;
			}
		}
	}
	
	return 0 ;
}

extern int SoundBuffer_Terminate_PF( SOUNDBUFFER *Buffer )
{
	if( SoundSysData.PF.EnableXAudioFlag )
	{
		if( SoundSysData.PF.XAudio2_8DLL != NULL )
		{
			if( Buffer->PF.XA2_8SourceVoice != NULL )
			{
				Buffer->PF.XA2_8SourceVoice->DestroyVoice() ;
				Buffer->PF.XA2_8SourceVoice = NULL ;
			}

			if( Buffer->PF.XA2_8SubmixVoice != NULL )
			{
				Buffer->PF.XA2_8SubmixVoice->DestroyVoice() ;
				Buffer->PF.XA2_8SubmixVoice = NULL ;
			}
		}
		else
		{
			if( Buffer->PF.XA2SourceVoice != NULL )
			{
				Buffer->PF.XA2SourceVoice->DestroyVoice() ;
				Buffer->PF.XA2SourceVoice = NULL ;
			}

			if( Buffer->PF.XA2SubmixVoice != NULL )
			{
				Buffer->PF.XA2SubmixVoice->DestroyVoice() ;
				Buffer->PF.XA2SubmixVoice = NULL ;
			}
		}

		if( Buffer->PF.XA2Callback != NULL )
		{
			delete Buffer->PF.XA2Callback ;
			Buffer->PF.XA2Callback = NULL ;
		}
	}
	else
	{
		if( Buffer->PF.DSBuffer != NULL )
		{
			Buffer->PF.DSBuffer->Release() ;
			Buffer->PF.DSBuffer = NULL ;
		}
	}

	return 0 ;
}

extern int SoundBuffer_CheckEnable_PF( SOUNDBUFFER *Buffer )
{
	return Buffer->PF.DSBuffer != NULL ? TRUE : FALSE ;
}

extern int SoundBuffer_Play_PF( SOUNDBUFFER *Buffer, int Loop )
{
	if( SoundSysData.PF.EnableXAudioFlag )
	{
		Buffer->State = TRUE ;
		if( SoundSysData.PF.XAudio2_8DLL != NULL )
		{
			Buffer->PF.XA2_8SourceVoice->Start( 0 ) ;
			if( Buffer->PF.XA2_8SubmixVoice )
			{
				Buffer->PF.XA2_8SubmixVoice->EnableEffect( 0 ) ;
			}
		}
		else
		{
			Buffer->PF.XA2SourceVoice->Start( 0 ) ;
			if( Buffer->PF.XA2SubmixVoice )
			{
				Buffer->PF.XA2SubmixVoice->EnableEffect( 0 ) ;
			}
		}
		Buffer->StopTimeState = 1 ;
		Buffer->StopTime = 0 ;
	}
	else
	{
		if( Buffer->PF.DSBuffer->Play( 0, 0, ( DWORD )( Loop ? D_DSBPLAY_LOOPING : 0 ) ) != D_DS_OK )
		{
			return -1 ;
		}
	}
	
	return 0 ;
}

extern int SoundBuffer_Stop_PF(	SOUNDBUFFER *Buffer, int EffectStop )
{
	if( SoundSysData.PF.EnableXAudioFlag )
	{
		if( SoundSysData.PF.XAudio2_8DLL != NULL )
		{
			Buffer->PF.XA2_8SourceVoice->Stop( 0 ) ;
			if( Buffer->PF.XA2_8SubmixVoice && EffectStop )
			{
				Buffer->PF.XA2_8SubmixVoice->DisableEffect( 0 ) ;
			}
		}
		else
		{
			Buffer->PF.XA2SourceVoice->Stop( 0 ) ;
			if( Buffer->PF.XA2SubmixVoice && EffectStop )
			{
				Buffer->PF.XA2SubmixVoice->DisableEffect( 0 ) ;
			}
		}
		Buffer->State = FALSE ;
	}
	else
	{
		Buffer->PF.DSBuffer->Stop() ;
	}

	return 0 ;
}

extern int SoundBuffer_CheckPlay_PF( SOUNDBUFFER *Buffer )
{
	if( SoundSysData.PF.EnableXAudioFlag )
	{
		return Buffer->State ;
	}
	else
	{
		DWORD Status ;
		if( Buffer->PF.DSBuffer->GetStatus( &Status ) != D_DS_OK )
		{
			return -1 ;
		}

		if( Status & D_DSBSTATUS_PLAYING )
		{
			return TRUE ;
		}
		else
		{
			return FALSE ;
		}
	}
}

extern int SoundBuffer_Lock_PF( SOUNDBUFFER *Buffer, DWORD WritePos , DWORD WriteSize, void **LockPos1, DWORD *LockSize1, void **LockPos2, DWORD *LockSize2 )
{
	if( SoundSysData.PF.EnableXAudioFlag )
	{
		return 2 ;
	}
	else
	{
		if( Buffer->PF.DSBuffer->Lock( WritePos, WriteSize, LockPos1, LockSize1, LockPos2, LockSize2, 0 ) != D_DS_OK )
		{
			return -1 ;
		}
	}

	return 0 ;
}

extern int SoundBuffer_Unlock_PF( SOUNDBUFFER *Buffer, void *LockPos1, DWORD LockSize1, void *LockPos2, DWORD LockSize2 )
{
	if( SoundSysData.PF.EnableXAudioFlag )
	{
		DWORD i ;

		// �_�u���T�C�Y�̏ꍇ�̓X�e���I�f�[�^�ɂ���
		if( Buffer->Wave->DoubleSizeBuffer != NULL )
		{
			switch( Buffer->Format.wBitsPerSample )
			{
			case 8 :
				{
					BYTE *Src8bit ;
					WORD *Dest8bit ;
					DWORD SampleNum ;

					Src8bit = ( BYTE * )LockPos1 ;
					Dest8bit = ( WORD * )Buffer->Wave->DoubleSizeBuffer + ( ( BYTE * )LockPos1 - ( BYTE * )Buffer->Wave->Buffer ) ;
					SampleNum = LockSize1 ;
					for( i = 0 ; i < SampleNum ; i ++ )
					{
						Dest8bit[ i ] = ( WORD )( Src8bit[ i ] + ( Src8bit[ i ] << 8 ) ) ;
					}

					Src8bit = ( BYTE * )LockPos2 ;
					Dest8bit = ( WORD * )Buffer->Wave->DoubleSizeBuffer + ( ( BYTE * )LockPos2 - ( BYTE * )Buffer->Wave->Buffer ) ;
					SampleNum = LockSize2 ;
					for( i = 0 ; i < SampleNum ; i ++ )
					{
						Dest8bit[ i ] = ( WORD )( Src8bit[ i ] + ( Src8bit[ i ] << 8 ) ) ;
					}
				}
				break ;

			case 16 :
				{
					WORD *Src16bit ;
					DWORD *Dest16bit ;
					DWORD SampleNum ;

					Src16bit = ( WORD * )LockPos1 ;
					Dest16bit = ( DWORD * )Buffer->Wave->DoubleSizeBuffer + ( ( WORD * )LockPos1 - ( WORD * )Buffer->Wave->Buffer ) ;
					SampleNum = LockSize1 / 2 ;
					for( i = 0 ; i < SampleNum ; i ++ )
					{
						Dest16bit[ i ] = ( DWORD )( Src16bit[ i ] + ( Src16bit[ i ] << 16 ) ) ;
					}

					Src16bit = ( WORD * )LockPos2 ;
					Dest16bit = ( DWORD * )Buffer->Wave->DoubleSizeBuffer + ( ( WORD * )LockPos2 - ( WORD * )Buffer->Wave->Buffer ) ;
					SampleNum = LockSize2 / 2 ;
					for( i = 0 ; i < SampleNum ; i ++ )
					{
						Dest16bit[ i ] = ( DWORD )( Src16bit[ i ] + ( Src16bit[ i ] << 16 ) ) ;
					}
				}
				break ;
			}
		}
	}
	else
	{
		if( Buffer->PF.DSBuffer->Unlock( LockPos1, LockSize1, LockPos2, LockSize2 ) != D_DS_OK )
		{
			return -1 ;
		}
	}

	return 0 ;
}

extern int SoundBuffer_SetFrequency_PF( SOUNDBUFFER *Buffer, DWORD Frequency )
{
	if( SoundSysData.PF.EnableXAudioFlag )
	{
		float SetFreq ;

		if( Frequency == D_DSBFREQUENCY_ORIGINAL )
		{
			SetFreq = 1.0f ;
		}
		else
		{
			SetFreq = ( float )Frequency / Buffer->Format.nSamplesPerSec ;
		}

		if( SoundSysData.PF.XAudio2_8DLL != NULL )
		{
			Buffer->PF.XA2_8SourceVoice->SetFrequencyRatio( SetFreq ) ;
		}
		else
		{
			Buffer->PF.XA2SourceVoice->SetFrequencyRatio( SetFreq ) ;
		}
	}
	else
	{
		Buffer->PF.DSBuffer->SetFrequency( Frequency ) ;
	}

	return 0 ;
}

extern int SoundBuffer_GetFrequency_PF( SOUNDBUFFER *Buffer, LPDWORD Frequency )
{
	if( SoundSysData.PF.EnableXAudioFlag )
	{
		return 2 ;
	}
	else
	{
		if( Buffer->PF.DSBuffer->GetFrequency( Frequency ) != D_DS_OK )
		{
			return -1 ;
		}
	}

	return 0 ;
}

extern int SoundBuffer_RefreshVolume_PF( SOUNDBUFFER *Buffer )
{
	int i ;
	int ChannelNum ;

	ChannelNum = Buffer->Format.nChannels > SOUNDBUFFER_MAX_CHANNEL_NUM ? SOUNDBUFFER_MAX_CHANNEL_NUM : Buffer->Format.nChannels ;

	if( SoundSysData.PF.EnableXAudioFlag )
	{
		float ChannelVolume[ 16 ] ;

		for( i = 0 ; i < SOUNDBUFFER_MAX_CHANNEL_NUM ; i ++ )
		{
			ChannelVolume[ i ] = D_XAudio2DecibelsToAmplitudeRatio( Buffer->Volume[ i ] / 100.0f ) ;
		}

		if( ChannelNum <= 2 )
		{
			if( ChannelNum == 1 )
			{
				ChannelVolume[ 1 ] = ChannelVolume[ 0 ] ;
			}

			if( Buffer->Pan < 0 )
			{
				ChannelVolume[ 1 ] *= D_XAudio2DecibelsToAmplitudeRatio( Buffer->Pan / 100.0f ) ;
			}
			else
			if( Buffer->Pan > 0 )
			{
				ChannelVolume[ 0 ] *= D_XAudio2DecibelsToAmplitudeRatio( -Buffer->Pan / 100.0f ) ;
			}
		}

		if( SoundSysData.PF.XAudio2_8DLL != NULL )
		{
			Buffer->PF.XA2_8SourceVoice->SetChannelVolumes( ( DWORD )Buffer->PF.XAudioChannels, ChannelVolume ) ;
		}
		else
		{
			Buffer->PF.XA2SourceVoice->SetChannelVolumes( ( DWORD )Buffer->PF.XAudioChannels, ChannelVolume ) ;
		}
	}
	else
	{
		if( ChannelNum > 2 )
		{
			LONG Volume ;

			Volume = ( LONG )Buffer->Volume[ 0 ] ;
			if( Volume > 0 )
			{
				Volume = 0 ;
			}
			else
			if( Volume < -10000 )
			{
				Volume = -10000 ;
			}
			Buffer->PF.DSBuffer->SetVolume( Volume ) ;
			Buffer->PF.DSBuffer->SetPan( Buffer->Pan ) ;
		}
		else
		{
			LONG CalcVolume[ 2 ] ;
			LONG TempVolume[ 2 ] ;
			FLOAT OrigVolume[ 2 ] ;
			LONG Volume ;
			LONG Pan ;

			if( Buffer->Is3DSound )
			{
				OrigVolume[ 0 ] = D_XAudio2DecibelsToAmplitudeRatio( Buffer->Volume[ 0 ] / 100.0f ) ;
				OrigVolume[ 1 ] = D_XAudio2DecibelsToAmplitudeRatio( Buffer->Volume[ 1 ] / 100.0f ) ;

				if( Buffer->DSound_Calc3DPan < 0.0f )
				{
					OrigVolume[ 0 ] *= 1.0f + Buffer->DSound_Calc3DPan ;
				}
				else
				if( Buffer->DSound_Calc3DPan > 0.0f )
				{
					OrigVolume[ 1 ] *= 1.0f - Buffer->DSound_Calc3DPan ;
				}

				TempVolume[ 0 ] = ( LONG )_DTOL( D_XAudio2AmplitudeRatioToDecibels( OrigVolume[ 0 ] * Buffer->DSound_Calc3DVolume ) * 100.0f ) ;
				TempVolume[ 1 ] = ( LONG )_DTOL( D_XAudio2AmplitudeRatioToDecibels( OrigVolume[ 1 ] * Buffer->DSound_Calc3DVolume ) * 100.0f ) ;
			}
			else
			{
				TempVolume[ 0 ] = Buffer->Volume[ 0 ] ;
				if( ChannelNum == 1 )
				{
					TempVolume[ 1 ] = Buffer->Volume[ 0 ] ;
				}
				else
				{
					TempVolume[ 1 ] = Buffer->Volume[ 1 ] ;
				}
			}

			if( Buffer->Pan < 0 )
			{
				CalcVolume[ 0 ] = 10000 ;
				CalcVolume[ 1 ] = 10000 + Buffer->Pan ;
			}
			else
			{
				CalcVolume[ 0 ] = 10000 - Buffer->Pan ;
				CalcVolume[ 1 ] = 10000 ;
			}

			if( TempVolume[ 0 ] > 0 )
			{
				TempVolume[ 0 ] = 0 ;
			}
			else
			if( TempVolume[ 0 ] < -10000 )
			{
				TempVolume[ 0 ] = -10000 ;
			}
			if( TempVolume[ 1 ] > 0 )
			{
				TempVolume[ 1 ] = 0 ;
			}
			else
			if( TempVolume[ 1 ] < -10000 )
			{
				TempVolume[ 1 ] = -10000 ;
			}

			CalcVolume[ 0 ] = CalcVolume[ 0 ] * ( TempVolume[ 0 ] + 10000 ) / 10000 ;
			CalcVolume[ 1 ] = CalcVolume[ 1 ] * ( TempVolume[ 0 ] + 10000 ) / 10000 ;

			if( CalcVolume[ 0 ] > CalcVolume[ 1 ] )
			{
				Volume = CalcVolume[ 0 ] - 10000 ;
				Pan =    _FTOL( CalcVolume[ 1 ] * ( 10000.0f / CalcVolume[ 0 ] ) ) - 10000 ;
			}
			else
			if( CalcVolume[ 0 ] < CalcVolume[ 1 ] )
			{
				Volume = CalcVolume[ 1 ] - 10000 ;
				Pan = -( _FTOL( CalcVolume[ 0 ] * ( 10000.0f / CalcVolume[ 1 ] ) ) - 10000 ) ;
			}
			else
			{
				Volume = CalcVolume[ 0 ] - 10000 ;
				Pan = 0 ;
			}

			Buffer->PF.DSBuffer->SetPan( ( LONG )Pan ) ;
			Buffer->PF.DSBuffer->SetVolume( ( LONG )Volume ) ;
		}
	}

	return 0 ;
}

extern int SoundBuffer_GetCurrentPosition_PF(	SOUNDBUFFER *Buffer, DWORD * PlayPos, DWORD * WritePos )
{
	if( SoundSysData.PF.EnableXAudioFlag )
	{
		return 2 ;
	}
	else
	{
		Buffer->PF.DSBuffer->GetCurrentPosition( PlayPos, WritePos ) ;
	}

	return 0 ;
}

extern int SoundBuffer_SetCurrentPosition_PF( SOUNDBUFFER *Buffer, DWORD NewPos )
{
	if( SoundSysData.PF.EnableXAudioFlag )
	{
		Buffer->PlayPos = ( int )( NewPos / Buffer->Format.nBlockAlign ) ;
		Buffer->CompPos = Buffer->PlayPos ;

		if( SoundSysData.PF.XAudio2_8DLL != NULL )
		{
			Buffer->PF.XA2_8SourceVoice->FlushSourceBuffers() ;
		}
		else
		{
			Buffer->PF.XA2SourceVoice->FlushSourceBuffers() ;
		}
	}
	else
	{
		Buffer->PF.DSBuffer->SetCurrentPosition( NewPos ) ;
	}

	return 0 ;
}

extern int SoundBuffer_CycleProcess_PF( SOUNDBUFFER *Buffer )
{
	if( SoundSysData.PF.EnableXAudioFlag )
	{
		int NowCount ;
		int Time ;

		if( Buffer->PF.XA2SubmixVoice == NULL )
		{
			return -1 ;
		}

		if( Buffer->StopTimeState == 0 )
		{
			return -1 ;
		}

		if( Buffer->State == TRUE )
		{
			return 0 ;
		}

		if( Buffer->StopTimeState == 1 )
		{
			Buffer->StopTimeState = 2 ;
			Buffer->StopTime = NS_GetNowCount( FALSE ) ;

			return 0 ;
		}

		NowCount = NS_GetNowCount( FALSE ) ;

		// �Đ�����~���Ă���R�b��ɃG�t�F�N�g����~����
		if( NowCount < Buffer->StopTime )
		{
			Time = 0x7ffffff - Buffer->StopTime + NowCount ;
		}
		else
		{
			Time = NowCount - Buffer->StopTime ;
		}
		if( Time < 3000 )
		{
			return 0 ;
		}

		Buffer->StopTimeState = 0 ;

		if( SoundSysData.PF.XAudio2_8DLL != NULL )
		{
			if( Buffer->PF.XA2_8SubmixVoice )
			{
				Buffer->PF.XA2_8SubmixVoice->DisableEffect( 0 ) ;
			}
		}
		else
		{
			if( Buffer->PF.XA2SubmixVoice )
			{
				Buffer->PF.XA2SubmixVoice->DisableEffect( 0 ) ;
			}
		}

		return -1 ;
	}

	return -1 ;
}

extern int SoundBuffer_Set3DPosition_PF( SOUNDBUFFER *Buffer, VECTOR *Position )
{
	// �l��ۑ�
	Buffer->PF.X3DAudioEmitterData.Position.x = Position->x ;
	Buffer->PF.X3DAudioEmitterData.Position.y = Position->y ;
	Buffer->PF.X3DAudioEmitterData.Position.z = Position->z ;

	return 0 ;
}

extern int SoundBuffer_Set3DRadius_PF( SOUNDBUFFER *Buffer, float Radius )
{
	// �������鋗����ۑ�
	Buffer->PF.X3DAudioEmitterData.CurveDistanceScaler = Radius ;

	return 0 ;
}

extern int SoundBuffer_Set3DInnerRadius_PF(	SOUNDBUFFER *Buffer, float Radius )
{
	Buffer->PF.X3DAudioEmitterData.InnerRadius = Radius ;

	return 0 ;
}

extern int SoundBuffer_Set3DVelocity_PF( SOUNDBUFFER *Buffer, VECTOR *Velocity )
{
	// �l��ۑ�
	Buffer->PF.X3DAudioEmitterData.Velocity.x = Velocity->x ;
	Buffer->PF.X3DAudioEmitterData.Velocity.y = Velocity->y ;
	Buffer->PF.X3DAudioEmitterData.Velocity.z = Velocity->z ;

	return 0 ;
}

extern int SoundBuffer_Set3DFrontPosition_PF( SOUNDBUFFER *Buffer, VECTOR * /*FrontPosition*/, VECTOR * /*UpVector*/ )
{
	// �l��ۑ�
	Buffer->PF.X3DAudioEmitterData.OrientFront.x = Buffer->EmitterInfo.FrontDirection.x ;
	Buffer->PF.X3DAudioEmitterData.OrientFront.y = Buffer->EmitterInfo.FrontDirection.y ;
	Buffer->PF.X3DAudioEmitterData.OrientFront.z = Buffer->EmitterInfo.FrontDirection.z ;

	Buffer->PF.X3DAudioEmitterData.OrientTop.x = Buffer->EmitterInfo.UpDirection.x ;
	Buffer->PF.X3DAudioEmitterData.OrientTop.y = Buffer->EmitterInfo.UpDirection.y ;
	Buffer->PF.X3DAudioEmitterData.OrientTop.z = Buffer->EmitterInfo.UpDirection.z ;

	return 0 ;
}

extern int SoundBuffer_Set3DConeAngle_PF( SOUNDBUFFER *Buffer, float InnerAngle, float OuterAngle )
{
	Buffer->PF.X3DAudioEmitterConeData.InnerAngle = InnerAngle ;
	Buffer->PF.X3DAudioEmitterConeData.OuterAngle = OuterAngle ;

	return 0 ;
}

extern int SoundBuffer_Set3DConeVolume_PF( SOUNDBUFFER *Buffer, float InnerAngleVolume, float OuterAngleVolume )
{
	Buffer->PF.X3DAudioEmitterConeData.InnerVolume = InnerAngleVolume ;
	Buffer->PF.X3DAudioEmitterConeData.OuterVolume = OuterAngleVolume ;

	return 0 ;
}

extern int SoundBuffer_Refresh3DSoundParam_PF(	SOUNDBUFFER *Buffer, int /*AlwaysFlag*/ )
{
	DWORD CalcFlags ;
	D_X3DAUDIO_DSP_SETTINGS	DspSettings ;
	float MatrixCoefficients[ D_X3DAUDIO_INPUTCHANNELS * 32 ] ;
	D_XAUDIO2_FILTER_PARAMETERS FilterParametersDirect ;
	D_XAUDIO2_FILTER_PARAMETERS FilterParametersReverb ;
	float Sin, Cos ;
	int i ;
	int Num ;

	// XAudio2 ���g�p���邩�ǂ����ŏ����𕪊�
	if( SoundSysData.PF.EnableXAudioFlag )
	{
		// XAudio2 ���g�p����ꍇ
		CalcFlags =
			D_X3DAUDIO_CALCULATE_MATRIX |
			D_X3DAUDIO_CALCULATE_DOPPLER |
			D_X3DAUDIO_CALCULATE_LPF_DIRECT |
			D_X3DAUDIO_CALCULATE_LPF_REVERB |
			D_X3DAUDIO_CALCULATE_REVERB ;
		if( ( SoundSysData.PF.XAudio2OutputChannelMask & D_SPEAKER_LOW_FREQUENCY ) != 0 )
		{
			CalcFlags |= D_X3DAUDIO_CALCULATE_REDIRECT_TO_LFE ;
		}

		_MEMSET( MatrixCoefficients, 0, sizeof( MatrixCoefficients ) ) ;
		_MEMSET( &DspSettings, 0, sizeof( DspSettings ) ) ;
	//	DspSettings.SrcChannelCount = D_X3DAUDIO_INPUTCHANNELS ;
		DspSettings.SrcChannelCount = Buffer->Format.nChannels ;
		DspSettings.DstChannelCount = SoundSysData.OutputChannels ;
		DspSettings.pMatrixCoefficients = MatrixCoefficients ;

		SoundSysData.PF.X3DAudioCalculateFunc(
			SoundSysData.PF.X3DAudioInstance,
			&SoundSysData.PF.X3DAudioListenerData,
			&Buffer->PF.X3DAudioEmitterData,
			CalcFlags,
			&DspSettings ) ;

		_SINCOS( DX_PI_F / 6.0f * DspSettings.LPFDirectCoefficient, &Sin, &Cos ) ;
		FilterParametersDirect.Type = D_LowPassFilter ;
		FilterParametersDirect.Frequency = 2.0f * Sin ;
		FilterParametersDirect.OneOverQ = 1.0f ;

		_SINCOS( DX_PI_F / 6.0f * DspSettings.LPFReverbCoefficient, &Sin, &Cos ) ;
		FilterParametersReverb.Type = D_LowPassFilter ;
		FilterParametersReverb.Frequency = 2.0f * Sin ;
		FilterParametersReverb.OneOverQ = 1.0f ;
		if( SoundSysData.PF.XAudio2_8DLL != NULL )
		{
			Buffer->PF.XA2_8SourceVoice->SetFrequencyRatio( DspSettings.DopplerFactor ) ;

			Buffer->PF.XA2_8SourceVoice->SetOutputMatrix(
				SoundSysData.PF.XAudio2_8MasteringVoiceObject,
				Buffer->Format.nChannels,
				SoundSysData.OutputChannels,
				MatrixCoefficients
			) ;

			Num = Buffer->Format.nChannels * Buffer->Format.nChannels ;
			for( i = 0 ; i < Num ; i ++ )
			{
				MatrixCoefficients[ i ] = DspSettings.ReverbLevel ;
			}
			Buffer->PF.XA2_8SourceVoice->SetOutputMatrix(
				Buffer->PF.XA2_8SubmixVoice,
				Buffer->Format.nChannels,
				Buffer->Format.nChannels,
				MatrixCoefficients
			) ;

			Buffer->PF.XA2_8SourceVoice->SetOutputFilterParameters( SoundSysData.PF.XAudio2_8MasteringVoiceObject, &FilterParametersDirect ) ;
			Buffer->PF.XA2_8SourceVoice->SetOutputFilterParameters( Buffer->PF.XA2_8SubmixVoice,             &FilterParametersReverb ) ;
		}
		else
		{
			Buffer->PF.XA2SourceVoice->SetFrequencyRatio( DspSettings.DopplerFactor ) ;
			Buffer->PF.XA2SourceVoice->SetOutputMatrix(
				SoundSysData.PF.XAudio2MasteringVoiceObject,
				Buffer->Format.nChannels,
				SoundSysData.OutputChannels,
				MatrixCoefficients
			) ;

			Num = Buffer->Format.nChannels * Buffer->Format.nChannels ;
			for( i = 0 ; i < Num ; i ++ )
			{
				MatrixCoefficients[ i ] = DspSettings.ReverbLevel ;
			}
			Buffer->PF.XA2SourceVoice->SetOutputMatrix(
				Buffer->PF.XA2SubmixVoice,
				Buffer->Format.nChannels,
				Buffer->Format.nChannels,
				MatrixCoefficients
			) ;

			Buffer->PF.XA2SourceVoice->SetOutputFilterParameters( SoundSysData.PF.XAudio2MasteringVoiceObject, &FilterParametersDirect ) ;
			Buffer->PF.XA2SourceVoice->SetOutputFilterParameters( Buffer->PF.XA2SubmixVoice,             &FilterParametersReverb ) ;
		}
	}
	else
	{
		float Distance ;
		float Angle ;
		float DistanceVolumeRatio ;
		float AngleVolumeRatio ;
		float fVolume ;
		float fPan ;
		VECTOR ListenerToEmitterVec ;
		VECTOR PanVec ;

		// �����ł̌��������v�Z
		ListenerToEmitterVec = VSub( Buffer->EmitterInfo.Position, SoundSysData.ListenerInfo.Position ) ;
		Distance = VSize( ListenerToEmitterVec ) ;
		if( Distance < 0.0000000000001f )
		{
			Distance = 0.0000000000001f ;
		}
		if( Distance > Buffer->EmitterRadius )
		{
			DistanceVolumeRatio = 0.0f ;
		}
		else
		{
			DistanceVolumeRatio = 1.0f - Distance / Buffer->EmitterRadius ;
		}
		ListenerToEmitterVec = VScale( ListenerToEmitterVec, 1.0f / Distance ) ;

		// �p�x�ł̌��������v�Z
		Angle = _ACOS( VDot( SoundSysData.ListenerInfo.FrontDirection, ListenerToEmitterVec ) ) * 2.0f ;
		if( Angle < SoundSysData.ListenerInfo.InnerAngle )
		{
			AngleVolumeRatio = SoundSysData.ListenerInfo.InnerVolume ;
		}
		else
		if( Angle > SoundSysData.ListenerInfo.OuterAngle )
		{
			AngleVolumeRatio = SoundSysData.ListenerInfo.OuterVolume ;
		}
		else
		{
			AngleVolumeRatio = ( Angle - SoundSysData.ListenerInfo.InnerAngle ) / ( SoundSysData.ListenerInfo.OuterAngle - SoundSysData.ListenerInfo.InnerAngle ) ;
			AngleVolumeRatio = ( SoundSysData.ListenerInfo.OuterVolume - SoundSysData.ListenerInfo.InnerVolume ) * AngleVolumeRatio + SoundSysData.ListenerInfo.InnerVolume ;
		}
		// �v�f�̊|�����킹
		fVolume = DistanceVolumeRatio * AngleVolumeRatio ;

		// ���E�o�����X���v�Z
		PanVec.x = VDot( ListenerToEmitterVec, SoundSysData.ListenerSideDirection ) ;
		PanVec.y = VDot( ListenerToEmitterVec, SoundSysData.ListenerInfo.UpDirection ) ;
		PanVec.z = VDot( ListenerToEmitterVec, SoundSysData.ListenerInfo.FrontDirection ) ;
		fPan = PanVec.x < 0.0f ? -PanVec.x : PanVec.x ;
		if( fPan > 0.80f ) fPan = 0.80f ;
		if( PanVec.x < 0.0f )
		{
			fPan = -fPan ;
		}

		Buffer->DSound_Calc3DVolume = fVolume ;
		Buffer->DSound_Calc3DPan = fPan ;

		SoundBuffer_RefreshVolume( Buffer ) ;
	}

	// �I��
	return 0 ;
}

extern int SoundBuffer_SetReverbParam_PF( SOUNDBUFFER *Buffer, SOUND3D_REVERB_PARAM *Param )
{
	// XAudio2 ���g�p����ꍇ�̂ݗL��
	if( SoundSysData.PF.EnableXAudioFlag )
	{
		if( SoundSysData.PF.XAudio2_8DLL != NULL )
		{
			Buffer->PF.XAudio2_8ReverbParameter.WetDryMix           = Param->WetDryMix ;

			Buffer->PF.XAudio2_8ReverbParameter.ReflectionsDelay    = Param->ReflectionsDelay;
			Buffer->PF.XAudio2_8ReverbParameter.ReverbDelay         = Param->ReverbDelay ;
			Buffer->PF.XAudio2_8ReverbParameter.RearDelay           = Param->RearDelay ;

			Buffer->PF.XAudio2_8ReverbParameter.PositionLeft        = Param->PositionLeft ;
			Buffer->PF.XAudio2_8ReverbParameter.PositionRight       = Param->PositionRight ;
			Buffer->PF.XAudio2_8ReverbParameter.PositionMatrixLeft  = Param->PositionMatrixLeft ;
			Buffer->PF.XAudio2_8ReverbParameter.PositionMatrixRight = Param->PositionMatrixRight ;
			Buffer->PF.XAudio2_8ReverbParameter.EarlyDiffusion      = Param->EarlyDiffusion ;
			Buffer->PF.XAudio2_8ReverbParameter.LateDiffusion       = Param->LateDiffusion ;
			Buffer->PF.XAudio2_8ReverbParameter.LowEQGain           = Param->LowEQGain ;
			Buffer->PF.XAudio2_8ReverbParameter.LowEQCutoff         = Param->LowEQCutoff ;
			Buffer->PF.XAudio2_8ReverbParameter.HighEQGain          = Param->HighEQGain ;
			Buffer->PF.XAudio2_8ReverbParameter.HighEQCutoff        = Param->HighEQCutoff ;

			Buffer->PF.XAudio2_8ReverbParameter.RoomFilterFreq      = Param->RoomFilterFreq ;
			Buffer->PF.XAudio2_8ReverbParameter.RoomFilterMain      = Param->RoomFilterMain ;
			Buffer->PF.XAudio2_8ReverbParameter.RoomFilterHF        = Param->RoomFilterHF ;
			Buffer->PF.XAudio2_8ReverbParameter.ReflectionsGain     = Param->ReflectionsGain ;
			Buffer->PF.XAudio2_8ReverbParameter.ReverbGain          = Param->ReverbGain ;
			Buffer->PF.XAudio2_8ReverbParameter.DecayTime           = Param->DecayTime ;
			Buffer->PF.XAudio2_8ReverbParameter.Density             = Param->Density ;
			Buffer->PF.XAudio2_8ReverbParameter.RoomSize            = Param->RoomSize ;

			Buffer->PF.XAudio2_8ReverbParameter.DisableLateField    = FALSE ;

			Buffer->PF.XA2_8SubmixVoice->SetEffectParameters( 0, &Buffer->PF.XAudio2_8ReverbParameter, sizeof( Buffer->PF.XAudio2_8ReverbParameter ) ) ;
		}
		else
		{
			Buffer->PF.XAudio2ReverbParameter.WetDryMix           = Param->WetDryMix ;

			Buffer->PF.XAudio2ReverbParameter.ReflectionsDelay    = Param->ReflectionsDelay;
			Buffer->PF.XAudio2ReverbParameter.ReverbDelay         = Param->ReverbDelay ;
			Buffer->PF.XAudio2ReverbParameter.RearDelay           = Param->RearDelay ;

			Buffer->PF.XAudio2ReverbParameter.PositionLeft        = Param->PositionLeft ;
			Buffer->PF.XAudio2ReverbParameter.PositionRight       = Param->PositionRight ;
			Buffer->PF.XAudio2ReverbParameter.PositionMatrixLeft  = Param->PositionMatrixLeft ;
			Buffer->PF.XAudio2ReverbParameter.PositionMatrixRight = Param->PositionMatrixRight ;
			Buffer->PF.XAudio2ReverbParameter.EarlyDiffusion      = Param->EarlyDiffusion ;
			Buffer->PF.XAudio2ReverbParameter.LateDiffusion       = Param->LateDiffusion ;
			Buffer->PF.XAudio2ReverbParameter.LowEQGain           = Param->LowEQGain ;
			Buffer->PF.XAudio2ReverbParameter.LowEQCutoff         = Param->LowEQCutoff ;
			Buffer->PF.XAudio2ReverbParameter.HighEQGain          = Param->HighEQGain ;
			Buffer->PF.XAudio2ReverbParameter.HighEQCutoff        = Param->HighEQCutoff ;

			Buffer->PF.XAudio2ReverbParameter.RoomFilterFreq      = Param->RoomFilterFreq ;
			Buffer->PF.XAudio2ReverbParameter.RoomFilterMain      = Param->RoomFilterMain ;
			Buffer->PF.XAudio2ReverbParameter.RoomFilterHF        = Param->RoomFilterHF ;
			Buffer->PF.XAudio2ReverbParameter.ReflectionsGain     = Param->ReflectionsGain ;
			Buffer->PF.XAudio2ReverbParameter.ReverbGain          = Param->ReverbGain ;
			Buffer->PF.XAudio2ReverbParameter.DecayTime           = Param->DecayTime ;
			Buffer->PF.XAudio2ReverbParameter.Density             = Param->Density ;
			Buffer->PF.XAudio2ReverbParameter.RoomSize            = Param->RoomSize ;

			Buffer->PF.XA2SubmixVoice->SetEffectParameters( 0, &Buffer->PF.XAudio2ReverbParameter, sizeof( Buffer->PF.XAudio2ReverbParameter ) ) ;
		}
	}
	else
	{
	}

	return 0 ;
}

extern int SoundBuffer_SetPresetReverbParam_PF( SOUNDBUFFER *Buffer, int PresetNo )
{
	// XAudio2 ���g�p����ꍇ�̂ݗL��
	if( SoundSysData.PF.EnableXAudioFlag )
	{
		if( SoundSysData.PF.XAudio2_8DLL != NULL )
		{
			Buffer->PF.XAudio2_8ReverbParameter = SoundSysData.PF.XAudio2_8ReverbParameters[ PresetNo ] ;
			Buffer->PF.XA2_8SubmixVoice->SetEffectParameters( 0, &Buffer->PF.XAudio2_8ReverbParameter, sizeof( Buffer->PF.XAudio2_8ReverbParameter ) ) ;
		}
		else
		{
			Buffer->PF.XAudio2ReverbParameter = SoundSysData.PF.XAudio2ReverbParameters[ PresetNo ] ;
			Buffer->PF.XA2SubmixVoice->SetEffectParameters( 0, &Buffer->PF.XAudio2ReverbParameter, sizeof( Buffer->PF.XAudio2ReverbParameter ) ) ;
		}
	}
	else
	{
	}

	return 0 ;
}





















// �l�h�c�h���t�I�����Ă΂��R�[���o�b�N�֐�
extern int MidiCallBackProcess( void )
{
	MCI_PLAY_PARMS		mciPlayParms;

	SETUP_WIN_API

	// ���[�v�w�肪����ꍇ�Ăщ��t���J�n����
	if( MidiSystemData.LoopFlag == TRUE )
	{
		mciPlayParms.dwCallback = (DWORD_PTR) NS_GetMainWindowHandle() ;
		mciPlayParms.dwFrom		= 0 ;
		WinAPIData.Win32Func.mciSendCommandFunc( MidiSystemData.PF.MidiDeviceID , MCI_PLAY, MCI_NOTIFY | MCI_FROM, (DWORD_PTR)(LPVOID) &mciPlayParms );

		MidiSystemData.PlayFlag = TRUE ;
	}
	else 
	{
		// ���t���������ꍇ�͎~�߂�
		if( MidiSystemData.PlayFlag == TRUE )
		{
			WinAPIData.Win32Func.mciSendCommandFunc( MidiSystemData.PF.MidiDeviceID , MCI_CLOSE, 0, 0 );
			MidiSystemData.PlayFlag = FALSE;
		}
	}

	return 0 ;
}












// �T�E���h�̏������\�t�g�E�G�A�ōs�����ǂ�����ݒ肷��( TRUE:�\�t�g�E�G�A  FALSE:�n�[�h�E�G�A( �f�t�H���g ) )
extern int NS_SetUseSoftwareMixingSoundFlag( int Flag )
{
	// �t���O��ۑ�
	SoundSysData.PF.UseSoftwareMixing = Flag ;

	// �I��
	return 0 ;
}

// �T�E���h�̍Đ���XAudio���g�p���邩�ǂ�����ݒ肷��( TRUE:�g�p����  FALSE:�g�p���Ȃ�( �f�t�H���g ) )
extern int NS_SetEnableXAudioFlag( int Flag )
{
	// �������ς݂̏ꍇ�̓G���[
	if( SoundSysData.InitializeFlag != FALSE )
		return -1 ;

	// �t���O��ۑ�����
	if( Flag )
	{
		SoundSysData.PF.EnableXAudioFlag = TRUE ;
		SoundSysData.PF.DisableWASAPIFlag = TRUE ;
#ifndef DX_NON_ASIO
		SoundSysData.PF.EnableASIOFlag = FALSE ;
#endif // DX_NON_ASIO
	}
	else
	{
		SoundSysData.PF.EnableXAudioFlag = FALSE ;
	}
	
	// �I��
	return 0 ;
}

// �T�E���h�̍Đ���WASAPI���g�p���邩�ǂ�����ݒ肷��( Flag  TRUE:�g�p����  FALSE:�g�p���Ȃ�( �f�t�H���g ), IsExclusive  TRUE:�r�����[�h���g�p����  FALSE:�r�����[�h���g�p���Ȃ�, DevicePeriod �Đ��x�����ԁA100�i�m�b�P��( 100000 �� 10�~���b )�A-1�Ńf�t�H���g�l, SamplePerSec �T���v�����O���[�g )
extern int NS_SetEnableWASAPIFlag( int Flag, int IsExclusive, int DevicePeriod, int SamplePerSec )
{
	// �������ς݂̏ꍇ�̓G���[
	if( SoundSysData.InitializeFlag != FALSE )
		return -1 ;

	if( Flag )
	{
		// �t���O��ۑ�����
		SoundSysData.PF.DisableWASAPIFlag = FALSE ;
		SoundSysData.PF.EnableXAudioFlag = FALSE ;
#ifndef DX_NON_ASIO
		SoundSysData.PF.EnableASIOFlag = FALSE ;
#endif // DX_NON_ASIO

		// �r�����[�h���ǂ�����ۑ�����
		SoundSysData.PF.WASAPI_IsExclusiveFlag = IsExclusive ;

		// �p�����[�^��ۑ�����
		SoundSysData.PF.WASAPI_EnableUserParam = TRUE ;
		SoundSysData.PF.WASAPI_UserParam_DevicePeriod = DevicePeriod ;
		SoundSysData.PF.WASAPI_UserParam_SamplePerSec = SamplePerSec ;
	}
	else
	{
		// �t���O��ۑ�����
		SoundSysData.PF.DisableWASAPIFlag = TRUE ;
		SoundSysData.PF.WASAPI_EnableUserParam = FALSE ;
	}
	
	// �I��
	return 0 ;
}

#ifndef DX_NON_ASIO

// �T�E���h�̍Đ���ASIO���g�p���邩�ǂ�����ݒ肷��( Flag  TRUE:�g�p����  FALSE:�g�p���Ȃ�( �f�t�H���g ), BufferSize �Đ��o�b�t�@�̃T�C�Y�A�������قǒx�������Ȃ��Ȃ�܂����A�������Ԃɍ��킸�Ƀu�c�u�c�m�C�Y����������\���������Ȃ�܂�( -1 �Ńf�t�H���g�l ), SamplePerSec �T���v�����O���[�g )
extern int NS_SetEnableASIOFlag( int Flag, int BufferSize, int SamplePerSec )
{
	// �������ς݂̏ꍇ�̓G���[
	if( SoundSysData.InitializeFlag != FALSE )
		return -1 ;

	if( Flag )
	{
		// �t���O��ۑ�����
		SoundSysData.PF.EnableASIOFlag = TRUE ;
		SoundSysData.PF.DisableWASAPIFlag = TRUE ;
		SoundSysData.PF.EnableXAudioFlag = FALSE ;

		// �p�����[�^��ۑ�����
		SoundSysData.PF.ASIO_EnableUserParam = TRUE ;
		SoundSysData.PF.ASIO_UserParam_BufferSize = BufferSize ;
		SoundSysData.PF.ASIO_UserParam_SamplePerSec = SamplePerSec ;
	}
	else
	{
		SoundSysData.PF.EnableASIOFlag = FALSE ;
		SoundSysData.PF.ASIO_EnableUserParam = FALSE ;
	}
	
	// �I��
	return 0 ;
}

#endif // DX_NON_ASIO

// ���擾�n�֐�

// �c�w���C�u�������g�p���Ă��� DirectSound �I�u�W�F�N�g���擾����
extern const void *NS_GetDSoundObj( void )
{
	return SoundSysData.PF.DirectSoundObject ;
}







#ifndef DX_NON_NAMESPACE

}

#endif // DX_NON_NAMESPACE

#endif // DX_NON_SOUND

