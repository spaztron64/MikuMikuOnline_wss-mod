// -------------------------------------------------------------------------------
// 
// 		ＤＸライブラリ		Android用GraphFilter系プログラムヘッダファイル
// 
// 				Ver 3.22e
// 
// -------------------------------------------------------------------------------

#ifndef DX_GRAPHICSFILTERANDROID_H
#define DX_GRAPHICSFILTERANDROID_H

#include "../DxCompileConfig.h"

#ifndef DX_NON_FILTER

// インクルード ------------------------------------------------------------------
#include "../DxLib.h"
#include "../DxGraphicsFilter.h"
#include "../DxArchive_.h"
#include "DxGraphicsAndroid.h"

#ifndef DX_NON_NAMESPACE

namespace DxLib
{

#endif // DX_NON_NAMESPACE

// マクロ定義 --------------------------------------------------------------------

// 構造体定義 --------------------------------------------------------------------

// フィルター処理用の情報構造体
struct GRAPHFILTER_SYSTEMIFNO_ANDR
{
	int						WorkDrawValidGrHandle[ 2 ][ 18 ][ 18 ][ 2 ] ;	// フィルター処理作業用描画可能テクスチャ[ 0:整数テクスチャ 1:浮動小数点テクスチャ ][ ２のｎ乗 ][ 作業用２枚 ]

	GRAPHICS_ANDROID_SHADER	Gauss[ 3 ] ;							// ガウスフィルタのピクセルシェーダー
	GRAPHICS_ANDROID_SHADER	BrightClip[ 2 ][ 2 ][ 2 ] ;				// 明るさでクリップするフィルタのピクセルシェーダー[ 1:一定以上クリップ  0:一定以下クリップ ][ 1:クリップ塗りつぶしあり 0:塗りつぶしなし ][ 0:通常用  1:乗算済みアルファ用 ]
	GRAPHICS_ANDROID_SHADER	BrightScale[ 2 ] ;						// 指定の明るさの領域を拡大するフィルタのピクセルシェーダー[ 0:通常用  1:乗算済みアルファ用 ]
	GRAPHICS_ANDROID_SHADER	DownScale[ 3 ] ;						// 縮小フィルター( 0:X2 1:X4 2:X8 )
	GRAPHICS_ANDROID_SHADER	Hsb[ 3 ][ 2 ] ;							// ＨＳＢフィルター[ 0:RGB to HSI  1:HSI to RGB  2:HSB の HLock ][ 0:通常用  1:乗算済みアルファ用 ]
	GRAPHICS_ANDROID_SHADER	Mono ;									// モノトーンフィルター
	GRAPHICS_ANDROID_SHADER	Invert[ 2 ] ;							// 階調反転フィルター[ 0:通常用  1:乗算済みアルファ用 ]
	GRAPHICS_ANDROID_SHADER	Level[ 2 ] ;							// レベル補正フィルター[ 0:通常用  1:乗算済みアルファ用 ]
	GRAPHICS_ANDROID_SHADER	TwoColor[ 2 ] ;							// ２値化フィルター[ 0:通常用  1:乗算済みアルファ用 ]
	GRAPHICS_ANDROID_SHADER	Replacement[ 2 ] ;						// 色の置換フィルター[ 0:通常用  1:乗算済みアルファ用 ]
	GRAPHICS_ANDROID_SHADER	GradientMap[ 2 ][ 2 ] ;					// グラデーションマップフィルター[ 0:通常用  1:乗算済みアルファ用 ]
	GRAPHICS_ANDROID_SHADER	PreMulAlpha ;							// 通常画像から乗算済みアルファ画像を作成する為のフィルター
	GRAPHICS_ANDROID_SHADER	InterpAlpha ;							// 乗算済みアルファ画像から通常画像を作成する為のフィルター
	GRAPHICS_ANDROID_SHADER	YUVtoRGB[ 4 ] ;							// YUVカラーをRGBカラーに変換するフィルター
	GRAPHICS_ANDROID_SHADER	Bicubic ;								// バイキュービック補間フィルター
	GRAPHICS_ANDROID_SHADER	Lanczos3 ;								// Lanczos-3補間フィルター

	GRAPHICS_ANDROID_SHADER	BasicBlend[ DX_GRAPH_BLEND_NUM ] ;		// 基本的なブレンドフィルター
	GRAPHICS_ANDROID_SHADER	RgbaMixBase[ 2 ] ;						// RGBAMixブレンド基本[ 0:通常用  1:乗算済みアルファ用 ]
	GRAPHICS_ANDROID_SHADER	RgbaMixSRGBB[ 4 ][ 2 ] ;				// RGBAMixブレンドの A だけ BRGBA の４つ[ 0:通常用  1:乗算済みアルファ用 ]
	GRAPHICS_ANDROID_SHADER	RgbaMixSRRRB[ 4 ][ 4 ][ 2 ] ;			// RGBAMixブレンドの SYYY BX の１６こ [ Y ][ X ][ 0:通常用  1:乗算済みアルファ用 ]
	GRAPHICS_ANDROID_SHADER	RgbaMixS[ 4 ][ 4 ][ 4 ][ 4 ][ 2 ] ;		// RGBAMixブレンドの S だけの組み合わせ256個[ R ][ G ][ B ][ A ][ 0:通常用  1:乗算済みアルファ用 ]
} ;

// 内部大域変数宣言 --------------------------------------------------------------

extern GRAPHFILTER_SYSTEMIFNO_ANDR GraphFilterSystemInfoAndroid ;

// 関数プロトタイプ宣言-----------------------------------------------------------

// フィルターで使用している全てのシェーダーを解放する
extern int GraphFilter_Android_ReleaseShaderAll( void ) ;

#ifndef DX_NON_NAMESPACE

}

#endif // DX_NON_NAMESPACE

#endif // DX_NON_FILTER

#endif // DX_GRAPHICSFILTERANDROID_H

