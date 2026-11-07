// POV-Ray 3.7 の sgnano SGX のフロントエンド（2026-11-06）。
//
//   povray.sgx SCENE.pov [+W640 +H480 などの POV-Ray のオプション...] [-fresh] [-fbview] [-nofb]
//
// - ファイラーの対応表（S:/SystemSettings/FileTypes.cfg の `pov = S:/bin/povray.sgx %f`）と、シェルの `povray SCENE.pov` から走らせる
// - 描いている間はモード 7（320x200 の縮小、SRAM）、終わったらモード 2 で 640x480（ABI 4.3。FB を出していると 20 倍遅い）。
//   -fbview で描いている間もモード 2（-nofb はそのとき FB を隠す）
// - 描いた行は app/sgnano_app.c がチェックポイント（SCENE.ckp、ブロックストア）・FB のページ 0・PNG（SCENE.png）へ
// - ESC（SGX_F_ESC_KEY）で自分で止める: 描けた分を救い出し SCENE.partial.png を残す。次に同じシーンを走らせると続きから描く（-fresh で最初から）。
//   Shift+ESC はファームが強く止める（書けた行はチェックポイントに残る）
// - スレッドは compat_sgx/boost/thread.hpp（M33BEATOS のタスク）、VFE のプラットフォームは platform/sgx_pov
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <exception>
#include <typeinfo>
#include <cxxabi.h>

#include "vfe.h"
#include "sgx_api.h"
#include "app/sgnano_app.h"
#include "app/sgx/sgnano_app_sgx.h"

extern "C" const sgx_api_t *sgx;
extern "C" { unsigned sgx_compat_counts[4]; }
static void logf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void logf(const char *fmt, ...)
{
	char b[300];
	va_list ap; va_start(ap, fmt); const int n = vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
	if (n > 0) sgx->log(b, (unsigned)strlen(b));
}

// ---- base/timer.h・VFE のプラットフォームの口 ----
namespace pov_base
{
	void Delay(unsigned int msec) { sgx->sleep_us(msec * 1000u); }
	void vfeSysThreadStartup(void) {}
	void vfeSysThreadCleanup(void) {}

	// POV-Ray の時計（vfe/syspovprotobase.h）。CPU の時間は分けられないので実時間と同じ
	vfeTimer::vfeTimer(bool CPUTimeIsThreadOnly) : m_ThreadTimeOnly(CPUTimeIsThreadOnly), m_ThreadHandle(NULL), m_IsNT(false) { Reset(); }
	vfeTimer::~vfeTimer() {}
	unsigned POV_LONG vfeTimer::GetWallTime(void) const { return (unsigned POV_LONG)(sgx->us() / 1000u); }
	unsigned POV_LONG vfeTimer::GetCPUTime(void) const { return GetWallTime(); }
	POV_LONG vfeTimer::ElapsedRealTime(void) const { return (POV_LONG)(GetWallTime() - m_WallTimeStart); }
	POV_LONG vfeTimer::ElapsedCPUTime(void) const { return (POV_LONG)(GetCPUTime() - m_CPUTimeStart); }
	void vfeTimer::Reset(void) { m_WallTimeStart = GetWallTime(); m_CPUTimeStart = m_WallTimeStart; }
	bool vfeTimer::HasValidCPUTime() const { return false; }
}

namespace vfePlatform
{
	POVMS_Sys_Thread_Type GetThreadId() { return (POVMS_Sys_Thread_Type)(uintptr_t)sgx->task_self(); }

	UCS2String vfeSgxSession::GetTemporaryPath(void) const { return ASCIItoUCS2String((m_TempDir + "/").c_str()); }
	UCS2String vfeSgxSession::CreateTemporaryFile(void) const
	{
		static unsigned n;
		char s[FILE_NAME_LENGTH];
		snprintf(s, sizeof s, "%s/~pov%u.tmp", m_TempDir.c_str(), ++n);
		remove(s);
		return ASCIItoUCS2String(s);
	}
	void vfeSgxSession::DeleteTemporaryFile(const UCS2String& filename) const { remove(UCS2toASCIIString(filename).c_str()); }
	POV_LONG vfeSgxSession::GetTimestamp(void) const
	{
		POV_LONG t = (POV_LONG)(sgx->us() / 1000u);
		if (t < m_Last) t = m_Last;
		m_Last = t;
		return t;
	}
	void vfeSgxSession::NotifyCriticalError(const char *message, const char *file, int line)
	{
		char b[256];
		snprintf(b, sizeof b, "POV-Ray critical error: %s (%s:%d)\n", message, file ? file : "", line);
		sgx->term_write_err ? sgx->term_write_err(b, (unsigned)strlen(b)) : sgx->term_write(b, (unsigned)strlen(b));
	}
	int vfeSgxSession::RequestNewOutputPath(int, const string&, const UCS2String&, UCS2String&) { return 0; }
}

