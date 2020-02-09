﻿#include "includes_win32.h"
#include "log_win32.h"
#include "hookdll_win32.h"
#include "proc_bookkeeping_win32.h"

tab_per_process_t* g_tabPerProcess;
tab_fake_ip_hostname_t* g_tabFakeIpHostname;

void PrintTablePerProcess()
{
	tab_per_process_t* Entry;
	tab_per_process_t* TempEntry;
	WCHAR TempBuf[400];
	WCHAR *pTempBuf = TempBuf;

	StringCchPrintfExW(pTempBuf, _countof(TempBuf) - (pTempBuf - TempBuf), &pTempBuf, NULL, 0, L"PerProcessTable:");

	HASH_ITER(hh, g_tabPerProcess, Entry, TempEntry) {
		IpNode* IpNodeEntry;

		StringCchPrintfExW(pTempBuf, _countof(TempBuf) - (pTempBuf - TempBuf), &pTempBuf, NULL, 0, L"%ls[WINPID" WPRDW L" PerProcessData] ", L"\n", Entry->Data.dwPid);

		LL_FOREACH(g_tabPerProcess->Ips, IpNodeEntry) {
			tab_fake_ip_hostname_t* IpHostnameEntry;
			tab_fake_ip_hostname_t IpHostnameEntryAsKey;

			StringCchPrintfExW(pTempBuf, _countof(TempBuf) - (pTempBuf - TempBuf), &pTempBuf, NULL, 0, L"%ls%ls", IpNodeEntry == g_tabPerProcess->Ips ? L"" : L", ", FormatHostPortToStr(&IpNodeEntry->Ip, sizeof(PXCH_IP_ADDRESS)));

			IpHostnameEntryAsKey.Ip = IpNodeEntry->Ip;
			IpHostnameEntryAsKey.dwOptionalPid = 0;
			IpHostnameEntry = NULL;
			HASH_FIND(hh, g_tabFakeIpHostname, &IpHostnameEntryAsKey.Ip, sizeof(PXCH_IP_ADDRESS) + sizeof(PXCH_UINT32), IpHostnameEntry);

			if (IpHostnameEntry) {
				PXCH_UINT32 i;
				StringCchPrintfExW(pTempBuf, _countof(TempBuf) - (pTempBuf - TempBuf), &pTempBuf, NULL, 0, L"(FakeIp,%ls<%ls> - ", IpHostnameEntry->Hostname.szValue, IpHostnameEntry->bWillProxy ? L"Proxied" : L"Direct");
				for (i = 0; i < IpHostnameEntry->dwResovledIpNum; i++) {
					StringCchPrintfExW(pTempBuf, _countof(TempBuf) - (pTempBuf - TempBuf), &pTempBuf, NULL, 0, L"%ls%ls", i ? L"/" : L"", FormatHostPortToStr(&IpHostnameEntry->ResolvedIps[i], sizeof(PXCH_IP_ADDRESS)));
				}
				StringCchPrintfExW(pTempBuf, _countof(TempBuf) - (pTempBuf - TempBuf), &pTempBuf, NULL, 0, L")");
			}

			IpHostnameEntryAsKey.dwOptionalPid = Entry->Data.dwPid;
			IpHostnameEntry = NULL;
			HASH_FIND(hh, g_tabFakeIpHostname, &IpHostnameEntryAsKey.Ip, sizeof(PXCH_IP_ADDRESS) + sizeof(PXCH_UINT32), IpHostnameEntry);

			if (IpHostnameEntry) {
				PXCH_UINT32 i;
				StringCchPrintfExW(pTempBuf, _countof(TempBuf) - (pTempBuf - TempBuf), &pTempBuf, NULL, 0, L"(ResolvedIp,%ls<%ls> - ", IpHostnameEntry->Hostname.szValue, IpHostnameEntry->bWillProxy ? L"Proxied" : L"Direct");
				for (i = 0; i < IpHostnameEntry->dwResovledIpNum; i++) {
					StringCchPrintfExW(pTempBuf, _countof(TempBuf) - (pTempBuf - TempBuf), &pTempBuf, NULL, 0, L"%ls%ls", i ? L"/" : L"", FormatHostPortToStr(&IpHostnameEntry->ResolvedIps[i], sizeof(PXCH_IP_ADDRESS)));
				}
				StringCchPrintfExW(pTempBuf, _countof(TempBuf) - (pTempBuf - TempBuf), &pTempBuf, NULL, 0, L")");
			}
		}
	}

	LOGI(L"%ls", TempBuf);
}

