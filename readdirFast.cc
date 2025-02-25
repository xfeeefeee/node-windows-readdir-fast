#include "napi.h"

#define WIN32_LEAN_AND_MEAN

#include <vector>
#include <string>
#define NTDDI_VERSION NTDDI_WIN7
#include <Windows.h>

typedef LONG NTSTATUS;

#pragma comment( lib, "ntdll" )

// Convert a wide Unicode string to an UTF8 string
std::string utf8_encode(const std::wstring &wstr)
{
    if (wstr.empty()) return std::string();

    int cchNeeded = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);

    std::string strTo(cchNeeded, '\0');
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], cchNeeded, NULL, NULL);
    return strTo;
}

// Convert an UTF8 string to a wide Unicode String
std::wstring utf8_decode(const std::string &str)
{
    if (str.empty()) return std::wstring();

    int cchNeeded = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);

    std::wstring wstrTo(cchNeeded, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstrTo[0], cchNeeded);
    return wstrTo;
}

// https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/ns-wdm-_io_status_block?redirectedfrom=MSDN
typedef struct _IO_STATUS_BLOCK {
	union {
		NTSTATUS Status;
		PVOID    Pointer;
	};
	ULONG_PTR Information;
} IO_STATUS_BLOCK, * PIO_STATUS_BLOCK;

// https://learn.microsoft.com/en-us/windows/win32/api/ntdef/ns-ntdef-_unicode_string
typedef struct _UNICODE_STRING {
	USHORT Length;
	USHORT MaximumLength;
	PWSTR  Buffer;
} UNICODE_STRING, * PUNICODE_STRING;

// See the handy table linked on the page below to learn where these values comes from.
// https://learn.microsoft.com/en-us/windows-hardware/drivers/kernel/using-ntstatus-values
constexpr NTSTATUS STATUS_NO_MORE_FILES = 0x80000006;
constexpr NTSTATUS STATUS_NO_SUCH_FILE = 0xC000000F;

extern "C" {
	NTSYSCALLAPI NTSTATUS NTAPI NtQueryDirectoryFileEx(
		HANDLE FileHandle,
		HANDLE Event,
		// This here is PIO_APC_ROUTINE, but we don't use APCs and just set it to null.
		PVOID ApcRoutine,
		PVOID ApcContext,
		PIO_STATUS_BLOCK IoStatusBlock,
		// The struct for this depends on what information you need. All documented here:
		// https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-ntquerydirectoryfileex
		PVOID FileInformation,
		ULONG Length,
		// This is FILE_INFORMATION_CLASS, which I am not going to paste here.
		// https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/ne-wdm-_file_information_class
		DWORD FileInformationClass,
		ULONG QueryFlags,
		// Your puns here.
		PUNICODE_STRING FileName
	);
}

typedef struct _FILE_DIRECTORY_INFORMATION
{
	ULONG NextEntryOffset;
	ULONG FileIndex;
	LARGE_INTEGER CreationTime;
	LARGE_INTEGER LastAccessTime;
	LARGE_INTEGER LastWriteTime;
	LARGE_INTEGER ChangeTime;
	LARGE_INTEGER EndOfFile;
	LARGE_INTEGER AllocationSize;
	ULONG FileAttributes;
	ULONG FileNameLength;
	WCHAR FileName[1];
} FILE_DIRECTORY_INFORMATION;

#define NT_SUCCESS(Status)  (((NTSTATUS)(Status)) >= 0)


/*

prior art:

https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-ntquerydirectoryfile
https://github.com/chromium/vs-chromium/commit/ca8e2f5bdb6d74c16d000abd74805991e1ec40a5
https://github.com/git-for-windows/git/commit/b69c08c338403a3f8fd2394180664cb9f8164c78#diff-4b6d4f2af4b31f0bc3ff43eac9a2a437
https://blog.s-schoener.com/2024-06-24-find-files-internals/

*/

