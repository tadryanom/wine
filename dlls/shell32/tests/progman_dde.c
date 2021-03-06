/*
 * Unit test of the Program Manager DDE Interfaces
 *
 * Copyright 2009 Mikey Alexander
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

/* DDE Program Manager Tests
 * - Covers basic CreateGroup, ShowGroup, DeleteGroup, AddItem, and DeleteItem
 *   functionality
 * - Todo: Handle CommonGroupFlag
 *         Better AddItem Tests (Lots of parameters to test)
 *         Tests for Invalid Characters in Names / Invalid Parameters
 */

#include <stdio.h>
#include <wine/test.h>
#include <winbase.h>
#include "dde.h"
#include "ddeml.h"
#include "winuser.h"
#include "shlobj.h"

static HRESULT (WINAPI *pSHGetLocalizedName)(LPCWSTR, LPWSTR, UINT, int *);
static BOOL (WINAPI *pSHGetSpecialFolderPathA)(HWND, LPSTR, int, BOOL);
static BOOL (WINAPI *pReadCabinetState)(CABINETSTATE *, int);

static void init_function_pointers(void)
{
    HMODULE hmod;

    hmod = GetModuleHandleA("shell32.dll");
    pSHGetLocalizedName = (void*)GetProcAddress(hmod, "SHGetLocalizedName");
    pSHGetSpecialFolderPathA = (void*)GetProcAddress(hmod, "SHGetSpecialFolderPathA");
    pReadCabinetState = (void*)GetProcAddress(hmod, "ReadCabinetState");
    if (!pReadCabinetState)
        pReadCabinetState = (void*)GetProcAddress(hmod, (LPSTR)651);
}

static BOOL use_common(void)
{
    HMODULE hmod;
    static BOOL (WINAPI *pIsNTAdmin)(DWORD, LPDWORD);

    /* IsNTAdmin() is available on all platforms. */
    hmod = LoadLibraryA("advpack.dll");
    pIsNTAdmin = (void*)GetProcAddress(hmod, "IsNTAdmin");

    if (!pIsNTAdmin(0, NULL))
    {
        /* We are definitely not an administrator */
        FreeLibrary(hmod);
        return FALSE;
    }
    FreeLibrary(hmod);

    /* If we end up here we are on NT4+ as Win9x and WinMe don't have the
     * notion of administrators (as we need it).
     */

    /* As of Vista  we should always use the users directory. Tests with the
     * real Administrator account on Windows 7 proved this.
     *
     * FIXME: We need a better way of identifying Vista+ as currently this check
     * also covers Wine and we don't know yet which behavior we want to follow.
     */
    if (pSHGetLocalizedName)
        return FALSE;

    return TRUE;
}

static BOOL full_title(void)
{
    CABINETSTATE cs;

    memset(&cs, 0, sizeof(cs));
    if (pReadCabinetState)
    {
        pReadCabinetState(&cs, sizeof(cs));
    }
    else
    {
        HKEY key;
        DWORD size;

        win_skip("ReadCabinetState is not available, reading registry directly\n");
        RegOpenKeyA(HKEY_CURRENT_USER, "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\CabinetState", &key);
        size = sizeof(cs);
        RegQueryValueExA(key, "Settings", NULL, NULL, (LPBYTE)&cs, &size);
        RegCloseKey(key);
    }

    return (cs.fFullPathTitle == -1);
}

static char ProgramsDir[MAX_PATH];

