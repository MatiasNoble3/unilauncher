// UniLauncher
// -----------
// Una sola ventana para lanzar Leonida (gta6rt.exe), XValkyrie (xvboot.exe)
// y VIctory (victory.exe) sin tener que abrir tres terminales distintas.
//
// IMPORTANTE - leer antes de usar:
//   Este programa NO modifica, arregla ni fusiona el codigo de esos tres
//   proyectos. Cada uno sigue siendo un proyecto independiente, con su
//   propio estado de avance (a la fecha de escribir esto, ninguno de los
//   tres deja un juego jugable de punta a punta). UniLauncher solo arma la
//   linea de comandos documentada de cada uno, lo ejecuta, y te muestra su
//   log en una ventana. Si el backend se cuelga o falla, es un problema de
//   ese backend, no de este launcher - lo unico que garantiza este programa
//   es que VOS podes cortar ese proceso (boton Stop) sin que quede una
//   instancia zombie corriendo en segundo plano.
//
// Deteccion de rutas por defecto (documentadas en cada proyecto):
//   Leonida    -> <algo>\Leonida\gta6rt.exe               (make_game.py --out)
//   XValkyrie  -> <algo>\xvalkyrie\build\win\Release\xvboot.exe  (build.cmd)
//   VIctory    -> <algo>\VIctory\target\release\victory.exe      (cargo build --release)
// Tambien se puede fijar la ruta a mano desde el menu Opciones.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include <shlobj.h>
#include <commdlg.h>
#include <string>
#include <vector>

#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

// ---------------------------------------------------------------------------
// Mensajes y IDs
// ---------------------------------------------------------------------------

#define WM_APP_LOG       (WM_APP + 1)   // lParam = std::wstring* (en heap, el handler lo borra)
#define WM_APP_PROC_DONE (WM_APP + 2)   // lParam = codigo de salida del proceso

#define ID_LIST_GAMES        1001
#define ID_COMBO_BACKEND     1002
#define ID_BTN_RUN           1003
#define ID_BTN_STOP          1004
#define ID_EDIT_LOG          1005

#define ID_MENU_ADD_FOLDER     2001
#define ID_MENU_REFRESH        2002
#define ID_MENU_EXIT            2003
#define ID_MENU_SET_LEONIDA     2010
#define ID_MENU_SET_XVALKYRIE   2011
#define ID_MENU_SET_VICTORY     2012
#define ID_MENU_AUTODETECT      2013
#define ID_MENU_ABOUT           2020

// ---------------------------------------------------------------------------
// Estructuras y estado global
// ---------------------------------------------------------------------------

struct Backend {
    std::wstring name;
    std::wstring exePath;
    bool available = false;
};

struct GameEntry {
    std::wstring displayName;
    std::wstring path; // carpeta del titulo (contiene MicrosoftGame.config)
};

static HINSTANCE g_hInst = NULL;
static HWND g_hList = NULL, g_hEdit = NULL, g_hCombo = NULL, g_hBtnRun = NULL, g_hBtnStop = NULL;

static std::vector<GameEntry> g_games;
static std::vector<std::wstring> g_libraryFolders;
static Backend g_backends[3]; // 0 = Leonida, 1 = XValkyrie, 2 = VIctory
static std::wstring g_configPath;

static volatile bool g_running = false;
static HANDLE g_hProcess = NULL;
static HANDLE g_hJob = NULL;

// ---------------------------------------------------------------------------
// Declaraciones adelantadas
// ---------------------------------------------------------------------------

