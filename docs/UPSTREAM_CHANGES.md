# 元プログラム（POV-Ray 3.7）からの変更点

この文書は、`third_party/povray/` に取り込んだ POV-Ray 3.7.0.10 に対する
ローカル変更を説明する。上流のファイルを変更した場合は、必ずここに理由と
パッチを追加すること。

## 上流

| 項目 | 値 |
|---|---|
| プログラム | POV-Ray 3.7.0.10 |
| タグ | `v3.7.0.10` |
| コミット | `cd074e4d2698b3faa5b157c6b05c5f3ad432fddc` |
| 取得元 | https://github.com/POV-Ray/povray |
| ライセンス | AGPL-3.0-or-later (`third_party/povray/COPYING`) |
| 取り込んだ範囲 | `source/`, `vfe/`, `unix/`, `include/` |
| 取り込んでいない範囲 | 上流の `.git`、`libraries/`（外部ライブラリ）、`doc/`、`distribution/scenes/` |

Boost は 1.62.0 のヘッダだけを `third_party/boost/` に置いている（未変更）。
ホスト・ビルドでは libpng / libjpeg / zlib をシステムのものにリンクする。

## 変更したファイル

パッチは `third_party/povray/patches/` にある。パスは `third_party/povray/` からの相対。

### 1. `vfe/unix/syspovconfig.h`

パッチ: `vfe_unix_syspovconfig.h.patch`

- 既定の出力形式を PNG から PPM に変更した。
- 理由: ホスト・ビルドでは POV-Ray の既定出力が PNG だと、PNG を無効にした
  構成で既定の出力が使えない。PPM は外部ライブラリ不要で、テストの確認も簡単。
- ターゲットでは `FbImage` へ直接書くため、この既定は主にホスト用。

### 2. `source/backend/parser/parsestr.cpp`

パッチ: `source_backend_parser_parsestr.cpp.patch`

- `boost::posix_time::to_tm(boost::posix_time::from_time_t(timestamp))` を
  `gmtime_r()` に置き換えた。
- 理由: ベアメタル・ターゲットでは Boost.DateTime のコンパイル済みライブラリを
  使わない。`datetime()` の書式化は C の `strftime()` で足りる。

### 3. `source/backend/parser/express.cpp`

パッチ: `source_backend_parser_express.cpp.patch`

- `NOW_TOKEN` の計算を `boost::posix_time` / `boost::gregorian` から
  `time(NULL)` と 2000-01-01 UTC（Unix 秒 946684800）の差に置き換えた。
- 理由: 同上。POV-Ray の `now` は 2000-01-01 からの日数なので、意味は同じ。

### 4. `source/base/image/metadata.h`

パッチ: `source_base_image_metadata.h.patch`

- `boost::posix_time` / `boost::gregorian` の日時保持を、`gmtime_r()` で埋める
  整数（year/month/day/hour/minute/second）に置き換えた。
- `getDateTime()` は `snprintf()` で ISO 8601 風の文字列を作る。
- 理由: 画像メタデータのために Boost.DateTime をリンクしたくない。
- 注意: メタデータはホストの PPM などでは使わない。ターゲットでは出力形式を
  限定するため、さらに削れる余地がある。

### 5. `vfe/unix/unixconsole.cpp`

パッチ: `vfe_unix_unixconsole.cpp.patch`

- `SGNANO_CHECKPOINT` が設定されているとき、表示クリエータが
  `pov_frontend::SgnanoDisplay` を返すようにした。
- `SGNANO_START_ROW` があれば `+SR<行>` を追加する。
- チェックポイント利用時は **1 スレッド**、**AA なし**、**4×4 ブロック**に固定する。
- 理由:
  - `SgnanoDisplay` が POV-Ray の描画ブロックを受け取り、sgnano の RGB555 FB と
    チェックポイントへ渡す。
  - 1 ラインずつの確定と再開を単純にするため、描画順とスレッド数を固定する。
  - `+BS4` は POV-Ray の最小ブロックサイズ。4×4 ブロックが揃った時点で
    1 ラインの確定を判定する。