static void init_strings(void)
{
    char commonprograms[MAX_PATH];
    char programs[MAX_PATH];

    if (pSHGetSpecialFolderPathA)
    {
        pSHGetSpecialFolderPathA(NULL, programs, CSIDL_PROGRAMS, FALSE);
        pSHGetSpecialFolderPathA(NULL, commonprograms, CSIDL_COMMON_PROGRAMS, FALSE);
    }
    else
    {
        HKEY key;
        DWORD size;

        /* Older Win9x and NT4 */

        RegOpenKeyA(HKEY_CURRENT_USER, "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Shell Folders", &key);
        size = sizeof(programs);
        RegQueryValueExA(key, "Programs", NULL, NULL, (LPBYTE)&programs, &size);
        RegCloseKey(key);

        RegOpenKeyA(HKEY_LOCAL_MACHINE, "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Shell Folders", &key);
        size = sizeof(commonprograms);
        RegQueryValueExA(key, "Common Programs", NULL, NULL, (LPBYTE)&commonprograms, &size);
        RegCloseKey(key);
    }

    /* ProgramsDir on Vista+ is always the users one (CSIDL_PROGRAMS). Before Vista
     * it depends on whether the user is an administrator (CSIDL_COMMON_PROGRAMS) or
     * not (CSIDL_PROGRAMS).
     */
    if (use_common())
        lstrcpyA(ProgramsDir, commonprograms);
    else
        lstrcpyA(ProgramsDir, programs);
}

static HDDEDATA CALLBACK DdeCallback(UINT type, UINT format, HCONV hConv, HSZ hsz1, HSZ hsz2,
                                     HDDEDATA hDDEData, ULONG_PTR data1, ULONG_PTR data2)
{
    trace("Callback: type=%i, format=%i\n", type, format);
    return NULL;
}

static UINT dde_execute(DWORD instance, HCONV hconv, const char *command_str)
{
    HDDEDATA command, hdata;
    DWORD result;
    UINT ret;

    command = DdeCreateDataHandle(instance, (BYTE *)command_str, strlen(command_str)+1, 0, 0, 0, 0);
    ok(command != NULL, "DdeCreateDataHandle() failed: %u\n", DdeGetLastError(instance));

    hdata = DdeClientTransaction((BYTE *)command, -1, hconv, 0, 0, XTYP_EXECUTE, 2000, &result);
    ret = DdeGetLastError(instance);
    /* PROGMAN always returns 1 on success */
    ok((UINT_PTR)hdata == !ret, "expected %u, got %p\n", !ret, hdata);

    return ret;
}

static BOOL check_window_exists(const char *name)
{
    char title[MAX_PATH];
    HWND window = NULL;
    int i;

    if (full_title())
    {
        strcpy(title, ProgramsDir);
        strcat(title, "\\");
        strcat(title, name);
    }
    else
        strcpy(title, name);

    for (i = 0; i < 20; i++)
    {
        Sleep(100);
        if ((window = FindWindowA("ExplorerWClass", title)) ||
            (window = FindWindowA("CabinetWClass", title)))
        {
            SendMessageA(window, WM_SYSCOMMAND, SC_CLOSE, 0);
            break;
        }
    }

    return (window != NULL);
}

static BOOL check_exists(const char *name)
{
    char path[MAX_PATH];

    strcpy(path, ProgramsDir);
    strcat(path, "\\");
    strcat(path, name);
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
}