bool FileExists(const std::wstring& p);
std::wstring DirectoryOf(const std::wstring& filePath);
std::wstring GetConfigPath();
void LoadConfig();
void SaveBackendPaths();
void SaveLibraryFolders();
void ScanDir(const std::wstring& dir, int depth, std::vector<GameEntry>& out);
void RescanGames();
void AutoDetectBackends();
void RefreshBackendCombo();
void AppendLog(const std::wstring& text);
bool BrowseForFolder(HWND owner, std::wstring& outPath);
bool BrowseForExe(HWND owner, LPCWSTR filter, std::wstring& outPath);
std::wstring FindTitleExe(const std::wstring& folder);
void Layout(HWND hwnd);
void OnRun(HWND hwnd);
void OnStop(HWND hwnd);
void LaunchBackend(HWND hwnd, const std::wstring& exePath, const std::wstring& args);
HMENU BuildMenu();
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

// ---------------------------------------------------------------------------
// Utilidades de archivos / rutas
// ---------------------------------------------------------------------------

bool FileExists(const std::wstring& p) {
    if (p.empty()) return false;
    DWORD attr = GetFileAttributesW(p.c_str());
    return (attr != INVALID_FILE_ATTRIBUTES) && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

std::wstring DirectoryOf(const std::wstring& filePath) {
    size_t pos = filePath.find_last_of(L"\\/");
    return (pos == std::wstring::npos) ? L"." : filePath.substr(0, pos);
}

// ---------------------------------------------------------------------------
// Config (%APPDATA%\UniLauncher\config.ini)
// ---------------------------------------------------------------------------

std::wstring GetConfigPath() {
    wchar_t appdata[MAX_PATH];
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appdata))) {
        return L"unilauncher_config.ini";
    }
    std::wstring dir = std::wstring(appdata) + L"\\UniLauncher";
    CreateDirectoryW(dir.c_str(), NULL);
    return dir + L"\\config.ini";
}

void LoadConfig() {
    g_configPath = GetConfigPath();
    wchar_t buf[2048];

    GetPrivateProfileStringW(L"Backends", L"Leonida", L"", buf, 2048, g_configPath.c_str());
    g_backends[0] = { L"Leonida", buf, FileExists(buf) };

    GetPrivateProfileStringW(L"Backends", L"XValkyrie", L"", buf, 2048, g_configPath.c_str());
    g_backends[1] = { L"XValkyrie", buf, FileExists(buf) };

    GetPrivateProfileStringW(L"Backends", L"VIctory", L"", buf, 2048, g_configPath.c_str());
    g_backends[2] = { L"VIctory", buf, FileExists(buf) };

    g_libraryFolders.clear();
    for (int i = 0; i < 64; i++) {
        std::wstring key = L"Folder" + std::to_wstring(i);
        GetPrivateProfileStringW(L"Library", key.c_str(), L"", buf, 2048, g_configPath.c_str());
        if (buf[0] == 0) break;
        g_libraryFolders.push_back(buf);
    }
}

void SaveBackendPaths() {
    WritePrivateProfileStringW(L"Backends", L"Leonida", g_backends[0].exePath.c_str(), g_configPath.c_str());
    WritePrivateProfileStringW(L"Backends", L"XValkyrie", g_backends[1].exePath.c_str(), g_configPath.c_str());
    WritePrivateProfileStringW(L"Backends", L"VIctory", g_backends[2].exePath.c_str(), g_configPath.c_str());
}

void SaveLibraryFolders() {
    // Borra la seccion entera y la vuelve a escribir, mas simple que llevar cuenta de huecos.
    WritePrivateProfileStringW(L"Library", NULL, NULL, g_configPath.c_str());
    for (size_t i = 0; i < g_libraryFolders.size(); i++) {
        std::wstring key = L"Folder" + std::to_wstring(i);
        WritePrivateProfileStringW(L"Library", key.c_str(), g_libraryFolders[i].c_str(), g_configPath.c_str());
    }
}

// ---------------------------------------------------------------------------
// Escaneo de la biblioteca de juegos
// ---------------------------------------------------------------------------

