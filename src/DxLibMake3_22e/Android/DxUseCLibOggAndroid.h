// -------------------------------------------------------------------------------
// 
// 		ＤＸライブラリ		Android用Ogg関係ヘッダファイル
// 
// 				Ver 3.22e
// 
// -------------------------------------------------------------------------------

#ifndef DX_USECLIBOGGANDROID_H
#define DX_USECLIBOGGANDROID_H

// インクルード ------------------------------------------------------------------
#include "../DxCompileConfig.h"
#include "../DxLib.h"
#include "DxUseCLibOggAndroid.h"
//#include <alloca.h>

#if !defined( DX_NON_OGGVORBIS ) && !defined( DX_NON_OGGTHEORA )

//namespace DxLib
//{

// マクロ定義 --------------------------------------------------------------------

// 構造体定義 --------------------------------------------------------------------

// Ogg Theora デコード処理用環境依存データ構造体
struct DECODE_THEORA_PF
{
	int									Dummy ;
} ;

// 内部大域変数宣言 --------------------------------------------------------------

// 関数プロトタイプ宣言-----------------------------------------------------------

//}

#endif // #if !defined( DX_NON_OGGVORBIS ) || !defined( DX_NON_OGGTHEORA )

#endif // DX_USECLIBOGGANDROID_H