DWORD ChildProcessExitedCallbackWorker(PVOID lpParameter, BOOLEAN TimerOrWaitFired)
{
	PXCH_DO_IN_CRITICAL_SECTION_RETURN_DWORD{
		tab_per_process_t * Entry = (tab_per_process_t*)lpParameter;
		tab_fake_ip_hostname_t IpHostnameAsKey;
		tab_fake_ip_hostname_t* IpHostnameEntry;
		IpNode* pIpNode;
		IpNode* pTmpIpNode;

		DWORD dwExitCode = UINT32_MAX;
		HASH_DELETE(hh, g_tabPerProcess, Entry);
		if (!GetExitCodeProcess(Entry->hProcess, &dwExitCode)) {
			LOGE(L"GetExitCodeProcess() error: %ls", FormatErrorToStr(GetLastError()));
		}
		LOGI(L"Child process winpid " WPRDW L" exited (%#010x).", Entry->Data.dwPid, dwExitCode);

		LL_FOREACH_SAFE(Entry->Ips, pIpNode, pTmpIpNode) {
			IpHostnameAsKey.Ip = pIpNode->Ip;
			IpHostnameAsKey.dwOptionalPid = Entry->Data.dwPid;
			HASH_FIND(hh, g_tabFakeIpHostname, &IpHostnameAsKey.Ip, sizeof(PXCH_IP_ADDRESS) + sizeof(PXCH_UINT32), IpHostnameEntry);
			if (IpHostnameEntry) {
				HASH_DELETE(hh, g_tabFakeIpHostname, IpHostnameEntry);
				HeapFree(GetProcessHeap(), 0, IpHostnameEntry);
			}

			if (g_pPxchConfig->dwWillDeleteFakeIpAfterChildProcessExits) {
				IpHostnameAsKey.dwOptionalPid = 0;
				HASH_FIND(hh, g_tabFakeIpHostname, &IpHostnameAsKey.Ip, sizeof(PXCH_IP_ADDRESS) + sizeof(PXCH_UINT32), IpHostnameEntry);
				if (IpHostnameEntry) {
					HASH_DELETE(hh, g_tabFakeIpHostname, IpHostnameEntry);
					HeapFree(GetProcessHeap(), 0, IpHostnameEntry);
				}
			}

			LL_DELETE(Entry->Ips, pIpNode);
			HeapFree(GetProcessHeap(), 0, pIpNode);
		}

		HeapFree(GetProcessHeap(), 0, Entry);

		PrintTablePerProcess();

		if (g_tabPerProcess == NULL) {
			LOGI(L"All windows descendant process exited.");
			HeapUnlock(GetProcessHeap());	// go out of critical section
			IF_WIN32_EXIT(0);
		}
	}
}


VOID CALLBACK ChildProcessExitedCallback(
	_In_ PVOID   lpParameter,
	_In_ BOOLEAN TimerOrWaitFired
)
{
	ChildProcessExitedCallbackWorker(lpParameter, TimerOrWaitFired);
}