using namespace vfe;
using namespace vfePlatform;

// ---- 表示: 描いた画素を sgnano_app へ ----
namespace
{
static uint16_t to555(const vfeDisplay::RGBA8 &c)
{
	// sgnano のネイティブの RGB555: bit0-4=R, bit5-9=G, bit10-14=B
	return (uint16_t)(((c.red >> 3) & 31u) | (((c.green >> 3) & 31u) << 5) | (((c.blue >> 3) & 31u) << 10));
}

class SgxDisplay : public vfeDisplay
{
public:
	SgxDisplay(unsigned int w, unsigned int h, GammaCurvePtr gamma, vfeSession *session, bool visible)
		: vfeDisplay(w, h, gamma, session, visible) {}
	virtual void Initialise() {}
	virtual void Close() {}
	virtual void Show() {}
	virtual void Hide() {}
	virtual void DrawPixel(unsigned int x, unsigned int y, const RGBA8& c)
	{
		const uint16_t p = to555(c);
		sgnano_app_put_block(x, y, x, y, &p);
	}
	virtual void DrawPixelBlock(unsigned int x1, unsigned int y1, unsigned int x2, unsigned int y2, const RGBA8 *c)
	{
		if (!c || x2 < x1 || y2 < y1) return;
		const unsigned w = x2 - x1 + 1u, h = y2 - y1 + 1u;
		if (w * h > sizeof buf / sizeof buf[0]) {        // 大きいブロックは行ごとに
			for (unsigned y = 0; y < h; y++) DrawPixelBlock(x1, y1 + y, x2, y1 + y, c + (size_t)y * w);
			return;
		}
		for (unsigned i = 0; i < w * h; i++) buf[i] = to555(c[i]);
		sgnano_app_put_block(x1, y1, x2, y2, buf);
	}
	virtual void DrawFilledRectangle(unsigned int x1, unsigned int y1, unsigned int x2, unsigned int y2, const RGBA8& c)
	{
		sgnano_app_put_rect(x1, y1, x2, y2, to555(c));
	}
	virtual void DrawRectangleFrame(unsigned int, unsigned int, unsigned int, unsigned int, const RGBA8&) {}
	virtual void Clear() {}
private:
	uint16_t buf[640 * 4];
};

vfeDisplay *SgxDisplayCreator(unsigned int w, unsigned int h, GammaCurvePtr gamma, vfeSession *session, bool visible)
{
	return new SgxDisplay(w, h, gamma, session, visible);
}

static void say(const char *s) { sgx->term_write(s, (unsigned)strlen(s)); }
static void sayf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void sayf(const char *fmt, ...)
{
	char b[400];
	va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
	say(b);
}

// "C:/dir/scene.pov" -> "C:/dir/scene" + ext
static std::string with_ext(const std::string &scene, const char *ext)
{
	const size_t slash = scene.find_last_of('/'), dot = scene.find_last_of('.');
	const std::string base = (dot != std::string::npos && (slash == std::string::npos || dot > slash)) ? scene.substr(0, dot) : scene;
	return base + ext;
}
static std::string dir_of(const std::string &p)
{
	const size_t slash = p.find_last_of('/');
	return slash == std::string::npos ? std::string(".") : (slash > 0 && p[slash - 1] == ':' ? p.substr(0, slash + 1) : p.substr(0, slash));
}
static unsigned opt_num(const std::vector<std::string> &opts, char key, unsigned def)
{
	for (size_t i = 0; i < opts.size(); i++) {
		const std::string &o = opts[i];
		if (o.size() > 2 && (o[0] == '+' || o[0] == '-') && (o[1] == key || o[1] == (char)(key + 32))) return (unsigned)atoi(o.c_str() + 2);
	}
	return def;
}
}

