// -------------------------------------------------------------------------------
// 
// 		ＤＸライブラリ		iOS用ファイル関係プログラムヘッダファイル
// 
// 				Ver 3.22e
// 
// -------------------------------------------------------------------------------

#ifndef DX_FILE_IOS_H
#define DX_FILE_IOS_H

// インクルード ------------------------------------------------------------------
#include "../DxCompileConfig.h"
#include "../DxThread.h"
#include <stdio.h>
#include <sys/types.h>
#include <dirent.h>

#ifndef DX_NON_NAMESPACE

namespace DxLib
{

#endif // DX_NON_NAMESPACE

// マクロ定義 --------------------------------------------------------------------

// 構造体定義 --------------------------------------------------------------------

// iOS用ファイルアクセス処理用構造体
struct FILEACCESS_PF
{
	FILE					*FilePointer ;						// ファイルアクセス用ファイルポインタ
} ;

// ファイル検索処理用構造体
struct FINDINFO_PF
{
	DIR						*Dir ;								// ディレクトリアクセス用ディレクトリ
	wchar_t					SearchFileName[ 1024 ] ;			// 検索ファイル名
} ;

// ファイルアクセス処理が使用するグローバルデータの環境依存データ構造体
struct FILEMANAGEDATA_PF
{
	int						Dummy ;
} ;

// テーブル-----------------------------------------------------------------------

// 内部大域変数宣言 --------------------------------------------------------------

// 関数プロトタイプ宣言-----------------------------------------------------------

#ifndef DX_NON_NAMESPACE

}

#endif // DX_NON_NAMESPACE

#endif // DX_FILE_IOS_H