DWORD RegisterNewChildProcess(const REPORTED_CHILD_DATA* pChildData)
{
	PXCH_DO_IN_CRITICAL_SECTION_RETURN_DWORD{
		tab_per_process_t* Entry;
		HANDLE hWaitHandle;
		HANDLE hChildHandle;

		LOGV(L"Before HeapAlloc...");
		Entry = (tab_per_process_t*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(tab_per_process_t));
		LOGV(L"After HeapAlloc...");
		if ((hChildHandle = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_INFORMATION, FALSE, pChildData->dwPid)) == NULL) {
			dwReturn = GetLastError();
			if (dwReturn == ERROR_ACCESS_DENIED) {
				if ((hChildHandle = OpenProcess(SYNCHRONIZE, FALSE, pChildData->dwPid)) == NULL) {
					dwReturn = GetLastError();
				} else {
					goto after_open_process;
				}
			}
			LOGC(L"OpenProcess() error: %ls", FormatErrorToStr(dwReturn));
			goto lock_after_critical_section;
		}
	after_open_process:
		LOGD(L"After OpenProcess(" WPRDW L")...", hChildHandle);

		if (!RegisterWaitForSingleObject(&hWaitHandle, hChildHandle, &ChildProcessExitedCallback, Entry, INFINITE, WT_EXECUTELONGFUNCTION | WT_EXECUTEONLYONCE)) {
			dwReturn = GetLastError();
			LOGC(L"RegisterWaitForSingleObject() error: %ls", FormatErrorToStr(dwReturn));
			goto lock_after_critical_section;
		}
		LOGV(L"After RegisterWaitForSingleObject...");
		Entry->Data = *pChildData;
		Entry->hProcess = hChildHandle;
		Entry->Ips = NULL;
		LOGV(L"After Entry->Data = *pChildData;");
		HASH_ADD(hh, g_tabPerProcess, Data, sizeof(pid_key_t), Entry);
		LOGV(L"After HASH_ADD");
		LOGI(L"Registered child pid " WPRDW, pChildData->dwPid);
		PrintTablePerProcess();
	}
}

DWORD QueryChildStorage(REPORTED_CHILD_DATA* pChildData)
{
	PXCH_DO_IN_CRITICAL_SECTION_RETURN_DWORD{
		tab_per_process_t* Entry;
		HASH_FIND(hh, g_tabPerProcess, &pChildData->dwPid, sizeof(pid_key_t), Entry);
		if (Entry) {
			*pChildData = Entry->Data;
		}

		goto lock_after_critical_section;
	}
}

