#include "stdafx.h"
#include "CommonUtility.h"
#include "INIReader.h"
#include <Knownfolders.h>
#include <Shlobj.h>
// #include <Pathcch.h>
#include <sstream>
#include <Shlwapi.h>

// Mandatory initialization of static class variable outside the header file.
static FILETIME local;
FILETIME EditRecordTimer::lastUpdatedTimeStamp = (GetSystemTimeAsFileTime(&local), local);
ManageWakaTimeConfigFile gConfigFileManager;
static const std::wstring pythoncmd = L"python.exe";

ManageWakaTimeConfigFile::ManageWakaTimeConfigFile() : SECTION(L"settings"), API_KEY(L"api_key")
{
	m_FileName = L"";
}

bool ManageWakaTimeConfigFile::DoesWakaTimeConfigFileExist()
{
	WCHAR* path;
	bool status = false;
	if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Profile, SHGFP_TYPE_CURRENT, 0, &path)))
	{
		WCHAR fullPath[MAX_PATH];
#pragma  warning(disable : 4995)
		// I know PathCombine is insecure - but its alternate does not work 
		// for windows 7. 
		if (SUCCEEDED(PathCombine(fullPath, path, WAKATIME_CONFIG_NAME)))
		{
			m_FileName = fullPath;
		}
#pragma warning(default : 4995)

		if	(INVALID_FILE_ATTRIBUTES != GetFileAttributes(fullPath))
		{
			status = true;
		}

		CoTaskMemFree(path);
	}

	return status;
}


bool ManageWakaTimeConfigFile::ReadWakaTimeConfigFile()
{
	// Read the wakatime.cfg file under users home directory if it exists.
	// Else create the config file 
	if (!DoesWakaTimeConfigFileExist())
	{
		CreateWakaTimeConfigFile(L"");
	}

	return true;
}

std::wstring ManageWakaTimeConfigFile::GetAPIKeyFromConfigFile()
{
	if (m_FileName.empty())
		return L"";
	INIReader reader(m_FileName);
	return reader.GetKeyValuePairsUnderASection(SECTION)[API_KEY];
}

bool ManageWakaTimeConfigFile::UpdateWakaTimeAPIKey(std::wstring key)
{
	if (m_FileName.empty())
		return false;
	INIReader reader(m_FileName);
	reader.ChangeValue(SECTION, API_KEY, key);
	reader.WriteIniFile(m_FileName);
	return true;
}