// 受け取られなかった例外: 型の名前と what() を出してから終わる（切り分けのため。2026-11-06）
static void on_terminate()
{
	const std::type_info *t = abi::__cxa_current_exception_type();
	const char *what = "";
	if (t) { try { throw; } catch (const std::exception &e) { what = e.what(); } catch (...) {} }
	logf("[pov] terminate: uncaught %s: %s (task %p)\n", t ? t->name() : "(none)", what, (void *)sgx->task_self());
	sgnano_app_finish(false);   // 描けた分をチェックポイントへ救い出し、.partial.png を残す（2026-11-07）
	char b[300]; snprintf(b, sizeof b, "povray: uncaught exception %s: %s\n", t ? t->name() : "(none)", what); say(b);
	sgx->exit(134);
	for (;;) {}
}
// std::set_terminate は PSRAM の変数を不可分の命令で書き換えて止まるので、libsupc++ の変数そのものをこちらで定める
// （eh_term_handler.o は引き込まれない）
namespace __cxxabiv1 { extern std::terminate_handler __terminate_handler; std::terminate_handler __terminate_handler = on_terminate; }

extern "C" uint32_t sgx_heap_trace_min;   // SDK（切り分けのときは 16384 などにする: その大きさ以上の malloc をログへ）

namespace
{
struct Args
{
	std::string scene;
	std::vector<std::string> opts;   // POV-Ray にそのまま渡すもの
	bool fresh = false;              // -fresh: チェックポイントを消して最初から
	bool fbview = false;             // -fbview: 描いている間もモード 2 で FB を出す（遅い。2026-11-07）
	bool nofb = false;               // -nofb: （-fbview のとき）描いている間 FB を隠す（2026-11-06）
	unsigned W = 640, H = 480;
};

// false = 引数が悪い（言うことは言った）
static bool parse_args(int argc, char **argv, Args &a)
{
	if (argc < 2) { say("usage: povray.sgx SCENE.pov [POV-Ray options...] [-fresh] [-fbview] [-nofb]\n"); return false; }
	a.scene = argv[1];
	for (int i = 2; i < argc; i++) {
		if (!strcmp(argv[i], "-fresh")) a.fresh = true;
		else if (!strcmp(argv[i], "-nofb")) a.nofb = true;
		else if (!strcmp(argv[i], "-fbview")) a.fbview = true;
		else a.opts.push_back(argv[i]);
	}
	a.W = opt_num(a.opts, 'W', 640);
	a.H = opt_num(a.opts, 'H', 480);
	if (a.W < 16 || a.W > 640 || a.H < 16 || a.H > SGNANO_CKP_MAX_LINES) {
		sayf("povray: size %ux%u is out of range (16..640 x 16..%u)\n", a.W, a.H, SGNANO_CKP_MAX_LINES);
		return false;
	}
	return true;
}

// シーンの中身が変われば続きを使わない（シーンの名前と大きさと書いた日時、オプションと大きさ）
static void hash_inputs(const Args &a, sgnano_app_config_t &cfg)
{
	sgx_stat_t st; memset(&st, 0, sizeof st);
	(void)sgx->stat(a.scene.c_str(), &st);
	uint32_t h = sgnano_app_fnv1a(a.scene.data(), a.scene.size(), 0x811C9DC5u);
	h = sgnano_app_fnv1a(&st.size, sizeof st.size, h);
	h = sgnano_app_fnv1a(&st.mtime, sizeof st.mtime, h);
	cfg.scene_hash = h;
	std::string all; for (size_t i = 0; i < a.opts.size(); i++) all += a.opts[i] + " ";
	cfg.options_hash = sgnano_app_fnv1a(all.data(), all.size(), (a.W << 16) | a.H);
}

// 画面を決め、チェックポイントを開き、余りの FB のページをヒープへ。preview = モード 7 で縮めて出す。false = 開けない（言った）
static bool open_screen_and_app(const Args &a, const std::string &ckp, const std::string &png, bool &preview)
{
	preview = !a.fbview && sgnano_sgx_enter_preview(a.W, a.H);   // モード 7 に縮めて出す（FB のページ 0 には全部を書く）
	if (!preview && !sgnano_sgx_enter_fb(a.W, a.H)) { say("povray: could not enter screen mode 2\n"); return false; }
	logf("[pov] screen: %s\n", preview ? "mode 7 preview (320x200), the FB at the end" : "mode 2 (the FB while rendering)");
	sgnano_app_config_t cfg;
	memset(&cfg, 0, sizeof cfg);
	cfg.scene_path = a.scene.c_str();
	cfg.checkpoint_path = ckp.c_str();
	cfg.image_path = png.c_str();
	cfg.width = a.W; cfg.height = a.H;
	cfg.block_size = 4;
	hash_inputs(a, cfg);
	cfg.outputs = SGNANO_OUTPUT_FB | SGNANO_OUTPUT_PNG;
	cfg.resume = true;
	if (!sgnano_app_init(&cfg, sgnano_sgx_platform())) { say("povray: could not open the checkpoint\n"); sgnano_sgx_shutdown(); return false; }
	if (a.nofb && sgx->set_fb_visible) sgx->set_fb_visible(false);
	if (sgnano_sgx_lend_spare()) {
		const unsigned h = sgnano_sgx_spare_to_heap();   // 描画のあいだのメモリにも使う（PSRAM が足りずスレッドが作れなかった。2026-11-06）
		logf("[pov] spare FB pages to the heap: %u bytes\n", h);
	} else logf("[pov] spare FB pages: not lent\n");
	return true;
}

static void make_options(vfeRenderOptions &ro, const Args &a, const std::string &dir, uint32_t start)
{
	ro.SetThreadCount(1);
	ro.AddCommand("+I" + a.scene);
	ro.AddCommand("+D");          // 表示（SgxDisplay）
	ro.AddCommand("-F");          // POV-Ray のファイル出力は使わない（sgnano_app が PNG を書く）
	ro.AddCommand("-V");
	ro.AddCommand("-A");          // 行ごとの再開と合わせる（host と同じ）
	ro.AddCommand("+BS4");
	ro.AddCommand("-P");
	ro.AddCommand("+WT1");        // 描画のスレッドは 1 本（SGX は core1 だけで走るので増やしても速くならず、スタック 128KB ずつ PSRAM を食う。引数で上書きできる）
	char b[32];
	snprintf(b, sizeof b, "+W%u", a.W); ro.AddCommand(b);
	snprintf(b, sizeof b, "+H%u", a.H); ro.AddCommand(b);
	if (start) { snprintf(b, sizeof b, "+SR%u", (unsigned)start + 1u); ro.AddCommand(b); }   // POV-Ray の行は 1 から
	for (size_t i = 0; i < a.opts.size(); i++) ro.AddCommand(a.opts[i]);
	ro.AddLibraryPath(ASCIItoUCS2String(dir.c_str()));
}

// 5 秒ごとの様子（ログだけ）
static void log_alive(int state)
{
	uint32_t puts = 0, c0 = 0, cpl = 0; int why = 0; bool err = false;
	sgnano_app_debug(&puts, &why, &err, &c0, &cpl);
	logf("[pov] alive: state %d lines %u (saved %u, errors %u) puts %lu ckp_err %d why %d heap %lu free %lu\n", state,
	     (unsigned)sgnano_app_lines_drawn(), (unsigned)sgnano_app_lines_done(), (unsigned)sgnano_app_ckp_fails(), (unsigned long)puts,
	     (int)err, why, (unsigned long)sgx_heap_total(), (unsigned long)sgx_heap_free_bytes());
}

struct RunResult { bool cancelled = false; bool threw = false; };

// 描き終わる（または止まる）まで回す。止める頼み:
// - ESC（SGX_F_ESC_KEY で届く）: 自分で止めて、描けた分をチェックポイントへ救い出し .partial.png を残す（2026-11-07）
// - Shift+ESC・遠隔の RSTOP はファームが強く止める（書けた行はチェックポイントに残る。.partial.png は無い）
// 例外（2026-11-07）: 止めたあとの POVMS の時間切れ（pov_base::Exception）などはここで受けて終わりの処理へ進む。
// 止めてから 60 秒たっても終わらなければ待つのをやめる
static RunResult run_render(vfeSgxSession *session)
{
	RunResult r;
	int last_state = -1;
	uint32_t last_beat = sgx->ms(), cancel_ms = 0;
	auto cancel = [&](const char *why) { r.cancelled = true; cancel_ms = sgx->ms(); say(why); session->CancelRender(); };
	try {
		vfeStatusFlags flags;
		while (((flags = session->GetStatus(true, 200)) & stRenderShutdown) == 0) {
			if (cancel_ms && sgx->ms() - cancel_ms > 60000u) { logf("[pov] the render did not stop in 60 s after the cancel\n"); break; }
			vfeSession::MessageType type; string str;
			while (session->GetNextCombinedMessage(type, str)) {
				logf("[pov] msg %d: %s\n", (int)type, str.c_str());
				if (type == vfeSession::mGenericStatus || type == vfeSession::mUnclassified) continue;
				str += "\n"; say(str.c_str());
			}
			const int st = (int)session->GetBackendState();
			if (st != last_state) { logf("[pov] backend state %d (flags 0x%x)\n", st, (unsigned)flags); last_state = st; }
			if (sgx->ms() - last_beat > 5000u) { last_beat = sgx->ms(); log_alive(st); }
			sgnano_app_tick();
			for (int k; !r.cancelled && (k = sgx->key(false)) >= 0; )
				if ((k & 0xFF) == 0x1B) cancel("povray: ESC -- stopping (saving the finished lines and a .partial.png)\n");
			if (!r.cancelled && sgx->stop_requested()) cancel("povray: stopping (the checkpoint keeps the finished lines)\n");
		}
	} catch (const std::exception &e) { r.threw = true; logf("[pov] main loop: exception %s\n", e.what()); sayf("povray: %s\n", e.what()); }
	catch (...) { r.threw = true; logf("[pov] main loop: unknown exception\n"); }
	return r;
}

static void shutdown_session(vfeSgxSession *session)
{
	try { session->Shutdown(); delete session; }
	catch (const std::exception &e) { logf("[pov] shutdown: exception %s\n", e.what()); }
	catch (...) { logf("[pov] shutdown: unknown exception\n"); }
}
}