DWORD NextAvailableFakeIp(PXCH_IP_ADDRESS* pFakeIpv4, PXCH_IP_ADDRESS* pFakeIpv6)
{
	static PXCH_UINT32 iSearchIpv4 = 1;
	static PXCH_UINT32 iSearchIpv6 = 1;
	PXCH_UINT32 iSearchIpv4WhenEnter;
	PXCH_UINT32 iSearchIpv6WhenEnter;
	PXCH_UINT32 iIpv4ShiftLength;
	PXCH_UINT32 iIpv6ShiftLength;
	tab_fake_ip_hostname_t* Entry;
	tab_fake_ip_hostname_t AsKey;
	AsKey.dwOptionalPid = 0;

	iSearchIpv4WhenEnter = iSearchIpv4;
	AsKey.Ip.CommonHeader.wTag = PXCH_HOST_TYPE_IPV4;
	iIpv4ShiftLength = (32 > g_pPxchConfig->dwFakeIpv4PrefixLength) ? (32 - g_pPxchConfig->dwFakeIpv4PrefixLength) : 0;
	while (1) {
		Entry = NULL;
		IndexToIp(g_pPxchConfig, &AsKey.Ip, iSearchIpv4);
		LOGI(L"Map index to IPv4: " WPRDW L" -> %ls", iSearchIpv4, FormatHostPortToStr(&AsKey.Ip, sizeof(PXCH_IP_ADDRESS)));
		HASH_FIND(hh, g_tabFakeIpHostname, &AsKey.Ip, sizeof(PXCH_IP_ADDRESS) + sizeof(PXCH_UINT32), Entry);
		if (!Entry) break;
		iSearchIpv4++;
		if (iSearchIpv4 >= ((PXCH_UINT64)1 << iIpv4ShiftLength) - 1) {
			iSearchIpv4 = 1;
		}

		if (iSearchIpv4 == iSearchIpv4WhenEnter) {
			return ERROR_RESOURCE_NOT_AVAILABLE;
		}
	}

	LOGI(L"Next available IPv4 index: " WPRDW, iSearchIpv4);
	*pFakeIpv4 = AsKey.Ip;


	iSearchIpv6WhenEnter = iSearchIpv6;
	AsKey.Ip.CommonHeader.wTag = PXCH_HOST_TYPE_IPV6;
	iIpv6ShiftLength = (128 > g_pPxchConfig->dwFakeIpv6PrefixLength) ? (128 - g_pPxchConfig->dwFakeIpv6PrefixLength) : 0;
	iIpv6ShiftLength = iIpv6ShiftLength > 64 ? 64 : iIpv6ShiftLength;
	while (1) {
		Entry = NULL;
		IndexToIp(g_pPxchConfig, &AsKey.Ip, iSearchIpv6);
		LOGI(L"Map index to IPv6: " WPRDW L" -> %ls", iSearchIpv6, FormatHostPortToStr(&AsKey.Ip, sizeof(PXCH_IP_ADDRESS)));
		HASH_FIND(hh, g_tabFakeIpHostname, &AsKey.Ip, sizeof(PXCH_IP_ADDRESS) + sizeof(PXCH_UINT32), Entry);
		if (!Entry) break;
		iSearchIpv6++;
		if (iSearchIpv6 >= ((iIpv6ShiftLength == 64) ? 0xFFFFFFFFFFFFFFFF : (((PXCH_UINT64)1 << iIpv6ShiftLength) - 1))) {
			iSearchIpv6 = 1;
		}

		if (iSearchIpv6 == iSearchIpv6WhenEnter) {
			return ERROR_RESOURCE_NOT_AVAILABLE;
		}
	}

	LOGI(L"Next available IPv6 index: " WPRDW, iSearchIpv6);
	*pFakeIpv6 = AsKey.Ip;

	return NO_ERROR;
}