void ScanDir(const std::wstring& dir, int depth, std::vector<GameEntry>& out) {
    // Profundidad limitada y se ignoran reparse points (symlinks/junctions):
    // asi nunca se cuelga escaneando un enlace circular ni una unidad entera.
    if (depth > 2) return;

    std::wstring pattern = dir + L"\\*";
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;

    bool hasConfig = false;
    std::vector<std::wstring> subdirs;

    do {
        std::wstring name = fd.cFileName;
        if (name == L"." || name == L"..") continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            subdirs.push_back(dir + L"\\" + name);
        } else {
            if (_wcsicmp(name.c_str(), L"MicrosoftGame.config") == 0) hasConfig = true;
            if (_wcsicmp(name.c_str(), L"AppxManifest.xml") == 0) hasConfig = true;
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);

    if (hasConfig) {
        GameEntry ge;
        size_t pos = dir.find_last_of(L"\\/");
        ge.displayName = (pos == std::wstring::npos) ? dir : dir.substr(pos + 1);
        ge.path = dir;
        out.push_back(ge);
        return; // no sigas bajando dentro de un titulo ya reconocido
    }

    for (auto& sd : subdirs) ScanDir(sd, depth + 1, out);
}

void RescanGames() {
    g_games.clear();
    for (auto& folder : g_libraryFolders) {
        ScanDir(folder, 0, g_games);
    }
    SendMessageW(g_hList, LB_RESETCONTENT, 0, 0);
    for (auto& g : g_games) {
        SendMessageW(g_hList, LB_ADDSTRING, 0, (LPARAM)g.displayName.c_str());
    }
}

// Busca el ejecutable del titulo dentro de una carpeta (lo necesita VIctory,
// que a diferencia de XValkyrie pide el .exe y no la carpeta). Primero
// intenta leer el atributo Executable/Name de MicrosoftGame.config con una
// busqueda de texto simple (no es un parser XML completo a proposito, para
// no sumar dependencias); si no lo encuentra, usa el primer .exe de la
// carpeta como ultimo recurso.
std::wstring FindTitleExe(const std::wstring& folder) {
    std::wstring configPath = folder + L"\\MicrosoftGame.config";
    HANDLE hFile = CreateFileW(configPath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
        DWORD size = GetFileSize(hFile, NULL);
        if (size != INVALID_FILE_SIZE && size > 0 && size < (1024 * 1024)) {
            std::vector<char> buf(size);
            DWORD read = 0;
            if (ReadFile(hFile, buf.data(), size, &read, NULL) && read > 0) {
                std::string content(buf.data(), read);
                size_t pos = content.find("Executable");
                if (pos != std::string::npos) {
                    size_t namePos = content.find("Name", pos);
                    if (namePos != std::string::npos) {
                        size_t q1 = content.find('"', namePos);
                        if (q1 != std::string::npos) {
                            size_t q2 = content.find('"', q1 + 1);
                            if (q2 != std::string::npos) {
                                std::string exeName = content.substr(q1 + 1, q2 - q1 - 1);
                                CloseHandle(hFile);
                                int wlen = MultiByteToWideChar(CP_UTF8, 0, exeName.c_str(), -1, NULL, 0);
                                if (wlen > 1) {
                                    std::wstring wexe(wlen - 1, L'\0'); // sin el \0 final
                                    MultiByteToWideChar(CP_UTF8, 0, exeName.c_str(), -1, &wexe[0], wlen);
                                    return folder + L"\\" + wexe;
                                }
                            }
                        }
                    }
                }
            }
        }
        CloseHandle(hFile);
    }

    // Fallback: el primer .exe suelto en la raiz de la carpeta.
    std::wstring pattern = folder + L"\\*.exe";
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        std::wstring result = folder + L"\\" + fd.cFileName;
        FindClose(h);
        return result;
    }
    return L"";
}

// ---------------------------------------------------------------------------
// Deteccion automatica de backends
// ---------------------------------------------------------------------------