std::wstring to_wstr(uint32_t etc) {
	wchar_t buf[64];
	auto str = _itow(etc, buf, 10);
	return std::wstring(str);
}

struct BufferView {
	size_t length = 0;
	uint8_t* data = nullptr;
};

// tested an alternate that maintains a list of 0x20000 or whatever sized pages
// and consolidates at the end. less copying for huge results, but also means
// twice as much memory at the very end when allocating a new buffer the size 
// of all buffers in the list. Using VirtualAlloc can avoid this by copying
// one buffer at a time and releasing, since pages from VirtualAlloc does 
// not grab physical memory until accessed, but does not seem worth the trouble
//
// actually since electron prevents us from using external buffers, we might as
// well just collect the buffers and write them all out to avoid unnecessary copies

struct SimpleBinaryStream {
	SimpleBinaryStream() 
		: pData(std::unique_ptr<uint8_t[]>((uint8_t*)malloc(0x20000)))
		, pPos(pData.get())
		, pEnd(pData.get() + 0x20000)
	{
	}
	
	void write(const void* pSrc, size_t len) {
		auto pSrcBytes = (const uint8_t*)pSrc;
		while ((pEnd - pPos) < static_cast<ptrdiff_t>(len)) {
			realloc();
		}
		
		memcpy(pPos, pSrcBytes, len);
		pPos += len;
	}
	
	size_t len() const {
		return pPos - pData.get();
	}

	void copyAndReset(void* dest) {
		memcpy(dest, pData.get(), len());
		pData.reset();
		pPos = nullptr;
		pEnd = nullptr;
	}
	
protected:

	void realloc() {
		size_t curSize = pEnd - pData.get();
		size_t newSize = curSize * 2;
		
		size_t currentPos = pPos - pData.get();
		
		auto pNewData = std::unique_ptr<uint8_t[]>((uint8_t*)malloc(newSize));
		memcpy(pNewData.get(), pData.get(), currentPos);

		pData.swap(pNewData);
		
		pEnd = pData.get() + newSize;
		pPos = pData.get() + currentPos;
	}

	std::unique_ptr<uint8_t[]> pData;
	uint8_t* pPos;
	uint8_t* pEnd;	
};

struct PagedBinaryStream {
	static const size_t pageSize = 0x10000;

	PagedBinaryStream() 
		: pData(std::unique_ptr<uint8_t[]>(new uint8_t[pageSize]))
		, pPos(pData.get())
		, pEnd(pData.get() + pageSize)
	{
	}
	
	void write(const void* pSrcVoid, size_t len) {
		auto pSrc = (const uint8_t*)pSrcVoid;

		do {
			if (pPos >= pEnd) {
				fullBuffers.push_back(std::move(pData));
				pData = std::unique_ptr<uint8_t[]>(new uint8_t[pageSize]);
				pPos = pData.get();
				pEnd = pData.get() + pageSize;
			}
			size_t availableBytes = static_cast<size_t>(pEnd - pPos);
			size_t copiedBytes = (len > availableBytes) ? availableBytes : len;

			memcpy(pPos, pSrc, copiedBytes);
			pSrc += copiedBytes;
			pPos += copiedBytes;
			len -= copiedBytes;
		} while (len > 0);
	}

	size_t len() const {
		return (fullBuffers.size() * pageSize) + (pPos - pData.get());
	}

	// TODO actually destructive, rename this
	void copyAndReset(void* dest) {
		auto pDest = (uint8_t*)dest;
		for (auto& pBuf : fullBuffers) {
			memcpy(pDest, pBuf.get(), pageSize);
			pDest += pageSize;
			pBuf.reset();
		}
		memcpy(pDest, pData.get(), (pPos - pData.get()));
		fullBuffers.clear();
		pData.reset();
		pPos = nullptr;
		pEnd = nullptr;
	}
	
protected:

	std::unique_ptr<uint8_t[]> pData;
	uint8_t* pPos;
	uint8_t* pEnd;