DWORD RegisterHostnameAndGetFakeIp(PXCH_IP_ADDRESS* pFakeIpv4, PXCH_IP_ADDRESS* pFakeIpv6, const tab_fake_ip_hostname_t* TempEntry, PXCH_UINT32 dwPid, BOOL bWillMapResolvedIpToHost)
{
	PXCH_DO_IN_CRITICAL_SECTION_RETURN_DWORD{
		tab_fake_ip_hostname_t* FakeIpv4Entry;
		tab_fake_ip_hostname_t* FakeIpv6Entry;
		tab_fake_ip_hostname_t* ResolvedIpEntry;
		tab_fake_ip_hostname_t* DummyEntry;
		tab_per_process_t* CurrentProcessDataEntry;
		IpNode* pIpv4Node;
		IpNode* pIpv6Node;
		IpNode* pResolvedIpNode;
		DWORD dwErrorCode;
		DWORD dw;

		CurrentProcessDataEntry = NULL;
		HASH_FIND(hh, g_tabPerProcess, &dwPid, sizeof(dwPid), CurrentProcessDataEntry);

		if (CurrentProcessDataEntry == NULL) goto err_no_proc_entry;

		pIpv4Node = (IpNode*)HeapAlloc(GetProcessHeap(), 0, sizeof(IpNode));
		pIpv6Node = (IpNode*)HeapAlloc(GetProcessHeap(), 0, sizeof(IpNode));
		dwErrorCode = NextAvailableFakeIp(&pIpv4Node->Ip, &pIpv6Node->Ip);
		if (dwErrorCode != NO_ERROR) goto err_no_avail_fake_ip;
		LL_PREPEND(CurrentProcessDataEntry->Ips, pIpv4Node);
		LL_PREPEND(CurrentProcessDataEntry->Ips, pIpv6Node);
	
		FakeIpv4Entry = (tab_fake_ip_hostname_t*)HeapAlloc(GetProcessHeap(), 0, sizeof(tab_fake_ip_hostname_t));
		FakeIpv4Entry->Ip = pIpv4Node->Ip;
		FakeIpv4Entry->dwOptionalPid = 0;
		FakeIpv4Entry->Hostname = TempEntry->Hostname;
		FakeIpv4Entry->bWillProxy = TempEntry->bWillProxy;
		FakeIpv4Entry->dwResovledIpNum = TempEntry->dwResovledIpNum;
		LOGI(L"Fake Ipv4: %ls", FormatHostPortToStr(&pIpv4Node->Ip, sizeof(PXCH_IP_ADDRESS)));
		CopyMemory(FakeIpv4Entry->ResolvedIps, TempEntry->ResolvedIps, sizeof(PXCH_IP_ADDRESS) * FakeIpv4Entry->dwResovledIpNum);

		HASH_ADD(hh, g_tabFakeIpHostname, Ip, sizeof(PXCH_IP_ADDRESS) + sizeof(PXCH_UINT32), FakeIpv4Entry);
		*pFakeIpv4 = pIpv4Node->Ip;


		FakeIpv6Entry = (tab_fake_ip_hostname_t*)HeapAlloc(GetProcessHeap(), 0, sizeof(tab_fake_ip_hostname_t));
		FakeIpv6Entry->Ip = pIpv6Node->Ip;
		FakeIpv6Entry->dwOptionalPid = 0;
		FakeIpv6Entry->Hostname = TempEntry->Hostname;
		FakeIpv6Entry->bWillProxy = TempEntry->bWillProxy;
		FakeIpv6Entry->dwResovledIpNum = TempEntry->dwResovledIpNum;
		LOGI(L"Fake Ipv6: %ls", FormatHostPortToStr(&pIpv6Node->Ip, sizeof(PXCH_IP_ADDRESS)));
		CopyMemory(FakeIpv6Entry->ResolvedIps, TempEntry->ResolvedIps, sizeof(PXCH_IP_ADDRESS) * FakeIpv6Entry->dwResovledIpNum);

		HASH_ADD(hh, g_tabFakeIpHostname, Ip, sizeof(PXCH_IP_ADDRESS) + sizeof(PXCH_UINT32), FakeIpv6Entry);
		*pFakeIpv6 = pIpv6Node->Ip;

		if (bWillMapResolvedIpToHost) {
			for (dw = 0; dw < TempEntry->dwResovledIpNum; dw++) {
				pResolvedIpNode = (IpNode*)HeapAlloc(GetProcessHeap(), 0, sizeof(IpNode));
				pResolvedIpNode->Ip = TempEntry->ResolvedIps[dw];
				LOGI(L"Resolved Ip: %ls", FormatHostPortToStr(&pResolvedIpNode->Ip, sizeof(PXCH_IP_ADDRESS)));
				LL_PREPEND(CurrentProcessDataEntry->Ips, pResolvedIpNode);

				ResolvedIpEntry = (tab_fake_ip_hostname_t*)HeapAlloc(GetProcessHeap(), 0, sizeof(tab_fake_ip_hostname_t));
				ResolvedIpEntry->Ip = TempEntry->ResolvedIps[dw];
				ResolvedIpEntry->dwOptionalPid = dwPid;
				ResolvedIpEntry->Hostname = TempEntry->Hostname;
				ResolvedIpEntry->dwResovledIpNum = TempEntry->dwResovledIpNum;
				CopyMemory(ResolvedIpEntry->ResolvedIps, TempEntry->ResolvedIps, sizeof(PXCH_IP_ADDRESS) * TempEntry->dwResovledIpNum);

				HASH_REPLACE(hh, g_tabFakeIpHostname, Ip, sizeof(PXCH_IP_ADDRESS) + sizeof(PXCH_UINT32), ResolvedIpEntry, DummyEntry);
			}
		}

		PrintTablePerProcess();
		dwReturn = NO_ERROR;
		goto lock_after_critical_section;

	err_no_proc_entry:
		dwReturn = ERROR_NOT_FOUND;
		goto lock_after_critical_section;

	err_no_avail_fake_ip:
		goto ret_free;

	ret_free:
		HeapFree(GetProcessHeap(), 0, pIpv4Node);
		HeapFree(GetProcessHeap(), 0, pIpv6Node);
		dwReturn = dwErrorCode;
		goto lock_after_critical_section;
	}
}