void AutoDetectBackends() {
    wchar_t modPath[MAX_PATH];
    GetModuleFileNameW(NULL, modPath, MAX_PATH);
    std::wstring exeDir = DirectoryOf(modPath);

    std::vector<std::wstring> bases = {
        exeDir,
        exeDir + L"\\..",
        exeDir + L"\\..\\..",
        exeDir + L"\\..\\..\\..",
    };

    wchar_t docs[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_PERSONAL, NULL, 0, docs))) {
        bases.push_back(docs);
    }

    std::vector<std::wstring> leonidaCandidates, xvCandidates, vicCandidates;
    for (auto& b : bases) {
        leonidaCandidates.push_back(b + L"\\Leonida\\gta6rt.exe");
        xvCandidates.push_back(b + L"\\xvalkyrie\\build\\win\\Release\\xvboot.exe");
        xvCandidates.push_back(b + L"\\xvalkyrie\\build\\win\\Debug\\xvboot.exe");
        vicCandidates.push_back(b + L"\\VIctory\\target\\release\\victory.exe");
        vicCandidates.push_back(b + L"\\VIctory\\target\\debug\\victory.exe");
    }

    for (auto& c : leonidaCandidates) {
        if (FileExists(c)) { g_backends[0].exePath = c; g_backends[0].available = true; break; }
    }
    for (auto& c : xvCandidates) {
        if (FileExists(c)) { g_backends[1].exePath = c; g_backends[1].available = true; break; }
    }
    for (auto& c : vicCandidates) {
        if (FileExists(c)) { g_backends[2].exePath = c; g_backends[2].available = true; break; }
    }

    SaveBackendPaths();
}

void RefreshBackendCombo() {
    SendMessageW(g_hCombo, CB_RESETCONTENT, 0, 0);
    for (auto& be : g_backends) {
        std::wstring label = be.name + (FileExists(be.exePath) ? L"" : L"  (no encontrado)");
        SendMessageW(g_hCombo, CB_ADDSTRING, 0, (LPARAM)label.c_str());
    }
    SendMessageW(g_hCombo, CB_SETCURSEL, 0, 0);
}

// ---------------------------------------------------------------------------
// Dialogos de archivo / carpeta
// ---------------------------------------------------------------------------

bool BrowseForFolder(HWND owner, std::wstring& outPath) {
    BROWSEINFOW bi{};
    bi.hwndOwner = owner;
    bi.lpszTitle = L"Selecciona una carpeta de biblioteca de juegos";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return false;
    wchar_t buf[MAX_PATH];
    bool ok = SHGetPathFromIDListW(pidl, buf) == TRUE;
    CoTaskMemFree(pidl);
    if (ok) outPath = buf;
    return ok;
}