static void test_parser(DWORD instance, HCONV hConv)
{
    UINT error;

    /* Invalid Command */
    error = dde_execute(instance, hConv, "[InvalidCommand()]");
    ok(error == DMLERR_NOTPROCESSED, "expected DMLERR_NOTPROCESSED, got %u\n", error);

    /* test parsing */
    error = dde_execute(instance, hConv, "");
    ok(error == DMLERR_NOTPROCESSED, "expected DMLERR_NOTPROCESSED, got %u\n", error);

    error = dde_execute(instance, hConv, "CreateGroup");
    ok(error == DMLERR_NOTPROCESSED, "expected DMLERR_NOTPROCESSED, got %u\n", error);

    error = dde_execute(instance, hConv, "[CreateGroup");
    ok(error == DMLERR_NOTPROCESSED, "expected DMLERR_NOTPROCESSED, got %u\n", error);

    error = dde_execute(instance, hConv, "[CreateGroup]");
    ok(error == DMLERR_NOTPROCESSED, "expected DMLERR_NOTPROCESSED, got %u\n", error);

    error = dde_execute(instance, hConv, "[CreateGroup()]");
    ok(error == DMLERR_NOTPROCESSED, "expected DMLERR_NOTPROCESSED, got %u\n", error);

    error = dde_execute(instance, hConv, "[cREATEgROUP(test)]");
    ok(error == DMLERR_NO_ERROR, "expected DMLERR_NO_ERROR, got %u\n", error);
    ok(check_exists("test"), "directory not created\n");
    ok(check_window_exists("test"), "window not created\n");

    error = dde_execute(instance, hConv, "[AddItem(notepad,foobar)]");
    ok(error == DMLERR_NO_ERROR, "expected DMLERR_NO_ERROR, got %u\n", error);
    ok(check_exists("test/foobar.lnk"), "link not created\n");

    error = dde_execute(instance, hConv, "[AddItem(notepad,foo bar)]");
    ok(error == DMLERR_NO_ERROR, "expected DMLERR_NO_ERROR, got %u\n", error);
    ok(check_exists("test/foo bar.lnk"), "link not created\n");

    error = dde_execute(instance, hConv, "[AddItem(notepad,a[b,c]d)]");
    ok(error == DMLERR_NOTPROCESSED, "expected DMLERR_NOTPROCESSED, got %u\n", error);

    error = dde_execute(instance, hConv, "[AddItem(notepad,\"a[b,c]d\")]");
    ok(error == DMLERR_NO_ERROR, "expected DMLERR_NO_ERROR, got %u\n", error);
    ok(check_exists("test/a[b,c]d.lnk"), "link not created\n");

    error = dde_execute(instance, hConv, "  [  AddItem  (  notepad  ,  test  )  ]  ");
    ok(error == DMLERR_NO_ERROR, "expected DMLERR_NO_ERROR, got %u\n", error);
    ok(check_exists("test/test.lnk"), "link not created\n");

    error = dde_execute(instance, hConv, "[AddItem(notepad,one)][AddItem(notepad,two)]");
    ok(error == DMLERR_NO_ERROR, "expected DMLERR_NO_ERROR, got %u\n", error);
    ok(check_exists("test/one.lnk"), "link not created\n");
    ok(check_exists("test/two.lnk"), "link not created\n");

    error = dde_execute(instance, hConv, "[FakeCommand(test)][DeleteGroup(test)]");
    ok(error == DMLERR_NOTPROCESSED, "expected DMLERR_NOTPROCESSED, got %u\n", error);
    ok(check_exists("test"), "directory should exist\n");

    error = dde_execute(instance, hConv, "[DeleteGroup(test)]");
    ok(error == DMLERR_NO_ERROR, "expected DMLERR_NO_ERROR, got %u\n", error);
    ok(!check_exists("test"), "directory should not exist\n");
}

