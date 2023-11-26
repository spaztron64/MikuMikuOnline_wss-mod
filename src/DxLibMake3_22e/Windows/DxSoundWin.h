// -------------------------------------------------------------------------------
// 
// 		�c�w���C�u����		WindowsOS�p�T�E���h�v���O�����w�b�_�t�@�C��
// 
// 				Ver 3.22e
// 
// -------------------------------------------------------------------------------

#ifndef DX_SOUNDWIN_H
#define DX_SOUNDWIN_H

#include "../DxCompileConfig.h"

#ifndef DX_NON_SOUND

// �C���N���[�h ------------------------------------------------------------------
#include "../DxLib.h"
#include "DxDirectX.h"

#ifndef DX_NON_NAMESPACE

namespace DxLib
{

#endif // DX_NON_NAMESPACE

// �}�N����` --------------------------------------------------------------------

// �X�g���[���֌W�̒�`( Windows����HDD���X���[�v����̂ŁA�t�@�C�����璼�ڍĐ�����ꍇ�̓o�b�t�@�𑽂߂Ɋm�� )
#define STS_BUFSEC_FILE						(256 * 13)			// �X�g���[�����T�E���h�Đ����Ɋm�ۂ���T�E���h�o�b�t�@�̎���( �P�ʂ� STS_DIVNUM����1�b )( �t�@�C������̃X�g���[���Đ��p )
#define STS_ADVANCECOPYSEC_FILE				(256 * 12 + 192)	// �X�g���[�����T�E���h�Đ����ɉ����f�[�^���s�W�J���Ă����ő厞��( �P�ʂ� STS_DIVNUM����1�b )( �t�@�C������̃X�g���[���Đ��p )
#define STS_MINADVANCECOPYSEC_FILE			(256 * 12)			// �X�g���[�����T�E���h�Đ����ɉ����f�[�^���s�W�J���Ă����ŏ�����( �P�ʂ� STS_DIVNUM����1�b )( �t�@�C������̃X�g���[���Đ��p )

#define D_X3DAUDIO_INPUTCHANNELS			(8)				// X3DAudio �̌v�Z�Ŏg�p����ő���̓`�����l����

#ifndef DX_NON_ASIO
#define ASIO_DRV_DESC						L"description"
#define ASIO_PATH							L"software\\asio"
#define ASIO_MAX_DRIVER_NUM					(32)
#define ASIO_MAX_BUFFER_NUM					(16)
#define ASIO_MAX_PATH_LEN					(512)
#define ASIO_MAX_DRVNAME_LEN				(256)
#define ASIO_MAX_DRVNAME2_LEN				(32)
#endif // DX_NON_ASIO

// �\���̒�` --------------------------------------------------------------------

// XAudio2�p�R�[���o�b�N�����p�\����
struct SOUNDBUFFER_CALLBACK : public D_IXAudio2VoiceCallback
{
	virtual void    __stdcall OnVoiceProcessingPassStart( DWORD BytesRequired ) ;
	virtual void    __stdcall OnVoiceProcessingPassEnd	() ;
	virtual void    __stdcall OnStreamEnd				() ;
	virtual void    __stdcall OnBufferStart				( void* pBufferContext ) ;
	virtual void    __stdcall OnBufferEnd				( void* pBufferContext ) ;
	virtual void    __stdcall OnLoopEnd					( void* pBufferContext ) ;
	virtual void    __stdcall OnVoiceError				( void* pBufferContext, HRESULT Error ) ;

	struct SOUNDBUFFER			*Buffer ;
} ;

// �T�E���h�o�b�t�@���ˑ����\����
struct SOUNDBUFFER_PF
{
	union
	{
		D_IDirectSoundBuffer 		*DSBuffer ;				// DirectSoundBuffer
		D_IXAudio2SourceVoice		*XA2SourceVoice ;		// XAudio2SourceVoice
		D_IXAudio2_8SourceVoice		*XA2_8SourceVoice ;		// XAudio2_8SourceVoice
	} ;
	union
	{
		D_IXAudio2SubmixVoice		*XA2SubmixVoice ;		// XAudio2SubmixVoice
		D_IXAudio2_8SubmixVoice		*XA2_8SubmixVoice ;		// XAudio2_8SubmixVoice
	} ;
	D_IUnknown					*XA2ReverbEffect ;		// ���o�[�u�G�t�F�N�g
	SOUNDBUFFER_CALLBACK		*XA2Callback ;			// XAudio2�p�̃R�[���o�b�N