	std::vector<std::unique_ptr<uint8_t[]>> fullBuffers;
};

using BinaryStream = PagedBinaryStream;

static inline long long filetimeToUnixTimestampInMs(long long value) {
	/* Windows to Unix Epoch conversion */
	value -= 116444736000000000LL;
	return value / 10000;
}

static const wchar_t wszDirPrefix[] = L"\\\\?\\";
static const size_t nbytesDirPrefix = sizeof(wszDirPrefix) - 2;
static const size_t cchDirPrefix = (sizeof(wszDirPrefix) - 1) / 2;

std::wstring normalizeDir(const wchar_t* wszDir) {
	// trim trailing slashes
	auto cchDir = wcslen(wszDir);
	while (cchDir > 1 && (wszDir[cchDir - 1] == L'\\' || wszDir[cchDir - 1] == L'/')) {
		cchDir -= 1;
	}
	
	std::wstring dirStr;
	
	// add prefix if necessary
	if (0 == memcmp(wszDir, wszDirPrefix, nbytesDirPrefix)) {
		// already has \\?\ prefix
		dirStr.assign(wszDir, cchDir);
	} else {
		dirStr.reserve(cchDirPrefix + cchDir + 1);
		dirStr.append(wszDirPrefix);
		dirStr.append(wszDir, cchDir);
	}
	
	// change any / to \ to normalize
	for (size_t ix = cchDirPrefix; ix < dirStr.length(); ++ix) {
		if (dirStr[ix] == L'/') {
			dirStr[ix] = L'\\';
		}
	}
	
	return dirStr;
}

constexpr uint32_t FILE_ATTRIBUTE_SPECIAL_FULLPATH_MASK = 0xFFFFFFFF;

static inline void writeFileToStream(const FILE_DIRECTORY_INFORMATION* file, BinaryStream& stream) {
	// file name length in bytes (not characters)
	stream.write(&file->FileNameLength, sizeof(file->FileNameLength));
	// file name (not null terminated)
	stream.write(file->FileName, file->FileNameLength);
	// attributes
	uint32_t attributes = file->FileAttributes;
	stream.write(&attributes, sizeof(attributes));
	// length
	double length = (double)(file->EndOfFile.QuadPart - 1);
	stream.write(&length, sizeof(length));
	// CreationTime
	double creationTime = (double)filetimeToUnixTimestampInMs(file->CreationTime.QuadPart);
	stream.write(&creationTime, sizeof(creationTime));
	// LastWriteTime
	double lastWriteTime = (double)filetimeToUnixTimestampInMs(file->LastWriteTime.QuadPart);
	stream.write(&lastWriteTime, sizeof(lastWriteTime));
}

static inline void writeFullPathToStream(const std::wstring& fullPath, BinaryStream& stream) {
	size_t cchFullPath = fullPath.length();
	const wchar_t* wszFullPath = fullPath.c_str();
	
	// remove \\?\ prefix if needed
	if (0 == memcmp(wszFullPath, wszDirPrefix, nbytesDirPrefix)) {
		wszFullPath += cchDirPrefix;
		cchFullPath -= cchDirPrefix;
	}
	
	// file name length in bytes (not characters)
	uint32_t fullPathLength = cchFullPath * sizeof(wchar_t);
	stream.write(&fullPathLength, 4);
	// file name (not null terminated)
	stream.write(wszFullPath, cchFullPath * sizeof(wchar_t));
	// attributes
	uint32_t attributes = FILE_ATTRIBUTE_SPECIAL_FULLPATH_MASK;
	stream.write(&attributes, sizeof(attributes));
}

