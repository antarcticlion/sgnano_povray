# tests

- `scenes/simple.pov`: ホストのスモークテスト用の簡単なシーン。
- 生成物（`*.ppm`, `*.ckp`）は `.gitignore` で除外している。

チェックポイント付きの実行:

```sh
python3 tools/run_host.py tests/scenes/simple.pov build_host/simple.ppm \
  --width 640 --height 480
```