	union
	{
		D_XAUDIO2FX_REVERB_PARAMETERS    XAudio2ReverbParameter ;	// ���o�[�u�v�Z�p�p�����[�^
		D_XAUDIO2FX_REVERB_PARAMETERS2_8 XAudio2_8ReverbParameter ;	// ���o�[�u�v�Z�p�p�����[�^XAudio2.8�p
	} ;

	int							XAudioChannels ;		// XAudio ���ł̃`�����l����

	// �R�c�T�E���h�Đ��p�f�[�^
	D_X3DAUDIO_EMITTER			X3DAudioEmitterData ;									// �R�c�T�E���h�p�������
	D_X3DAUDIO_CONE				X3DAudioEmitterConeData ;								// �R�c�T�E���h�p�������Ŏg�p����R�[�����
	float						X3DAudioEmitterChannelAzimuths[ D_X3DAUDIO_INPUTCHANNELS ] ;	// �R�c�T�E���h�p�������Ŏg�p����`�����l���ʒu�e�[�u��
} ;

// �T�E���h�V�X�e���p���ˑ��f�[�^�\����
struct SOUNDSYSTEMDATA_PF
{
	D_IDirectMusicLoader8		*DirectMusicLoaderObject ;		// DirectMusicLoader8 �I�u�W�F�N�g
	D_IDirectMusicPerformance8	*DirectMusicPerformanceObject ;	// DirectMusicPerformance8 �I�u�W�F�N�g

	HMODULE						XAudio2_8DLL ;					// XAudio2_8.dll
	HRESULT						( WINAPI *XAudio2CreateFunc )( D_IXAudio2_8 ** ppXAudio2, DWORD Flags, D_XAUDIO2_PROCESSOR XAudio2Processor );
	HRESULT						( WINAPI *CreateAudioVolumeMeterFunc )( D_IUnknown** ppApo ) ;
	HRESULT						( WINAPI *CreateAudioReverbFunc )( D_IUnknown** ppApo ) ;

	HMODULE						X3DAudioDLL ;					// X3DAudio.dll
	void						( __cdecl *X3DAudioInitializeFunc )( DWORD SpeakerChannelMask, float SpeedOfSound, D_X3DAUDIO_HANDLE Instance ) ;
	void						( __cdecl *X3DAudioCalculateFunc )( const D_X3DAUDIO_HANDLE Instance, const D_X3DAUDIO_LISTENER* pListener, const D_X3DAUDIO_EMITTER* pEmitter, DWORD Flags, D_X3DAUDIO_DSP_SETTINGS* pDSPSettings ) ;

	union
	{
		D_IXAudio2				*XAudio2Object ;				// XAudio2�I�u�W�F�N�g
		D_IXAudio2_8			*XAudio2_8Object ;				// XAudio2_8�I�u�W�F�N�g
		D_IDirectSound			*DirectSoundObject ;			// �c�����������r���������I�u�W�F�N�g
		DWORD_PTR				InitializeFlag ;				// ���O�~�L�V���O�p�������t���O
	} ;

	D_XAUDIO2FX_REVERB_PARAMETERS    XAudio2ReverbParameters[ D_XAUDIO2FX_PRESET_NUM ] ;	// ���o�[�u�v�Z�p�p�����[�^
	D_XAUDIO2FX_REVERB_PARAMETERS2_8 XAudio2_8ReverbParameters[ D_XAUDIO2FX_PRESET_NUM ] ;	// ���o�[�u�v�Z�p�p�����[�^XAudio2.8�p
	D_IXAudio2MasteringVoice	*XAudio2MasteringVoiceObject ;	// XAudio2MasteringVoice�I�u�W�F�N�g
	D_IXAudio2_8MasteringVoice	*XAudio2_8MasteringVoiceObject ;// XAudio2_8MasteringVoice�I�u�W�F�N�g
	D_XAUDIO2_EFFECT_CHAIN		XAudio2MasteringEffectChain ;	// �}�X�^�[�{�C�X�p�̃G�t�F�N�g�`�F�C��

	DWORD						XAudio2OutputChannelMask ;		// �o�̓`�����l���}�X�N

	D_X3DAUDIO_HANDLE			X3DAudioInstance ;				// �R�c�T�E���h�v�Z�p�C���X�^���X�n���h��
	D_X3DAUDIO_CONE				X3DAudioListenerConeData ;		// �R�c�T�E���h�̃��X�i�[���Ɏg�p����R�[�����
	D_X3DAUDIO_LISTENER			X3DAudioListenerData ;			// �R�c�T�E���h�̃��X�i�[���

	D_WAVEFORMATEXTENSIBLE		SelfMixingFormatEx ;			// ���O�~�L�V���O�t�H�[�}�b�g�� WAVEFORMATEXTENSIBLE ��

	D_IMMDeviceEnumerator		*IMMDeviceEnumerator ;			// �}���`���f�B�A�f�o�C�X�񋓃C���^�[�t�F�[�X
	D_IMMDevice					*IMMDevice ;					// �f�o�C�X�C���^�[�t�F�[�X
	D_IAudioClient				*AudioClient ;					// �I�[�f�B�I�N���C�A���g�C���^�[�t�F�[�X
	D_IAudioClock				*AudioClock ;					// �I�[�f�B�I�N���b�N�C���^�[�t�F�[�X
	D_IAudioRenderClient		*AudioRenderClient ;			// �����_�[�N���C�A���g�C���^�[�t�F�[�X
	HANDLE						WASAPI_Event ;					// WASAPI�p�C�x���g�n���h��
	HANDLE						WASAPI_ThreadHandle ;			// WASAPI�p�X���b�h�n���h��
	BOOL						WASAPI_ThreadLoop ;				// WASAPI�p�X���b�h��������
	D_REFERENCE_TIME			WASAPI_DevicePeriod ;			// WASAPI�̃f�o�C�X�s���I�h
	D_REFERENCE_TIME			WASAPI_DefaultDevicePeriod ;	// WASAPI�̃f�t�H���g�f�o�C�X�s���I�h
	D_REFERENCE_TIME			WASAPI_MinimumDevicePeriod ;	// WASAPI�̍ŏ��f�o�C�X�s���I�h
	DWORD						WASAPI_BufferSamples ;			// WASAPI�̃o�b�t�@�̃T���v����
	int							WASAPI_IsExclusiveFlag ;		// WASAPI��r�����[�h�Ŏg�p���邩�ǂ����̃t���O( TRUE:�r�����[�h  FALSE:���L���[�h )

