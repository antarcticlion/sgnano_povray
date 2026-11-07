// sgnano の SGX の VFE のプラットフォーム（2026-11-06）。vfe/unix/vfeplatform.h の代わり。
// 一時ファイル・時刻・誤りの知らせだけ（シェルアウトと入出力の制限は無し）
#ifndef __VFEPLATFORM_H__
#define __VFEPLATFORM_H__

namespace vfePlatform
{
	using namespace vfe;

	class vfeSgxSession : public vfeSession
	{
		public:
			vfeSgxSession(int id = 0) : vfeSession(id), m_Last(0) {}
			virtual ~vfeSgxSession() {}
			virtual UCS2String GetTemporaryPath(void) const;
			virtual UCS2String CreateTemporaryFile(void) const;
			virtual void DeleteTemporaryFile(const UCS2String& filename) const;
			virtual POV_LONG GetTimestamp(void) const;
			virtual void NotifyCriticalError(const char *message, const char *file, int line);
			virtual int RequestNewOutputPath(int CallCount, const string& Reason, const UCS2String& OldPath, UCS2String& NewPath);
			virtual bool TestAccessAllowed(const Path& file, bool isWrite) const { return true; }
			void SetTempDir(const char *dir) { m_TempDir = dir; }
		protected:
			virtual void WorkerThreadStartup() {}
			virtual void WorkerThreadShutdown() {}
			mutable POV_LONG m_Last;
			string m_TempDir;
	};

	POVMS_Sys_Thread_Type GetThreadId();
}

#endif