bool BrowseForExe(HWND owner, LPCWSTR filter, std::wstring& outPath) {
    wchar_t buf[MAX_PATH] = L"";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = buf;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    ofn.lpstrTitle = L"Selecciona el ejecutable";
    if (GetOpenFileNameW(&ofn)) {
        outPath = buf;
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Log
// ---------------------------------------------------------------------------

void AppendLog(const std::wstring& text) {
    int len = GetWindowTextLengthW(g_hEdit);
    SendMessageW(g_hEdit, EM_SETSEL, (WPARAM)len, (LPARAM)len);
    SendMessageW(g_hEdit, EM_REPLACESEL, FALSE, (LPARAM)text.c_str());
    SendMessageW(g_hEdit, EM_SCROLLCARET, 0, 0);
}

// ---------------------------------------------------------------------------
// Lanzado de procesos: pipes + Job Object + hilos de lectura/espera
// ---------------------------------------------------------------------------

struct ReaderThreadParam {
    HANDLE hRead;
    HWND hwndMain;
};

struct WaiterParam {
    HANDLE hProcess;
    HWND hwndMain;
};

DWORD WINAPI ReaderThreadProc(LPVOID lp) {
    ReaderThreadParam* p = (ReaderThreadParam*)lp;
    char buf[4096];
    DWORD n;
    while (true) {
        BOOL ok = ReadFile(p->hRead, buf, sizeof(buf), &n, NULL);
        if (!ok || n == 0) break;

        int wlen = MultiByteToWideChar(CP_UTF8, 0, buf, (int)n, NULL, 0);
        if (wlen > 0) {
            std::wstring* ws = new std::wstring();
            ws->resize(wlen);
            MultiByteToWideChar(CP_UTF8, 0, buf, (int)n, &(*ws)[0], wlen);
            PostMessageW(p->hwndMain, WM_APP_LOG, 0, (LPARAM)ws);
        }
    }
    CloseHandle(p->hRead);
    delete p;
    return 0;
}

DWORD WINAPI WaiterThreadProc(LPVOID lp) {
    WaiterParam* p = (WaiterParam*)lp;
    WaitForSingleObject(p->hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(p->hProcess, &code);
    PostMessageW(p->hwndMain, WM_APP_PROC_DONE, 0, (LPARAM)code);
    delete p;
    return 0;
}

void LaunchBackend(HWND hwnd, const std::wstring& exePath, const std::wstring& args) {
    SECURITY_ATTRIBUTES sa{ sizeof(sa), NULL, TRUE };
    HANDLE hOutRead, hOutWrite, hErrRead, hErrWrite;

    if (!CreatePipe(&hOutRead, &hOutWrite, &sa, 0)) {
        AppendLog(L"[launcher] CreatePipe (stdout) fallo.\r\n");
        return;
    }
    if (!CreatePipe(&hErrRead, &hErrWrite, &sa, 0)) {
        AppendLog(L"[launcher] CreatePipe (stderr) fallo.\r\n");
        CloseHandle(hOutRead); CloseHandle(hOutWrite);
        return;
    }
    SetHandleInformation(hOutRead, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(hErrRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = hOutWrite;
    si.hStdError = hErrWrite;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION pi{};
    std::wstring workDir = DirectoryOf(exePath);
    std::wstring cmd = L"\"" + exePath + L"\" " + args;

    std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back(0);

    BOOL ok = CreateProcessW(
        NULL, cmdBuf.data(), NULL, NULL, TRUE,
        CREATE_NO_WINDOW | CREATE_SUSPENDED,
        NULL, workDir.c_str(), &si, &pi);

    CloseHandle(hOutWrite);
    CloseHandle(hErrWrite);

    if (!ok) {
        AppendLog(L"[launcher] No se pudo iniciar el proceso. Codigo de error " +
                  std::to_wstring(GetLastError()) + L"\r\n");
        CloseHandle(hOutRead);
        CloseHandle(hErrRead);
        return;
    }

    // Job Object con KILL_ON_JOB_CLOSE: al cerrar el handle (Stop, o cerrar
    // la ventana) se mata todo el arbol de procesos de una vez. Es lo que
    // evita que algo quede "trabado" corriendo en segundo plano.
    if (g_hJob) { CloseHandle(g_hJob); g_hJob = NULL; }
    g_hJob = CreateJobObjectW(NULL, NULL);
    if (g_hJob) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli{};
        jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(g_hJob, JobObjectExtendedLimitInformation, &jeli, sizeof(jeli));
        AssignProcessToJobObject(g_hJob, pi.hProcess);
    }

    g_hProcess = pi.hProcess;
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);

    g_running = true;
    EnableWindow(g_hBtnRun, FALSE);
    EnableWindow(g_hBtnStop, TRUE);
    SetWindowTextW(g_hEdit, L"");
    AppendLog(L"[launcher] Lanzando: " + cmd + L"\r\n");
    AppendLog(L"[launcher] Carpeta de trabajo: " + workDir + L"\r\n\r\n");

    ReaderThreadParam* po = new ReaderThreadParam{ hOutRead, hwnd };
    ReaderThreadParam* pe = new ReaderThreadParam{ hErrRead, hwnd };
    CloseHandle(CreateThread(NULL, 0, ReaderThreadProc, po, 0, NULL));
    CloseHandle(CreateThread(NULL, 0, ReaderThreadProc, pe, 0, NULL));

    WaiterParam* pw = new WaiterParam{ pi.hProcess, hwnd };
    CloseHandle(CreateThread(NULL, 0, WaiterThreadProc, pw, 0, NULL));
}

// ---------------------------------------------------------------------------
// Acciones de la interfaz
// ---------------------------------------------------------------------------

void OnRun(HWND hwnd) {
    if (g_running) return;

    int bIdx = (int)SendMessageW(g_hCombo, CB_GETCURSEL, 0, 0);
    if (bIdx == CB_ERR) {
        MessageBoxW(hwnd, L"Elegi un backend de la lista.", L"UniLauncher", MB_ICONWARNING);
        return;
    }
    Backend& be = g_backends[bIdx];
    if (!FileExists(be.exePath)) {
        MessageBoxW(hwnd,
            (L"No se encontro el ejecutable de " + be.name +
             L".\n\nConfiguralo en Opciones, o usa \"Auto-detectar backends\" "
             L"despues de compilar ese proyecto.").c_str(),
            L"UniLauncher", MB_ICONERROR);
        return;
    }

    bool needsGame = (be.name != L"Leonida"); // Leonida ya trae su propio game_path.txt del build
    std::wstring gameFolder;

    if (needsGame) {
        int gIdx = (int)SendMessageW(g_hList, LB_GETCURSEL, 0, 0);
        if (gIdx == LB_ERR) {
            MessageBoxW(hwnd,
                L"Elegi un juego de la lista (o agrega una carpeta de biblioteca "
                L"desde Archivo > Agregar carpeta de biblioteca...).",
                L"UniLauncher", MB_ICONWARNING);
            return;
        }
        gameFolder = g_games[gIdx].path;
    }

    std::wstring args;
    if (be.name == L"Leonida") {
        args = L"--run";
    } else if (be.name == L"XValkyrie") {
        // xvboot acepta directamente la carpeta del titulo.
        args = L"exec \"" + gameFolder + L"\"";
    } else { // VIctory
        // victory.exe pide el .exe del titulo, no la carpeta.
        std::wstring titleExe = FindTitleExe(gameFolder);
        if (titleExe.empty()) {
            MessageBoxW(hwnd,
                L"No se encontro un .exe de titulo dentro de esa carpeta para VIctory.",
                L"UniLauncher", MB_ICONERROR);
            return;
        }
        args = L"run \"" + titleExe + L"\" --execute";
    }

    LaunchBackend(hwnd, be.exePath, args);
}

void OnStop(HWND hwnd) {
    (void)hwnd;
    if (!g_running) return;
    AppendLog(L"\r\n[launcher] Deteniendo (se mata todo el arbol de procesos)...\r\n");
    if (g_hJob) {
        CloseHandle(g_hJob); // KILL_ON_JOB_CLOSE hace el resto.
        g_hJob = NULL;
    } else if (g_hProcess) {
        TerminateProcess(g_hProcess, 1);
    }
}

// ---------------------------------------------------------------------------
// Layout / ventana
// ---------------------------------------------------------------------------

void Layout(HWND hwnd) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    int margin = 8;
    int bottomBarH = 28;

    int listW = (w - margin * 3) * 55 / 100;
    if (listW < 100) listW = 100;
    int logW = (w - margin * 3) - listW;
    int topH = h - bottomBarH - margin * 3;
    if (topH < 50) topH = 50;

    MoveWindow(g_hList, margin, margin, listW, topH, TRUE);
    MoveWindow(g_hEdit, margin * 2 + listW, margin, logW, topH, TRUE);

    int y = margin * 2 + topH;
    MoveWindow(g_hCombo, margin, y, 220, 200, TRUE);
    MoveWindow(g_hBtnRun, margin + 230, y, 90, 24, TRUE);
    MoveWindow(g_hBtnStop, margin + 330, y, 90, 24, TRUE);
}

HMENU BuildMenu() {
    HMENU hMenu = CreateMenu();

    HMENU hFile = CreatePopupMenu();
    AppendMenuW(hFile, MF_STRING, ID_MENU_ADD_FOLDER, L"Agregar carpeta de biblioteca...");
    AppendMenuW(hFile, MF_STRING, ID_MENU_REFRESH, L"Volver a escanear");
    AppendMenuW(hFile, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hFile, MF_STRING, ID_MENU_EXIT, L"Salir");
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hFile, L"Archivo");

    HMENU hOpt = CreatePopupMenu();
    AppendMenuW(hOpt, MF_STRING, ID_MENU_SET_LEONIDA, L"Ruta de Leonida (gta6rt.exe)...");
    AppendMenuW(hOpt, MF_STRING, ID_MENU_SET_XVALKYRIE, L"Ruta de XValkyrie (xvboot.exe)...");
    AppendMenuW(hOpt, MF_STRING, ID_MENU_SET_VICTORY, L"Ruta de VIctory (victory.exe)...");
    AppendMenuW(hOpt, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hOpt, MF_STRING, ID_MENU_AUTODETECT, L"Auto-detectar backends");
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hOpt, L"Opciones");

    HMENU hHelp = CreatePopupMenu();
    AppendMenuW(hHelp, MF_STRING, ID_MENU_ABOUT, L"Acerca de...");
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hHelp, L"Ayuda");

    return hMenu;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

        g_hList = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", NULL,
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY,
            0, 0, 0, 0, hwnd, (HMENU)(INT_PTR)ID_LIST_GAMES, g_hInst, NULL);
        SendMessageW(g_hList, WM_SETFONT, (WPARAM)hFont, TRUE);

        g_hEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", NULL,
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
            0, 0, 0, 0, hwnd, (HMENU)(INT_PTR)ID_EDIT_LOG, g_hInst, NULL);
        SendMessageW(g_hEdit, WM_SETFONT, (WPARAM)hFont, TRUE);

        g_hCombo = CreateWindowExW(0, L"COMBOBOX", NULL,
            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
            0, 0, 0, 0, hwnd, (HMENU)(INT_PTR)ID_COMBO_BACKEND, g_hInst, NULL);
        SendMessageW(g_hCombo, WM_SETFONT, (WPARAM)hFont, TRUE);

        g_hBtnRun = CreateWindowExW(0, L"BUTTON", L"Run",
            WS_CHILD | WS_VISIBLE,
            0, 0, 0, 0, hwnd, (HMENU)(INT_PTR)ID_BTN_RUN, g_hInst, NULL);
        SendMessageW(g_hBtnRun, WM_SETFONT, (WPARAM)hFont, TRUE);

        g_hBtnStop = CreateWindowExW(0, L"BUTTON", L"Stop",
            WS_CHILD | WS_VISIBLE | WS_DISABLED,
            0, 0, 0, 0, hwnd, (HMENU)(INT_PTR)ID_BTN_STOP, g_hInst, NULL);
        SendMessageW(g_hBtnStop, WM_SETFONT, (WPARAM)hFont, TRUE);

        LoadConfig();
        AutoDetectBackends();
        RefreshBackendCombo();
        RescanGames();
        Layout(hwnd);

        AppendLog(L"UniLauncher listo.\r\n");
        AppendLog(L"Backends detectados automaticamente donde fue posible (ver el combo de abajo).\r\n");
        AppendLog(L"Este programa SOLO lanza los ejecutables de Leonida, XValkyrie y VIctory:\r\n");
        AppendLog(L"no arregla ni completa ninguno de los tres. Si un backend se cuelga o no\r\n");
        AppendLog(L"bootea el juego, es un problema de ese proyecto; aca podes al menos cortarlo\r\n");
        AppendLog(L"con el boton Stop sin que quede nada corriendo en segundo plano.\r\n\r\n");
        return 0;
    }

    case WM_SIZE:
        Layout(hwnd);
        return 0;

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        switch (id) {
        case ID_BTN_RUN:
            OnRun(hwnd);
            return 0;
        case ID_BTN_STOP:
            OnStop(hwnd);
            return 0;
        case ID_MENU_ADD_FOLDER: {
            std::wstring p;
            if (BrowseForFolder(hwnd, p)) {
                g_libraryFolders.push_back(p);
                SaveLibraryFolders();
                RescanGames();
            }
            return 0;
        }
        case ID_MENU_REFRESH:
            RescanGames();
            return 0;
        case ID_MENU_SET_LEONIDA: {
            std::wstring p;
            if (BrowseForExe(hwnd, L"gta6rt.exe\0gta6rt.exe\0Todos los archivos\0*.*\0", p)) {
                g_backends[0].exePath = p;
                g_backends[0].available = true;
                SaveBackendPaths();
                RefreshBackendCombo();
            }
            return 0;
        }
        case ID_MENU_SET_XVALKYRIE: {
            std::wstring p;
            if (BrowseForExe(hwnd, L"xvboot.exe\0xvboot.exe\0Todos los archivos\0*.*\0", p)) {
                g_backends[1].exePath = p;
                g_backends[1].available = true;
                SaveBackendPaths();
                RefreshBackendCombo();
            }
            return 0;
        }
        case ID_MENU_SET_VICTORY: {
            std::wstring p;
            if (BrowseForExe(hwnd, L"victory.exe\0victory.exe\0Todos los archivos\0*.*\0", p)) {
                g_backends[2].exePath = p;
                g_backends[2].available = true;
                SaveBackendPaths();
                RefreshBackendCombo();
            }
            return 0;
        }
        case ID_MENU_AUTODETECT:
            AutoDetectBackends();
            RefreshBackendCombo();
            MessageBoxW(hwnd, L"Deteccion automatica terminada. Revisa el selector de backend.",
                        L"UniLauncher", MB_ICONINFORMATION);
            return 0;
        case ID_MENU_ABOUT:
            MessageBoxW(hwnd,
                L"UniLauncher\n\n"
                L"Lanza Leonida, XValkyrie y VIctory desde una sola ventana.\n\n"
                L"Este programa NO modifica, arregla ni fusiona el codigo de esos tres "
                L"proyectos. Cada uno sigue siendo independiente, con su propio estado de "
                L"avance (a la fecha de este build, ninguno deja un juego jugable de punta "
                L"a punta). UniLauncher solo arma la linea de comandos documentada de cada "
                L"uno, la ejecuta, y te muestra su log. El boton Stop mata todo el arbol de "
                L"procesos para que nada quede trabado en segundo plano.",
                L"Acerca de", MB_ICONINFORMATION);
            return 0;
        case ID_MENU_EXIT:
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
            return 0;
        }
        return 0;
    }

    case WM_APP_LOG: {
        std::wstring* ws = (std::wstring*)lParam;
        AppendLog(*ws);
        delete ws;
        return 0;
    }

    case WM_APP_PROC_DONE: {
        DWORD code = (DWORD)lParam;
        AppendLog(L"\r\n[launcher] Proceso terminado (codigo " + std::to_wstring(code) + L").\r\n");
        if (g_hProcess) { CloseHandle(g_hProcess); g_hProcess = NULL; }
        g_running = false;
        EnableWindow(g_hBtnRun, TRUE);
        EnableWindow(g_hBtnStop, FALSE);
        return 0;
    }

    case WM_CLOSE:
        if (g_running && g_hJob) {
            CloseHandle(g_hJob); // mata el arbol de procesos antes de cerrar la ventana
            g_hJob = NULL;
        }
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow) {
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    g_hInst = hInstance;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"UniLauncherMain";
    wc.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(0, L"UniLauncherMain",
        L"UniLauncher - Leonida / XValkyrie / VIctory",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1000, 650,
        NULL, BuildMenu(), hInstance, NULL);

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    CoUninitialize();
    return (int)msg.wParam;
}