### 6. `vfe/vfe.cpp`

パッチ: `vfe_vfe.cpp.patch`（2026-11-07）

- `VirtualFrontEnd::~VirtualFrontEnd` の `DisconnectFromBackend` を `try { } catch (...) { }` で包んだ。
- 理由: 切り離しは後ろの返事を待つ（POVMS の WaitReply）。返事が時間切れになると `pov_base::Exception` が出るが、
  デストラクタは暗に noexcept なので catch に届かず `std::terminate` になった（実機で FB を出したまま描いている途中に
  ESC で止めたとき。全体が約 20 倍遅く、10 秒の上限を越えた）。切り離しの失敗は後片付けの中なので捨ててよい。
- あわせて SGX では返事を待つ上限 `kDefaultTimeout` を 10 秒から 60 秒に延ばした（`platform/sgx_pov/syspovconfig.h`。
  `povms.cpp` は `#ifndef` で受けるので上流のコードは変えていない）。

### 7. `source/backend/support/randomsequences.cpp` / `.h`、`source/backend/pattern/warps.cpp`、`source/backend/pattern/pattern.cpp`

パッチ: `source_backend_support_randomsequences.cpp.patch`、`source_backend_support_randomsequences.h.patch`、
`source_backend_pattern_warps.cpp.patch`、`source_backend_pattern_pattern.cpp.patch`（2026-11-06）

- `POV_RAND_HASHED` を定義したとき（SGX だけ。`platform/sgx_pov/syspovconfig.h`）、`RandomDoubleSequence` と
  ワープ・模様の乱数の表を作らず、番号から splitmix64 で計算する（`platform/sgx_pov/pov_hashrand.h`）。
- 理由: 表は 32768 個の double（256KB）で、Trace・MediaFunction・RadiosityFunction ごと・スレッドごとに作られ、
  PSRAM が足りなかった。定義しなければ上流と同じ。

## 追加したファイル（上流には無い）

| ファイル | 説明 |
|---|---|
| `compat/boost/thread.hpp` | Boost.Thread を `std::thread` / `std::mutex` / `std::condition_variable_any` に写すホスト用の互換層。ターゲットでは単一スレッド版に差し替える。 |
| `compat/boost/thread/condition.hpp` | 上記の condition 用ラッパー。 |
| `compat/jversion.h` | 上流の `povray.cpp` が `jversion.h` を include するため、bundled JPEG の代わりに置いたもの。ホストはシステム libjpeg を使う。 |
| `platform/host/config.h` | autoconf の `config.h` の代わり。ホスト・ビルド用の定義。 |
| `cmake/povray_sources.cmake` | 上流の `source/Makefile.am` と `vfe/Makefile.am` から生成したソース一覧。 |
| `app/` | sgnano アプリ共通層（チェックポイント、状態表示、ホスト FB）。 |
| `tools/run_host.py` | ホストでチェックポイント付き実行をするラッパー。 |

## 変更の再適用

上流を更新するときは、次の順で行う。

1. 新しい上流を `third_party/povray/` に展開する。
2. `third_party/povray/patches/*.patch` を `third_party/povray/` で適用する。
3. `source/Makefile.am` / `vfe/Makefile.am` が変わったら
   `cmake/povray_sources.cmake` を再生成する。
4. `platform/host/config.h` の `PACKAGE_VERSION` などを新しい版に合わせる。
5. ホストで `simple.pov` を描画し、チェックポイントの再開を確認する。

## 上流に返す候補

- Boost.DateTime の除去（`gmtime_r` 化）は、上流の移植性を損なわない形にできる
  かもしれない。ただし上流は Windows も対象なので、そのままでは返せない。
- `SgnanoDisplay` のフックは sgnano 固有なので上流には返さない。
- `syspovconfig.h` の既定出力変更はホスト用なので上流には返さない。