DWORD GetMsgHostnameAndResolvedIpsFromMsgIp(PXCH_IPC_MSGBUF chMessageBuf, PXCH_UINT32 *pcbMessageSize, const PXCH_IPC_MSGHDR_HOSTNAMEANDIPS* pMsgIp)
{
	tab_fake_ip_hostname_t AsKey;
	tab_fake_ip_hostname_t* Entry;
	PXCH_HOSTNAME EmptyHostname;
	DWORD dwErrorCode;

	EmptyHostname.wTag = PXCH_HOST_TYPE_HOSTNAME;
	EmptyHostname.wPort = 0;
	EmptyHostname.szValue[0] = L'\0';

	AsKey.Ip = *PXCH_IPC_IP_ARR(pMsgIp);

	if (g_pPxchConfig->dwWillSearchForHostByResolvedIp) {
		AsKey.dwOptionalPid = pMsgIp->dwPid;

		Entry = NULL;
		HASH_FIND(hh, g_tabFakeIpHostname, &AsKey.Ip, sizeof(PXCH_IP_ADDRESS) + sizeof(PXCH_UINT32), Entry);

		if (Entry) {
			LOGI(L"ResolvedIp %ls -> Hostname %ls, %ls", FormatHostPortToStr(PXCH_IPC_IP_ARR(pMsgIp), sizeof(PXCH_IP_ADDRESS)), Entry->Hostname.szValue, Entry->bWillProxy ? L"Proxied" : L"Direct");
			dwErrorCode = HostnameAndIpsToMessage(chMessageBuf, pcbMessageSize, pMsgIp->dwPid, &Entry->Hostname, FALSE /*ignored*/, Entry->dwResovledIpNum, Entry->ResolvedIps, Entry->bWillProxy);
			if (dwErrorCode != NO_ERROR) goto error;

			return NO_ERROR;
		}
	}

	AsKey.dwOptionalPid = 0;
	Entry = NULL;
	HASH_FIND(hh, g_tabFakeIpHostname, &AsKey.Ip, sizeof(PXCH_IP_ADDRESS) + sizeof(PXCH_UINT32), Entry);

	if (Entry) {
		LOGI(L"FakeIp %ls -> Hostname %ls, %ls", FormatHostPortToStr(PXCH_IPC_IP_ARR(pMsgIp), sizeof(PXCH_IP_ADDRESS)), Entry->Hostname.szValue, Entry->bWillProxy ? L"Proxied" : L"Direct");
		dwErrorCode = HostnameAndIpsToMessage(chMessageBuf, pcbMessageSize, pMsgIp->dwPid, &Entry->Hostname, FALSE /*ignored*/, Entry->dwResovledIpNum, Entry->ResolvedIps, Entry->bWillProxy);
		if (dwErrorCode != NO_ERROR) goto error;

		return NO_ERROR;
	}

	// return ERROR_NOT_FOUND;

	LOGI(L"NotRegisteredIp %ls, return it As-is", FormatHostPortToStr(PXCH_IPC_IP_ARR(pMsgIp), sizeof(PXCH_IP_ADDRESS)));
	dwErrorCode = HostnameAndIpsToMessage(chMessageBuf, pcbMessageSize, pMsgIp->dwPid, &EmptyHostname, FALSE /*ignored*/, 1, &AsKey.Ip, FALSE);
	if (dwErrorCode != NO_ERROR) goto error;

	return NO_ERROR;

error:
	return dwErrorCode;
}