/* 1st set of tests */
static void test_progman_dde(DWORD instance, HCONV hConv)
{
    UINT error;

    /* test creating and deleting groups and items */
    error = dde_execute(instance, hConv, "[CreateGroup(Group1)]");
    ok(error == DMLERR_NO_ERROR, "expected DMLERR_NO_ERROR, got %u\n", error);
    ok(check_exists("Group1"), "directory not created\n");
    ok(check_window_exists("Group1"), "window not created\n");

    error = dde_execute(instance, hConv, "[AddItem]");
    ok(error == DMLERR_NOTPROCESSED, "expected DMLERR_NOTPROCESSED, got %u\n", error);

    error = dde_execute(instance, hConv, "[AddItem(test)]");
    ok(error == DMLERR_NOTPROCESSED, "expected DMLERR_NOTPROCESSED, got %u\n", error);

    error = dde_execute(instance, hConv, "[AddItem(notepad.exe)]");
    ok(error == DMLERR_NO_ERROR, "expected DMLERR_NO_ERROR, got %u\n", error);
    ok(check_exists("Group1/notepad.lnk"), "link not created\n");

    error = dde_execute(instance, hConv, "[DeleteItem(notepad.exe)]");
    ok(error == DMLERR_NOTPROCESSED, "expected DMLERR_NOTPROCESSED, got %u\n", error);

    error = dde_execute(instance, hConv, "[DeleteItem(notepad)]");
    ok(error == DMLERR_NO_ERROR, "expected DMLERR_NO_ERROR, got %u\n", error);
    ok(!check_exists("Group1/notepad.lnk"), "link should not exist\n");

    error = dde_execute(instance, hConv, "[DeleteItem(notepad)]");
    ok(error == DMLERR_NOTPROCESSED, "expected DMLERR_NOTPROCESSED, got %u\n", error);

    error = dde_execute(instance, hConv, "[AddItem(notepad)]");
    ok(error == DMLERR_NO_ERROR, "expected DMLERR_NO_ERROR, got %u\n", error);
    ok(check_exists("Group1/notepad.lnk"), "link not created\n");

    error = dde_execute(instance, hConv, "[AddItem(notepad)]");
    ok(error == DMLERR_NO_ERROR, "expected DMLERR_NO_ERROR, got %u\n", error);

    /* XP allows any valid path even if it does not exist; Vista+ requires that
     * the path both exist and be a file (directories are invalid). */

    error = dde_execute(instance, hConv, "[AddItem(C:\\windows\\system.ini)]");
    ok(error == DMLERR_NO_ERROR, "expected DMLERR_NO_ERROR, got %u\n", error);
    ok(check_exists("Group1/system.lnk"), "link not created\n");

    error = dde_execute(instance, hConv, "[AddItem(notepad,test1)]");
    ok(error == DMLERR_NO_ERROR, "expected DMLERR_NO_ERROR, got %u\n", error);
    ok(check_exists("Group1/test1.lnk"), "link not created\n");

    error = dde_execute(instance, hConv, "[DeleteItem(test1)]");
    ok(error == DMLERR_NO_ERROR, "expected DMLERR_NO_ERROR, got %u\n", error);
    ok(!check_exists("Group1/test1.lnk"), "link should not exist\n");

    /* test ShowGroup() and test which group an item gets added to */
    error = dde_execute(instance, hConv, "[ShowGroup(Group1)]");
    ok(error == DMLERR_NOTPROCESSED, "expected DMLERR_NOTPROCESSED, got %u\n", error);

    error = dde_execute(instance, hConv, "[ShowGroup(Group1, 0)]");
    ok(error == DMLERR_NO_ERROR, "expected DMLERR_NO_ERROR, got %u\n", error);
    ok(check_window_exists("Group1"), "window not created\n");

    error = dde_execute(instance, hConv, "[CreateGroup(Group2)]");
    ok(error == DMLERR_NO_ERROR, "expected DMLERR_NO_ERROR, got %u\n", error);
    ok(check_exists("Group2"), "directory not created\n");
    ok(check_window_exists("Group2"), "window not created\n");

    error = dde_execute(instance, hConv, "[AddItem(notepad,test2)]");
    ok(error == DMLERR_NO_ERROR, "expected DMLERR_NO_ERROR, got %u\n", error);
    ok(check_exists("Group2/test2.lnk"), "link not created\n");

    error = dde_execute(instance, hConv, "[ShowGroup(Group1, 0)]");
    ok(error == DMLERR_NO_ERROR, "expected DMLERR_NO_ERROR, got %u\n", error);
    ok(check_window_exists("Group1"), "window not created\n");

    error = dde_execute(instance, hConv, "[AddItem(notepad,test3)]");
    ok(error == DMLERR_NO_ERROR, "expected DMLERR_NO_ERROR, got %u\n", error);
    ok(check_exists("Group1/test3.lnk"), "link not created\n");

    error = dde_execute(instance, hConv, "[DeleteGroup(Group1)]");
    ok(error == DMLERR_NO_ERROR, "expected DMLERR_NO_ERROR, got %u\n", error);
    ok(!check_exists("Group1"), "directory should not exist\n");

    error = dde_execute(instance, hConv, "[DeleteGroup(Group1)]");
    ok(error == DMLERR_NOTPROCESSED, "expected DMLERR_NOTPROCESSED, got %u\n", error);

    error = dde_execute(instance, hConv, "[ShowGroup(Group2, 0)]");
    ok(error == DMLERR_NO_ERROR, "expected DMLERR_NO_ERROR, got %u\n", error);
    ok(check_window_exists("Group2"), "window not created\n");
}

