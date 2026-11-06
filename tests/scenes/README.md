# テストシーン

POV-Ray 3.7 用のテストデータ。すべて自己完結（外部 include なし）で、
`#version 3.7;` と sRGB 色を使う。`generate_scenes.py` から生成しているので、
シーンを直すときは生成器を直して再生成する。

```sh
python3 tests/scenes/generate_scenes.py
```

## 一覧

| ファイル | 内容 | 主な特徴 | 目安 |
|---|---|---|---|
| `00_fast_sphere.pov` | **高速テスト** | 球1個＋平面＋1灯。反射なし | 640×480 でも数秒 |
| `01_geo_landscape.pov` | 幾何学的ランドスケープ | 低ポリ地形、水面、浮遊形状、紫〜橙の空 | 中 |
| `02_arch_perspective.pov` | 建築パースペクティブ | ガラス張りの建物、柱、階段、広場 | 中 |
| `03_car.pov` | 自動車 | 低ポリのスポーツカー、ショールーム、反射床 | 中 |
| `04_city_night.pov` | ネオン都市（夜） | 塔、窓の光、ネオン、濡れた路面 | 中〜重 |
| `05_desert_monolith.pov` | 砂漠のモノリス | 砂丘、黒い巨石、発光する継ぎ目、夕日 | 中 |
| `06_underwater.pov` | 水中の珊瑚礁 | フォグ、珊瑚、泡、宝箱 | 中 |
| `07_space_station.pov` | 軌道リングステーション | 星、金属のリング、ソーラーパネル | 中 |
| `08_fruit_still_life.pov` | フルーツの静物 | 果物、器、ガラスの花瓶 | 軽〜中 |
| `09_abstract_crystal.pov` | 抽象クリスタル | 透明な結晶、反射床、カラーライト | 中 |
| `10_forest_path.pov` | 森の小道 | 木、道、蛍、夕方の光 | 中 |
| `11_slow_spheres.pov` | **低速テスト** | SDL ループで 10,000 球、反射床、ガラス | 非常に重い |

## 使い方

1 シーンをチェックポイント付きで描く:

```sh
python3 tools/run_host.py tests/scenes/01_geo_landscape.pov build_host/geo.ppm \
  --width 640 --height 480
```

全部まとめて描く:

```sh
tools/render_scenes.sh 320 240 build_host/scenes
```

`render_scenes.sh` は POV-Ray を直接呼ぶ（チェックポイントなし）。
`00_fast_sphere` はスモークテスト、`11_slow_spheres` は
「途中で止めて再開する」試験や性能比較に向く。

## 色

- `global_settings { assumed_gamma 1.0 }`
- 色は `srgb <r,g,b>`（0〜1）。
- 反射・ガラス・発光（`ambient 1 diffuse 0`）を積極的に使う。
- ネオン・水中・クリスタルは、複数の色付きライトで色味を出している。

## 注意

- `11_slow_spheres.pov` は 160×120 でもホストで数秒、640×480 では数分かかる。
  実機（302MHz M33）ではさらに長くなる。チェックポイントの試験に使うときは
  `--timeout` で途中終了させるとよい。
- POV-Ray の AA は `-A` で切っている。AA を有効にすると時間とメモリが増える。
- 画像出力は PPM（`+FP`）。PNG/JPEG はホスト版では無効にしている。