DWORD HandleMessage(int i, PXCH_IPC_INSTANCE* pipc)
{
	PPXCH_IPC_MSGBUF pMsg = pipc->chReadBuf;

	if (MsgIsType(WSTR, pMsg)) {
		WCHAR sz[PXCH_IPC_BUFSIZE / sizeof(WCHAR)];
		MessageToWstr(sz, pMsg, pipc->cbRead);
		StdWprintf(STD_ERROR_HANDLE, L"%ls", sz);
		StdFlush(STD_ERROR_HANDLE);

		goto after_handling_resp_ok;
	}

	if (MsgIsType(CHILDDATA, pMsg)) {
		REPORTED_CHILD_DATA ChildData;
		LOGV(L"Message is CHILDDATA");
		MessageToChildData(&ChildData, pMsg, pipc->cbRead);
		LOGD(L"Child process pid " WPRDW L" created.", ChildData.dwPid);
		LOGV(L"RegisterNewChildProcess...");
		RegisterNewChildProcess(&ChildData);
		LOGV(L"RegisterNewChildProcess done.");
		goto after_handling_resp_ok;
	}

	if (MsgIsType(QUERYSTORAGE, pMsg)) {
		REPORTED_CHILD_DATA ChildData = { 0 };
		LOGV(L"Message is QUERYSTORAGE");
		MessageToQueryStorage(&ChildData.dwPid, pMsg, pipc->cbRead);
		QueryChildStorage(&ChildData);
		ChildDataToMessage(pipc->chWriteBuf, &pipc->cbToWrite, &ChildData);
		goto ret;
	}

	if (MsgIsType(HOSTNAMEANDIPS, pMsg)) {
		BOOL bWillMapResolvedIpToHost;
		PXCH_UINT32 dwPid;
		tab_fake_ip_hostname_t Entry;
		PXCH_IP_ADDRESS FakeIps[2];
		
		LOGV(L"Message is HOSTNAMEANDIPS");
		MessageToHostnameAndIps(&dwPid, &Entry.Hostname, &bWillMapResolvedIpToHost, &Entry.dwResovledIpNum, Entry.ResolvedIps, &Entry.bWillProxy, pMsg, pipc->cbRead);

		if (HostIsType(HOSTNAME, Entry.Hostname) && Entry.Hostname.szValue[0]) {
			LOGV(L"Client is registering hostname, retrieving fake ips");
			if (RegisterHostnameAndGetFakeIp(&FakeIps[0], &FakeIps[1], &Entry, dwPid, bWillMapResolvedIpToHost) != NO_ERROR) LOGE(L"RegisterHostnameAndGetFakeIp() failed");
			HostnameAndIpsToMessage(pipc->chWriteBuf, &pipc->cbToWrite, 0 /*ignored*/, &Entry.Hostname /*ignored*/, FALSE /*ignored*/, 2, FakeIps, FALSE /*ignored*/);
		} else {
			LOGV(L"Client is querying with fake ip");
			if (GetMsgHostnameAndResolvedIpsFromMsgIp(pipc->chWriteBuf, &pipc->cbToWrite, (const PXCH_IPC_MSGHDR_HOSTNAMEANDIPS*)pMsg) != NO_ERROR) LOGE(L"GetHostnameFromIp() failed");
		}
		goto ret;
	}

	goto after_handling_not_recognized;

after_handling_resp_ok:
	WstrToMessage(pipc->chWriteBuf, &pipc->cbToWrite, L"OK");
	return 0;

after_handling_not_recognized:
	WstrToMessage(pipc->chWriteBuf, &pipc->cbToWrite, L"NOT RECOGNIZED");
	return 0;

ret:
	return 0;
}


DWORD InitProcessBookkeeping(void)
{
	g_tabPerProcess = NULL;
	g_tabFakeIpHostname = NULL;

	return 0;
}