	int							WASAPI_EnableUserParam ;		// WASAPI�̃��[�U�[�ݒ�p�����[�^���L����
	int							WASAPI_UserParam_DevicePeriod ;	// WASAPI�̃f�o�C�X�s���I�h
	int							WASAPI_UserParam_SamplePerSec ;	// WASAPI�̃T���v�����O���[�g

#ifndef DX_NON_ASIO
	long						ASIO_DriverNum ;				// ASIO�h���C�o�[�̐�
	int							ASIO_DriverUseIndex ;			// �g�p����ASIO�h���C�o
	char						ASIO_DriverPaths[ ASIO_MAX_DRIVER_NUM ][ ASIO_MAX_PATH_LEN ] ;		// ASIO�h���C�o�[�̃p�X
	wchar_t						ASIO_DriverNames[ ASIO_MAX_DRIVER_NUM ][ ASIO_MAX_DRVNAME_LEN ] ;	// ASIO�h���C�o�[�̖��O
	char						ASIO_DriverNames2[ ASIO_MAX_DRIVER_NUM ][ ASIO_MAX_DRVNAME2_LEN ] ;	// ASIO�h���C�o�[�̖��O���̂Q
	long						ASIO_DriverVersion[ ASIO_MAX_DRIVER_NUM ] ;	// ASIO�h���C�o�[�̃o�[�W����
	GUID						ASIO_DriverGUID[ ASIO_MAX_DRIVER_NUM ] ;	// ASIO�h���C�o��GUID
	D_IASIO						*ASIO_Driver[ ASIO_MAX_DRIVER_NUM ] ;		// ASIO�h���C�o�I�u�W�F�N�g
	long						ASIO_BufferMiniNum ;			// �o�b�t�@�̍ŏ��T�C�Y
	long						ASIO_BufferMaxiNum ;			// �o�b�t�@�̍ő�T�C�Y
	long						ASIO_Preferred ;				// �D��o�b�t�@�T�C�Y�i�T�C�Y�p�t�H�[�}���X�ƃn�[�h�E�F�A�̗v���ɍœK�j
	long						ASIO_BufferSize ;				// �g�p������ۂ̃o�b�t�@�T�C�Y
	long						ASIO_Granularity ;				// �o�b�t�@�T�C�Y�̗��x
	D_ASIOBufferInfo			ASIO_BufferInfo[ ASIO_MAX_BUFFER_NUM ] ;	// �o�b�t�@���
	int							ASIO_EnableUserParam ;			// ASIO�̃��[�U�[�ݒ�p�����[�^���L����
	int							ASIO_UserParam_BufferSize ;		// ASIO�̃f�o�C�X�s���I�h
	int							ASIO_UserParam_SamplePerSec ;	// ASIO�̃T���v�����O���[�g
#endif // DX_NON_ASIO

	D_IDirectSoundBuffer		*PrimarySoundBuffer ;			// �v���C�}���T�E���h�o�b�t�@

	D_IDirectSoundBuffer		*NoSoundBuffer ;				// �����o�b�t�@

	int							EnableXAudioFlag ;				// XAudio���g�p���邩�ǂ����̃t���O( TRUE:�g�p����  FALSE:�g�p���Ȃ� ) 
	int							DisableWASAPIFlag ;				// WASAPI���g�p���Ȃ����ǂ����̃t���O( TRUE:�g�p����  FALSE:�g�p���Ȃ� ) 
#ifndef DX_NON_ASIO
	int							EnableASIOFlag ;				// ASIO���g�p���邩�ǂ����̃t���O( TRUE:�g�p����  FALSE:�g�p���Ȃ� ) 
#endif // DX_NON_ASIO
	int							UseSoftwareMixing ;				// �\�t�g�E�G�A�~�L�V���O���s�����ǂ����̃t���O( TRUE:�\�t�g�E�G�A  FALSE:�n�[�h�E�G�A )

	HANDLE						StreamSoundThreadHandle ;		// �X�g���[���T�E���h���Đ����鏈�����s���X���b�h�̃n���h��
	DWORD						StreamSoundThreadID ;			// �X�g���[���T�E���h���Đ����鏈�����s���X���b�h�̂h�c
	DWORD						StreamSoundThreadEndFlag ;		// �X�g���[���T�E���h���Đ����鏈�����I�����邩�ǂ����̃t���O
} ;

// �l�h�c�h�f�[�^���ˑ����
struct MIDIHANDLEDATA_PF
{
	D_IDirectMusicSegment8		*DirectMusicSegmentObject ;		// DirectMusicSegment8 �I�u�W�F�N�g
} ;

// �l�h�c�h�V�X�e���p���ˑ��f�[�^�\����
struct MIDISYSTEMDATA_PF
{
	UINT						MidiDeviceID ;					// �l�h�c�h���t���̃f�o�C�X�h�c
} ;

// �������ϐ��錾 --------------------------------------------------------------

// �֐��v���g�^�C�v�錾-----------------------------------------------------------

extern	int		MidiCallBackProcess( void ) ;																			// �l�h�c�h���t�I�����Ă΂��R�[���o�b�N�֐�


#ifndef DX_NON_NAMESPACE

}

#endif // DX_NON_NAMESPACE

#endif // DX_NON_SOUND

#endif // DX_SOUNDWIN_H