extern "C" int sgx_main(const sgx_api_t *api, int argc, char **argv)
{
	(void)api;
	sgx_heap_trace_min = 0;
	Args a;
	if (!parse_args(argc, argv, a)) return 2;
	const std::string ckp = with_ext(a.scene, ".ckp"), png = with_ext(a.scene, ".png"), dir = dir_of(a.scene);
	const uint32_t t0 = sgx->ms();
	sayf("POV-Ray 3.7 for sgnano: %s (%ux%u)\n", a.scene.c_str(), a.W, a.H);
	if (a.fresh) remove(ckp.c_str());

	bool preview = false;
	if (!open_screen_and_app(a, ckp, png, preview)) return 1;
	const uint32_t start = sgnano_app_resume_row();
	if (start) sayf("povray: resuming at line %u\n", (unsigned)start);
	if (sgnano_app_all_done()) { say("povray: the checkpoint is already complete\n"); sgnano_app_finish(true); sgnano_sgx_shutdown(); return 0; }

	logf("[pov] creating the session\n");
	vfeSgxSession *session = new vfeSgxSession();
	session->SetTempDir(dir.c_str());
	if (session->Initialize(NULL, NULL) != vfeNoError) { sayf("povray: %s\n", session->GetErrorString()); sgnano_app_finish(false); sgnano_sgx_shutdown(); return 1; }
	logf("[pov] session initialised; sync objects: mutex %u (+%u own) sem %u (+%u polled)\n", sgx_compat_counts[0], sgx_compat_counts[1], sgx_compat_counts[2], sgx_compat_counts[3]);
	session->SetDisplayCreator(SgxDisplayCreator);
	vfeRenderOptions ro;
	make_options(ro, a, dir, start);
	if (session->SetOptions(ro) != vfeNoError || session->StartRender() != vfeNoError) {
		sayf("povray: %s\n", session->GetErrorString());
		shutdown_session(session);
		sgnano_app_finish(false); sgnano_sgx_shutdown();
		return 1;
	}

	const RunResult r = run_render(session);
	const bool ok = !r.threw && session->Succeeded() && sgnano_app_lines_drawn() >= a.H;   // 描き終えたか（チェックポイントに書けたかは状態の行に出す）
	if (!r.threw && !session->Succeeded() && !r.cancelled) sayf("povray: %s\n", session->GetErrorString());
	shutdown_session(session);
	if (preview && !sgnano_sgx_show_fb()) logf("[pov] could not switch to mode 2 to show the FB\n");   // 描き終えたら 640x480 を出す
	sgnano_app_finish(ok);
	const uint32_t dt = sgx->ms() - t0;
	sayf("POVRAY RESULT: %s %s %ux%u %lu.%03lu s\n", ok ? "done" : (r.cancelled ? "stopped" : "failed"), a.scene.c_str(), a.W, a.H,
	     (unsigned long)(dt / 1000u), (unsigned long)(dt % 1000u));
	if (ok) sayf("povray: wrote %s\n", png.c_str());
	sgnano_sgx_shutdown();
	return ok ? 0 : 1;
}
