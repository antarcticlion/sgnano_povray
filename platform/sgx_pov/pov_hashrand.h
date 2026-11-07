// 模様・ワープの乱数（2026-11-06、sgnano）。
// POV-Ray は PatternRands(Hash3d(升の座標)) のように「同じ番号にはいつも同じ値」を引くために、起動のとき 32768 個の double の表
// （256KB ずつ、作る途中の写しも入れて 1.5MB）を作る。SGX 版はその代わりに番号から値を計算する（splitmix64 の混ぜ方）。
// 決まった値を返すので、升の中の画素どうし・再開の前後で同じ値になる（ハードの乱数ではこれができない）。表は要らない。
// 値の並びはホストの表と違うので、crackle・cells・black_hole などの乱れの模様はホストの絵と一致しない
#ifndef POV_HASHRAND_H
#define POV_HASHRAND_H
#include <stdint.h>
#include <stddef.h>

namespace pov
{
class HashedDoubleSequence
{
public:
	HashedDoubleSequence(double minval, double maxval) : lo(minval), span(maxval - minval) {}
	double operator()(size_t index) const
	{
		uint64_t z = (uint64_t)index + 0x9E3779B97F4A7C15ull;
		z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
		z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
		z ^= z >> 31;
		return lo + span * ((double)(z >> 11) * (1.0 / 9007199254740992.0));   // [0, 1) の 53 ビット
	}
private:
	double lo, span;
};
}
#endif