void ManageWakaTimeConfigFile::CreateWakaTimeConfigFile(std::wstring key)
{
	if (!DoesWakaTimeConfigFileExist())
	{
		HANDLE hFileHandle =  CreateFile(m_FileName.c_str(), GENERIC_READ | GENERIC_WRITE,
			FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		CloseHandle(hFileHandle);
	}

	INIReader reader(m_FileName);
	reader.CreateSectionAndAddKeyValue(SECTION, API_KEY, key);
	reader.WriteIniFile(m_FileName);
}

std::wstring CommonUtility::GetCurrentNPPDocument()
{
	wchar_t fileName[MAX_PATH];
	SendMessage(nppData._nppHandle, NPPM_GETFULLCURRENTPATH, MAX_PATH, (LPARAM)fileName);
	return std::wstring(fileName);
}

std::wstring CommonUtility::GetNPPConfigDirectory()
{
	wchar_t directoryPath[MAX_PATH];
	SendMessage(nppData._nppHandle, NPPM_GETPLUGINSCONFIGDIR, MAX_PATH, (LPARAM)directoryPath);
	return std::wstring(directoryPath);
}

#include <iostream>
#include <io.h>
#include <fstream>
#include <locale>
#include <codecvt>

bool CommonUtility::IsWakaTimeModuleAvailable()
{
	std::wstring wakatimeCliPath = CommonUtility::GetNPPConfigDirectory() + 
									L"\\wakatime\\wakatime-master\\wakatime-cli.py";
	return (INVALID_FILE_ATTRIBUTES != GetFileAttributes(wakatimeCliPath.c_str()));
}

// This function uses the quiet mode of where.exe to find whether python
// is available in the path. Returns true if python was found and false otherwise.
bool CommonUtility::IsPythonAvailableInPath()
{
	PROCESS_INFORMATION piProcInfo;
	TCHAR systemPath[MAX_PATH];
	GetSystemDirectory(systemPath, MAX_PATH);
	const TCHAR pythonQuery[] = L"\\where.exe /Q python.exe";
	std::wstring python = std::wstring(systemPath) + pythonQuery;
	STARTUPINFO siStartInfo;
	bool bSuccess = FALSE;

	ZeroMemory(&piProcInfo, sizeof(PROCESS_INFORMATION));
	ZeroMemory(&siStartInfo, sizeof(STARTUPINFO));
	siStartInfo.cb = sizeof(STARTUPINFO);
	siStartInfo.dwFlags |= (STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW);
	siStartInfo.wShowWindow = SW_HIDE;

	CreateProcess(NULL,
		const_cast<wchar_t*>(python.c_str()),
		NULL, NULL, TRUE, 0, NULL, NULL,
		&siStartInfo, &piProcInfo);

	WaitForSingleObject(piProcInfo.hProcess, INFINITE);
	int result = -1;
	GetExitCodeProcess(piProcInfo.hProcess, (LPDWORD)&result);
	CloseHandle(piProcInfo.hProcess);
	CloseHandle(piProcInfo.hThread);
	return result == 0;
}

std::wstring CommonUtility::GetPythonPath()
{
	// This function does four things(!) : 1) Executes which.exe python.exe
	// to find the full path of python executable using CreateProcess. 
	// 2) Opens a pipe to read from the standard output of the CreateProcess.
	// 3) Sets the locale to utf-8 and read the standard output to a wstring.
	// 4) Converts each of \ with \\ so that it is directly useful for CreateProcess.
	static std::wstring output;
	if (!output.empty())
		return output;

	HANDLE hChildStd_OUT_Rd = NULL;
	HANDLE hChildStd_OUT_Wr = NULL;
	SECURITY_ATTRIBUTES sa;
	sa.nLength = sizeof(SECURITY_ATTRIBUTES);
	sa.bInheritHandle = TRUE;
	sa.lpSecurityDescriptor = NULL;
	if (!CreatePipe(&hChildStd_OUT_Rd, &hChildStd_OUT_Wr, &sa, 0) ||
		!SetHandleInformation(hChildStd_OUT_Rd, HANDLE_FLAG_INHERIT, 0))
		return L"";

	PROCESS_INFORMATION piProcInfo;
	TCHAR systemPath[MAX_PATH];
	GetSystemDirectory(systemPath, MAX_PATH);
	const TCHAR pythonQuery[] = L"\\where.exe python.exe";
	std::wstring python = std::wstring(systemPath) + pythonQuery;
	STARTUPINFO siStartInfo;
	bool bSuccess = FALSE;

	ZeroMemory(&piProcInfo, sizeof(PROCESS_INFORMATION));
	ZeroMemory(&siStartInfo, sizeof(STARTUPINFO));
	siStartInfo.cb = sizeof(STARTUPINFO);
	siStartInfo.hStdOutput = hChildStd_OUT_Wr;
	siStartInfo.dwFlags |= (STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW);
	siStartInfo.wShowWindow = SW_HIDE;

	CreateProcess(NULL,
		const_cast<wchar_t*>(python.c_str()),
		NULL, NULL, TRUE, 0, NULL, NULL,
		&siStartInfo, &piProcInfo);

	CloseHandle(hChildStd_OUT_Wr);

	int fd = _open_osfhandle((intptr_t)hChildStd_OUT_Rd, 0);
	if (-1 == fd)
		return L"";
	FILE* file = _wfdopen(fd, L"rb");
	if (NULL == file)
		return L"";
	
	std::wifstream stream(file);

	const std::locale empty_locale = std::locale::empty();
	typedef std::codecvt_utf8<wchar_t> converter_type;
	const std::codecvt_utf8<wchar_t>* converter = new std::codecvt_utf8 < wchar_t > ;
	const std::locale utf8_locale(empty_locale, converter);

	stream.imbue(utf8_locale);
	stream >> output;
	stream.close();

	std::wstringstream canonical;
	std::wstring::iterator it(output.begin());
	while (it != output.end())
	{
		if (*it == '\\')
			canonical << "\\\\";
		else
			canonical << *it;
		++it;
	}
	return canonical.str();
}

std::wstring CommonUtility::GetCommandPrefix()
{
	static std::wstring commandprefix;
	if (!commandprefix.empty())
		return commandprefix;
	std::wstring pythonpath = GetPythonPath();
	std::wstring filepath = GetCurrentNPPDocument();
	std::wstring configpath = GetNPPConfigDirectory() + L"\\wakatime\\wakatime-master\\";
	std::wstring wakatimecmd = L"wakatime-cli.py";
	std::wstring pluginver = L" --plugin \"notepadpp notepadpp-wakatime/1.0.0\" ";
	commandprefix = L" " + configpath + L"\\" + wakatimecmd + pluginver;
	return commandprefix;
}

void CommonUtility::OnCurrentNPPDocumentSaved()
{
	// Tell the WakaTime backend that a document has been saved in Notepad++ .
	std::wstring pythonpath = GetPythonPath();
	std::wstring filepath = GetCurrentNPPDocument();
	std::wstring fileinvoke = L" --file ";
	std::wstring command = GetCommandPrefix() + fileinvoke + filepath + L" --write";

	PROCESS_INFORMATION piProcInfo;
	STARTUPINFO siStartInfo;

	ZeroMemory(&piProcInfo, sizeof(PROCESS_INFORMATION));
	ZeroMemory(&siStartInfo, sizeof(STARTUPINFO));
	siStartInfo.cb = sizeof(STARTUPINFO);
	siStartInfo.dwFlags |= STARTF_USESHOWWINDOW;
	siStartInfo.wShowWindow = SW_HIDE;
	LPTSTR commanddup = _wcsdup(command.c_str());
	if (CreateProcess(pythonpath.c_str(), commanddup, NULL,
		NULL, true, CREATE_NO_WINDOW, NULL, NULL, &siStartInfo, &piProcInfo))
	{
		CloseHandle(piProcInfo.hProcess);
		CloseHandle(piProcInfo.hThread);
	}
	delete commanddup;
}

void CommonUtility::OnNewNPPDocumentCreated()
{
	// Tell the WakaTime backend that a document has been created in Notepad++.
	// Not sure if there should be a new verb for this action.
	CommonUtility::OnNPPDocumentModified();
}

void CommonUtility::OnNPPDocumentModified()
{
	// Tell the WakaTime backend that a document has been modified in Notepad++.
	std::wstring pythonpath = GetPythonPath();
	std::wstring filepath = GetCurrentNPPDocument();
	std::wstring fileinvoke =  L" --file ";
	std::wstring command = GetCommandPrefix() + fileinvoke + filepath;

	PROCESS_INFORMATION piProcInfo;
	STARTUPINFO siStartInfo;

	ZeroMemory(&piProcInfo, sizeof(PROCESS_INFORMATION));
	ZeroMemory(&siStartInfo, sizeof(STARTUPINFO));
	siStartInfo.cb = sizeof(STARTUPINFO);
	siStartInfo.dwFlags |= STARTF_USESHOWWINDOW;
	siStartInfo.wShowWindow = SW_HIDE;
	LPTSTR commanddup = _wcsdup(command.c_str());
	if (CreateProcess(pythonpath.c_str(), commanddup, NULL,
		NULL, true, CREATE_NO_WINDOW, NULL, NULL, &siStartInfo, &piProcInfo))
	{
		CloseHandle(piProcInfo.hProcess);
		CloseHandle(piProcInfo.hThread);
	}
	delete commanddup;
}