// returns error message or empty string if no error
std::string direnum_NtQueryDirectory_toStream(const Napi::Env& env, const wchar_t* rootDir, BinaryStream& stream, bool recurse) {
	std::vector<std::wstring> dirs;
	dirs.push_back(normalizeDir(rootDir));
	
	uint32_t dirCount = 0;

	uint8_t buffer[0x10000];

	while (!dirs.empty()) {
		auto dir = dirs.back();
		dirs.pop_back();

		HANDLE dirHandle = CreateFileW(dir.c_str(),
			FILE_LIST_DIRECTORY,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			nullptr,
			OPEN_EXISTING,
			FILE_FLAG_BACKUP_SEMANTICS,
			0
		);

		// ensure this opens OK
		if (dirHandle == 0 || dirHandle == INVALID_HANDLE_VALUE) {
			if (dirCount == 0) {
				// only fail on the first / root directory, otherwise just skip and ignore
				return "failed to open directory: " + utf8_encode(dir);
			} else {
				// ignore and skip
				continue;
			}
		}
		
		++dirCount;

		constexpr DWORD SL_RESTART_SCAN = 0x1;
		constexpr DWORD FileDirectoryInformation = 0x1;
		IO_STATUS_BLOCK statusBlock = {0};

		NTSTATUS status = NtQueryDirectoryFileEx(
			dirHandle, 0, nullptr, nullptr,
			&statusBlock, buffer, sizeof(buffer),
			FileDirectoryInformation,
			SL_RESTART_SCAN,
			nullptr
		);
		const size_t bytesWritten = (size_t)statusBlock.Information;
		// maybe check for NTSUCCESS of status (eg status >= 0) too
		if (bytesWritten == 0 || status == STATUS_NO_SUCH_FILE) {
			// No file entries found -- this is impossible in this case because we did not
			// specifiy a search string, so we'll find '.' and '..' at the very least.
			CloseHandle(dirHandle);
			continue;
		}
		
		bool bWroteFullPath = false;

		FILE_DIRECTORY_INFORMATION* file = (FILE_DIRECTORY_INFORMATION*)buffer;
		while (true) {
			if (!bWroteFullPath) {
				bWroteFullPath = true;
				writeFullPathToStream(dir, stream);
			}

			size_t CchFileName = file->FileNameLength / sizeof(wchar_t);

			if ( (file->FileAttributes & FILE_ATTRIBUTE_DIRECTORY) && ( (CchFileName == 1 && file->FileName[0] == L'.')
					|| (CchFileName == 2 && file->FileName[0] == L'.' && file->FileName[1] == L'.') ) ) {
				// ignore
			} else {				
				
				if (file->FileAttributes & FILE_ATTRIBUTE_DIRECTORY && recurse) {
					if (recurse) {
						std::wstring nextDir;
						nextDir.reserve(dir.length() + 1 + CchFileName + 1);
						nextDir.append(dir);
						nextDir.append(L"\\", 1);
						nextDir.append(file->FileName, CchFileName);
						dirs.push_back(std::move(nextDir));
					}
				}
				
				writeFileToStream(file, stream);
			}

			if (file->NextEntryOffset != 0) {
				file = (FILE_DIRECTORY_INFORMATION*)(((uint8_t*)file) + file->NextEntryOffset);
			}
			else {
				// Now just call the function again. The state of the search is implictly tied
				// to the handle we are using for the directory.
				NTSTATUS status = NtQueryDirectoryFileEx(
					dirHandle, 0, nullptr, nullptr,
					&statusBlock, buffer, sizeof(buffer),
					FileDirectoryInformation,
					0,
					nullptr
				);
				
				if (status == STATUS_NO_MORE_FILES) {
					// we're done!
					break;
				} else if (!NT_SUCCESS(status)) {
					break;
				}
				const size_t bytesWritten = (size_t)statusBlock.Information;
				// maybe check for NTSUCCESS of status (eg status >= 0) too
				if (bytesWritten == 0) {
					// meh
				}
				file = (FILE_DIRECTORY_INFORMATION*)buffer;
			}
		}

		CloseHandle(dirHandle);
	}

	return {};
}



class ReaddirFastWorker : public Napi::AsyncWorker {
	public:
	ReaddirFastWorker(const Napi::Env& env, std::u16string path, bool recurse)
		: Napi::AsyncWorker{env, "ReaddirFastWorker"},
		m_deferred{env},
		m_path(path),
		m_recurse(recurse)
	{}