/* 2nd set of tests - 2nd connection */
static void test_progman_dde2(DWORD instance, HCONV hConv)
{
    UINT error;

    /* last open group is retained across connections */
    error = dde_execute(instance, hConv, "[AddItem(notepad)]");
    ok(error == DMLERR_NO_ERROR, "expected DMLERR_NO_ERROR, got %u\n", error);
    ok(check_exists("Group2/notepad.lnk"), "link not created\n");

    error = dde_execute(instance, hConv, "[DeleteGroup(Group2)]");
    ok(error == DMLERR_NO_ERROR, "expected DMLERR_NO_ERROR, got %u\n", error);
    ok(!check_exists("Group2"), "directory should not exist\n");
}

START_TEST(progman_dde)
{
    DWORD instance = 0;
    UINT err;
    HSZ hszProgman;
    HCONV hConv;

    init_function_pointers();
    init_strings();

    /* Initialize DDE Instance */
    err = DdeInitializeA(&instance, DdeCallback, APPCMD_CLIENTONLY, 0);
    ok(err == DMLERR_NO_ERROR, "DdeInitialize() failed: %u\n", err);

    /* Create Connection */
    hszProgman = DdeCreateStringHandleA(instance, "PROGMAN", CP_WINANSI);
    ok(hszProgman != NULL, "DdeCreateStringHandle() failed: %u\n", DdeGetLastError(instance));
    hConv = DdeConnect(instance, hszProgman, hszProgman, NULL);
    ok(DdeFreeStringHandle(instance, hszProgman), "DdeFreeStringHandle() failed: %u\n", DdeGetLastError(instance));
    /* Seeing failures on early versions of Windows Connecting to progman, exit if connection fails */
    if (hConv == NULL)
    {
        ok (DdeUninitialize(instance), "DdeUninitialize failed\n");
        return;
    }

    test_parser(instance, hConv);
    test_progman_dde(instance, hConv);

    /* Cleanup & Exit */
    ok(DdeDisconnect(hConv), "DdeDisonnect() failed: %u\n", DdeGetLastError(instance));
    ok(DdeUninitialize(instance), "DdeUninitialize() failed: %u\n", DdeGetLastError(instance));

    /* 2nd Instance (Followup Tests) */
    /* Initialize DDE Instance */
    instance = 0;
    err = DdeInitializeA(&instance, DdeCallback, APPCMD_CLIENTONLY, 0);
    ok (err == DMLERR_NO_ERROR, "DdeInitialize() failed: %u\n", err);

    /* Create Connection */
    hszProgman = DdeCreateStringHandleA(instance, "PROGMAN", CP_WINANSI);
    ok(hszProgman != NULL, "DdeCreateStringHandle() failed: %u\n", DdeGetLastError(instance));
    hConv = DdeConnect(instance, hszProgman, hszProgman, NULL);
    ok(hConv != NULL, "DdeConnect() failed: %u\n", DdeGetLastError(instance));
    ok(DdeFreeStringHandle(instance, hszProgman), "DdeFreeStringHandle() failed: %u\n", DdeGetLastError(instance));

    /* Run Tests */
    test_progman_dde2(instance, hConv);

    /* Cleanup & Exit */
    ok(DdeDisconnect(hConv), "DdeDisonnect() failed: %u\n", DdeGetLastError(instance));
    ok(DdeUninitialize(instance), "DdeUninitialize() failed: %u\n", DdeGetLastError(instance));
}