	Napi::Promise GetPromise() { return m_deferred.Promise(); }

	protected:
	void Execute() {
		auto errorMessage = direnum_NtQueryDirectory_toStream(Env(), (wchar_t*)m_path.c_str(), m_resultStream, m_recurse);
		if (!errorMessage.empty()) {
			SetError(errorMessage);
		}
	}

	void OnOK() {
		// no external buffers in electron, hence the PagedBinaryStream

		// auto buf = m_resultStream.detach();
		// auto arrayBuffer = Napi::ArrayBuffer::NewOrCopy(Env(), buf.data, buf.length, [](Napi::BasicEnv env, void* finalizeData) {
		// 	free(static_cast<uint8_t*>(finalizeData));
		// });

		auto arrayBuffer = Napi::ArrayBuffer::New(Env(), m_resultStream.len());
		m_resultStream.copyAndReset(arrayBuffer.Data());

		m_deferred.Resolve(arrayBuffer);
	}

	void OnError(const Napi::Error& err) { m_deferred.Reject(err.Value()); }

	private:
	Napi::Promise::Deferred m_deferred;

	std::u16string m_path;
	bool m_recurse;

	BinaryStream m_resultStream;
};


Napi::Value DoFastReadDir(const Napi::CallbackInfo& info) {
	Napi::Env env = info.Env();

	if (!info[0].IsString()) {
		Napi::TypeError::New(env, "path must be a string")
			.ThrowAsJavaScriptException();
		return env.Undefined();
	}
	std::u16string path = info[0].As<Napi::String>();

	if (!info[1].IsBoolean()) {
		Napi::TypeError::New(env, "recurse must be boolean")
			.ThrowAsJavaScriptException();
		return env.Undefined();
	}
	bool recurse = info[1].As<Napi::Boolean>();

	ReaddirFastWorker* worker = new ReaddirFastWorker(env, path, recurse);
	worker->Queue();
	return worker->GetPromise();
}

Napi::Value DoFastReadDirSync(const Napi::CallbackInfo& info) {
	Napi::Env env = info.Env();

	if (!info[0].IsString()) {
		Napi::TypeError::New(env, "path must be a string")
			.ThrowAsJavaScriptException();
		return env.Undefined();
	}
	std::u16string path = info[0].As<Napi::String>();

	if (!info[1].IsBoolean()) {
		Napi::TypeError::New(env, "recurse must be boolean")
			.ThrowAsJavaScriptException();
		return env.Undefined();
	}
	bool recurse = info[1].As<Napi::Boolean>();

	BinaryStream resultStream;
	auto errorMessage = direnum_NtQueryDirectory_toStream(env, (wchar_t*)path.c_str(), resultStream, recurse);

	if (!errorMessage.empty()) {
		Napi::Error::New(env, errorMessage).ThrowAsJavaScriptException();
	}

	// no external buffers in electron, hence the PagedBinaryStream
	//   auto buf = resultStream.detach();
	//   auto arrayBuffer = Napi::ArrayBuffer::NewOrCopy(env, buf.data, buf.length, [](Napi::BasicEnv env, void* finalizeData) {
	//     free(static_cast<uint8_t*>(finalizeData));
	//   });

	auto arrayBuffer = Napi::ArrayBuffer::New(env, resultStream.len());
	resultStream.copyAndReset(arrayBuffer.Data());

	return arrayBuffer;
}

Napi::Object Init(Napi::Env env, Napi::Object exports) {
	exports.Set(Napi::String::New(env, "doFastReadDir"),		Napi::Function::New(env, DoFastReadDir));
	exports.Set(Napi::String::New(env, "doFastReadDirSync"),	Napi::Function::New(env, DoFastReadDirSync));
	return exports;
}

NODE_API_MODULE(NODE_GYP_MODULE_NAME, Init)

// meow
