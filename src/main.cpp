#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <winspool.h>
#include <shellapi.h>
#include <uxtheme.h>
#include <wtypes.h>
#include <objidl.h>
#include <gdiplus.h>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cwctype>
#include <ctime>
#include <map>
#include <cstring>
#include <cmath>
#include <limits>
#include <memory>
#include <optional>
#include "sqlite3.h"
#include "resource.h"
#include "version.h"
#include "qrcodegen.hpp"

using std::string;
using std::wstring;

enum ControlId {
    ID_NAME = 1001, ID_STORAGE_NO, ID_BARCODE, ID_LOCATION, ID_AMOUNT,
    ID_UNIT, ID_SHELF, ID_PRICE, ID_CHANGED, ID_NOTES, ID_SEARCH, ID_LIST,
    ID_NEW, ID_SCAN, ID_DELETE, ID_SAVE, ID_OLDEST, ID_NEWEST, ID_STATUS, ID_GRIP,
    ID_DB_EXPORT = 2001, ID_DB_NEW, ID_DB_BACKUP, ID_DB_RESTORE, ID_EXIT,
    ID_SETTINGS, ID_ABOUT,
    ID_SETTING_DARK = 4001, ID_SETTING_CURRENCY, ID_SETTING_UNIT,
    ID_SETTING_LABEL_LOCATION, ID_SETTING_LABEL_SHELF, ID_SETTING_LABEL_BARCODE,
    ID_SETTING_LABEL_LOCATION_ENABLED, ID_SETTING_LABEL_SHELF_ENABLED, ID_SETTING_LABEL_BARCODE_ENABLED,
    ID_TOP_DATABASE = 5001, ID_IMAGE_PREVIEW, ID_IMAGE_LOAD, ID_IMAGE_SAVE, ID_IMAGE_DELETE,
    ID_STORAGE_AUTO_NUMBER, ID_CODE_GENERATOR, ID_SHELF_AUTO_NUMBER, ID_SHELF_CODE_GENERATOR, ID_CODE_TAB, ID_CODE_KIND, ID_CODE_START, ID_CODE_COUNT,
    ID_CODE_DIRECTION, ID_CODE_WIDTH, ID_CODE_HEIGHT, ID_CODE_PREVIEW, ID_CODE_PRINT,
    ID_CODE_AUTOSCALE, ID_CODE_FIT, ID_CODE_LOGO, ID_CODE_TAPE_WIDTH, ID_CODE_FIT_TAPE, ID_CODE_QR_TEXT_POSITION, ID_CODE_MAX_LENGTH, ID_CODE_PRINTER, ID_CODE_PRINTER_STATUS, ID_MESSAGE_TEXT, ID_PROMPT_LABEL, ID_PROMPT_EDIT,
    ID_FILE_PATH, ID_FILE_LIST, ID_FILE_NAME, ID_FILE_UP, ID_FILE_REFRESH, ID_FILE_LABEL,
    ID_SCANNER_VALUE, ID_SCANNER_STATUS, ID_SCANNER_INSTRUCTION, ID_SCANNER_AUTO_CLOSE
};

struct Item {
    sqlite3_int64 id = 0;
    wstring name, storageNo, barcode, location, unit, shelf, notes, changed;
    int amount = 0;
    double price = 0;
};

HINSTANCE g_instance;
HWND g_main = nullptr;
sqlite3* g_db = nullptr;
sqlite3_int64 g_currentId = 0;
wstring g_originalStorageNo;
HFONT g_font = nullptr, g_titleFont = nullptr;
HBRUSH g_bgBrush = nullptr, g_panelBrush = nullptr;
HBRUSH g_lightBgBrush = nullptr, g_lightPanelBrush = nullptr, g_darkBgBrush = nullptr, g_darkPanelBrush = nullptr;
COLORREF g_accent = RGB(205, 111, 28);
wstring g_dbPath;
wstring g_shelfFilter;
std::map<string, wstring> g_settings;
std::map<HWND, bool> g_modalOwnerEnabled;
bool g_darkMode = false;
bool g_runningUnderWine = false, g_windowsSystemDark = false;
bool g_gripDragging = false;
POINT g_gripStart{};
RECT g_gripWindowStart{};
HMENU g_databaseMenu = nullptr;
ULONG_PTR g_gdiplusToken = 0;
std::vector<unsigned char> g_currentImage;
bool g_imageDirty = false;
volatile LONG g_printing = 0;
Gdiplus::Image* g_qrLogoImage = nullptr;
IStream* g_qrLogoStream = nullptr;
SRWLOCK g_qrLogoCacheLock = SRWLOCK_INIT;
std::map<int,std::shared_ptr<const std::vector<BYTE>>> g_qrLogoCache;
bool g_formDirty = false, g_loadingForm = false;

bool confirmAction(const wchar_t* title,const wstring& message);
void showNotice(HWND owner,const wchar_t* title,const wstring& message,bool error=false);
bool showThemedMessage(HWND owner,const wchar_t* title,const wstring& message,bool yesNo);
bool resolveUnsavedChanges();

void applyTheme();
LRESULT themeControlColor(UINT msg, WPARAM wp);
LRESULT eraseThemedBackground(HWND hwnd, WPARAM wp);
BOOL CALLBACK ThemeChild(HWND child, LPARAM);
void applyWindowFrameTheme(HWND window);
void applyCustomLabels();
bool browseFile(wchar_t* path,DWORD size,bool save,const wchar_t* filter,const wchar_t* defExt,HWND owner=nullptr);

void beginModal(HWND owner,HWND dialog){
    if(!owner||!dialog)return;
    g_modalOwnerEnabled[dialog]=IsWindowEnabled(owner)!=FALSE;
    EnableWindow(owner,FALSE);
}

void endModal(HWND owner,HWND dialog){
    if(!owner||!dialog)return;
    auto state=g_modalOwnerEnabled.find(dialog);
    bool wasEnabled=state==g_modalOwnerEnabled.end()||state->second;
    if(state!=g_modalOwnerEnabled.end())g_modalOwnerEnabled.erase(state);
    if(wasEnabled&&IsWindow(owner)){EnableWindow(owner,TRUE);SetActiveWindow(owner);}
}

void runModalLoop(HWND dialog){
    MSG message{};
    while(IsWindow(dialog)){
        BOOL result=GetMessageW(&message,nullptr,0,0);
        if(result<=0){if(IsWindow(dialog))DestroyWindow(dialog);PostQuitMessage(result==0?(int)message.wParam:1);break;}
        if(!IsDialogMessageW(dialog,&message)){TranslateMessage(&message);DispatchMessageW(&message);}
    }
}

bool runningUnderWine(){HMODULE ntdll=GetModuleHandleW(L"ntdll.dll");return ntdll&&GetProcAddress(ntdll,"wine_get_version")!=nullptr;}

bool windowsSystemDark(bool& dark){
    HKEY key=nullptr;DWORD light=1,size=sizeof(light),type=0;
    if(RegOpenKeyExW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",0,KEY_QUERY_VALUE,&key)!=ERROR_SUCCESS)return false;
    LONG result=RegQueryValueExW(key,L"AppsUseLightTheme",nullptr,&type,(BYTE*)&light,&size);RegCloseKey(key);
    if(result!=ERROR_SUCCESS||type!=REG_DWORD)return false;dark=light==0;return true;
}

wstring utf8ToWide(const char* input) {
    if (!input) return L"";
    size_t byteCount = std::strlen(input);
    if (!byteCount || byteCount > static_cast<size_t>(std::numeric_limits<int>::max())) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, input, static_cast<int>(byteCount), nullptr, 0);
    if (n <= 0) return L"";
    wstring result(static_cast<size_t>(n), L'\0');
    if (MultiByteToWideChar(CP_UTF8, 0, input, static_cast<int>(byteCount), result.data(), n) != n) return L"";
    return result;
}

string wideToUtf8(const wstring& input) {
    if (input.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, input.c_str(), (int)input.size(), nullptr, 0, nullptr, nullptr);
    string result((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, input.c_str(), (int)input.size(), result.data(), n, nullptr, nullptr);
    return result;
}

wstring getText(HWND parent, int id) {
    HWND h = GetDlgItem(parent, id);
    int len = GetWindowTextLengthW(h);
    wstring value((size_t)len + 1, L'\0');
    if (len) GetWindowTextW(h, value.data(), len + 1);
    value.resize((size_t)len);
    return value;
}

void setText(HWND parent, int id, const wstring& value) {
    SetWindowTextW(GetDlgItem(parent, id), value.c_str());
}

void setPriceAlignment(bool hasValue) {
    if (!g_main) return;
    HWND price = GetDlgItem(g_main, ID_PRICE);
    LONG_PTR style = GetWindowLongPtrW(price, GWL_STYLE);
    style &= ~(ES_CENTER | ES_RIGHT);
    style |= hasValue ? ES_RIGHT : ES_CENTER;
    SetWindowLongPtrW(price, GWL_STYLE, style);
    SetWindowPos(price, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    InvalidateRect(price, nullptr, TRUE);
}

wstring moduleDirectory() {
    std::vector<wchar_t> path(512);
    for(;;){
        DWORD length=GetModuleFileNameW(nullptr,path.data(),(DWORD)path.size());
        if(length==0)return L".";
        if(length<path.size()-1){path.resize(length);break;}
        if(path.size()>=32768)return L".";
        path.resize(path.size()*2);
    }
    wstring dir(path.data(),path.size());
    size_t pos = dir.find_last_of(L"\\/");
    return pos == wstring::npos ? L"." : dir.substr(0, pos);
}

bool dbExec(sqlite3* database, const char* sql) {
    char* error = nullptr;
    if (!database || sqlite3_exec(database, sql, nullptr, nullptr, &error) != SQLITE_OK) {
        const char* detail = error ? error : (database ? sqlite3_errmsg(database) : "Keine Datenbank geöffnet");
        wstring msg = L"Datenbankfehler: " + utf8ToWide(detail);
        sqlite3_free(error);
        if(g_main)showNotice(g_main,L"LogS",msg,true);else MessageBoxW(nullptr,msg.c_str(),L"LogS",MB_ICONERROR);
        return false;
    }
    return true;
}

bool dbExec(const char* sql) {
    return dbExec(g_db, sql);
}

wstring discoverDatabase() {
    wstring directory = moduleDirectory();
    std::vector<wstring> databases;
    WIN32_FIND_DATAW data{};
    HANDLE search = FindFirstFileW((directory + L"\\*.db").c_str(), &data);
    if (search != INVALID_HANDLE_VALUE) {
        do {
            if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                databases.push_back(directory + L"\\" + data.cFileName);
        } while (FindNextFileW(search, &data));
        FindClose(search);
    }
    std::sort(databases.begin(), databases.end());
    if (databases.empty()) return directory + L"\\logs.db";
    if (databases.size() == 1) return databases.front();

    wchar_t selected[MAX_PATH] = L"";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = g_main;
    ofn.lpstrFile = selected; ofn.nMaxFile = MAX_PATH;
    ofn.lpstrInitialDir = directory.c_str(); ofn.lpstrTitle = L"Datenbank auswählen";
    ofn.lpstrFilter = L"LogS-Datenbank (*.db)\0*.db\0Alle Dateien\0*.*\0";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    return GetOpenFileNameW(&ofn) ? wstring(selected) : L"";
}

bool loadSettings(sqlite3* database) {
    std::map<string,wstring> loaded;sqlite3_stmt* st=nullptr;wstring detail;
    if(sqlite3_prepare_v2(database,"SELECT key,value FROM settings",-1,&st,nullptr)!=SQLITE_OK)detail=utf8ToWide(sqlite3_errmsg(database));
    int step=SQLITE_DONE;
    if(detail.empty())while((step=sqlite3_step(st))==SQLITE_ROW){const char* key=(const char*)sqlite3_column_text(st,0);loaded[key?key:""]=utf8ToWide((const char*)sqlite3_column_text(st,1));}
    if(detail.empty()&&step!=SQLITE_DONE)detail=utf8ToWide(sqlite3_errmsg(database));
    if(sqlite3_finalize(st)!=SQLITE_OK&&detail.empty())detail=utf8ToWide(sqlite3_errmsg(database));
    if(!detail.empty()){wstring message=L"Die Einstellungen konnten nicht aus der Datenbank geladen werden:\n"+detail;if(g_main)showNotice(g_main,L"LogS",message,true);else MessageBoxW(nullptr,message.c_str(),L"LogS",MB_ICONERROR);return false;}
    g_settings.swap(loaded);auto dark=g_settings.find("dark_mode");g_darkMode=dark!=g_settings.end()&&dark->second==L"1";return true;
}

constexpr int LOGS_SQLITE_APPLICATION_ID=0x4C6F6753;
constexpr int LOGS_SCHEMA_VERSION=2;

std::optional<int> databasePragmaInt(sqlite3* database,const char* pragma){
    sqlite3_stmt* st=nullptr;
    if(sqlite3_prepare_v2(database,pragma,-1,&st,nullptr)!=SQLITE_OK){sqlite3_finalize(st);return std::nullopt;}
    int step=sqlite3_step(st),value=step==SQLITE_ROW?sqlite3_column_int(st,0):0;
    sqlite3_finalize(st);
    return step==SQLITE_ROW?std::optional<int>(value):std::nullopt;
}

bool databaseHasTable(sqlite3* database,const char* table){
    sqlite3_stmt* st=nullptr;bool found=false;if(sqlite3_prepare_v2(database,"SELECT 1 FROM sqlite_master WHERE type='table' AND name=?1 LIMIT 1",-1,&st,nullptr)==SQLITE_OK){sqlite3_bind_text(st,1,table,-1,SQLITE_STATIC);found=sqlite3_step(st)==SQLITE_ROW;}sqlite3_finalize(st);return found;
}

bool databaseTableHasColumn(sqlite3* database,const char* table,const char* column){
    string sql="PRAGMA table_info(\""+string(table)+"\")";sqlite3_stmt* st=nullptr;bool found=false;if(sqlite3_prepare_v2(database,sql.c_str(),-1,&st,nullptr)==SQLITE_OK)while(sqlite3_step(st)==SQLITE_ROW){const char* current=(const char*)sqlite3_column_text(st,1);if(current&&strcmp(current,column)==0){found=true;break;}}sqlite3_finalize(st);return found;
}

bool databaseIsLogSOrEmpty(sqlite3* database){
    sqlite3_stmt* st=nullptr;int applicationId=0,userTables=0;if(sqlite3_prepare_v2(database,"PRAGMA application_id",-1,&st,nullptr)==SQLITE_OK&&sqlite3_step(st)==SQLITE_ROW)applicationId=sqlite3_column_int(st,0);sqlite3_finalize(st);
    if(applicationId==LOGS_SQLITE_APPLICATION_ID)return true;if(applicationId!=0)return false;
    if(sqlite3_prepare_v2(database,"SELECT count(*) FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%'",-1,&st,nullptr)==SQLITE_OK&&sqlite3_step(st)==SQLITE_ROW)userTables=sqlite3_column_int(st,0);sqlite3_finalize(st);
    if(userTables==0)return true;
    return databaseHasTable(database,"items")&&databaseHasTable(database,"settings")&&databaseTableHasColumn(database,"items","name")&&databaseTableHasColumn(database,"items","storage_no")&&databaseTableHasColumn(database,"items","barcode")&&databaseTableHasColumn(database,"items","shelf");
}

bool openDatabase(const wstring& requestedPath = L"") {
    wstring selected = requestedPath.empty() ? discoverDatabase() : requestedPath;
    if (selected.empty()) return false;
    sqlite3* candidate = nullptr;
    string path = wideToUtf8(selected);
    if (sqlite3_open_v2(path.c_str(), &candidate, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK) {
        if (candidate) sqlite3_close(candidate);
        MessageBoxW(g_main, L"Die gewählte Datenbank konnte nicht geöffnet werden. Prüfen Sie Datei und Schreibrechte.", L"LogS", MB_ICONERROR);
        return false;
    }
    char* validationError = nullptr;
    if (sqlite3_exec(candidate, "PRAGMA schema_version;", nullptr, nullptr, &validationError) != SQLITE_OK) {
        sqlite3_free(validationError); sqlite3_close(candidate);
        MessageBoxW(g_main, L"Die gewählte Datei ist keine gültige SQLite-Datenbank.", L"LogS", MB_ICONERROR);
        return false;
    }
    if(!databaseIsLogSOrEmpty(candidate)){sqlite3_close(candidate);const wchar_t* message=L"Die ausgewählte SQLite-Datei ist keine LogS-Datenbank und wird deshalb nicht verändert.";if(g_main)showNotice(g_main,L"LogS",message,true);else MessageBoxW(nullptr,message,L"LogS",MB_ICONERROR);return false;}
    std::optional<int> userVersion=databasePragmaInt(candidate,"PRAGMA user_version");
    if(!userVersion){sqlite3_close(candidate);const wchar_t* message=L"Die Version des Datenbankschemas konnte nicht gelesen werden.";if(g_main)showNotice(g_main,L"LogS",message,true);else MessageBoxW(nullptr,message,L"LogS",MB_ICONERROR);return false;}
    if(*userVersion>LOGS_SCHEMA_VERSION){
        wstring message=L"Diese Datenbank wurde mit einer neueren LogS-Version erstellt.\n\nDatenbankschema: "+std::to_wstring(*userVersion)+L"\nUnterstützt bis: "+std::to_wstring(LOGS_SCHEMA_VERSION);
        sqlite3_close(candidate);if(g_main)showNotice(g_main,L"LogS",message,true);else MessageBoxW(nullptr,message.c_str(),L"LogS",MB_ICONERROR);return false;
    }
    sqlite3_busy_timeout(candidate, 4000);
    bool initialized=dbExec(candidate,"PRAGMA journal_mode=DELETE; PRAGMA synchronous=FULL; PRAGMA foreign_keys=ON; PRAGMA temp_store=MEMORY;");
    bool migrationStarted=false;
    if(initialized){migrationStarted=dbExec(candidate,"BEGIN IMMEDIATE;");initialized=migrationStarted;}
    if(initialized)initialized=dbExec(candidate,"PRAGMA application_id=1282369363;"
           "CREATE TABLE IF NOT EXISTS items("
           "id INTEGER PRIMARY KEY AUTOINCREMENT,"
           "name TEXT NOT NULL, storage_no TEXT, barcode TEXT, location TEXT,"
           "amount INTEGER NOT NULL DEFAULT 0, unit TEXT NOT NULL DEFAULT 'Stück',"
           "shelf TEXT, price REAL NOT NULL DEFAULT 0, notes TEXT,"
           "created_at TEXT NOT NULL DEFAULT (datetime('now','localtime')) ,"
           "updated_at TEXT NOT NULL DEFAULT (datetime('now','localtime')));"
           "CREATE INDEX IF NOT EXISTS idx_items_name ON items(name);"
           "CREATE INDEX IF NOT EXISTS idx_items_barcode ON items(barcode);"
           "CREATE TABLE IF NOT EXISTS settings(key TEXT PRIMARY KEY, value TEXT NOT NULL);"
           "INSERT OR IGNORE INTO settings(key,value) VALUES('currency','€');"
           "INSERT OR IGNORE INTO settings(key,value) VALUES('default_unit','Stück');"
           "INSERT OR IGNORE INTO settings(key,value) VALUES('dark_mode','0');"
           "INSERT OR IGNORE INTO settings(key,value) VALUES('windows_theme_seen','');"
           "INSERT OR IGNORE INTO settings(key,value) VALUES('windows_theme_override','0');"
           "INSERT OR IGNORE INTO settings(key,value) VALUES('label_location','Lagerort');"
           "INSERT OR IGNORE INTO settings(key,value) VALUES('label_shelf','Fach');"
           "INSERT OR IGNORE INTO settings(key,value) VALUES('label_barcode','EAN / UPC / GTIN');"
           "INSERT OR IGNORE INTO settings(key,value) VALUES('label_location_enabled','0');"
           "INSERT OR IGNORE INTO settings(key,value) VALUES('label_shelf_enabled','0');"
           "INSERT OR IGNORE INTO settings(key,value) VALUES('label_barcode_enabled','0');");
    if(initialized&&!databaseTableHasColumn(candidate,"items","image_data"))initialized=dbExec(candidate,"ALTER TABLE items ADD COLUMN image_data BLOB;");
    if(initialized&&!databaseTableHasColumn(candidate,"items","image_mime"))initialized=dbExec(candidate,"ALTER TABLE items ADD COLUMN image_mime TEXT;");
    // Older databases may contain duplicates from builds that did not enforce
    // uniqueness. Keep the most recently changed assignment and unassign the
    // older records so no item data is lost during migration.
    if(initialized&&*userVersion<2)initialized=dbExec(candidate,"DROP INDEX IF EXISTS idx_items_storage_no_unique;"
           "UPDATE items AS older SET storage_no='' "
           "WHERE storage_no<>'' AND EXISTS(SELECT 1 FROM items AS newer "
           "WHERE newer.storage_no=older.storage_no COLLATE NOCASE AND "
           "(newer.updated_at>older.updated_at OR "
           "(newer.updated_at=older.updated_at AND newer.id>older.id)));"
           "CREATE UNIQUE INDEX idx_items_storage_no_unique "
           "ON items(storage_no COLLATE NOCASE) WHERE storage_no<>'';");
    if(initialized&&*userVersion>=2)initialized=dbExec(candidate,"CREATE UNIQUE INDEX IF NOT EXISTS idx_items_storage_no_unique ON items(storage_no COLLATE NOCASE) WHERE storage_no<>'';");
    if(initialized){string schemaSql="PRAGMA user_version="+std::to_string(LOGS_SCHEMA_VERSION)+";";initialized=dbExec(candidate,schemaSql.c_str());}
    if(initialized)initialized=dbExec(candidate,"COMMIT;");
    if(!initialized){if(migrationStarted)sqlite3_exec(candidate,"ROLLBACK;",nullptr,nullptr,nullptr);sqlite3_close(candidate);return false;}
    if(!loadSettings(candidate)){sqlite3_close(candidate);return false;}
    sqlite3* previous=g_db;
    g_db=candidate;
    g_dbPath=selected;
    if(previous)sqlite3_close(previous);
    return true;
}

wstring setting(const char* key, const wchar_t* fallback) {
    auto found = g_settings.find(key);
    return found == g_settings.end() ? wstring(fallback) : found->second;
}

bool saveSetting(const char* key, const wstring& value) {
    sqlite3_stmt* st = nullptr;
    if(sqlite3_prepare_v2(g_db, "INSERT INTO settings(key,value) VALUES(?,?) ON CONFLICT(key) DO UPDATE SET value=excluded.value", -1, &st, nullptr)!=SQLITE_OK){
        showNotice(g_main,L"Einstellungen",utf8ToWide(sqlite3_errmsg(g_db)),true);return false;
    }
    string v = wideToUtf8(value);
    sqlite3_bind_text(st, 1, key, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, v.c_str(), -1, SQLITE_TRANSIENT);
    bool saved=sqlite3_step(st)==SQLITE_DONE;wstring detail=saved?L"":utf8ToWide(sqlite3_errmsg(g_db));
    if(sqlite3_finalize(st)!=SQLITE_OK&&saved){saved=false;detail=utf8ToWide(sqlite3_errmsg(g_db));}
    if(saved)g_settings[key]=value;else showNotice(g_main,L"Einstellungen",detail,true);return saved;
}

bool saveSettingsBatch(const std::vector<std::pair<string,wstring>>& values,HWND owner){
    char* rawError=nullptr;
    if(sqlite3_exec(g_db,"BEGIN IMMEDIATE;",nullptr,nullptr,&rawError)!=SQLITE_OK){
        wstring detail=utf8ToWide(rawError?rawError:sqlite3_errmsg(g_db));sqlite3_free(rawError);
        wstring message=L"Die Einstellungen konnten nicht gespeichert werden:\n"+detail;if(owner||g_main)showNotice(owner?owner:g_main,L"Einstellungen",message,true);else MessageBoxW(nullptr,message.c_str(),L"Einstellungen",MB_ICONERROR);return false;
    }
    sqlite3_stmt* st=nullptr;wstring detail;
    if(sqlite3_prepare_v2(g_db,"INSERT INTO settings(key,value) VALUES(?1,?2) ON CONFLICT(key) DO UPDATE SET value=excluded.value",-1,&st,nullptr)!=SQLITE_OK)detail=utf8ToWide(sqlite3_errmsg(g_db));
    for(const auto& entry:values){
        if(!detail.empty())break;
        string encoded=wideToUtf8(entry.second);sqlite3_reset(st);sqlite3_clear_bindings(st);
        if(sqlite3_bind_text(st,1,entry.first.c_str(),-1,SQLITE_TRANSIENT)!=SQLITE_OK||sqlite3_bind_text(st,2,encoded.c_str(),-1,SQLITE_TRANSIENT)!=SQLITE_OK||sqlite3_step(st)!=SQLITE_DONE)detail=utf8ToWide(sqlite3_errmsg(g_db));
    }
    if(st&&sqlite3_finalize(st)!=SQLITE_OK&&detail.empty())detail=utf8ToWide(sqlite3_errmsg(g_db));
    if(detail.empty()){
        rawError=nullptr;if(sqlite3_exec(g_db,"COMMIT;",nullptr,nullptr,&rawError)!=SQLITE_OK)detail=utf8ToWide(rawError?rawError:sqlite3_errmsg(g_db));sqlite3_free(rawError);
    }
    if(!detail.empty()){
        sqlite3_exec(g_db,"ROLLBACK;",nullptr,nullptr,nullptr);
        wstring message=L"Die Einstellungen konnten nicht vollständig gespeichert werden:\n"+detail;if(owner||g_main)showNotice(owner?owner:g_main,L"Einstellungen",message,true);else MessageBoxW(nullptr,message.c_str(),L"Einstellungen",MB_ICONERROR);return false;
    }
    for(const auto& entry:values)g_settings[entry.first]=entry.second;
    return true;
}

void synchronizeWindowsTheme(bool startup){
    if(g_runningUnderWine)return;bool systemDark=false;if(!windowsSystemDark(systemDark))return;
    wstring current=systemDark?L"1":L"0";wstring seen=setting("windows_theme_seen",L"");bool overridden=setting("windows_theme_override",L"0")==L"1";
    bool systemChanged=!seen.empty()&&seen!=current;
    std::vector<std::pair<string,wstring>> changes;
    if(!overridden||systemChanged||seen.empty()){
        changes.push_back({"dark_mode",current});changes.push_back({"windows_theme_override",L"0"});
    }
    changes.push_back({"windows_theme_seen",current});if(!saveSettingsBatch(changes,g_main))return;
    g_windowsSystemDark=systemDark;g_darkMode=setting("dark_mode",L"0")==L"1";
    if(!startup&&(!overridden||systemChanged||seen.empty())){applyTheme();HWND settings=FindWindowW(L"LogSSettings",nullptr);if(settings){SendMessageW(GetDlgItem(settings,ID_SETTING_DARK),BM_SETCHECK,systemDark?BST_CHECKED:BST_UNCHECKED,0);SetClassLongPtrW(settings,GCLP_HBRBACKGROUND,(LONG_PTR)g_bgBrush);applyWindowFrameTheme(settings);EnumChildWindows(settings,ThemeChild,0);RedrawWindow(settings,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_FRAME|RDW_ALLCHILDREN);}}
}

void checkWindowsThemeChange(){bool systemDark=false;if(!g_runningUnderWine&&windowsSystemDark(systemDark)&&systemDark!=g_windowsSystemDark)synchronizeWindowsTheme(false);}

HWND addControl(const wchar_t* cls, const wchar_t* text, DWORD style, int id, HWND parent, DWORD ex = 0) {
    HWND h = CreateWindowExW(ex, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10,
                             parent, (HMENU)(INT_PTR)id, g_instance, nullptr);
    SendMessageW(h, WM_SETFONT, (WPARAM)g_font, TRUE);
    return h;
}

void selectCombo(HWND combo, const wstring& value, bool addMissing = true) {
    LRESULT index = SendMessageW(combo, CB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)value.c_str());
    if (index == CB_ERR && addMissing)
        index = SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)value.c_str());
    if (index != CB_ERR) SendMessageW(combo, CB_SETCURSEL, (WPARAM)index, 0);
}

void fillUnitCombo(HWND combo, const wstring& selected) {
    for(const wchar_t* unit:{L"Stück",L"Stange",L"Karton",L"Palette",L"kg",L"g",L"l",L"m",L"m²",L"Packung"})
        SendMessageW(combo,CB_ADDSTRING,0,(LPARAM)unit);
    selectCombo(combo, selected);
}

bool measureOwnerItem(MEASUREITEMSTRUCT* measure) {
    if (measure->CtlType == ODT_MENU) {
        const wchar_t* text = (const wchar_t*)measure->itemData;
        if (!text) { measure->itemWidth=8;measure->itemHeight=9;return true; }
        HDC dc = GetDC(g_main); HFONT old = (HFONT)SelectObject(dc,g_font); SIZE size{};
        GetTextExtentPoint32W(dc,text ? text : L"",text ? lstrlenW(text) : 0,&size);
        SelectObject(dc,old);ReleaseDC(g_main,dc);
        measure->itemWidth=(UINT)std::max<LONG>(0,size.cx+24);measure->itemHeight=24;return true;
    }
    if (measure->CtlType == ODT_COMBOBOX) { measure->itemHeight=24; return true; }
    return false;
}

bool drawOwnerItem(DRAWITEMSTRUCT* draw) {
    if(draw->CtlType==ODT_TAB){
        TCITEMW item{};wchar_t label[80]{};item.mask=TCIF_TEXT;item.pszText=label;item.cchTextMax=80;TabCtrl_GetItem(draw->hwndItem,(int)draw->itemID,&item);
        bool selected=(draw->itemState&ODS_SELECTED)!=0;COLORREF color=g_darkMode?(selected?RGB(62,65,70):RGB(42,44,48)):(selected?RGB(255,255,255):RGB(232,233,235));HBRUSH brush=CreateSolidBrush(color);FillRect(draw->hDC,&draw->rcItem,brush);DeleteObject(brush);
        SetBkMode(draw->hDC,TRANSPARENT);SetTextColor(draw->hDC,g_darkMode?RGB(240,240,240):RGB(20,20,20));HFONT old=(HFONT)SelectObject(draw->hDC,g_font);RECT r=draw->rcItem;DrawTextW(draw->hDC,label,-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE);SelectObject(draw->hDC,old);return true;
    }
    if (draw->CtlType == ODT_BUTTON) {
        bool pressed=(draw->itemState&ODS_SELECTED)!=0;
        int controlId=GetDlgCtrlID(draw->hwndItem);
        bool menuButton=controlId==ID_TOP_DATABASE||controlId==ID_SETTINGS||controlId==ID_ABOUT;
        COLORREF bg=g_darkMode?(pressed?RGB(70,73,78):RGB(52,55,60)):(pressed?RGB(215,215,215):RGB(245,245,245));
        if(menuButton)bg=g_darkMode?(pressed?RGB(68,72,78):RGB(31,33,36)):(pressed?RGB(220,232,246):RGB(246,247,249));
        HBRUSH brush=CreateSolidBrush(bg);FillRect(draw->hDC,&draw->rcItem,brush);DeleteObject(brush);
        if(!menuButton){HPEN pen=CreatePen(PS_SOLID,1,g_darkMode?RGB(112,116,122):RGB(150,150,150));
            HGDIOBJ oldPen=SelectObject(draw->hDC,pen);HGDIOBJ oldBrush=SelectObject(draw->hDC,GetStockObject(NULL_BRUSH));
            Rectangle(draw->hDC,draw->rcItem.left,draw->rcItem.top,draw->rcItem.right,draw->rcItem.bottom);
            SelectObject(draw->hDC,oldBrush);SelectObject(draw->hDC,oldPen);DeleteObject(pen);}
        wchar_t text[160]{};GetWindowTextW(draw->hwndItem,text,160);
        SetBkMode(draw->hDC,TRANSPARENT);SetTextColor(draw->hDC,g_darkMode?RGB(240,240,240):RGB(20,20,20));
        HFONT old=(HFONT)SelectObject(draw->hDC,g_font);RECT r=draw->rcItem;
        if(controlId==ID_CODE_GENERATOR||controlId==ID_SHELF_CODE_GENERATOR){constexpr int symbolWidth=16;int mid=(r.top+r.bottom)/2,x=r.left+(r.right-r.left-symbolWidth)/2;for(int i=0;i<18;i+=3){int w=(i%6==0)?2:1;RECT bar{x+i,r.top+6,x+i+w,r.bottom-6};FillRect(draw->hDC,&bar,(HBRUSH)GetStockObject(g_darkMode?WHITE_BRUSH:BLACK_BRUSH));}MoveToEx(draw->hDC,x,mid,nullptr);}
        else DrawTextW(draw->hDC,text,-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE);SelectObject(draw->hDC,old);
        if(draw->itemState&ODS_FOCUS){InflateRect(&r,-3,-3);DrawFocusRect(draw->hDC,&r);}return true;
    }
    if (draw->CtlType == ODT_COMBOBOX) {
        bool selected=(draw->itemState&ODS_SELECTED)!=0;
        COLORREF bg=g_darkMode?(selected?RGB(74,78,84):RGB(45,47,51)):(selected?RGB(220,232,246):RGB(255,255,255));
        HBRUSH brush=CreateSolidBrush(bg);FillRect(draw->hDC,&draw->rcItem,brush);DeleteObject(brush);
        wchar_t text[120]{};
        if(draw->itemID!=(UINT)-1)SendMessageW(draw->hwndItem,CB_GETLBTEXT,draw->itemID,(LPARAM)text);
        else GetWindowTextW(draw->hwndItem,text,120);
        SetBkMode(draw->hDC,TRANSPARENT);SetTextColor(draw->hDC,g_darkMode?RGB(240,240,240):RGB(20,20,20));
        HFONT old=(HFONT)SelectObject(draw->hDC,g_font);RECT r=draw->rcItem;
        DrawTextW(draw->hDC,text,-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE);SelectObject(draw->hDC,old);return true;
    }
    if (draw->CtlType == ODT_MENU) {
        bool selected=(draw->itemState&ODS_SELECTED)!=0;
        COLORREF bg=g_darkMode?(selected?RGB(68,72,78):RGB(31,33,36)):(selected?RGB(220,232,246):RGB(246,247,249));
        HBRUSH brush=CreateSolidBrush(bg);FillRect(draw->hDC,&draw->rcItem,brush);DeleteObject(brush);
        const wchar_t* text=(const wchar_t*)draw->itemData;
        if(!text){HPEN pen=CreatePen(PS_SOLID,1,g_darkMode?RGB(82,86,92):RGB(190,190,190));HGDIOBJ old=SelectObject(draw->hDC,pen);int y=(draw->rcItem.top+draw->rcItem.bottom)/2;MoveToEx(draw->hDC,draw->rcItem.left+8,y,nullptr);LineTo(draw->hDC,draw->rcItem.right-8,y);SelectObject(draw->hDC,old);DeleteObject(pen);return true;}
        SetBkMode(draw->hDC,TRANSPARENT);SetTextColor(draw->hDC,g_darkMode?RGB(240,240,240):RGB(20,20,20));
        HFONT old=(HFONT)SelectObject(draw->hDC,g_font);RECT r=draw->rcItem;r.left+=8;r.right-=8;
        DrawTextW(draw->hDC,text?text:L"",-1,&r,DT_LEFT|DT_VCENTER|DT_SINGLELINE);SelectObject(draw->hDC,old);return true;
    }
    if (draw->CtlType == ODT_HEADER) {
        HBRUSH brush=CreateSolidBrush(g_darkMode?RGB(52,55,60):RGB(246,247,249));FillRect(draw->hDC,&draw->rcItem,brush);DeleteObject(brush);
        wchar_t text[160]{};HDITEMW item{};item.mask=HDI_TEXT;item.pszText=text;item.cchTextMax=160;
        Header_GetItem(draw->hwndItem,(int)draw->itemID,&item);
        SetBkMode(draw->hDC,TRANSPARENT);SetTextColor(draw->hDC,g_darkMode?RGB(245,245,245):RGB(20,20,20));
        HFONT old=(HFONT)SelectObject(draw->hDC,g_font);RECT r=draw->rcItem;r.left+=7;
        DrawTextW(draw->hDC,text,-1,&r,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);SelectObject(draw->hDC,old);
        HPEN pen=CreatePen(PS_SOLID,1,g_darkMode?RGB(90,94,100):RGB(190,190,190));HGDIOBJ oldPen=SelectObject(draw->hDC,pen);
        MoveToEx(draw->hDC,draw->rcItem.right-1,draw->rcItem.top,nullptr);LineTo(draw->hDC,draw->rcItem.right-1,draw->rcItem.bottom);
        MoveToEx(draw->hDC,draw->rcItem.left,draw->rcItem.bottom-1,nullptr);LineTo(draw->hDC,draw->rcItem.right,draw->rcItem.bottom-1);
        SelectObject(draw->hDC,oldPen);DeleteObject(pen);return true;
    }
    return false;
}

bool imageEncoder(const WCHAR* mime, CLSID& clsid) {
    UINT count=0,size=0;Gdiplus::GetImageEncodersSize(&count,&size);if(!size)return false;
    std::vector<BYTE> storage(size);auto codecs=(Gdiplus::ImageCodecInfo*)storage.data();
    if(Gdiplus::GetImageEncoders(count,size,codecs)!=Gdiplus::Ok)return false;
    for(UINT i=0;i<count;i++)if(lstrcmpiW(codecs[i].MimeType,mime)==0){clsid=codecs[i].Clsid;return true;}
    return false;
}

bool normalizeImage(const wchar_t* path, std::vector<unsigned char>& output) {
    Gdiplus::Bitmap source(path,FALSE);
    if(source.GetLastStatus()!=Gdiplus::Ok||!source.GetWidth()||!source.GetHeight())return false;
    Gdiplus::Bitmap canvas(1024,1024,PixelFormat32bppARGB);
    Gdiplus::Graphics graphics(&canvas);graphics.Clear(Gdiplus::Color(0,0,0,0));
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    double scale=std::min(1024.0/source.GetWidth(),1024.0/source.GetHeight());
    int width=std::max(1,(int)(source.GetWidth()*scale));int height=std::max(1,(int)(source.GetHeight()*scale));
    graphics.DrawImage(&source,(1024-width)/2,(1024-height)/2,width,height);
    IStream* stream=nullptr;if(CreateStreamOnHGlobal(nullptr,TRUE,&stream)!=S_OK)return false;
    CLSID encoder{};bool ok=imageEncoder(L"image/png",encoder)&&canvas.Save(stream,&encoder,nullptr)==Gdiplus::Ok;
    if(ok){HGLOBAL memory=nullptr;STATSTG stat{};ok=GetHGlobalFromStream(stream,&memory)==S_OK&&stream->Stat(&stat,STATFLAG_NONAME)==S_OK&&stat.cbSize.HighPart==0;
        SIZE_T size=ok?(SIZE_T)stat.cbSize.LowPart:0;void* bytes=ok?GlobalLock(memory):nullptr;if(bytes&&size)output.assign((BYTE*)bytes,(BYTE*)bytes+size);else ok=false;if(bytes)GlobalUnlock(memory);}
    stream->Release();return ok;
}

LRESULT CALLBACK ImagePreviewProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR){
    if(msg==WM_PAINT){PAINTSTRUCT ps;HDC dc=BeginPaint(hwnd,&ps);RECT r;GetClientRect(hwnd,&r);
        HBRUSH bg=CreateSolidBrush(g_darkMode?RGB(38,40,44):RGB(238,239,241));FillRect(dc,&r,bg);DeleteObject(bg);
        FrameRect(dc,&r,g_darkMode?g_panelBrush:(HBRUSH)GetStockObject(GRAY_BRUSH));
        if(g_currentImage.empty()){SetBkMode(dc,TRANSPARENT);SetTextColor(dc,g_darkMode?RGB(155,158,164):RGB(100,100,100));HFONT old=(HFONT)SelectObject(dc,g_font);DrawTextW(dc,L"Kein Bild",-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE);SelectObject(dc,old);}
        else{HGLOBAL memory=GlobalAlloc(GMEM_MOVEABLE,g_currentImage.size());void* bytes=memory?GlobalLock(memory):nullptr;IStream* stream=nullptr;
            if(bytes){memcpy(bytes,g_currentImage.data(),g_currentImage.size());GlobalUnlock(memory);if(CreateStreamOnHGlobal(memory,TRUE,&stream)!=S_OK){GlobalFree(memory);memory=nullptr;}}
            else if(memory){GlobalFree(memory);memory=nullptr;}
            if(stream){{Gdiplus::Bitmap image(stream,FALSE);if(image.GetLastStatus()==Gdiplus::Ok&&image.GetWidth()&&image.GetHeight()){int pad=5,aw=r.right-r.left-pad*2,ah=r.bottom-r.top-pad*2;double scale=std::min((double)aw/image.GetWidth(),(double)ah/image.GetHeight());int w=(int)(image.GetWidth()*scale),h=(int)(image.GetHeight()*scale);Gdiplus::Graphics graphics(dc);graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);graphics.DrawImage(&image,pad+(aw-w)/2,pad+(ah-h)/2,w,h);}}stream->Release();}}
        EndPaint(hwnd,&ps);return 0;}
    return DefSubclassProc(hwnd,msg,wp,lp);
}

void releaseQrLogo(){
    AcquireSRWLockExclusive(&g_qrLogoCacheLock);g_qrLogoCache.clear();ReleaseSRWLockExclusive(&g_qrLogoCacheLock);
    delete g_qrLogoImage;g_qrLogoImage=nullptr;
    if(g_qrLogoStream){g_qrLogoStream->Release();g_qrLogoStream=nullptr;}
}

bool loadQrLogo(){
    releaseQrLogo();HRSRC resource=FindResourceW(g_instance,MAKEINTRESOURCE(IDR_QR_LOGO),RT_RCDATA);if(!resource)return false;
    HGLOBAL loaded=LoadResource(g_instance,resource);DWORD size=SizeofResource(g_instance,resource);const void* source=loaded?LockResource(loaded):nullptr;if(!source||!size)return false;
    HGLOBAL copy=GlobalAlloc(GMEM_MOVEABLE,size);if(!copy)return false;void* target=GlobalLock(copy);if(!target){GlobalFree(copy);return false;}
    memcpy(target,source,size);GlobalUnlock(copy);if(CreateStreamOnHGlobal(copy,TRUE,&g_qrLogoStream)!=S_OK){GlobalFree(copy);return false;}
    g_qrLogoImage=Gdiplus::Image::FromStream(g_qrLogoStream,FALSE);
    if(!g_qrLogoImage||g_qrLogoImage->GetLastStatus()!=Gdiplus::Ok){releaseQrLogo();return false;}return true;
}

void loadCurrentImage(sqlite3_int64 id){
    g_currentImage.clear();g_imageDirty=false;sqlite3_stmt* st=nullptr;
    if(sqlite3_prepare_v2(g_db,"SELECT image_data FROM items WHERE id=?",-1,&st,nullptr)!=SQLITE_OK){showNotice(g_main,L"Bild laden",utf8ToWide(sqlite3_errmsg(g_db)),true);return;}sqlite3_bind_int64(st,1,id);
    int step=sqlite3_step(st);if(step==SQLITE_ROW&&sqlite3_column_type(st,0)!=SQLITE_NULL){const void* data=sqlite3_column_blob(st,0);int size=sqlite3_column_bytes(st,0);if(data&&size>0)g_currentImage.assign((const BYTE*)data,(const BYTE*)data+size);}
    wstring detail=step==SQLITE_ROW||step==SQLITE_DONE?L"":utf8ToWide(sqlite3_errmsg(g_db));sqlite3_finalize(st);if(!detail.empty())showNotice(g_main,L"Bild laden",detail,true);if(g_main)InvalidateRect(GetDlgItem(g_main,ID_IMAGE_PREVIEW),nullptr,TRUE);
}

void loadPicture(){
    wchar_t path[MAX_PATH]=L"";if(!browseFile(path,MAX_PATH,false,L"Bilder (*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tif;*.tiff)\0*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tif;*.tiff\0Alle Dateien\0*.*\0",L"png"))return;
    std::vector<unsigned char> image;if(!normalizeImage(path,image)){showNotice(g_main,L"Bild laden",L"Das Bild konnte nicht geladen werden.",true);return;}
    g_currentImage.swap(image);g_imageDirty=true;g_formDirty=true;InvalidateRect(GetDlgItem(g_main,ID_IMAGE_PREVIEW),nullptr,TRUE);setText(g_main,ID_STATUS,L"Bild geladen – Eintrag speichern, um es zu übernehmen");
}

void savePicture(){
    if(g_currentImage.empty()){showNotice(g_main,L"Bild speichern",L"Für diesen Eintrag ist kein Bild vorhanden.");return;}
    wchar_t path[MAX_PATH]=L"Artikelbild.png";if(!browseFile(path,MAX_PATH,true,L"PNG-Bild (*.png)\0*.png\0Alle Dateien\0*.*\0",L"png"))return;
    if(g_currentImage.size()>MAXDWORD){showNotice(g_main,L"Bild speichern",L"Das Bild ist zu groß, um gespeichert zu werden.",true);return;}
    wstring target=path;size_t slash=target.find_last_of(L"\\/");wstring directory=slash==wstring::npos?moduleDirectory():(slash==2&&target[1]==L':'?target.substr(0,3):(slash?target.substr(0,slash):L"\\"));
    wchar_t temporary[MAX_PATH]{};if(!GetTempFileNameW(directory.c_str(),L"LGS",0,temporary)){showNotice(g_main,L"Bild speichern",L"Im Zielordner konnte keine temporäre Datei erstellt werden.",true);return;}
    HANDLE file=CreateFileW(temporary,GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);bool ok=file!=INVALID_HANDLE_VALUE;
    if(ok){DWORD written=0,imageSize=(DWORD)g_currentImage.size();ok=WriteFile(file,g_currentImage.data(),imageSize,&written,nullptr)&&written==imageSize;ok=FlushFileBuffers(file)&&ok;ok=CloseHandle(file)&&ok;file=INVALID_HANDLE_VALUE;}
    if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);
    if(ok)ok=MoveFileExW(temporary,path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;
    if(!ok)DeleteFileW(temporary);
    showNotice(g_main,L"Bild speichern",ok?L"Das Bild wurde gespeichert.":L"Das Bild konnte nicht vollständig gespeichert werden.",!ok);
}

void deletePicture(){
    if(g_currentImage.empty())return;if(!confirmAction(L"Bild löschen",L"Das Bild dieses Eintrags wirklich löschen?"))return;
    g_currentImage.clear();g_imageDirty=true;g_formDirty=true;InvalidateRect(GetDlgItem(g_main,ID_IMAGE_PREVIEW),nullptr,TRUE);setText(g_main,ID_STATUS,L"Bild entfernt – Eintrag speichern, um die Änderung zu übernehmen");
}

wstring numberedCode(const wstring& start,int offset,bool down){
    size_t pos=start.size();while(pos>0&&iswdigit(start[pos-1]))--pos;
    wstring prefix=start.substr(0,pos),digits=start.substr(pos);if(digits.empty()){prefix=start+L"-";digits=L"001";}
    long long number=0;try{number=std::stoll(digits);}catch(...){number=1;}
    number+=down?-offset:offset;if(number<0)number=0;
    std::wostringstream out;out<<prefix<<std::setw((int)digits.size())<<std::setfill(L'0')<<number;return out.str();
}

const char* const code128Patterns[]={
"212222","222122","222221","121223","121322","131222","122213","122312","132212","221213","221312","231212","112232","122132","122231","113222","123122","123221","223211","221132","221231","213212","223112","312131","311222","321122","321221","312212","322112","322211","212123","212321","232121","111323","131123","131321","112313","132113","132311","211313","231113","231311","112133","112331","132131","113123","113321","133121","313121","211331","231131","213113","213311","213131","311123","311321","331121","312113","312311","332111","314111","221411","431111","111224","111422","121124","121421","141122","141221","112214","112412","122114","122411","142112","142211","241211","221114","413111","241112","134111","111242","121142","121241","114212","124112","124211","411212","421112","421211","212141","214121","412121","111143","111341","131141","114113","114311","411113","411311","113141","114131","311141","411131","211412","211214","211232","2331112"};

void drawCode128(HDC dc,const RECT& area,const wstring& value,bool maximizeCode){
    string text=wideToUtf8(value);std::vector<int> codes{104};int checksum=104;
    for(size_t i=0;i<text.size();++i){unsigned char ch=(unsigned char)text[i];int code=(ch>=32&&ch<=126)?ch-32:'?'-32;codes.push_back(code);checksum+=code*(int)(i+1);}
    codes.push_back(checksum%103);codes.push_back(106);int modules=20;for(int c:codes)for(const char* p=code128Patterns[c];*p;++p)modules+=*p-'0';
    int areaWidth=(int)(area.right-area.left),areaHeight=(int)(area.bottom-area.top),textH=std::max(16,areaHeight/6),barBottom=area.bottom-textH-3,horizontalInset=maximizeCode?2:std::max(10,areaWidth/10),topInset=maximizeCode?2:std::max(4,areaHeight/10),x=area.left+horizontalInset,usable=std::max(1,areaWidth-horizontalInset*2);
    double unit=(double)usable/modules;double cursor=x+10*unit;HBRUSH black=(HBRUSH)GetStockObject(BLACK_BRUSH);
    for(int c:codes){const char* p=code128Patterns[c];bool bar=true;for(;*p;++p){double next=cursor+(*p-'0')*unit;if(bar){RECT r{(LONG)cursor,area.top+topInset,(LONG)std::ceil(next),barBottom};FillRect(dc,&r,black);}cursor=next;bar=!bar;}}
    RECT tr{area.left,barBottom,area.right,area.bottom};SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(0,0,0));DrawTextW(dc,value.c_str(),-1,&tr,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
}

std::optional<qrcodegen::QrCode> encodeQrSafely(const wstring& value) noexcept{
    try{return qrcodegen::QrCode::encodeText(wideToUtf8(value).c_str(),qrcodegen::QrCode::Ecc::HIGH);}catch(...){return std::nullopt;}
}

struct QrLayout{
    int textHeight=0,outerMargin=0,quietModules=4,reservedCodeSize=0,size=0,modulePixels=0,x=0,y=0;
};

QrLayout calculateQrLayout(int areaWidth,int areaHeight,int qrSize,bool textRight,bool maximizeCode){
    QrLayout layout;
    layout.textHeight=textRight?0:std::max(16,areaHeight/7);
    layout.outerMargin=textRight&&maximizeCode?0:4;
    layout.quietModules=textRight&&maximizeCode?1:4;
    layout.reservedCodeSize=textRight?std::min(std::max(1,areaHeight-layout.outerMargin*2),std::max(1,areaWidth/2-layout.outerMargin-2)):std::min(areaWidth,areaHeight-layout.textHeight);
    int modules=qrSize+layout.quietModules*2;
    layout.size=std::max(1,maximizeCode?layout.reservedCodeSize:layout.reservedCodeSize*3/4);
    layout.modulePixels=std::max(1,layout.size/std::max(1,modules));
    layout.x=textRight?layout.outerMargin+(layout.reservedCodeSize-layout.size)/2:(areaWidth-layout.size)/2;
    layout.y=textRight?layout.outerMargin+(layout.reservedCodeSize-layout.size)/2:(layout.reservedCodeSize-layout.size)/2;
    return layout;
}

std::shared_ptr<const std::vector<BYTE>> qrLogoPixels(int logoSize){
    AcquireSRWLockShared(&g_qrLogoCacheLock);auto cached=g_qrLogoCache.find(logoSize);if(cached!=g_qrLogoCache.end()){auto pixels=cached->second;ReleaseSRWLockShared(&g_qrLogoCacheLock);return pixels;}ReleaseSRWLockShared(&g_qrLogoCacheLock);
    AcquireSRWLockExclusive(&g_qrLogoCacheLock);cached=g_qrLogoCache.find(logoSize);if(cached!=g_qrLogoCache.end()){auto pixels=cached->second;ReleaseSRWLockExclusive(&g_qrLogoCacheLock);return pixels;}
    auto pixels=std::make_shared<std::vector<BYTE>>((size_t)logoSize*(size_t)logoSize,0);
    if(g_qrLogoImage){
        Gdiplus::Bitmap logo(logoSize,logoSize,PixelFormat32bppARGB);{
            Gdiplus::Graphics graphics(&logo);graphics.Clear(Gdiplus::Color(0,255,255,255));graphics.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);graphics.DrawImage(g_qrLogoImage,0,0,logoSize,logoSize);
        }
        std::vector<BYTE> visible((size_t)logoSize*(size_t)logoSize,0);
        for(int py=0;py<logoSize;py++)for(int px=0;px<logoSize;px++){Gdiplus::Color pixel;if(logo.GetPixel(px,py,&pixel)==Gdiplus::Ok&&pixel.GetA()>=128){size_t index=(size_t)py*(size_t)logoSize+(size_t)px;visible[index]=1;int luminance=(299*(int)pixel.GetR()+587*(int)pixel.GetG()+114*(int)pixel.GetB())/1000;(*pixels)[index]=luminance<150?2:1;}}
        for(int py=0;py<logoSize;py++)for(int px=0;px<logoSize;px++)if(!visible[(size_t)py*(size_t)logoSize+(size_t)px]){bool outline=false;for(int oy=-3;oy<=3&&!outline;oy++)for(int ox=-3;ox<=3;ox++){int sx=px+ox,sy=py+oy;if(sx>=0&&sx<logoSize&&sy>=0&&sy<logoSize&&visible[(size_t)sy*(size_t)logoSize+(size_t)sx]){outline=true;break;}}if(outline)(*pixels)[(size_t)py*(size_t)logoSize+(size_t)px]=1;}
    }
    auto immutable=std::shared_ptr<const std::vector<BYTE>>(pixels);if(g_qrLogoCache.size()>=32)g_qrLogoCache.clear();g_qrLogoCache.emplace(logoSize,immutable);ReleaseSRWLockExclusive(&g_qrLogoCacheLock);return immutable;
}

void drawQr(HDC dc,const RECT& area,const wstring& value,bool includeLogo,bool textRight,bool maximizeCode){
    auto encoded=encodeQrSafely(value);if(!encoded){RECT messageArea=area;SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(160,0,0));DrawTextW(dc,L"QR-Inhalt zu lang",-1,&messageArea,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);return;}const auto& qr=*encoded;
    int areaWidth=(int)(area.right-area.left),areaHeight=(int)(area.bottom-area.top);QrLayout layout=calculateQrLayout(areaWidth,areaHeight,qr.getSize(),textRight,maximizeCode);int textH=layout.textHeight,outerMargin=layout.outerMargin,quietModules=layout.quietModules,reservedCodeSize=layout.reservedCodeSize,size=layout.size,modulePixels=layout.modulePixels,x=area.left+layout.x,y=area.top+layout.y,modules=qr.getSize()+quietModules*2;
    HBRUSH white=(HBRUSH)GetStockObject(WHITE_BRUSH),black=(HBRUSH)GetStockObject(BLACK_BRUSH);RECT bg{x,y,x+size,y+size};FillRect(dc,&bg,white);
    auto edge=[&](int module){return (int)std::lround((double)module*size/modules);};
    for(int row=0;row<qr.getSize();++row)for(int col=0;col<qr.getSize();++col)if(qr.getModule(col,row)){RECT r{x+edge(col+quietModules),y+edge(row+quietModules),x+edge(col+quietModules+1),y+edge(row+quietModules+1)};FillRect(dc,&r,black);}
    if(includeLogo&&g_qrLogoImage){
        int logoSize=std::max(modulePixels*5,size*30/100),logoX=x+(size-logoSize)/2,logoY=y+(size-logoSize)/2;
        auto pixels=qrLogoPixels(logoSize);for(int py=0;py<logoSize;py++){int px=0;while(px<logoSize){BYTE color=(*pixels)[(size_t)py*(size_t)logoSize+(size_t)px];if(!color){px++;continue;}int end=px+1;while(end<logoSize&&(*pixels)[(size_t)py*(size_t)logoSize+(size_t)end]==color)end++;RECT run{logoX+px,logoY+py,logoX+end,logoY+py+1};FillRect(dc,&run,color==2?black:white);px=end;}}
    }
    RECT tr=textRight?RECT{area.left+outerMargin+reservedCodeSize+4,area.top+2,area.right-4,area.bottom-2}:RECT{area.left,area.bottom-textH,area.right,area.bottom};SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(0,0,0));HFONT rightFont=nullptr,oldFont=nullptr;if(textRight){int fontHeight=std::max(12,reservedCodeSize*4/5),availableTextWidth=std::max(1,(int)(tr.right-tr.left));for(;;){HFONT candidate=CreateFontW(-fontHeight,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,NONANTIALIASED_QUALITY,DEFAULT_PITCH,L"Arial Narrow");HFONT previous=(HFONT)SelectObject(dc,candidate);SIZE extent{};GetTextExtentPoint32W(dc,value.c_str(),(int)value.size(),&extent);SelectObject(dc,previous);if(extent.cx<=availableTextWidth||fontHeight<=12){rightFont=candidate;break;}DeleteObject(candidate);fontHeight=std::max(12,fontHeight-2);}oldFont=(HFONT)SelectObject(dc,rightFont);}DrawTextW(dc,value.c_str(),-1,&tr,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);if(rightFont){SelectObject(dc,oldFont);DeleteObject(rightFont);}
}

void drawStorageLabel(HDC dc,RECT area,const wstring& value,bool qr,bool includeLogo=false,bool qrTextRight=false,bool maximizeCode=true){
    FillRect(dc,&area,(HBRUSH)GetStockObject(WHITE_BRUSH));FrameRect(dc,&area,(HBRUSH)GetStockObject(LTGRAY_BRUSH));
    HFONT font=CreateFontW(-std::max(12,(int)(area.bottom-area.top)/9),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,NONANTIALIASED_QUALITY,DEFAULT_PITCH,L"Arial");HFONT old=(HFONT)SelectObject(dc,font);
    if(qr)drawQr(dc,area,value,includeLogo,qrTextRight,maximizeCode);else drawCode128(dc,area,value,maximizeCode);SelectObject(dc,old);DeleteObject(font);
}

bool positiveNumber(HWND hwnd,int id,double& value);
bool boundedInteger(wstring text,int minimum,int maximum,int& value);
double selectedTapeWidth(HWND hwnd);
int a4Capacity(double width,double height);

LRESULT CALLBACK CodePreviewProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR){
    if(msg==WM_PAINT){
        PAINTSTRUCT ps;HDC dc=BeginPaint(hwnd,&ps);RECT r;GetClientRect(hwnd,&r);HBRUSH previewBackground=CreateSolidBrush(RGB(92,94,98));FillRect(dc,&r,previewBackground);DeleteObject(previewBackground);InflateRect(&r,-8,-8);
        HWND parent=GetParent(hwnd);wstring code=getText(parent,ID_CODE_START);if(code.empty())code=L"001";bool qr=SendMessageW(GetDlgItem(parent,ID_CODE_KIND),CB_GETCURSEL,0,0)==1;bool includeLogo=qr&&SendMessageW(GetDlgItem(parent,ID_CODE_LOGO),BM_GETCHECK,0,0)==BST_CHECKED;
        if(TabCtrl_GetCurSel(GetDlgItem(parent,ID_CODE_TAB))==1){
            double width=0,height=0;bool dimensionsValid=positiveNumber(parent,ID_CODE_WIDTH,width)&&positiveNumber(parent,ID_CODE_HEIGHT,height);int capacity=dimensionsValid?a4Capacity(width,height):0;
            int availableW=r.right-r.left,availableH=r.bottom-r.top,pageH=availableH,pageW=pageH*210/297;if(pageW>availableW){pageW=availableW;pageH=pageW*297/210;}int pageX=r.left+(availableW-pageW)/2,pageY=r.top+(availableH-pageH)/2;RECT page{pageX,pageY,pageX+pageW,pageY+pageH};FillRect(dc,&page,(HBRUSH)GetStockObject(WHITE_BRUSH));FrameRect(dc,&page,(HBRUSH)GetStockObject(GRAY_BRUSH));
            if(capacity<1){RECT message=page;InflateRect(&message,-12,-12);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(95,95,95));HFONT old=(HFONT)SelectObject(dc,g_font);DrawTextW(dc,dimensionsValid?L"Diese Etikettengröße passt nicht auf DIN A4.":L"Bitte gültige Abmessungen eingeben.",-1,&message,DT_CENTER|DT_VCENTER|DT_WORDBREAK|DT_NOPREFIX);SelectObject(dc,old);}
            else{int cols=(int)std::floor(190.0/width),rows=(int)std::floor(277.0/height),count=1;boundedInteger(getText(parent,ID_CODE_COUNT),1,std::numeric_limits<int>::max(),count);int margin=std::max(2,pageW*5/210),cellW=(pageW-margin*2)/cols,cellH=(pageH-margin*2)/rows,maxLabels=std::min(count,capacity);
                for(int i=0;i<maxLabels;i++){int col=i%cols,row=i/cols;RECT cell{pageX+margin+col*cellW,pageY+margin+row*cellH,pageX+margin+(col+1)*cellW,pageY+margin+(row+1)*cellH};FillRect(dc,&cell,(HBRUSH)GetStockObject(WHITE_BRUSH));FrameRect(dc,&cell,(HBRUSH)GetStockObject(LTGRAY_BRUSH));RECT symbol=cell;InflateRect(&symbol,-std::max(1,cellW/8),-std::max(1,cellH/8));symbol.bottom-=std::max(2,cellH/5);if(qr){int side=std::max(1,(int)std::min(symbol.right-symbol.left,symbol.bottom-symbol.top));symbol.right=symbol.left+side;symbol.bottom=symbol.top+side;FrameRect(dc,&symbol,(HBRUSH)GetStockObject(BLACK_BRUSH));RECT finder{symbol.left+1,symbol.top+1,symbol.left+std::max(2,side/3),symbol.top+std::max(2,side/3)};FillRect(dc,&finder,(HBRUSH)GetStockObject(BLACK_BRUSH));}else{for(int x=symbol.left;x<symbol.right;x+=3){RECT bar{x,symbol.top,std::min(x+1,(int)symbol.right),symbol.bottom};FillRect(dc,&bar,(HBRUSH)GetStockObject(BLACK_BRUSH));}}}
            }
        }else{double labelWidth=50.0,labelHeight=selectedTapeWidth(parent);positiveNumber(parent,ID_CODE_MAX_LENGTH,labelWidth);int availableWidth=r.right-r.left,availableHeight=r.bottom-r.top,labelPixelWidth=availableWidth,labelPixelHeight=std::max(1,(int)std::lround(labelPixelWidth*labelHeight/labelWidth));if(labelPixelHeight>availableHeight){labelPixelHeight=availableHeight;labelPixelWidth=std::max(1,(int)std::lround(labelPixelHeight*labelWidth/labelHeight));}RECT label{r.left+(availableWidth-labelPixelWidth)/2,r.top+(availableHeight-labelPixelHeight)/2,r.left+(availableWidth-labelPixelWidth)/2+labelPixelWidth,r.top+(availableHeight-labelPixelHeight)/2+labelPixelHeight};bool textRight=qr&&SendMessageW(GetDlgItem(parent,ID_CODE_QR_TEXT_POSITION),CB_GETCURSEL,0,0)==1;bool fitTape=SendMessageW(GetDlgItem(parent,ID_CODE_FIT_TAPE),BM_GETCHECK,0,0)==BST_CHECKED;drawStorageLabel(dc,label,code,qr,includeLogo,textRight,fitTape);}
        EndPaint(hwnd,&ps);return 0;
    }
    return DefSubclassProc(hwnd,msg,wp,lp);
}

LRESULT CALLBACK TabBackgroundProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR){
    if(msg==WM_ERASEBKGND&&g_darkMode){RECT area{};GetClientRect(hwnd,&area);FillRect((HDC)wp,&area,g_bgBrush);return 1;}
    LRESULT result=DefSubclassProc(hwnd,msg,wp,lp);
    if(msg==WM_PAINT&&g_darkMode){RECT client{};GetClientRect(hwnd,&client);int count=TabCtrl_GetItemCount(hwnd),right=client.left,bottom=client.top;for(int i=0;i<count;i++){RECT item{};if(TabCtrl_GetItemRect(hwnd,i,&item)){right=std::max(right,(int)item.right);bottom=std::max(bottom,(int)item.bottom);}}HDC dc=GetDC(hwnd);if(right<client.right){RECT remainder{right,client.top,client.right,bottom};FillRect(dc,&remainder,g_bgBrush);}if(bottom<client.bottom){RECT lower{client.left,bottom,client.right,client.bottom};FillRect(dc,&lower,g_bgBrush);}ReleaseDC(hwnd,dc);}
    return result;
}

bool positiveNumber(HWND hwnd,int id,double& value){
    wstring text=getText(hwnd,id);text.erase(text.begin(),std::find_if(text.begin(),text.end(),[](wchar_t c){return !iswspace(c);}));text.erase(std::find_if(text.rbegin(),text.rend(),[](wchar_t c){return !iswspace(c);}).base(),text.end());std::replace(text.begin(),text.end(),L',',L'.');
    size_t consumed=0;try{value=std::stod(text,&consumed);}catch(...){return false;}
    return consumed==text.size()&&std::isfinite(value)&&value>=5.0&&value<=287.0;
}

bool boundedInteger(wstring text,int minimum,int maximum,int& value){
    text.erase(text.begin(),std::find_if(text.begin(),text.end(),[](wchar_t c){return !iswspace(c);}));text.erase(std::find_if(text.rbegin(),text.rend(),[](wchar_t c){return !iswspace(c);}).base(),text.end());
    if(text.empty())return false;size_t consumed=0;long long parsed=0;try{parsed=std::stoll(text,&consumed,10);}catch(...){return false;}
    if(consumed!=text.size()||parsed<minimum||parsed>maximum)return false;value=(int)parsed;return true;
}

double selectedTapeWidth(HWND hwnd){
    const double widths[]={6.0,9.0,12.0,24.0};
    int selection=(int)SendMessageW(GetDlgItem(hwnd,ID_CODE_TAPE_WIDTH),CB_GETCURSEL,0,0);
    return selection>=0&&selection<4?widths[selection]:12.0;
}

int a4Capacity(double width,double height){
    double columns=std::floor(190.0/width),rows=std::floor(277.0/height);
    if(columns<1||rows<1||columns>(double)std::numeric_limits<int>::max()/rows)return 0;
    return (int)(columns*rows);
}

bool code128Compatible(const wstring& value){return std::all_of(value.begin(),value.end(),[](wchar_t c){return c>=32&&c<=126;});}
int code128ModuleCount(const wstring& value){return 55+(int)value.size()*11;}

void updateCodeFit(HWND hwnd){
    bool a4=TabCtrl_GetCurSel(GetDlgItem(hwnd,ID_CODE_TAB))==1;double width=0,height=0;if(!positiveNumber(hwnd,ID_CODE_WIDTH,width)||!positiveNumber(hwnd,ID_CODE_HEIGHT,height)){setText(hwnd,ID_CODE_FIT,L"DIN A4: Abmessungen von 5 bis 287 mm eingeben");EnableWindow(GetDlgItem(hwnd,ID_CODE_PRINT),a4?FALSE:TRUE);return;}
    int capacity=a4Capacity(width,height);
    if(capacity<1){setText(hwnd,ID_CODE_FIT,L"DIN A4: Diese Etikettengröße passt nicht auf die Seite");EnableWindow(GetDlgItem(hwnd,ID_CODE_PRINT),a4?FALSE:TRUE);return;}
    int cols=(int)std::floor(190.0/width),rows=(int)std::floor(277.0/height);
    setText(hwnd,ID_CODE_FIT,L"DIN A4: "+std::to_wstring(cols)+L" × "+std::to_wstring(rows)+L" = "+std::to_wstring(capacity)+L" Etiketten pro Seite");
    if(a4){wstring current=getText(hwnd,ID_CODE_COUNT),maximum=std::to_wstring(capacity);int requested=0;bool valid=boundedInteger(current,1,std::numeric_limits<int>::max(),requested),automatic=SendMessageW(GetDlgItem(hwnd,ID_CODE_AUTOSCALE),BM_GETCHECK,0,0)==BST_CHECKED;if(automatic||(valid&&requested>capacity)){if(current!=maximum)setText(hwnd,ID_CODE_COUNT,maximum);}else if(!valid){setText(hwnd,ID_CODE_FIT,L"DIN A4: Bitte eine gültige Anzahl eingeben");EnableWindow(GetDlgItem(hwnd,ID_CODE_PRINT),FALSE);return;}}
    EnableWindow(GetDlgItem(hwnd,ID_CODE_PRINT),TRUE);
}

struct PrintJob{HDC dc;HWND notify;wstring start,outputPath;int count;bool down,qr,a4,autoScale,includeLogo,fitTapeWidth,qrTextRight;double widthMm,heightMm;};
struct PtouchJob{HWND notify;wstring windowsImagePath,unixImagePath;};

wstring readSmallFile(const wstring& path){
    HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);if(file==INVALID_HANDLE_VALUE)return L"";char buffer[64]{};DWORD read=0;BOOL ok=ReadFile(file,buffer,sizeof(buffer)-1,&read,nullptr);CloseHandle(file);if(!ok)return L"";wstring value=utf8ToWide(buffer);while(!value.empty()&&iswspace(value.back()))value.pop_back();std::transform(value.begin(),value.end(),value.begin(),[](wchar_t c){return (wchar_t)towlower(c);});return value;
}

bool brotherUsbConnected(){
    WIN32_FIND_DATAW data{};HANDLE search=FindFirstFileW(L"Z:\\sys\\bus\\usb\\devices\\*",&data);if(search==INVALID_HANDLE_VALUE)return false;bool found=false;do{if(wcscmp(data.cFileName,L".")==0||wcscmp(data.cFileName,L"..")==0)continue;wstring base=L"Z:\\sys\\bus\\usb\\devices\\"+wstring(data.cFileName);if(readSmallFile(base+L"\\idVendor")!=L"04f9")continue;wstring product=readSmallFile(base+L"\\product");if(product.find(L"p-touch")!=wstring::npos||product.find(L"ptouch")!=wstring::npos||product.find(L"pt-")!=wstring::npos||product.find(L"ql-")!=wstring::npos||product.find(L"label")!=wstring::npos){found=true;break;}}while(FindNextFileW(search,&data));FindClose(search);return found;
}

bool createPtouchImage(HWND hwnd,const wstring& value,bool qr,bool includeLogo,bool qrTextRight,bool fitTapeWidth,double tapeWidth,double maxLength,wstring& windowsPath,wstring& unixPath){
    unsigned long processId=GetCurrentProcessId();unsigned long long tick=GetTickCount64();wstring fileName=L"logs-ptouch-"+std::to_wstring(processId)+L"-"+std::to_wstring(tick)+L".png";windowsPath=L"Z:\\tmp\\"+fileName;unixPath=L"/tmp/"+fileName;
    int height=tapeWidth<=6.0?32:tapeWidth<=9.0?52:tapeWidth<=12.0?76:tapeWidth<=18.0?112:128,width=std::max(1,(int)std::lround(maxLength*180.0/25.4));
    auto encodedQr=qr?encodeQrSafely(value):std::nullopt;if(qr&&!encodedQr){showNotice(hwnd,L"QR-Code",L"Der Inhalt ist zu lang für einen QR-Code. Bitte kürzen Sie den Wert.",true);return false;}
    bool tooSmall=false;if(qr){QrLayout layout=calculateQrLayout(width,height,encodedQr->getSize(),qrTextRight,fitTapeWidth);tooSmall=layout.reservedCodeSize<=0||layout.modulePixels<2;if(qrTextRight){int textWidth=width-(layout.outerMargin+layout.reservedCodeSize+8);tooSmall=tooSmall||textWidth<12+(int)value.size()*6;}}else tooSmall=(width-(fitTapeWidth?4:std::max(20,width/5)))/std::max(1,code128ModuleCount(value))<1;
    if(tooSmall){showNotice(hwnd,L"Etikettendrucker",L"Der Code passt nicht lesbar auf die eingestellte Etikettenlänge. Bitte wählen Sie eine größere Etikettenlänge oder eine andere Anordnung.",true);return false;}
    Gdiplus::Bitmap bitmap(width,height,PixelFormat32bppARGB);if(bitmap.GetLastStatus()!=Gdiplus::Ok)return false;Gdiplus::Graphics graphics(&bitmap);graphics.Clear(Gdiplus::Color(255,255,255,255));HDC dc=graphics.GetHDC();if(!dc)return false;RECT label{0,0,width,height};drawStorageLabel(dc,label,value,qr,includeLogo,qrTextRight,fitTapeWidth);graphics.ReleaseHDC(dc);CLSID encoder{};if(!imageEncoder(L"image/png",encoder)||bitmap.Save(windowsPath.c_str(),&encoder,nullptr)!=Gdiplus::Ok){DeleteFileW(windowsPath.c_str());showNotice(hwnd,L"Etikettendrucker",L"Das temporäre Druckbild konnte nicht erzeugt werden.",true);return false;}return true;
}

DWORD WINAPI ptouchWorker(LPVOID parameter){
    PtouchJob* job=(PtouchJob*)parameter;bool ok=false;PROCESS_INFORMATION process{};wstring markerWindows;
    try{wstring markerUnix=job->unixImagePath+L".ok";markerWindows=job->windowsImagePath+L".ok";DeleteFileW(markerWindows.c_str());
        wchar_t systemDirectory[MAX_PATH]{};UINT systemLength=GetSystemDirectoryW(systemDirectory,MAX_PATH);wstring startPath=systemLength&&systemLength<MAX_PATH?wstring(systemDirectory)+L"\\start.exe":L"";
        wstring command=L"\""+startPath+L"\" /wait /unix /usr/bin/timeout --signal=TERM --kill-after=5s 45s /bin/sh -c \"/usr/bin/ptouch-print --image="+job->unixImagePath+L" --pad=0 --precut --timeout=20 && /usr/bin/touch "+markerUnix+L"\"";std::vector<wchar_t> writable(command.begin(),command.end());writable.push_back(L'\0');STARTUPINFOW startup{};startup.cb=sizeof(startup);
        ok=!startPath.empty()&&brotherUsbConnected()&&CreateProcessW(startPath.c_str(),writable.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process)!=FALSE;
        if(ok){DWORD waitResult=WaitForSingleObject(process.hProcess,60000);if(waitResult==WAIT_OBJECT_0)ok=GetFileAttributesW(markerWindows.c_str())!=INVALID_FILE_ATTRIBUTES;else{TerminateProcess(process.hProcess,ERROR_TIMEOUT);WaitForSingleObject(process.hProcess,2000);ok=false;}}
    }catch(...){ok=false;}
    if(process.hThread)CloseHandle(process.hThread);if(process.hProcess)CloseHandle(process.hProcess);if(!markerWindows.empty())DeleteFileW(markerWindows.c_str());DeleteFileW(job->windowsImagePath.c_str());InterlockedExchange(&g_printing,0);PostMessageW(job->notify,WM_APP+10,ok,0);delete job;return 0;
}

bool printWithPtouch(HWND hwnd,const wstring& value,bool qr,bool includeLogo,bool qrTextRight,bool fitTapeWidth,double tapeWidth,double maxLength){
    wstring windowsPath,unixPath;if(!createPtouchImage(hwnd,value,qr,includeLogo,qrTextRight,fitTapeWidth,tapeWidth,maxLength,windowsPath,unixPath)){InterlockedExchange(&g_printing,0);return false;}auto job=new PtouchJob{g_main,windowsPath,unixPath};HANDLE thread=CreateThread(nullptr,0,ptouchWorker,job,0,nullptr);if(thread){CloseHandle(thread);return true;}DeleteFileW(windowsPath.c_str());delete job;InterlockedExchange(&g_printing,0);showNotice(hwnd,L"Etikettendrucker",L"Der ptouch-Druckauftrag konnte nicht gestartet werden.",true);return false;
}

wstring findBrotherPrinter(){
    DWORD needed=0,count=0;const DWORD flags=PRINTER_ENUM_LOCAL|PRINTER_ENUM_CONNECTIONS;
    EnumPrintersW(flags,nullptr,4,nullptr,0,&needed,&count);if(!needed)return L"";
    std::vector<BYTE> buffer(needed);if(!EnumPrintersW(flags,nullptr,4,buffer.data(),needed,&needed,&count))return L"";
    PRINTER_INFO_4W* printers=(PRINTER_INFO_4W*)buffer.data();wstring firstLabelPrinter;
    for(DWORD i=0;i<count;i++){if(!printers[i].pPrinterName)continue;wstring name=printers[i].pPrinterName,lower=name;std::transform(lower.begin(),lower.end(),lower.begin(),[](wchar_t c){return (wchar_t)towlower(c);});if(lower.find(L"brother")==wstring::npos)continue;bool labelPrinter=lower.find(L"p-touch")!=wstring::npos||lower.find(L"ptouch")!=wstring::npos||lower.find(L"pt-")!=wstring::npos||lower.find(L"ql-")!=wstring::npos||lower.find(L"label")!=wstring::npos;if(!labelPrinter)continue;if(lower.find(L"pt-1950")!=wstring::npos||lower.find(L"pt1950")!=wstring::npos)return name;if(firstLabelPrinter.empty())firstLabelPrinter=name;}
    return firstLabelPrinter;
}

bool brotherPrinterConnected(){
    return g_runningUnderWine?brotherUsbConnected():!findBrotherPrinter().empty();
}

DWORD WINAPI printWorker(LPVOID parameter){
    PrintJob* job=(PrintJob*)parameter;HDC dc=job->dc;bool ok=true,documentStarted=false;
    try{
        DOCINFOW info{};info.cbSize=sizeof(info);info.lpszDocName=L"LogS Lageretiketten";if(!job->outputPath.empty())info.lpszOutput=job->outputPath.c_str();documentStarted=StartDocW(dc,&info)>0;ok=documentStarted;
        if(ok){int dpiX=GetDeviceCaps(dc,LOGPIXELSX),dpiY=GetDeviceCaps(dc,LOGPIXELSY),pageW=GetDeviceCaps(dc,HORZRES),pageH=GetDeviceCaps(dc,VERTRES);
            int labelW=std::max(1,(int)(job->widthMm*dpiX/25.4)),labelH=std::max(1,(int)(job->heightMm*dpiY/25.4));if(!job->a4&&job->fitTapeWidth)labelH=pageH;
            if(!job->a4){if(StartPage(dc)<=0)ok=false;else{RECT r{0,0,std::min(labelW,pageW),std::min(labelH,pageH)};drawStorageLabel(dc,r,job->start,job->qr,job->includeLogo,job->qrTextRight,job->fitTapeWidth);if(EndPage(dc)<=0)ok=false;}}
            else{int marginX=std::max(1,(int)(5.0*dpiX/25.4)),marginY=std::max(1,(int)(5.0*dpiY/25.4)),contentW=pageW-marginX*2,contentH=pageH-marginY*2,cols=contentW>0?contentW/labelW:0,rows=contentH>0?contentH/labelH:0;
                if(cols<1||rows<1)ok=false;else{int perPage=cols*rows;if(job->autoScale){labelW=contentW/cols;labelH=contentH/rows;}
                    for(int base=0;base<job->count&&ok;base+=perPage){if(StartPage(dc)<=0){ok=false;break;}for(int n=0;n<perPage&&base+n<job->count;n++){int col=n%cols,row=n/cols;RECT r{marginX+col*labelW,marginY+row*labelH,std::min(marginX+(col+1)*labelW,pageW-marginX),std::min(marginY+(row+1)*labelH,pageH-marginY)};drawStorageLabel(dc,r,numberedCode(job->start,base+n,job->down),job->qr,job->includeLogo,job->qrTextRight);}if(EndPage(dc)<=0)ok=false;}}
            }
        }
    }catch(...){ok=false;}
    if(documentStarted){if(ok&&EndDoc(dc)<=0)ok=false;if(!ok)AbortDoc(dc);}
    DeleteDC(dc);InterlockedExchange(&g_printing,0);PostMessageW(job->notify,WM_APP+10,ok,0);delete job;return 0;
}

void printCodes(HWND hwnd){
    if(InterlockedCompareExchange(&g_printing,1,0)!=0){showNotice(hwnd,L"Drucken",L"Es läuft bereits ein Druckauftrag.");return;}
    bool a4=TabCtrl_GetCurSel(GetDlgItem(hwnd,ID_CODE_TAB))==1;double widthMm=0,heightMm=0;int capacity=1;
    if(a4){if(!positiveNumber(hwnd,ID_CODE_WIDTH,widthMm)||!positiveNumber(hwnd,ID_CODE_HEIGHT,heightMm)){InterlockedExchange(&g_printing,0);showNotice(hwnd,L"Drucken",L"Breite und Höhe müssen zwischen 5 und 287 mm liegen.");return;}capacity=a4Capacity(widthMm,heightMm);if(capacity<1){InterlockedExchange(&g_printing,0);showNotice(hwnd,L"Drucken",L"Die gewählte Etikettengröße passt nicht auf eine DIN-A4-Seite.",true);return;}}
    else{heightMm=selectedTapeWidth(hwnd);if(!positiveNumber(hwnd,ID_CODE_MAX_LENGTH,widthMm)){InterlockedExchange(&g_printing,0);showNotice(hwnd,L"Etikettendrucker",L"Die Etikettenlänge muss zwischen 5 und 287 mm liegen.",true);return;}}
    int count=1;if(a4&&!boundedInteger(getText(hwnd,ID_CODE_COUNT),1,std::numeric_limits<int>::max(),count)){InterlockedExchange(&g_printing,0);showNotice(hwnd,L"Drucken",L"Bitte geben Sie eine gültige Anzahl ein.",true);return;}if(a4)count=std::min(count,capacity);wstring start=getText(hwnd,ID_CODE_START);if(start.empty())start=L"001";
    bool down=a4&&SendMessageW(GetDlgItem(hwnd,ID_CODE_DIRECTION),CB_GETCURSEL,0,0)==1,qr=SendMessageW(GetDlgItem(hwnd,ID_CODE_KIND),CB_GETCURSEL,0,0)==1;
    if(down){size_t pos=start.size();while(pos>0&&iswdigit(start[pos-1]))--pos;if(pos<start.size())try{long long available=std::stoll(start.substr(pos))+1;if(available<1)available=1;if(available<count)count=(int)available;}catch(...){count=1;}}
    wstring firstPrinted=a4?numberedCode(start,0,down):start,lastPrinted=a4?numberedCode(start,count-1,down):start;
    auto firstQr=qr?encodeQrSafely(firstPrinted):std::nullopt,lastQr=qr?encodeQrSafely(lastPrinted):std::nullopt;if(qr&&(!firstQr||!lastQr)){InterlockedExchange(&g_printing,0);showNotice(hwnd,L"QR-Code",L"Mindestens eine erzeugte Nummer ist zu lang für einen QR-Code. Bitte kürzen Sie den Startwert.",true);return;}
    if(!qr&&!code128Compatible(start)){InterlockedExchange(&g_printing,0);showNotice(hwnd,L"Code 128",L"Code 128 unterstützt hier nur druckbare ASCII-Zeichen. Bitte verwenden Sie für Sonderzeichen einen QR-Code.");return;}
    bool brotherAutomatic=!a4&&SendMessageW(GetDlgItem(hwnd,ID_CODE_PRINTER),CB_GETCURSEL,0,0)==0;
    bool usePtouch=brotherAutomatic&&g_runningUnderWine;
    if(usePtouch){bool includeLogo=qr&&SendMessageW(GetDlgItem(hwnd,ID_CODE_LOGO),BM_GETCHECK,0,0)==BST_CHECKED;bool qrTextRight=qr&&SendMessageW(GetDlgItem(hwnd,ID_CODE_QR_TEXT_POSITION),CB_GETCURSEL,0,0)==1;bool fitTapeWidth=SendMessageW(GetDlgItem(hwnd,ID_CODE_FIT_TAPE),BM_GETCHECK,0,0)==BST_CHECKED;printWithPtouch(hwnd,start,qr,includeLogo,qrTextRight,fitTapeWidth,heightMm,widthMm);return;}
    PRINTDLGW pd{};pd.lStructSize=sizeof(pd);wstring automaticallySelectedPrinter;
    if(brotherAutomatic){automaticallySelectedPrinter=findBrotherPrinter();if(automaticallySelectedPrinter.empty()){InterlockedExchange(&g_printing,0);showNotice(hwnd,L"Etikettendrucker",L"Es wurde kein installierter Brother-Drucker gefunden. Installieren Sie unter Windows den passenden Brother-Druckertreiber.",true);return;}pd.hDC=CreateDCW(L"WINSPOOL",automaticallySelectedPrinter.c_str(),nullptr,nullptr);if(!pd.hDC){InterlockedExchange(&g_printing,0);showNotice(hwnd,L"Etikettendrucker",L"Der Brother-Drucker konnte nicht geöffnet werden.",true);return;}}
    else{pd.hwndOwner=hwnd;pd.Flags=PD_RETURNDC|PD_USEDEVMODECOPIESANDCOLLATE|PD_NOPAGENUMS|PD_NOSELECTION;if(!PrintDlgW(&pd)){InterlockedExchange(&g_printing,0);return;}}
    if(a4&&pd.hDevMode){DEVMODEW* mode=(DEVMODEW*)GlobalLock(pd.hDevMode);if(mode){mode->dmFields|=DM_PAPERSIZE;mode->dmPaperSize=DMPAPER_A4;HDC reset=ResetDCW(pd.hDC,mode);if(reset)pd.hDC=reset;GlobalUnlock(pd.hDevMode);}}
    bool fitTapeWidth=!a4&&SendMessageW(GetDlgItem(hwnd,ID_CODE_FIT_TAPE),BM_GETCHECK,0,0)==BST_CHECKED;
    bool qrTextRight=qr&&SendMessageW(GetDlgItem(hwnd,ID_CODE_QR_TEXT_POSITION),CB_GETCURSEL,0,0)==1;
    {int dpiX=GetDeviceCaps(pd.hDC,LOGPIXELSX),dpiY=GetDeviceCaps(pd.hDC,LOGPIXELSY),pageW=GetDeviceCaps(pd.hDC,HORZRES),pageH=GetDeviceCaps(pd.hDC,VERTRES);int labelW=std::min(pageW,std::max(1,(int)(widthMm*dpiX/25.4))),labelH=std::min(pageH,std::max(1,(int)(heightMm*dpiY/25.4)));if(fitTapeWidth)labelH=pageH;bool maximizeCode=a4||fitTapeWidth,tooSmall=false;if(qr){QrLayout firstLayout=calculateQrLayout(labelW,labelH,firstQr->getSize(),qrTextRight,maximizeCode),lastLayout=calculateQrLayout(labelW,labelH,lastQr->getSize(),qrTextRight,maximizeCode);tooSmall=firstLayout.reservedCodeSize<=0||lastLayout.reservedCodeSize<=0||std::min(firstLayout.modulePixels,lastLayout.modulePixels)<2;}else tooSmall=(labelW-(maximizeCode?4:std::max(20,labelW/5)))/std::max(1,code128ModuleCount(start))<1;if(tooSmall){if(pd.hDevMode)GlobalFree(pd.hDevMode);if(pd.hDevNames)GlobalFree(pd.hDevNames);DeleteDC(pd.hDC);InterlockedExchange(&g_printing,0);showNotice(hwnd,L"Drucken",L"Der gewählte Drucker oder die Etikettengröße bietet nicht genügend Auflösung für einen zuverlässig lesbaren Code.",true);return;}}
    wstring printerName=automaticallySelectedPrinter,outputPath;if(printerName.empty()&&pd.hDevNames){DEVNAMES* names=(DEVNAMES*)GlobalLock(pd.hDevNames);if(names){printerName=(wchar_t*)names+names->wDeviceOffset;GlobalUnlock(pd.hDevNames);}}
    wstring printerLower=printerName;std::transform(printerLower.begin(),printerLower.end(),printerLower.begin(),[](wchar_t c){return (wchar_t)towlower(c);});
    if(printerLower.find(L"microsoft print to pdf")!=wstring::npos){wchar_t pdfPath[MAX_PATH]=L"LogS-Etiketten.pdf";if(!browseFile(pdfPath,MAX_PATH,true,L"PDF-Datei (*.pdf)\0*.pdf\0Alle Dateien\0*.*\0",L"pdf",hwnd)){if(pd.hDevMode)GlobalFree(pd.hDevMode);if(pd.hDevNames)GlobalFree(pd.hDevNames);DeleteDC(pd.hDC);InterlockedExchange(&g_printing,0);return;}outputPath=pdfPath;}
    if(a4){int dpiX=GetDeviceCaps(pd.hDC,LOGPIXELSX),dpiY=GetDeviceCaps(pd.hDC,LOGPIXELSY),pageW=GetDeviceCaps(pd.hDC,HORZRES),pageH=GetDeviceCaps(pd.hDC,VERTRES);int lw=std::max(1,(int)(widthMm*dpiX/25.4)),lh=std::max(1,(int)(heightMm*dpiY/25.4));int mx=std::max(1,(int)(5.0*dpiX/25.4)),my=std::max(1,(int)(5.0*dpiY/25.4)),columns=(pageW-mx*2)/lw,rows=(pageH-my*2)/lh;if(columns<1||rows<1){if(pd.hDevMode)GlobalFree(pd.hDevMode);if(pd.hDevNames)GlobalFree(pd.hDevNames);DeleteDC(pd.hDC);InterlockedExchange(&g_printing,0);showNotice(hwnd,L"Drucken",L"Auf dem druckbaren Bereich dieser DIN-A4-Seite passt kein Etikett mit den gewählten Abmessungen.",true);return;}int actual=columns*rows;count=std::min(count,actual);setText(hwnd,ID_CODE_COUNT,std::to_wstring(count));}
    bool includeLogo=qr&&SendMessageW(GetDlgItem(hwnd,ID_CODE_LOGO),BM_GETCHECK,0,0)==BST_CHECKED;
    auto job=new PrintJob{pd.hDC,g_main,start,outputPath,count,down,qr,a4,a4&&SendMessageW(GetDlgItem(hwnd,ID_CODE_AUTOSCALE),BM_GETCHECK,0,0)==BST_CHECKED,includeLogo,fitTapeWidth,qrTextRight,widthMm,heightMm};
    if(pd.hDevMode)GlobalFree(pd.hDevMode);if(pd.hDevNames)GlobalFree(pd.hDevNames);HANDLE thread=CreateThread(nullptr,0,printWorker,job,0,nullptr);if(thread)CloseHandle(thread);else{DeleteDC(pd.hDC);delete job;InterlockedExchange(&g_printing,0);showNotice(hwnd,L"Drucken",L"Der Druckauftrag konnte nicht gestartet werden.",true);}
}

void setRect(HWND parent, int id, int x, int y, int w, int h) {
    MoveWindow(GetDlgItem(parent, id), x, y, w, h, TRUE);
}

SIZE minimumWindowSize(HWND hwnd);

LRESULT CALLBACK ResizeGripProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                UINT_PTR, DWORD_PTR) {
    switch (msg) {
    case WM_SETCURSOR:
        SetCursor(LoadCursorW(nullptr, IDC_SIZENWSE));
        return TRUE;
    case WM_LBUTTONDOWN:
        g_gripDragging = true;
        GetCursorPos(&g_gripStart);
        GetWindowRect(g_main, &g_gripWindowStart);
        SetCapture(hwnd);
        return 0;
    case WM_MOUSEMOVE:
        if (g_gripDragging && GetCapture() == hwnd) {
            POINT cursor; GetCursorPos(&cursor);
            SIZE minimum = minimumWindowSize(g_main);
            int requestedWidth = (int)((g_gripWindowStart.right - g_gripWindowStart.left)
                                       + cursor.x - g_gripStart.x);
            int requestedHeight = (int)((g_gripWindowStart.bottom - g_gripWindowStart.top)
                                        + cursor.y - g_gripStart.y);
            int width = std::max(requestedWidth, (int)minimum.cx);
            int height = std::max(requestedHeight, (int)minimum.cy);
            SetWindowPos(g_main, nullptr, 0, 0, width, height,
                         SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
        return 0;
    case WM_LBUTTONUP:
    case WM_CAPTURECHANGED:
        if (g_gripDragging) {
            g_gripDragging = false;
            if (GetCapture() == hwnd) ReleaseCapture();
        }
        return 0;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

void addLabel(HWND parent, const wchar_t* text, int id = 0) {
    addControl(L"STATIC", text, SS_LEFT, id, parent);
}

void clearForm() {
    g_loadingForm=true;
    g_currentId = 0;
    g_originalStorageNo.clear();
    g_currentImage.clear();g_imageDirty=false;
    for (int id : {ID_NAME, ID_STORAGE_NO, ID_BARCODE, ID_LOCATION, ID_SHELF, ID_PRICE, ID_CHANGED, ID_NOTES}) setText(g_main, id, L"");
    setText(g_main, ID_AMOUNT, L"0");
    setText(g_main, ID_PRICE, L"-");
    setPriceAlignment(false);
    selectCombo(GetDlgItem(g_main, ID_UNIT), setting("default_unit", L"Stück"));
    setText(g_main, ID_STATUS, L"Neuer Eintrag");
    InvalidateRect(GetDlgItem(g_main,ID_IMAGE_PREVIEW),nullptr,TRUE);
    SetFocus(GetDlgItem(g_main, ID_NAME));
    g_loadingForm=false;g_formDirty=false;
}

bool findNextFreeStorageNumber(wstring& result) {
    result.clear();
    std::vector<bool> used(10001, false);
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(g_db, "SELECT storage_no FROM items WHERE storage_no IS NOT NULL AND storage_no<>''", -1, &st, nullptr) != SQLITE_OK)
        return false;

    int stepResult=SQLITE_ROW;
    while ((stepResult=sqlite3_step(st)) == SQLITE_ROW) {
        const unsigned char* text = sqlite3_column_text(st, 0);
        if (!text) continue;
        wstring number = utf8ToWide(reinterpret_cast<const char*>(text));
        if (number.size() < 3 || towupper(number[0]) != L'L' || number[1] != L'-') continue;

        unsigned int value = 0;
        bool valid = true;
        for (size_t i = 2; i < number.size(); ++i) {
            if (!iswdigit(number[i])) { valid = false; break; }
            value = value * 10 + static_cast<unsigned int>(number[i] - L'0');
            if (value > 10000) { valid = false; break; }
        }
        if (valid && value >= 1) used[value] = true;
    }
    sqlite3_finalize(st);
    if(stepResult!=SQLITE_DONE)return false;

    for (unsigned int value = 1; value <= 10000; ++value) {
        if (!used[value]) {
            std::wostringstream formatted;
            formatted << L"L-" << std::setw(6) << std::setfill(L'0') << value;
            result = formatted.str();
            return true;
        }
    }
    return true;
}

void newEntry() {
    clearForm();
    wstring storageNumber;
    if (!findNextFreeStorageNumber(storageNumber)) {
        showNotice(g_main, L"Neuer Eintrag", L"Die nächste freie Lagernummer konnte nicht ermittelt werden.", true);
        return;
    }
    if (storageNumber.empty()) {
        showNotice(g_main, L"Neuer Eintrag", L"Alle Lagernummern von L-000001 bis L-010000 sind bereits vergeben.");
        return;
    }
    g_loadingForm = true;
    setText(g_main, ID_STORAGE_NO, storageNumber);
    g_loadingForm = false;
    g_formDirty = false;
    SetFocus(GetDlgItem(g_main, ID_NAME));
}

void assignNextFreeStorageNumber(){
    if(!getText(g_main,ID_STORAGE_NO).empty()){showNotice(g_main,L"Lagernummer",L"Das Feld \"Lagernummer\" ist bereits ausgefüllt.");return;}
    wstring number;if(!findNextFreeStorageNumber(number)){showNotice(g_main,L"Lagernummer",L"Die nächste freie Lagernummer konnte nicht ermittelt werden.",true);return;}if(number.empty()){showNotice(g_main,L"Lagernummer",L"Alle Lagernummern von L-000001 bis L-010000 sind bereits vergeben.");return;}setText(g_main,ID_STORAGE_NO,number);SetFocus(GetDlgItem(g_main,ID_STORAGE_NO));
}

bool findNextFreeShelfNumber(wstring& result){
    result.clear();std::vector<bool> used(10001,false);sqlite3_stmt* st=nullptr;
    if(sqlite3_prepare_v2(g_db,"SELECT shelf FROM items WHERE shelf IS NOT NULL AND shelf<>''",-1,&st,nullptr)!=SQLITE_OK)return false;
    int stepResult=SQLITE_ROW;while((stepResult=sqlite3_step(st))==SQLITE_ROW){const char* text=(const char*)sqlite3_column_text(st,0);if(!text)continue;wstring number=utf8ToWide(text);if(number.size()<3||towupper(number[0])!=L'F'||number[1]!=L'-')continue;unsigned int value=0;bool valid=true;for(size_t i=2;i<number.size();++i){if(!iswdigit(number[i])){valid=false;break;}value=value*10+(unsigned int)(number[i]-L'0');if(value>10000){valid=false;break;}}if(valid&&value>=1)used[value]=true;}
    sqlite3_finalize(st);if(stepResult!=SQLITE_DONE)return false;for(unsigned int value=1;value<=10000;++value)if(!used[value]){std::wostringstream formatted;formatted<<L"F-"<<std::setw(6)<<std::setfill(L'0')<<value;result=formatted.str();return true;}return true;
}

void assignNextFreeShelfNumber(){
    if(!getText(g_main,ID_SHELF).empty()){showNotice(g_main,L"Fachnummer",L"Das Feld \"Fach\" ist bereits ausgefüllt.");return;}
    wstring number;if(!findNextFreeShelfNumber(number)){showNotice(g_main,L"Fachnummer",L"Die nächste freie Fachnummer konnte nicht ermittelt werden.",true);return;}if(number.empty()){showNotice(g_main,L"Fachnummer",L"Alle Fachnummern von F-000001 bis F-010000 sind bereits vergeben.");return;}setText(g_main,ID_SHELF,number);SetFocus(GetDlgItem(g_main,ID_SHELF));
}

int readItem(sqlite3_stmt* st, Item& item) {
    int step=sqlite3_step(st);if(step!=SQLITE_ROW)return step;
    item.id = sqlite3_column_int64(st, 0);
    item.name = utf8ToWide((const char*)sqlite3_column_text(st, 1));
    item.storageNo = utf8ToWide((const char*)sqlite3_column_text(st, 2));
    item.barcode = utf8ToWide((const char*)sqlite3_column_text(st, 3));
    item.location = utf8ToWide((const char*)sqlite3_column_text(st, 4));
    item.amount = sqlite3_column_int(st, 5);
    item.unit = utf8ToWide((const char*)sqlite3_column_text(st, 6));
    item.shelf = utf8ToWide((const char*)sqlite3_column_text(st, 7));
    item.price = sqlite3_column_double(st, 8);
    item.notes = utf8ToWide((const char*)sqlite3_column_text(st, 9));
    item.changed = utf8ToWide((const char*)sqlite3_column_text(st, 10));
    return SQLITE_ROW;
}

void showItem(const Item& item) {
    g_loadingForm=true;
    g_currentId = item.id;
    g_originalStorageNo = item.storageNo;
    setText(g_main, ID_NAME, item.name);
    setText(g_main, ID_STORAGE_NO, item.storageNo);
    setText(g_main, ID_BARCODE, item.barcode);
    setText(g_main, ID_LOCATION, item.location);
    setText(g_main, ID_AMOUNT, std::to_wstring(item.amount));
    selectCombo(GetDlgItem(g_main, ID_UNIT), item.unit);
    setText(g_main, ID_SHELF, item.shelf);
    if (item.price <= 0.0000001) {
        setText(g_main, ID_PRICE, L"-");
        setPriceAlignment(false);
    } else {
        std::wostringstream price;
        price << std::fixed << std::setprecision(2) << item.price;
        wstring p = price.str(); std::replace(p.begin(), p.end(), L'.', L',');
        setText(g_main, ID_PRICE, p + L" " + setting("currency", L"€"));
        setPriceAlignment(true);
    }
    setText(g_main, ID_CHANGED, item.changed);
    setText(g_main, ID_NOTES, item.notes);
    setText(g_main,ID_STATUS,L"Eintrag Nr. " + std::to_wstring(item.id));
    g_loadingForm=false;g_formDirty=false;
}

bool loadById(sqlite3_int64 id) {
    sqlite3_stmt* st = nullptr;
    if(sqlite3_prepare_v2(g_db, "SELECT id,name,storage_no,barcode,location,amount,unit,shelf,price,notes,updated_at FROM items WHERE id=?", -1, &st, nullptr)!=SQLITE_OK){showNotice(g_main,L"Eintrag laden",utf8ToWide(sqlite3_errmsg(g_db)),true);return false;}
    sqlite3_bind_int64(st, 1, id);
    Item item;int step=readItem(st,item);bool ok=step==SQLITE_ROW;wstring detail=step==SQLITE_ROW||step==SQLITE_DONE?L"":utf8ToWide(sqlite3_errmsg(g_db));
    sqlite3_finalize(st);
    if (ok) { showItem(item); loadCurrentImage(item.id); }
    else if(!detail.empty())showNotice(g_main,L"Eintrag laden",detail,true);
    return ok;
}

string escapedLikePattern(const wstring& query){
    string raw=wideToUtf8(query),escaped;escaped.reserve(raw.size()+2);escaped.push_back('%');
    for(char c:raw){if(c=='\\'||c=='%'||c=='_')escaped.push_back('\\');escaped.push_back(c);}
    escaped.push_back('%');return escaped;
}

void refreshList() {
    HWND list = GetDlgItem(g_main, ID_LIST);
    ListView_DeleteAllItems(list);
    wstring query = getText(g_main, ID_SEARCH);
    string like=escapedLikePattern(query);
    sqlite3_stmt* st = nullptr;
    if(sqlite3_prepare_v2(g_db,
        "SELECT id,name,amount,unit,location,shelf FROM items "
        "WHERE (?2='' OR shelf=?2 COLLATE NOCASE) AND ("
        "CAST(id AS TEXT) LIKE ?1 ESCAPE '\\' OR name LIKE ?1 ESCAPE '\\' OR storage_no LIKE ?1 ESCAPE '\\' OR "
        "barcode LIKE ?1 ESCAPE '\\' OR location LIKE ?1 ESCAPE '\\' OR CAST(amount AS TEXT) LIKE ?1 ESCAPE '\\' OR "
        "unit LIKE ?1 ESCAPE '\\' OR shelf LIKE ?1 ESCAPE '\\' OR CAST(price AS TEXT) LIKE ?1 ESCAPE '\\' OR "
        "replace(printf('%.2f',price),'.',',') LIKE ?1 ESCAPE '\\' OR COALESCE(notes,'') LIKE ?1 ESCAPE '\\' OR "
        "created_at LIKE ?1 ESCAPE '\\' OR updated_at LIKE ?1 ESCAPE '\\' OR COALESCE(image_mime,'') LIKE ?1 ESCAPE '\\') "
        "ORDER BY updated_at DESC,id DESC", -1, &st, nullptr)!=SQLITE_OK){showNotice(g_main,L"Liste laden",utf8ToWide(sqlite3_errmsg(g_db)),true);return;}
    sqlite3_bind_text(st, 1, like.c_str(), -1, SQLITE_TRANSIENT);
    string shelfFilter=wideToUtf8(g_shelfFilter);sqlite3_bind_text(st,2,shelfFilter.c_str(),-1,SQLITE_TRANSIENT);
    int row = 0;
    int stepResult=SQLITE_ROW;
    while ((stepResult=sqlite3_step(st)) == SQLITE_ROW) {
        LVITEMW li{}; li.mask = LVIF_TEXT | LVIF_PARAM; li.iItem = row;
        wstring name = utf8ToWide((const char*)sqlite3_column_text(st, 1));
        li.pszText = name.data(); li.lParam = (LPARAM)sqlite3_column_int64(st, 0);
        int idx = ListView_InsertItem(list, &li);
        wstring qty = std::to_wstring(sqlite3_column_int(st, 2)) + L" " + utf8ToWide((const char*)sqlite3_column_text(st, 3));
        wstring place = utf8ToWide((const char*)sqlite3_column_text(st, 4));
        wstring shelf = utf8ToWide((const char*)sqlite3_column_text(st, 5));
        ListView_SetItemText(list, idx, 1, qty.data());
        ListView_SetItemText(list, idx, 2, place.data());
        ListView_SetItemText(list, idx, 3, shelf.data());
        ++row;
    }
    wstring readError=stepResult==SQLITE_DONE?L"":utf8ToWide(sqlite3_errmsg(g_db));
    sqlite3_finalize(st);
    if(!readError.empty()){showNotice(g_main,L"Liste laden",readError,true);setText(g_main,ID_STATUS,L"Die Artikelliste konnte nicht vollständig geladen werden");return;}
    wstring status=std::to_wstring(row)+(row==1?L" Eintrag":L" Einträge");if(!g_shelfFilter.empty())status+=L" in Fach "+g_shelfFilter;setText(g_main,ID_STATUS,status);
}

void selectItemInList(sqlite3_int64 id){
    HWND list=GetDlgItem(g_main,ID_LIST);int count=ListView_GetItemCount(list);
    for(int row=0;row<count;++row){LVITEMW item{};item.mask=LVIF_PARAM;item.iItem=row;if(ListView_GetItem(list,&item)&&(sqlite3_int64)item.lParam==id){g_loadingForm=true;ListView_SetItemState(list,row,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);ListView_EnsureVisible(list,row,FALSE);g_loadingForm=false;return;}}
}

bool parsePrice(wstring text,double& value) {
    if (text.empty() || text == L"-") {value=0;return true;}
    wstring currency = setting("currency", L"€");
    if (!currency.empty()) {
        size_t pos;
        while ((pos = text.find(currency)) != wstring::npos) text.erase(pos, currency.size());
    }
    text.erase(std::remove_if(text.begin(), text.end(), [](wchar_t c) {
        return std::iswspace(c) != 0;
    }), text.end());
    std::replace(text.begin(), text.end(), L',', L'.');
    if(text.empty()){value=0;return true;}
    size_t consumed=0;try{value=std::stod(text,&consumed);}catch(...){return false;}
    return consumed==text.size()&&std::isfinite(value)&&value>=0;
}

bool parseAmount(wstring text,int& value){
    text.erase(text.begin(),std::find_if(text.begin(),text.end(),[](wchar_t c){return !iswspace(c);}));text.erase(std::find_if(text.rbegin(),text.rend(),[](wchar_t c){return !iswspace(c);}).base(),text.end());
    if(text.empty()){value=0;return true;}
    size_t consumed=0;long long parsed=0;try{parsed=std::stoll(text,&consumed,10);}catch(...){return false;}
    if(consumed!=text.size()||parsed<0||parsed>std::numeric_limits<int>::max())return false;value=(int)parsed;return true;
}

void saveCurrent() {
    sqlite3_int64 originalId=g_currentId;
    Item it;
    it.name = getText(g_main, ID_NAME);
    if (it.name.empty()) {
        showNotice(g_main,L"Eintrag speichern",L"Bitte geben Sie einen Artikelnamen ein.");
        SetFocus(GetDlgItem(g_main, ID_NAME)); return;
    }
    it.storageNo = getText(g_main, ID_STORAGE_NO); it.barcode = getText(g_main, ID_BARCODE);
    it.location = getText(g_main, ID_LOCATION); it.unit = getText(g_main, ID_UNIT);
    it.shelf = getText(g_main, ID_SHELF); it.notes = getText(g_main, ID_NOTES);
    if(!parseAmount(getText(g_main,ID_AMOUNT),it.amount)){showNotice(g_main,L"Eintrag speichern",L"Bitte geben Sie eine gültige, nicht negative Menge ein.");SetFocus(GetDlgItem(g_main,ID_AMOUNT));return;}
    if(!parsePrice(getText(g_main, ID_PRICE),it.price)){showNotice(g_main,L"Eintrag speichern",L"Bitte geben Sie einen gültigen, nicht negativen Preis ein.");SetFocus(GetDlgItem(g_main,ID_PRICE));return;}
    if (it.unit.empty()) it.unit = setting("default_unit", L"Stück");

    // A confirmation is only relevant when assigning a new Lagernummer or
    // changing the one that was loaded. Ordinary edits remain interruption-free.
    bool storageNumberChanged = !g_currentId||_wcsicmp(it.storageNo.c_str(),g_originalStorageNo.c_str())!=0;
    sqlite3_int64 conflictingId = 0;
    wstring conflictingName;
    if (storageNumberChanged && !it.storageNo.empty()) {
        sqlite3_stmt* conflict = nullptr;
        if(sqlite3_prepare_v2(g_db,
            "SELECT id,name FROM items WHERE storage_no=? COLLATE NOCASE AND id<>? ORDER BY id LIMIT 1",
            -1, &conflict, nullptr)!=SQLITE_OK){showNotice(g_main,L"Eintrag speichern",utf8ToWide(sqlite3_errmsg(g_db)),true);return;}
        string number = wideToUtf8(it.storageNo);
        bool conflictBound=sqlite3_bind_text(conflict,1,number.c_str(),-1,SQLITE_TRANSIENT)==SQLITE_OK&&sqlite3_bind_int64(conflict,2,g_currentId)==SQLITE_OK;
        int conflictStep=conflictBound?sqlite3_step(conflict):SQLITE_ERROR;
        if (conflictStep == SQLITE_ROW) {
            conflictingId = sqlite3_column_int64(conflict, 0);
            conflictingName = utf8ToWide((const char*)sqlite3_column_text(conflict, 1));
        }
        wstring conflictError=conflictStep==SQLITE_ROW||conflictStep==SQLITE_DONE?L"":utf8ToWide(sqlite3_errmsg(g_db));
        sqlite3_finalize(conflict);
        if(!conflictError.empty()){showNotice(g_main,L"Eintrag speichern",conflictError,true);return;}
        if (conflictingId) {
            wstring question = L"Die Lagernummer \"" + it.storageNo +
                L"\" wird bereits für \"" + conflictingName +
                L"\" verwendet.\n\nSoll der vorhandene Eintrag überschrieben werden?";
            if (!confirmAction(L"Lagernummer bereits vergeben",question))
                return;
        }
    }

    const char* insertSql = "INSERT INTO items(name,storage_no,barcode,location,amount,unit,shelf,price,notes) VALUES(?,?,?,?,?,?,?,?,?)";
    const char* updateSql = "UPDATE items SET name=?,storage_no=?,barcode=?,location=?,amount=?,unit=?,shelf=?,price=?,notes=?,updated_at=datetime('now','localtime') WHERE id=?";
    char* transactionError=nullptr;
    if(sqlite3_exec(g_db,"BEGIN IMMEDIATE;",nullptr,nullptr,&transactionError)!=SQLITE_OK){wstring detail=utf8ToWide(transactionError?transactionError:sqlite3_errmsg(g_db));sqlite3_free(transactionError);showNotice(g_main,L"Speichern fehlgeschlagen",detail,true);return;}
    bool saved=true;wstring saveError;auto failSave=[&](){if(saveError.empty())saveError=utf8ToWide(sqlite3_errmsg(g_db));saved=false;};
    if (conflictingId) {
        sqlite3_stmt* remove = nullptr;
        saved=sqlite3_prepare_v2(g_db, "DELETE FROM items WHERE storage_no=? COLLATE NOCASE AND id<>?", -1, &remove, nullptr)==SQLITE_OK;
        if(!saved)failSave();
        string number = wideToUtf8(it.storageNo);
        if(saved&&(sqlite3_bind_text(remove,1,number.c_str(),-1,SQLITE_TRANSIENT)!=SQLITE_OK||sqlite3_bind_int64(remove,2,g_currentId)!=SQLITE_OK||sqlite3_step(remove)!=SQLITE_DONE))failSave();
        int removeFinalize=sqlite3_finalize(remove);if(saved&&removeFinalize!=SQLITE_OK)failSave();
    }
    sqlite3_stmt* st = nullptr;
    if(saved&&sqlite3_prepare_v2(g_db,g_currentId?updateSql:insertSql,-1,&st,nullptr)!=SQLITE_OK)failSave();
    std::vector<string> fields = {wideToUtf8(it.name),wideToUtf8(it.storageNo),wideToUtf8(it.barcode),wideToUtf8(it.location),wideToUtf8(it.unit),wideToUtf8(it.shelf),wideToUtf8(it.notes)};
    if(saved){bool bound=sqlite3_bind_text(st,1,fields[0].c_str(),-1,SQLITE_TRANSIENT)==SQLITE_OK&&sqlite3_bind_text(st,2,fields[1].c_str(),-1,SQLITE_TRANSIENT)==SQLITE_OK&&sqlite3_bind_text(st,3,fields[2].c_str(),-1,SQLITE_TRANSIENT)==SQLITE_OK&&sqlite3_bind_text(st,4,fields[3].c_str(),-1,SQLITE_TRANSIENT)==SQLITE_OK&&sqlite3_bind_int(st,5,it.amount)==SQLITE_OK&&sqlite3_bind_text(st,6,fields[4].c_str(),-1,SQLITE_TRANSIENT)==SQLITE_OK&&sqlite3_bind_text(st,7,fields[5].c_str(),-1,SQLITE_TRANSIENT)==SQLITE_OK&&sqlite3_bind_double(st,8,it.price)==SQLITE_OK&&sqlite3_bind_text(st,9,fields[6].c_str(),-1,SQLITE_TRANSIENT)==SQLITE_OK;
        if(bound&&g_currentId)bound=sqlite3_bind_int64(st,10,g_currentId)==SQLITE_OK;if(!bound||sqlite3_step(st)!=SQLITE_DONE)failSave();}
    if (saved && !g_currentId) g_currentId = sqlite3_last_insert_rowid(g_db);
    if (saved && g_imageDirty) {
        sqlite3_stmt* imageUpdate=nullptr;
        if(sqlite3_prepare_v2(g_db,"UPDATE items SET image_data=?,image_mime=? WHERE id=?",-1,&imageUpdate,nullptr)!=SQLITE_OK)failSave();
        if(saved){bool bound=g_currentImage.empty()?(sqlite3_bind_null(imageUpdate,1)==SQLITE_OK&&sqlite3_bind_null(imageUpdate,2)==SQLITE_OK):(sqlite3_bind_blob(imageUpdate,1,g_currentImage.data(),(int)g_currentImage.size(),SQLITE_TRANSIENT)==SQLITE_OK&&sqlite3_bind_text(imageUpdate,2,"image/png",-1,SQLITE_STATIC)==SQLITE_OK);bound=bound&&sqlite3_bind_int64(imageUpdate,3,g_currentId)==SQLITE_OK;if(!bound||sqlite3_step(imageUpdate)!=SQLITE_DONE)failSave();}
        int imageFinalize=sqlite3_finalize(imageUpdate);if(saved&&imageFinalize!=SQLITE_OK)failSave();
    }
    int finalizeResult=sqlite3_finalize(st);st=nullptr;if(saved&&finalizeResult!=SQLITE_OK)failSave();
    if (!saved) {
        wstring detail=saveError.empty()?utf8ToWide(sqlite3_errmsg(g_db)):saveError;
        sqlite3_exec(g_db,"ROLLBACK;",nullptr,nullptr,nullptr);
        g_currentId=originalId;
        showNotice(g_main,L"Speichern fehlgeschlagen",detail,true);
    } else {
        transactionError=nullptr;saved=sqlite3_exec(g_db,"COMMIT;",nullptr,nullptr,&transactionError)==SQLITE_OK;
        wstring detail=saved?L"":utf8ToWide(transactionError?transactionError:sqlite3_errmsg(g_db));sqlite3_free(transactionError);
        if(saved){g_imageDirty=false;g_formDirty=false;loadById(g_currentId);refreshList();selectItemInList(g_currentId);setText(g_main,ID_STATUS,L"Eintrag gespeichert");}
        else{sqlite3_exec(g_db,"ROLLBACK;",nullptr,nullptr,nullptr);g_currentId=originalId;showNotice(g_main,L"Speichern fehlgeschlagen",detail,true);}
    }
}

void deleteCurrent() {
    if (!g_currentId) return;
    if (!confirmAction(L"Eintrag löschen",L"Diesen Eintrag wirklich löschen?")) return;
    sqlite3_stmt* st = nullptr;bool deleted=sqlite3_prepare_v2(g_db,"DELETE FROM items WHERE id=?",-1,&st,nullptr)==SQLITE_OK;
    if(deleted){sqlite3_bind_int64(st,1,g_currentId);deleted=sqlite3_step(st)==SQLITE_DONE&&sqlite3_changes(g_db)==1;}sqlite3_finalize(st);
    if(deleted){clearForm();refreshList();setText(g_main,ID_STATUS,L"Eintrag gelöscht");}
    else showNotice(g_main,L"Löschen fehlgeschlagen",utf8ToWide(sqlite3_errmsg(g_db)),true);
}

void loadEdge(bool newest) {
    sqlite3_stmt* st = nullptr;
    string sql = string("SELECT id FROM items ORDER BY created_at ") + (newest ? "DESC" : "ASC") + ",id " + (newest ? "DESC" : "ASC") + " LIMIT 1";
    if(sqlite3_prepare_v2(g_db,sql.c_str(),-1,&st,nullptr)!=SQLITE_OK){showNotice(g_main,L"Datenbank",utf8ToWide(sqlite3_errmsg(g_db)),true);return;}
    int step=sqlite3_step(st);
    if(step==SQLITE_ROW)loadById(sqlite3_column_int64(st,0));
    else if(step==SQLITE_DONE)showNotice(g_main,L"Datenbank",L"Die Datenbank enthält noch keine Einträge.");
    else{wstring detail=utf8ToWide(sqlite3_errmsg(g_db));sqlite3_finalize(st);showNotice(g_main,L"Datenbank",detail,true);return;}
    sqlite3_finalize(st);
}

struct FileDialogData{wstring directory,fileName,extension,pattern,result;bool save=false,accepted=false;};

wstring joinedPath(const wstring& directory,const wstring& name){return directory+(directory.empty()||directory.back()==L'\\'?L"":L"\\")+name;}

bool fileMatches(const wstring& name,const wstring& pattern){
    if(pattern.empty()||pattern==L"*.*")return true;size_t start=0;
    while(start<pattern.size()){size_t end=pattern.find(L';',start);wstring part=pattern.substr(start,end==wstring::npos?wstring::npos:end-start);size_t dot=part.find_last_of(L'.');if(dot!=wstring::npos){wstring suffix=part.substr(dot);if(name.size()>=suffix.size()&&lstrcmpiW(name.substr(name.size()-suffix.size()).c_str(),suffix.c_str())==0)return true;}if(end==wstring::npos)break;start=end+1;}return false;
}

void fillFileDialog(HWND hwnd,FileDialogData* data){
    HWND list=GetDlgItem(hwnd,ID_FILE_LIST);SendMessageW(list,LB_RESETCONTENT,0,0);setText(hwnd,ID_FILE_PATH,data->directory);
    WIN32_FIND_DATAW found{};HANDLE search=FindFirstFileW(joinedPath(data->directory,L"*").c_str(),&found);
    if(search!=INVALID_HANDLE_VALUE){do{if((found.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)&&lstrcmpW(found.cFileName,L".")&&lstrcmpW(found.cFileName,L"..")){wstring label=L"[Ordner] "+wstring(found.cFileName);LRESULT index=SendMessageW(list,LB_ADDSTRING,0,(LPARAM)label.c_str());SendMessageW(list,LB_SETITEMDATA,(WPARAM)index,1);}}while(FindNextFileW(search,&found));FindClose(search);}
    search=FindFirstFileW(joinedPath(data->directory,L"*").c_str(),&found);
    if(search!=INVALID_HANDLE_VALUE){do{if(!(found.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)){wstring name=found.cFileName;if(fileMatches(name,data->pattern)){LRESULT index=SendMessageW(list,LB_ADDSTRING,0,(LPARAM)name.c_str());SendMessageW(list,LB_SETITEMDATA,(WPARAM)index,0);}}}while(FindNextFileW(search,&found));FindClose(search);}
}

bool finishFileDialog(HWND hwnd,FileDialogData* data){
    wstring name=getText(hwnd,ID_FILE_NAME);if(name.empty()){showNotice(hwnd,L"Datei auswählen",L"Bitte geben Sie einen Dateinamen ein.");return false;}
    if(!data->extension.empty()&&name.find_last_of(L'.')==wstring::npos)name+=L"."+data->extension;
    wstring candidate=joinedPath(data->directory,name);DWORD attributes=GetFileAttributesW(candidate.c_str());
    if(!data->save&&(attributes==INVALID_FILE_ATTRIBUTES||(attributes&FILE_ATTRIBUTE_DIRECTORY))){showNotice(hwnd,L"Datei öffnen",L"Die ausgewählte Datei wurde nicht gefunden.",true);return false;}
    if(data->save&&attributes!=INVALID_FILE_ATTRIBUTES&&!showThemedMessage(hwnd,L"Datei ersetzen",L"Die ausgewählte Datei ist bereits vorhanden.\n\nMöchten Sie sie ersetzen?",true))return false;
    data->result=candidate;data->accepted=true;DestroyWindow(hwnd);return true;
}

LRESULT CALLBACK FileDialogWndProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
    FileDialogData* data=(FileDialogData*)GetWindowLongPtrW(hwnd,GWLP_USERDATA);
    if(msg==WM_CREATE){data=(FileDialogData*)((CREATESTRUCTW*)lp)->lpCreateParams;SetWindowLongPtrW(hwnd,GWLP_USERDATA,(LONG_PTR)data);
        addControl(L"BUTTON",L"Nach oben",WS_TABSTOP|BS_OWNERDRAW,ID_FILE_UP,hwnd);addControl(L"EDIT",data->directory.c_str(),WS_TABSTOP|ES_AUTOHSCROLL,ID_FILE_PATH,hwnd,WS_EX_CLIENTEDGE);addControl(L"BUTTON",L"Anzeigen",WS_TABSTOP|BS_OWNERDRAW,ID_FILE_REFRESH,hwnd);
        addControl(L"LISTBOX",L"",WS_TABSTOP|LBS_NOTIFY|WS_VSCROLL|LBS_NOINTEGRALHEIGHT,ID_FILE_LIST,hwnd,WS_EX_CLIENTEDGE);addControl(L"STATIC",L"Dateiname",SS_LEFT,ID_FILE_LABEL,hwnd);addControl(L"EDIT",data->fileName.c_str(),WS_TABSTOP|ES_AUTOHSCROLL,ID_FILE_NAME,hwnd,WS_EX_CLIENTEDGE);
        addControl(L"BUTTON",data->save?L"Speichern":L"Öffnen",WS_TABSTOP|BS_OWNERDRAW,IDOK,hwnd);addControl(L"BUTTON",L"Abbrechen",WS_TABSTOP|BS_OWNERDRAW,IDCANCEL,hwnd);
        MoveWindow(GetDlgItem(hwnd,ID_FILE_UP),16,14,90,28,TRUE);MoveWindow(GetDlgItem(hwnd,ID_FILE_PATH),114,14,366,28,TRUE);MoveWindow(GetDlgItem(hwnd,ID_FILE_REFRESH),488,14,96,28,TRUE);MoveWindow(GetDlgItem(hwnd,ID_FILE_LIST),16,52,568,300,TRUE);MoveWindow(GetDlgItem(hwnd,ID_FILE_LABEL),16,364,90,20,TRUE);MoveWindow(GetDlgItem(hwnd,ID_FILE_NAME),110,360,474,28,TRUE);MoveWindow(GetDlgItem(hwnd,IDOK),376,400,100,30,TRUE);MoveWindow(GetDlgItem(hwnd,IDCANCEL),484,400,100,30,TRUE);
        fillFileDialog(hwnd,data);applyWindowFrameTheme(hwnd);EnumChildWindows(hwnd,ThemeChild,0);return 0;}
    if(msg==WM_COMMAND){int id=LOWORD(wp);if(id==IDCANCEL){DestroyWindow(hwnd);return 0;}if(id==IDOK){finishFileDialog(hwnd,data);return 0;}
        if(id==ID_FILE_REFRESH){wstring path=getText(hwnd,ID_FILE_PATH);DWORD a=GetFileAttributesW(path.c_str());if(a!=INVALID_FILE_ATTRIBUTES&&(a&FILE_ATTRIBUTE_DIRECTORY)){data->directory=path;fillFileDialog(hwnd,data);}else showNotice(hwnd,L"Ordner",L"Der angegebene Ordner wurde nicht gefunden.",true);return 0;}
        if(id==ID_FILE_UP){size_t slash=data->directory.find_last_of(L"\\/");if(slash!=wstring::npos){data->directory=slash<=2?data->directory.substr(0,3):data->directory.substr(0,slash);fillFileDialog(hwnd,data);}return 0;}
        if(id==ID_FILE_LIST&&(HIWORD(wp)==LBN_SELCHANGE||HIWORD(wp)==LBN_DBLCLK)){HWND list=GetDlgItem(hwnd,ID_FILE_LIST);LRESULT index=SendMessageW(list,LB_GETCURSEL,0,0);if(index!=LB_ERR){wchar_t label[MAX_PATH]{};SendMessageW(list,LB_GETTEXT,(WPARAM)index,(LPARAM)label);bool folder=SendMessageW(list,LB_GETITEMDATA,(WPARAM)index,0)==1;if(folder&&HIWORD(wp)==LBN_DBLCLK){data->directory=joinedPath(data->directory,wstring(label).substr(9));fillFileDialog(hwnd,data);}else if(!folder){setText(hwnd,ID_FILE_NAME,label);if(HIWORD(wp)==LBN_DBLCLK)finishFileDialog(hwnd,data);}}return 0;}}
    if(msg==WM_CTLCOLORSTATIC||msg==WM_CTLCOLOREDIT||msg==WM_CTLCOLORBTN||msg==WM_CTLCOLORLISTBOX)return themeControlColor(msg,wp);if(msg==WM_ERASEBKGND)return eraseThemedBackground(hwnd,wp);if(msg==WM_MEASUREITEM&&measureOwnerItem((MEASUREITEMSTRUCT*)lp))return TRUE;if(msg==WM_DRAWITEM&&drawOwnerItem((DRAWITEMSTRUCT*)lp))return TRUE;if(msg==WM_CLOSE){DestroyWindow(hwnd);return 0;}return DefWindowProcW(hwnd,msg,wp,lp);
}

bool wineFileDialog(wchar_t* path,DWORD size,bool save,const wchar_t* filter,const wchar_t* defExt,HWND ownerWindow){
    FileDialogData data;data.save=save;data.extension=defExt?defExt:L"";if(filter)data.pattern=filter+lstrlenW(filter)+1;wstring initial=path;size_t slash=initial.find_last_of(L"\\/");data.directory=slash==wstring::npos?moduleDirectory():initial.substr(0,slash);data.fileName=slash==wstring::npos?initial:initial.substr(slash+1);
    RECT owner{};GetWindowRect(ownerWindow,&owner);HWND window=CreateWindowExW(WS_EX_DLGMODALFRAME,L"LogSFileDialog",save?L"Datei speichern":L"Datei öffnen",WS_CAPTION|WS_SYSMENU,owner.left+80,owner.top+50,620,480,ownerWindow,nullptr,g_instance,&data);beginModal(ownerWindow,window);ShowWindow(window,SW_SHOW);runModalLoop(window);endModal(ownerWindow,window);SetForegroundWindow(ownerWindow);if(data.accepted)lstrcpynW(path,data.result.c_str(),(int)size);return data.accepted;
}

UINT_PTR CALLBACK SaveFileHookProc(HWND hwnd,UINT msg,WPARAM,LPARAM lp){
    if(msg==WM_INITDIALOG){HWND dialog=GetParent(hwnd);applyWindowFrameTheme(dialog);EnumChildWindows(dialog,ThemeChild,0);return FALSE;}
    if(msg==WM_NOTIFY){OFNOTIFYW* notify=(OFNOTIFYW*)lp;if(notify&&notify->hdr.code==CDN_FILEOK){
        wchar_t selected[MAX_PATH]{};HWND dialog=GetParent(hwnd);SendMessageW(dialog,CDM_GETFILEPATH,MAX_PATH,(LPARAM)selected);
        if(selected[0]&&GetFileAttributesW(selected)!=INVALID_FILE_ATTRIBUTES&&!showThemedMessage(dialog,L"Datei ersetzen",L"Die ausgewählte Datei ist bereits vorhanden.\n\nMöchten Sie sie ersetzen?",true)){
            SetWindowLongPtrW(hwnd,DWLP_MSGRESULT,1);return TRUE;
        }
    }}
    return FALSE;
}

UINT_PTR CALLBACK OpenFileHookProc(HWND hwnd,UINT msg,WPARAM,LPARAM){if(msg==WM_INITDIALOG){HWND dialog=GetParent(hwnd);applyWindowFrameTheme(dialog);EnumChildWindows(dialog,ThemeChild,0);}return FALSE;}

bool browseFile(wchar_t* path,DWORD size,bool save,const wchar_t* filter,const wchar_t* defExt,HWND owner) {
    if(!owner)owner=g_main;if(g_runningUnderWine)return wineFileDialog(path,size,save,filter,defExt,owner);
    OPENFILENAMEW ofn{}; ofn.lStructSize=sizeof(ofn); ofn.hwndOwner=owner; ofn.lpstrFile=path; ofn.nMaxFile=size;
    ofn.lpstrFilter=filter;ofn.lpstrDefExt=defExt;ofn.Flags=OFN_PATHMUSTEXIST|OFN_EXPLORER|OFN_ENABLEHOOK|(save?0:OFN_FILEMUSTEXIST);
    ofn.lpfnHook=save?SaveFileHookProc:OpenFileHookProc;
    bool selected=save?GetSaveFileNameW(&ofn)!=FALSE:GetOpenFileNameW(&ofn)!=FALSE;
    return selected;
}

wstring csvCell(const wstring& s) {
    wstring safe=s;auto first=std::find_if_not(safe.begin(),safe.end(),[](wchar_t c){return iswspace(c)!=0;});
    if(first!=safe.end()&&(*first==L'='||*first==L'+'||*first==L'-'||*first==L'@'))safe.insert(safe.begin(),L'\'');
    wstring r=L"\"";
    for (wchar_t c:safe) { if(c==L'\"') r+=L'\"'; r+=c; }
    return r+L"\"";
}

void exportCsv() {
    wchar_t path[MAX_PATH]=L"LogS-Export.csv";
    if (!browseFile(path,MAX_PATH,true,L"CSV-Datei (*.csv)\0*.csv\0Alle Dateien\0*.*\0",L"csv")) return;
    sqlite3_stmt* st=nullptr;
    int prepareResult=sqlite3_prepare_v2(g_db,
        "SELECT id,name,storage_no,barcode,location,amount,unit,shelf,price,notes,created_at,updated_at,"
        "CASE WHEN image_data IS NULL OR length(image_data)=0 THEN 'Nein' ELSE 'Ja' END,"
        "COALESCE(image_mime,''),COALESCE(length(image_data),0) FROM items ORDER BY name,id",-1,&st,nullptr);
    if(prepareResult!=SQLITE_OK){sqlite3_finalize(st);showNotice(g_main,L"CSV-Export",L"Die Daten konnten nicht für den CSV-Export gelesen werden:\n"+utf8ToWide(sqlite3_errmsg(g_db)),true);return;}
    wstring targetPath=path;size_t slash=targetPath.find_last_of(L"\\/");wstring directory;
    if(slash==wstring::npos)directory=moduleDirectory();else if(slash==2&&targetPath.size()>2&&targetPath[1]==L':')directory=targetPath.substr(0,3);else directory=slash?targetPath.substr(0,slash):L"\\";
    wchar_t temporaryPath[MAX_PATH]{};
    if(!GetTempFileNameW(directory.c_str(),L"LGS",0,temporaryPath)){sqlite3_finalize(st);showNotice(g_main,L"CSV-Export",L"Im Zielordner konnte keine temporäre Exportdatei erstellt werden.",true);return;}
    HANDLE f=CreateFileW(temporaryPath,GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(f==INVALID_HANDLE_VALUE){DeleteFileW(temporaryPath);sqlite3_finalize(st);showNotice(g_main,L"CSV-Export",L"Die Datei konnte nicht geschrieben werden.",true);return;}
    bool ok=true;auto writeAll=[&](const void* data,DWORD size){DWORD written=0;return WriteFile(f,data,size,&written,nullptr)&&written==size;};
    const BYTE bom[]={0xEF,0xBB,0xBF};ok=writeAll(bom,3);
    string header="ID;Artikelname;Lagernummer;EAN / UPC / GTIN;Lagerort;Menge;Einheit;Fach;Preis;Beschreibung / Notizen;Erstellt;Letzte Änderung;Bild vorhanden;Bildformat;Bildgröße (Bytes)\r\n";
    ok=ok&&writeAll(header.data(),(DWORD)header.size());
    int stepResult=SQLITE_ROW;
    while(ok&&(stepResult=sqlite3_step(st))==SQLITE_ROW){
        std::wostringstream row;
        for(int i=0;i<15;i++){
            if(i)row<<L';';
            if(i==0||i==5||i==14)row<<sqlite3_column_int64(st,i);
            else if(i==8){std::wostringstream price;price<<std::fixed<<std::setprecision(2)<<sqlite3_column_double(st,i);wstring value=price.str();std::replace(value.begin(),value.end(),L'.',L',');row<<csvCell(value);}
            else{const unsigned char* value=sqlite3_column_text(st,i);row<<csvCell(value?utf8ToWide((const char*)value):L"");}
        }
        row<<L"\r\n";string bytes=wideToUtf8(row.str());ok=writeAll(bytes.data(),(DWORD)bytes.size());
    }
    if(ok&&stepResult!=SQLITE_DONE)ok=false;
    if(sqlite3_finalize(st)!=SQLITE_OK)ok=false;
    if(!FlushFileBuffers(f))ok=false;
    if(!CloseHandle(f))ok=false;
    if(ok&&!MoveFileExW(temporaryPath,path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))ok=false;
    if(!ok)DeleteFileW(temporaryPath);
    showNotice(g_main,L"CSV-Export",ok?L"Der CSV-Export wurde erstellt.":L"Der CSV-Export konnte nicht vollständig geschrieben werden.",!ok);
}

void saveDatabaseCopy() {
    if(!resolveUnsavedChanges())return;
    wchar_t path[MAX_PATH] = L"LogS-Datenbank.db";
    if(!browseFile(path,MAX_PATH,true,L"LogS-Datenbank (*.db)\0*.db\0Alle Dateien\0*.*\0",L"db")) return;
    if (_wcsicmp(g_dbPath.c_str(),path)==0) {
        showNotice(g_main,L"Datenbank speichern",L"Diese Datenbank ist bereits geöffnet und wird automatisch gespeichert.");
        return;
    }
    wstring targetPath=path;size_t slash=targetPath.find_last_of(L"\\/");wstring directory=slash==wstring::npos?moduleDirectory():(slash==2&&targetPath[1]==L':'?targetPath.substr(0,3):(slash?targetPath.substr(0,slash):L"\\"));
    wchar_t temporaryPath[MAX_PATH]{};if(!GetTempFileNameW(directory.c_str(),L"LGS",0,temporaryPath)){showNotice(g_main,L"Datenbank speichern",L"Im Zielordner konnte keine temporäre Datenbank erstellt werden.",true);return;}
    sqlite3* destination = nullptr;
    string target = wideToUtf8(temporaryPath);
    bool ok = sqlite3_open_v2(target.c_str(), &destination, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK;
    if (ok) {
        sqlite3_backup* backup = sqlite3_backup_init(destination, "main", g_db, "main");
        int step=backup?sqlite3_backup_step(backup,-1):SQLITE_ERROR,finished=backup?sqlite3_backup_finish(backup):SQLITE_ERROR;
        ok=backup&&step==SQLITE_DONE&&finished==SQLITE_OK;
    }
    if(destination&&sqlite3_close(destination)!=SQLITE_OK)ok=false;
    if(ok)ok=MoveFileExW(temporaryPath,path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;
    if(!ok)DeleteFileW(temporaryPath);
    showNotice(g_main,L"Datenbank speichern",ok?L"Die Datenbank wurde gespeichert.":L"Die Datenbank konnte nicht gespeichert werden.",!ok);
}

void createNewDatabase(){
    if(!resolveUnsavedChanges())return;
    wchar_t path[MAX_PATH]=L"Neue-LogS-Datenbank.db";
    if(!browseFile(path,MAX_PATH,true,L"LogS-Datenbank (*.db)\0*.db\0Alle Dateien\0*.*\0",L"db"))return;
    if(!g_dbPath.empty()&&_wcsicmp(g_dbPath.c_str(),path)==0){showNotice(g_main,L"Neue Datenbank",L"Die derzeit geöffnete Datenbank kann nicht durch eine neue Datenbank ersetzt werden.");return;}
    DWORD attributes=GetFileAttributesW(path);bool existed=attributes!=INVALID_FILE_ATTRIBUTES;wstring backupPath;
    if(existed){for(unsigned int suffix=0;;++suffix){backupPath=wstring(path)+L".logs-backup"+(suffix?L"-"+std::to_wstring(suffix):L"")+L".tmp";if(GetFileAttributesW(backupPath.c_str())==INVALID_FILE_ATTRIBUTES)break;}if(!MoveFileW(path,backupPath.c_str())){showNotice(g_main,L"Neue Datenbank",L"Die vorhandene Datei konnte nicht sicher zwischengespeichert werden.",true);return;}}
    if(!openDatabase(path)){
        bool removed=DeleteFileW(path)!=FALSE||GetLastError()==ERROR_FILE_NOT_FOUND;
        bool restored=!existed||(removed&&MoveFileW(backupPath.c_str(),path)!=FALSE);
        if(existed&&!restored)showNotice(g_main,L"Neue Datenbank",L"Die ursprüngliche Datei konnte nicht automatisch wiederhergestellt werden. Sie liegt weiterhin hier:\n"+backupPath,true);
        else if(!existed&&!removed)showNotice(g_main,L"Neue Datenbank",L"Die unvollständige neue Datenbank konnte nicht entfernt werden:\n"+wstring(path),true);
        return;
    }
    if(existed&&!DeleteFileW(backupPath.c_str()))showNotice(g_main,L"Neue Datenbank",L"Die neue Datenbank wurde erstellt. Die Sicherung der vorherigen Datei konnte jedoch nicht automatisch entfernt werden.",true);
    g_shelfFilter.clear();synchronizeWindowsTheme(true);applyTheme();applyCustomLabels();clearForm();refreshList();
    setText(g_main,ID_STATUS,L"Neue Datenbank erstellt: "+wstring(path).substr(wstring(path).find_last_of(L"\\/")+1));
}

void openDatabaseFromMenu() {
    wchar_t path[MAX_PATH] = L"";
    if(!browseFile(path,MAX_PATH,false,L"LogS-Datenbank (*.db)\0*.db\0Alle Dateien\0*.*\0",L"db")) return;
    if (_wcsicmp(g_dbPath.c_str(),path)==0) return;
    if(!resolveUnsavedChanges())return;
    if (!openDatabase(path)) return;
    synchronizeWindowsTheme(true);
    applyTheme();
    applyCustomLabels();
    g_shelfFilter.clear();clearForm(); refreshList();
    setText(g_main, ID_STATUS, L"Datenbank geladen: " + wstring(path).substr(wstring(path).find_last_of(L"\\/") + 1));
}

struct PromptData { const wchar_t* title; const wchar_t* label; wstring value; bool combo; };

LRESULT CALLBACK PromptWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    PromptData* data=(PromptData*)GetWindowLongPtrW(hwnd,GWLP_USERDATA);
    if(msg==WM_CREATE){
        data=(PromptData*)((CREATESTRUCTW*)lp)->lpCreateParams; SetWindowLongPtrW(hwnd,GWLP_USERDATA,(LONG_PTR)data);
        addControl(L"STATIC",data->label,SS_LEFT,ID_PROMPT_LABEL,hwnd);
        HWND edit=addControl(L"EDIT",data->value.c_str(),WS_TABSTOP|ES_AUTOHSCROLL|WS_BORDER,ID_PROMPT_EDIT,hwnd,WS_EX_CLIENTEDGE);
        addControl(L"BUTTON",L"Übernehmen",WS_TABSTOP|BS_OWNERDRAW,IDOK,hwnd);
        addControl(L"BUTTON",L"Schließen",WS_TABSTOP|BS_OWNERDRAW,IDCANCEL,hwnd);
        MoveWindow(GetDlgItem(hwnd,ID_PROMPT_LABEL),16,16,348,22,TRUE); MoveWindow(edit,16,42,348,27,TRUE);
        MoveWindow(GetDlgItem(hwnd,IDOK),188,84,84,28,TRUE); MoveWindow(GetDlgItem(hwnd,IDCANCEL),280,84,84,28,TRUE);
        applyWindowFrameTheme(hwnd);EnumChildWindows(hwnd,ThemeChild,0);SetFocus(edit); SendMessageW(edit,EM_SETSEL,0,-1); return 0;
    }
    if(msg==WM_COMMAND){
        if(LOWORD(wp)==IDOK){data->value=getText(hwnd,ID_PROMPT_EDIT); DestroyWindow(hwnd); return 0;}
        if(LOWORD(wp)==IDCANCEL){data->value=L"";DestroyWindow(hwnd);return 0;}
    }
    if(msg==WM_MEASUREITEM && measureOwnerItem((MEASUREITEMSTRUCT*)lp))return TRUE;
    if(msg==WM_DRAWITEM && drawOwnerItem((DRAWITEMSTRUCT*)lp))return TRUE;
    if(msg==WM_CTLCOLORSTATIC||msg==WM_CTLCOLOREDIT||msg==WM_CTLCOLORBTN||msg==WM_CTLCOLORLISTBOX)return themeControlColor(msg,wp);
    if(msg==WM_ERASEBKGND)return eraseThemedBackground(hwnd,wp);
    if(msg==WM_CLOSE){data->value=L"";DestroyWindow(hwnd);return 0;}
    return DefWindowProcW(hwnd,msg,wp,lp);
}

wstring prompt(const wchar_t* title,const wchar_t* label,const wstring& initial=L""){
    PromptData data{title,label,initial,false};
    RECT r;GetWindowRect(g_main,&r);
    HWND w=CreateWindowExW(WS_EX_DLGMODALFRAME,L"LogSPrompt",title,WS_CAPTION|WS_SYSMENU,
      r.left+(r.right-r.left-396)/2,r.top+(r.bottom-r.top-155)/2,396,155,g_main,nullptr,g_instance,&data);
    beginModal(g_main,w);ShowWindow(w,SW_SHOW);runModalLoop(w);
    endModal(g_main,w);SetForegroundWindow(g_main);return data.value;
}

struct ThemedMessageData{wstring message;bool yesNo=false;bool accepted=false;};

void layoutThemedMessage(HWND hwnd){
    RECT client{};GetClientRect(hwnd,&client);int width=client.right-client.left,height=client.bottom-client.top;
    MoveWindow(GetDlgItem(hwnd,ID_MESSAGE_TEXT),20,18,std::max(100,width-40),std::max(38,height-76),TRUE);
    if(GetDlgItem(hwnd,IDNO)){MoveWindow(GetDlgItem(hwnd,IDNO),width-104,height-42,84,30,TRUE);MoveWindow(GetDlgItem(hwnd,IDYES),width-196,height-42,84,30,TRUE);}
    else MoveWindow(GetDlgItem(hwnd,IDOK),width-120,height-42,100,30,TRUE);
}

LRESULT CALLBACK ThemedMessageWndProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
    ThemedMessageData* data=(ThemedMessageData*)GetWindowLongPtrW(hwnd,GWLP_USERDATA);
    if(msg==WM_CREATE){data=(ThemedMessageData*)((CREATESTRUCTW*)lp)->lpCreateParams;SetWindowLongPtrW(hwnd,GWLP_USERDATA,(LONG_PTR)data);addControl(L"STATIC",data->message.c_str(),data->yesNo?SS_LEFT:SS_CENTER,ID_MESSAGE_TEXT,hwnd);
        if(data->yesNo){addControl(L"BUTTON",L"Ja",WS_TABSTOP|BS_OWNERDRAW,IDYES,hwnd);addControl(L"BUTTON",L"Nein",WS_TABSTOP|BS_OWNERDRAW,IDNO,hwnd);SetFocus(GetDlgItem(hwnd,IDNO));}
        else{addControl(L"BUTTON",L"Schließen",WS_TABSTOP|BS_OWNERDRAW,IDOK,hwnd);SetFocus(GetDlgItem(hwnd,IDOK));}
        layoutThemedMessage(hwnd);applyWindowFrameTheme(hwnd);EnumChildWindows(hwnd,ThemeChild,0);return 0;}
    if(msg==WM_SIZE){layoutThemedMessage(hwnd);return 0;}
    if(msg==WM_COMMAND){if(LOWORD(wp)==IDYES){data->accepted=true;DestroyWindow(hwnd);return 0;}if(LOWORD(wp)==IDNO||LOWORD(wp)==IDOK||LOWORD(wp)==IDCANCEL){DestroyWindow(hwnd);return 0;}}
    if(msg==WM_CTLCOLORSTATIC||msg==WM_CTLCOLORBTN)return themeControlColor(msg,wp);if(msg==WM_ERASEBKGND)return eraseThemedBackground(hwnd,wp);if(msg==WM_MEASUREITEM&&measureOwnerItem((MEASUREITEMSTRUCT*)lp))return TRUE;if(msg==WM_DRAWITEM&&drawOwnerItem((DRAWITEMSTRUCT*)lp))return TRUE;if(msg==WM_CLOSE){DestroyWindow(hwnd);return 0;}return DefWindowProcW(hwnd,msg,wp,lp);
}

bool showThemedMessage(HWND owner,const wchar_t* title,const wstring& message,bool yesNo){
    ThemedMessageData data{message,yesNo,false};if(!owner)owner=g_main;if(owner&&!IsWindowEnabled(owner)){HWND popup=GetLastActivePopup(owner);if(popup&&popup!=owner&&IsWindow(popup))owner=popup;}RECT r;GetWindowRect(owner,&r);int dialogHeight=yesNo?230:165;HWND w=CreateWindowExW(WS_EX_DLGMODALFRAME,L"LogSMessage",title,WS_CAPTION|WS_SYSMENU,r.left+(r.right-r.left-460)/2,r.top+(r.bottom-r.top-dialogHeight)/2,460,dialogHeight,owner,nullptr,g_instance,&data);beginModal(owner,w);ShowWindow(w,SW_SHOW);runModalLoop(w);endModal(owner,w);SetForegroundWindow(owner);return data.accepted;
}

bool confirmAction(const wchar_t* title,const wstring& message){return showThemedMessage(g_main,title,message,true);}
void showNotice(HWND owner,const wchar_t* title,const wstring& message,bool){showThemedMessage(owner,title,message,false);}

struct UnsavedData{int result=IDCANCEL;};
LRESULT CALLBACK UnsavedWndProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
    UnsavedData* data=(UnsavedData*)GetWindowLongPtrW(hwnd,GWLP_USERDATA);if(msg==WM_CREATE){data=(UnsavedData*)((CREATESTRUCTW*)lp)->lpCreateParams;SetWindowLongPtrW(hwnd,GWLP_USERDATA,(LONG_PTR)data);addControl(L"STATIC",L"Der aktuelle Eintrag enthält nicht gespeicherte Änderungen.\nWas möchten Sie tun?",SS_LEFT,1,hwnd);addControl(L"BUTTON",L"Speichern",WS_TABSTOP|BS_OWNERDRAW,IDYES,hwnd);addControl(L"BUTTON",L"Verwerfen",WS_TABSTOP|BS_OWNERDRAW,IDNO,hwnd);addControl(L"BUTTON",L"Abbrechen",WS_TABSTOP|BS_OWNERDRAW,IDCANCEL,hwnd);MoveWindow(GetDlgItem(hwnd,1),20,20,440,58,TRUE);MoveWindow(GetDlgItem(hwnd,IDYES),184,92,88,30,TRUE);MoveWindow(GetDlgItem(hwnd,IDNO),280,92,88,30,TRUE);MoveWindow(GetDlgItem(hwnd,IDCANCEL),376,92,88,30,TRUE);applyWindowFrameTheme(hwnd);EnumChildWindows(hwnd,ThemeChild,0);SetFocus(GetDlgItem(hwnd,IDCANCEL));return 0;}if(msg==WM_COMMAND&&(LOWORD(wp)==IDYES||LOWORD(wp)==IDNO||LOWORD(wp)==IDCANCEL)){data->result=LOWORD(wp);DestroyWindow(hwnd);return 0;}if(msg==WM_CTLCOLORSTATIC||msg==WM_CTLCOLORBTN)return themeControlColor(msg,wp);if(msg==WM_ERASEBKGND)return eraseThemedBackground(hwnd,wp);if(msg==WM_MEASUREITEM&&measureOwnerItem((MEASUREITEMSTRUCT*)lp))return TRUE;if(msg==WM_DRAWITEM&&drawOwnerItem((DRAWITEMSTRUCT*)lp))return TRUE;if(msg==WM_CLOSE){DestroyWindow(hwnd);return 0;}return DefWindowProcW(hwnd,msg,wp,lp);
}

bool resolveUnsavedChanges(){
    if(!g_formDirty)return true;UnsavedData data;RECT r;GetWindowRect(g_main,&r);HWND w=CreateWindowExW(WS_EX_DLGMODALFRAME,L"LogSUnsaved",L"Nicht gespeicherte Änderungen",WS_CAPTION|WS_SYSMENU,r.left+(r.right-r.left-500)/2,r.top+(r.bottom-r.top-170)/2,500,170,g_main,nullptr,g_instance,&data);beginModal(g_main,w);ShowWindow(w,SW_SHOW);runModalLoop(w);endModal(g_main,w);SetForegroundWindow(g_main);if(data.result==IDYES){saveCurrent();return !g_formDirty;}if(data.result==IDNO){sqlite3_int64 id=g_currentId;if(!id||!loadById(id))clearForm();else selectItemInList(id);return true;}return false;
}

struct ScannerData{wstring input;};

void completeScan(HWND hwnd,ScannerData* data){
    if(data->input.empty())return;wstring code=data->input;data->input.clear();setText(hwnd,ID_SCANNER_VALUE,code);setText(g_main,ID_SEARCH,L"");g_shelfFilter.clear();refreshList();
    string encoded=wideToUtf8(code);sqlite3_stmt* st=nullptr;sqlite3_int64 foundId=0;wstring name;
    const char* itemSql="SELECT id,name FROM items WHERE storage_no=?1 COLLATE NOCASE OR barcode=?1 ORDER BY CASE WHEN storage_no=?1 COLLATE NOCASE THEN 0 ELSE 1 END,updated_at DESC,id DESC LIMIT 1";
    if(sqlite3_prepare_v2(g_db,itemSql,-1,&st,nullptr)!=SQLITE_OK){showNotice(hwnd,L"Scannen",utf8ToWide(sqlite3_errmsg(g_db)),true);SetFocus(hwnd);return;}
    sqlite3_bind_text(st,1,encoded.c_str(),-1,SQLITE_TRANSIENT);int step=sqlite3_step(st);
    if(step==SQLITE_ROW){foundId=sqlite3_column_int64(st,0);name=utf8ToWide((const char*)sqlite3_column_text(st,1));}
    else if(step!=SQLITE_DONE){wstring detail=utf8ToWide(sqlite3_errmsg(g_db));sqlite3_finalize(st);showNotice(hwnd,L"Scannen",detail,true);SetFocus(hwnd);return;}
    sqlite3_finalize(st);
    if(foundId){bool found=loadById(foundId);if(found)selectItemInList(foundId);setText(hwnd,ID_SCANNER_STATUS,found?L"Eintrag geöffnet: "+name:L"Der Eintrag konnte nicht geöffnet werden.");if(found&&SendMessageW(GetDlgItem(hwnd,ID_SCANNER_AUTO_CLOSE),BM_GETCHECK,0,0)==BST_CHECKED)DestroyWindow(hwnd);else SetFocus(hwnd);return;}
    wstring matchedShelf;st=nullptr;
    if(sqlite3_prepare_v2(g_db,"SELECT shelf FROM items WHERE shelf=?1 COLLATE NOCASE ORDER BY id LIMIT 1",-1,&st,nullptr)!=SQLITE_OK){showNotice(hwnd,L"Scannen",utf8ToWide(sqlite3_errmsg(g_db)),true);SetFocus(hwnd);return;}
    sqlite3_bind_text(st,1,encoded.c_str(),-1,SQLITE_TRANSIENT);step=sqlite3_step(st);
    if(step==SQLITE_ROW)matchedShelf=utf8ToWide((const char*)sqlite3_column_text(st,0));
    else if(step!=SQLITE_DONE){wstring detail=utf8ToWide(sqlite3_errmsg(g_db));sqlite3_finalize(st);showNotice(hwnd,L"Scannen",detail,true);SetFocus(hwnd);return;}
    sqlite3_finalize(st);
    if(!matchedShelf.empty()){g_shelfFilter=matchedShelf;refreshList();setText(hwnd,ID_SCANNER_STATUS,L"Fach geöffnet: "+matchedShelf);if(SendMessageW(GetDlgItem(hwnd,ID_SCANNER_AUTO_CLOSE),BM_GETCHECK,0,0)==BST_CHECKED)DestroyWindow(hwnd);else SetFocus(hwnd);return;}
    setText(hwnd,ID_SCANNER_STATUS,L"Kein passender Eintrag gefunden");SetFocus(hwnd);
}

LRESULT CALLBACK ScannerWndProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
    ScannerData* data=(ScannerData*)GetWindowLongPtrW(hwnd,GWLP_USERDATA);
    if(msg==WM_CREATE){data=(ScannerData*)((CREATESTRUCTW*)lp)->lpCreateParams;SetWindowLongPtrW(hwnd,GWLP_USERDATA,(LONG_PTR)data);addControl(L"STATIC",L"Bereit zum Scannen",SS_CENTER,ID_SCANNER_INSTRUCTION,hwnd);HWND value=addControl(L"STATIC",L"Bereit",SS_CENTER|SS_CENTERIMAGE,ID_SCANNER_VALUE,hwnd,WS_EX_CLIENTEDGE);SendMessageW(value,WM_SETFONT,(WPARAM)g_titleFont,TRUE);addControl(L"STATIC",L"Warte auf Scanner …",SS_CENTER,ID_SCANNER_STATUS,hwnd);HWND autoClose=addControl(L"BUTTON",L"Nach Treffer automatisch schließen",WS_TABSTOP|BS_AUTOCHECKBOX,ID_SCANNER_AUTO_CLOSE,hwnd);SendMessageW(autoClose,BM_SETCHECK,setting("scanner_auto_close",L"0")==L"1"?BST_CHECKED:BST_UNCHECKED,0);addControl(L"BUTTON",L"Schließen",WS_TABSTOP|BS_OWNERDRAW,IDCANCEL,hwnd);MoveWindow(GetDlgItem(hwnd,ID_SCANNER_INSTRUCTION),20,22,480,24,TRUE);MoveWindow(value,20,60,480,82,TRUE);MoveWindow(GetDlgItem(hwnd,ID_SCANNER_STATUS),20,158,480,40,TRUE);MoveWindow(GetDlgItem(hwnd,ID_SCANNER_AUTO_CLOSE),20,214,300,30,TRUE);MoveWindow(GetDlgItem(hwnd,IDCANCEL),400,214,100,30,TRUE);applyWindowFrameTheme(hwnd);EnumChildWindows(hwnd,ThemeChild,0);SetFocus(hwnd);return 0;}
    if(msg==WM_CHAR&&data){wchar_t character=(wchar_t)wp;if(character==L'\r'){completeScan(hwnd,data);return 0;}if(character==L'\b'){if(!data->input.empty())data->input.pop_back();return 0;}if(character>=32&&data->input.size()<255)data->input.push_back(character);return 0;}
    if(msg==WM_COMMAND&&LOWORD(wp)==ID_SCANNER_AUTO_CLOSE&&HIWORD(wp)==BN_CLICKED){HWND checkbox=GetDlgItem(hwnd,ID_SCANNER_AUTO_CLOSE);wstring value=SendMessageW(checkbox,BM_GETCHECK,0,0)==BST_CHECKED?L"1":L"0";if(!saveSetting("scanner_auto_close",value))SendMessageW(checkbox,BM_SETCHECK,setting("scanner_auto_close",L"0")==L"1"?BST_CHECKED:BST_UNCHECKED,0);return 0;}if(msg==WM_COMMAND&&LOWORD(wp)==IDCANCEL){DestroyWindow(hwnd);return 0;}if(msg==WM_CTLCOLORSTATIC||msg==WM_CTLCOLORBTN)return themeControlColor(msg,wp);if(msg==WM_ERASEBKGND)return eraseThemedBackground(hwnd,wp);if(msg==WM_MEASUREITEM&&measureOwnerItem((MEASUREITEMSTRUCT*)lp))return TRUE;if(msg==WM_DRAWITEM&&drawOwnerItem((DRAWITEMSTRUCT*)lp))return TRUE;if(msg==WM_CLOSE){DestroyWindow(hwnd);return 0;}return DefWindowProcW(hwnd,msg,wp,lp);
}

void scanBarcode(){
    ScannerData data;RECT r{};GetWindowRect(g_main,&r);HWND window=CreateWindowExW(WS_EX_DLGMODALFRAME,L"LogSScanner",L"Barcode scannen",WS_CAPTION|WS_SYSMENU,r.left+(r.right-r.left-540)/2,r.top+(r.bottom-r.top-300)/2,540,300,g_main,nullptr,g_instance,&data);beginModal(g_main,window);ShowWindow(window,SW_SHOW);SetFocus(window);MSG message{};while(IsWindow(window)){BOOL result=GetMessageW(&message,nullptr,0,0);if(result<=0){if(IsWindow(window))DestroyWindow(window);PostQuitMessage(result==0?(int)message.wParam:1);break;}if(message.message==WM_CHAR&&(message.hwnd==window||IsChild(window,message.hwnd))){SendMessageW(window,WM_CHAR,message.wParam,message.lParam);continue;}if(message.message>=WM_KEYFIRST&&message.message<=WM_KEYLAST){TranslateMessage(&message);DispatchMessageW(&message);continue;}if(!IsDialogMessageW(window,&message)){TranslateMessage(&message);DispatchMessageW(&message);}}endModal(g_main,window);SetForegroundWindow(g_main);
}

BOOL CALLBACK ThemeChild(HWND child, LPARAM) {
    wchar_t cls[40]{}; GetClassNameW(child, cls, 40);
    const wchar_t* darkTheme=L"DarkMode_Explorer";
    if(lstrcmpiW(cls,L"Edit")==0||lstrcmpiW(cls,WC_COMBOBOXW)==0)darkTheme=L"DarkMode_CFD";
    SetWindowTheme(child,g_darkMode?darkTheme:nullptr,nullptr);
    if (lstrcmpiW(cls, WC_LISTVIEWW) == 0) {
        COLORREF bg = g_darkMode ? RGB(45,47,51) : RGB(255,255,255);
        ListView_SetBkColor(child, bg); ListView_SetTextBkColor(child, bg);
        ListView_SetTextColor(child, g_darkMode ? RGB(235,235,235) : RGB(20,20,20));
        HWND header=ListView_GetHeader(child);SetWindowTheme(header,g_darkMode?L"":nullptr,nullptr);
        int columns=Header_GetItemCount(header);
        for(int i=0;i<columns;i++){HDITEMW item{};item.mask=HDI_FORMAT;Header_GetItem(header,i,&item);if(g_darkMode)item.fmt|=HDF_OWNERDRAW;else item.fmt&=~HDF_OWNERDRAW;Header_SetItem(header,i,&item);}
    } else if (lstrcmpiW(cls, L"Button") == 0) {
        SetWindowTheme(child,g_darkMode?L"DarkMode_Explorer":nullptr,nullptr);
    }
    RedrawWindow(child,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_FRAME|RDW_ALLCHILDREN);
    InvalidateRect(child, nullptr, TRUE);
    return TRUE;
}

void applyWindowFrameTheme(HWND window) {
    SetWindowTheme(window,g_darkMode?L"DarkMode_Explorer":nullptr,nullptr);
    using DwmSetWindowAttributeFn=HRESULT(WINAPI*)(HWND,DWORD,LPCVOID,DWORD);
    HMODULE dwm=LoadLibraryW(L"dwmapi.dll");
    if(dwm){auto setDwm=(DwmSetWindowAttributeFn)GetProcAddress(dwm,"DwmSetWindowAttribute");if(setDwm){BOOL dark=g_darkMode;setDwm(window,20,&dark,sizeof(dark));setDwm(window,19,&dark,sizeof(dark));}FreeLibrary(dwm);}
}

LRESULT eraseThemedBackground(HWND hwnd, WPARAM wp) {
    RECT area;GetClientRect(hwnd,&area);FillRect((HDC)wp,&area,g_bgBrush);return 1;
}

void applyTheme() {
    g_darkMode = setting("dark_mode", L"0") == L"1";
    g_bgBrush=g_darkMode?g_darkBgBrush:g_lightBgBrush;
    g_panelBrush=g_darkMode?g_darkPanelBrush:g_lightPanelBrush;
    if (g_main) {
        SetClassLongPtrW(g_main, GCLP_HBRBACKGROUND, (LONG_PTR)g_bgBrush);
        applyWindowFrameTheme(g_main);
        if(g_databaseMenu){MENUINFO menuInfo{};menuInfo.cbSize=sizeof(menuInfo);menuInfo.fMask=MIM_BACKGROUND;menuInfo.hbrBack=g_bgBrush;SetMenuInfo(g_databaseMenu,&menuInfo);}
        EnumChildWindows(g_main, ThemeChild, 0);
        InvalidateRect(g_main, nullptr, TRUE);
        DrawMenuBar(g_main);
    }
}

void applyCustomLabels(){
    if(!g_main)return;
    wstring location=setting("label_location_enabled",L"0")==L"1"?setting("label_location",L"Lagerort"):L"Lagerort";
    wstring shelf=setting("label_shelf_enabled",L"0")==L"1"?setting("label_shelf",L"Fach"):L"Fach";
    wstring barcode=setting("label_barcode_enabled",L"0")==L"1"?setting("label_barcode",L"EAN / UPC / GTIN"):L"EAN / UPC / GTIN";
    setText(g_main,3003,barcode);setText(g_main,3004,location);setText(g_main,3007,shelf);
    LVCOLUMNW column{};column.mask=LVCF_TEXT;column.pszText=location.data();ListView_SetColumn(GetDlgItem(g_main,ID_LIST),2,&column);
    column.pszText=shelf.data();ListView_SetColumn(GetDlgItem(g_main,ID_LIST),3,&column);
    wstring hint=L"Suchen: Artikel, "+barcode+L", "+location+L" …";SendMessageW(GetDlgItem(g_main,ID_SEARCH),EM_SETCUEBANNER,TRUE,(LPARAM)hint.c_str());
}

LRESULT themeControlColor(UINT msg, WPARAM wp) {
    HDC dc = (HDC)wp;
    SetTextColor(dc, g_darkMode ? RGB(235,235,235) : RGB(20,20,20));
    bool panel = msg == WM_CTLCOLOREDIT || msg == WM_CTLCOLORLISTBOX;
    COLORREF bg = g_darkMode ? (panel ? RGB(45,47,51) : RGB(31,33,36))
                             : (panel ? RGB(255,255,255) : RGB(246,247,249));
    SetBkColor(dc, bg);
    return (LRESULT)(panel ? g_panelBrush : g_bgBrush);
}

LRESULT CALLBACK SettingsWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        addControl(L"STATIC",L"Darstellung",SS_LEFT,4101,hwnd);
        addControl(L"BUTTON",L"Dark Mode",WS_TABSTOP|BS_AUTOCHECKBOX,ID_SETTING_DARK,hwnd);
        addControl(L"STATIC",L"Währungssymbol",SS_LEFT,4102,hwnd);
        { HWND currency=addControl(WC_COMBOBOXW,L"",WS_TABSTOP|CBS_DROPDOWNLIST|CBS_OWNERDRAWFIXED|CBS_HASSTRINGS,ID_SETTING_CURRENCY,hwnd);
          for(const wchar_t* symbol:{L"€",L"$",L"£",L"CHF",L"¥",L"kr"})SendMessageW(currency,CB_ADDSTRING,0,(LPARAM)symbol);
          selectCombo(currency,setting("currency",L"€")); }
        addControl(L"STATIC",L"Standard-Einheit",SS_LEFT,4103,hwnd);
        { HWND unit=addControl(WC_COMBOBOXW,L"",WS_TABSTOP|CBS_DROPDOWNLIST|CBS_OWNERDRAWFIXED|CBS_HASSTRINGS,ID_SETTING_UNIT,hwnd);
          fillUnitCombo(unit,setting("default_unit",L"Stück")); }
        addControl(L"STATIC",L"Bezeichnungen",SS_LEFT,4110,hwnd);
        addControl(L"BUTTON",L"EAN / UPC / GTIN",WS_TABSTOP|BS_AUTOCHECKBOX,ID_SETTING_LABEL_BARCODE_ENABLED,hwnd);addControl(L"EDIT",setting("label_barcode",L"EAN / UPC / GTIN").c_str(),WS_TABSTOP|ES_AUTOHSCROLL,ID_SETTING_LABEL_BARCODE,hwnd,WS_EX_CLIENTEDGE);
        addControl(L"BUTTON",L"Lagerort",WS_TABSTOP|BS_AUTOCHECKBOX,ID_SETTING_LABEL_LOCATION_ENABLED,hwnd);addControl(L"EDIT",setting("label_location",L"Lagerort").c_str(),WS_TABSTOP|ES_AUTOHSCROLL,ID_SETTING_LABEL_LOCATION,hwnd,WS_EX_CLIENTEDGE);
        addControl(L"BUTTON",L"Fach",WS_TABSTOP|BS_AUTOCHECKBOX,ID_SETTING_LABEL_SHELF_ENABLED,hwnd);addControl(L"EDIT",setting("label_shelf",L"Fach").c_str(),WS_TABSTOP|ES_AUTOHSCROLL,ID_SETTING_LABEL_SHELF,hwnd,WS_EX_CLIENTEDGE);
        addControl(L"BUTTON",L"Übernehmen",WS_TABSTOP|BS_OWNERDRAW,IDOK,hwnd);
        addControl(L"BUTTON",L"Schließen",WS_TABSTOP|BS_OWNERDRAW,IDCANCEL,hwnd);
        SendMessageW(GetDlgItem(hwnd,ID_SETTING_DARK),BM_SETCHECK,g_darkMode?BST_CHECKED:BST_UNCHECKED,0);
        SendMessageW(GetDlgItem(hwnd,ID_SETTING_LABEL_BARCODE_ENABLED),BM_SETCHECK,setting("label_barcode_enabled",L"0")==L"1"?BST_CHECKED:BST_UNCHECKED,0);
        SendMessageW(GetDlgItem(hwnd,ID_SETTING_LABEL_LOCATION_ENABLED),BM_SETCHECK,setting("label_location_enabled",L"0")==L"1"?BST_CHECKED:BST_UNCHECKED,0);
        SendMessageW(GetDlgItem(hwnd,ID_SETTING_LABEL_SHELF_ENABLED),BM_SETCHECK,setting("label_shelf_enabled",L"0")==L"1"?BST_CHECKED:BST_UNCHECKED,0);
        MoveWindow(GetDlgItem(hwnd,4101),18,16,360,20,TRUE);MoveWindow(GetDlgItem(hwnd,ID_SETTING_DARK),18,40,180,26,TRUE);
        MoveWindow(GetDlgItem(hwnd,4102),18,82,150,20,TRUE);MoveWindow(GetDlgItem(hwnd,ID_SETTING_CURRENCY),190,78,190,180,TRUE);
        MoveWindow(GetDlgItem(hwnd,4103),18,120,150,20,TRUE);MoveWindow(GetDlgItem(hwnd,ID_SETTING_UNIT),190,116,190,220,TRUE);
        MoveWindow(GetDlgItem(hwnd,4110),18,162,400,20,TRUE);
        MoveWindow(GetDlgItem(hwnd,ID_SETTING_LABEL_BARCODE_ENABLED),18,188,164,27,TRUE);MoveWindow(GetDlgItem(hwnd,ID_SETTING_LABEL_BARCODE),190,188,238,27,TRUE);
        MoveWindow(GetDlgItem(hwnd,ID_SETTING_LABEL_LOCATION_ENABLED),18,226,164,27,TRUE);MoveWindow(GetDlgItem(hwnd,ID_SETTING_LABEL_LOCATION),190,226,238,27,TRUE);
        MoveWindow(GetDlgItem(hwnd,ID_SETTING_LABEL_SHELF_ENABLED),18,264,164,27,TRUE);MoveWindow(GetDlgItem(hwnd,ID_SETTING_LABEL_SHELF),190,264,238,27,TRUE);
        MoveWindow(GetDlgItem(hwnd,IDOK),220,310,100,30,TRUE);MoveWindow(GetDlgItem(hwnd,IDCANCEL),328,310,100,30,TRUE);
        applyWindowFrameTheme(hwnd);EnumChildWindows(hwnd, ThemeChild, 0); return 0;
    case WM_COMMAND:
        if(LOWORD(wp)==ID_SETTING_DARK&&HIWORD(wp)==BN_CLICKED){
            HWND darkControl=GetDlgItem(hwnd,ID_SETTING_DARK);bool enabled=SendMessageW(darkControl,BM_GETCHECK,0,0)==BST_CHECKED;
            std::vector<std::pair<string,wstring>> themeSettings;std::optional<bool> observedSystemDark;if(!g_runningUnderWine){bool systemDark=false;if(windowsSystemDark(systemDark)){observedSystemDark=systemDark;themeSettings.push_back({"windows_theme_seen",systemDark?L"1":L"0"});}themeSettings.push_back({"windows_theme_override",L"1"});}themeSettings.push_back({"dark_mode",enabled?L"1":L"0"});
            if(!saveSettingsBatch(themeSettings,hwnd)){SendMessageW(darkControl,BM_SETCHECK,g_darkMode?BST_CHECKED:BST_UNCHECKED,0);return 0;}
            if(observedSystemDark)g_windowsSystemDark=*observedSystemDark;
            applyTheme();SetClassLongPtrW(hwnd,GCLP_HBRBACKGROUND,(LONG_PTR)g_bgBrush);applyWindowFrameTheme(hwnd);EnumChildWindows(hwnd,ThemeChild,0);RedrawWindow(hwnd,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_FRAME|RDW_ALLCHILDREN);return 0;
        }
        if (LOWORD(wp)==IDOK) {
            wstring currency=getText(hwnd,ID_SETTING_CURRENCY),unit=getText(hwnd,ID_SETTING_UNIT);
            if(currency.empty()||unit.empty()){showNotice(hwnd,L"Einstellungen",L"Währung und Standard-Einheit dürfen nicht leer sein.");return 0;}
            wstring location=getText(hwnd,ID_SETTING_LABEL_LOCATION),shelf=getText(hwnd,ID_SETTING_LABEL_SHELF),barcode=getText(hwnd,ID_SETTING_LABEL_BARCODE);
            if(location.empty())location=L"Lagerort";if(shelf.empty())shelf=L"Fach";if(barcode.empty())barcode=L"EAN / UPC / GTIN";
            std::vector<std::pair<string,wstring>> changedSettings={
                {"currency",currency},{"default_unit",unit},
                {"dark_mode",SendMessageW(GetDlgItem(hwnd,ID_SETTING_DARK),BM_GETCHECK,0,0)==BST_CHECKED?L"1":L"0"},
                {"label_location",location},{"label_shelf",shelf},{"label_barcode",barcode},
                {"label_location_enabled",SendMessageW(GetDlgItem(hwnd,ID_SETTING_LABEL_LOCATION_ENABLED),BM_GETCHECK,0,0)==BST_CHECKED?L"1":L"0"},
                {"label_shelf_enabled",SendMessageW(GetDlgItem(hwnd,ID_SETTING_LABEL_SHELF_ENABLED),BM_GETCHECK,0,0)==BST_CHECKED?L"1":L"0"},
                {"label_barcode_enabled",SendMessageW(GetDlgItem(hwnd,ID_SETTING_LABEL_BARCODE_ENABLED),BM_GETCHECK,0,0)==BST_CHECKED?L"1":L"0"}
            };
            bool settingsSaved=saveSettingsBatch(changedSettings,hwnd);
            applyTheme();SetClassLongPtrW(hwnd,GCLP_HBRBACKGROUND,(LONG_PTR)g_bgBrush);applyWindowFrameTheme(hwnd);EnumChildWindows(hwnd,ThemeChild,0);RedrawWindow(hwnd,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_FRAME|RDW_ALLCHILDREN);
            applyCustomLabels();
            setText(g_main,ID_STATUS,settingsSaved?L"Einstellungen gespeichert":L"Einstellungen konnten nicht vollständig gespeichert werden");return 0;
        }
        if (LOWORD(wp)==IDCANCEL){DestroyWindow(hwnd);return 0;}break;
    case WM_CTLCOLORSTATIC:case WM_CTLCOLOREDIT:case WM_CTLCOLORBTN:case WM_CTLCOLORLISTBOX:
        return themeControlColor(msg,wp);
    case WM_ERASEBKGND:return eraseThemedBackground(hwnd,wp);
    case WM_MEASUREITEM:if(measureOwnerItem((MEASUREITEMSTRUCT*)lp))return TRUE;break;
    case WM_DRAWITEM:if(drawOwnerItem((DRAWITEMSTRUCT*)lp))return TRUE;break;
    case WM_CLOSE:DestroyWindow(hwnd);return 0;
    case WM_DESTROY:return 0;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}

void settingsDialog(){
    RECT r;GetWindowRect(g_main,&r);
    HWND w=CreateWindowExW(WS_EX_DLGMODALFRAME,L"LogSSettings",L"Einstellungen",WS_CAPTION|WS_SYSMENU,
      r.left+(r.right-r.left-466)/2,r.top+(r.bottom-r.top-390)/2,466,390,g_main,nullptr,g_instance,nullptr);
    beginModal(g_main,w);ShowWindow(w,SW_SHOW);runModalLoop(w);
    endModal(g_main,w);SetForegroundWindow(g_main);
}

struct CodeDialogData{wstring initialCode,fieldName;};

void updateCodeTabControls(HWND hwnd){
    bool a4=TabCtrl_GetCurSel(GetDlgItem(hwnd,ID_CODE_TAB))==1;
    const wchar_t* fieldName=(const wchar_t*)GetPropW(hwnd,L"LogSCodeFieldName");setText(hwnd,4302,a4?L"Startwert":(fieldName?fieldName:L"Lagernummer"));
    for(int id:{4303,(int)ID_CODE_COUNT,4304,(int)ID_CODE_DIRECTION,(int)ID_CODE_AUTOSCALE,(int)ID_CODE_FIT})ShowWindow(GetDlgItem(hwnd,id),a4?SW_SHOW:SW_HIDE);
    ShowWindow(GetDlgItem(hwnd,4306),a4?SW_SHOW:SW_HIDE);ShowWindow(GetDlgItem(hwnd,ID_CODE_HEIGHT),a4?SW_SHOW:SW_HIDE);
    ShowWindow(GetDlgItem(hwnd,4307),a4?SW_HIDE:SW_SHOW);ShowWindow(GetDlgItem(hwnd,ID_CODE_TAPE_WIDTH),a4?SW_HIDE:SW_SHOW);
    ShowWindow(GetDlgItem(hwnd,4309),a4?SW_HIDE:SW_SHOW);ShowWindow(GetDlgItem(hwnd,ID_CODE_MAX_LENGTH),a4?SW_HIDE:SW_SHOW);
    ShowWindow(GetDlgItem(hwnd,4310),a4?SW_HIDE:SW_SHOW);ShowWindow(GetDlgItem(hwnd,ID_CODE_PRINTER),a4?SW_HIDE:SW_SHOW);
    ShowWindow(GetDlgItem(hwnd,ID_CODE_PRINTER_STATUS),a4?SW_HIDE:SW_SHOW);
    ShowWindow(GetDlgItem(hwnd,ID_CODE_FIT_TAPE),a4?SW_HIDE:SW_SHOW);
    bool qr=SendMessageW(GetDlgItem(hwnd,ID_CODE_KIND),CB_GETCURSEL,0,0)==1;
    ShowWindow(GetDlgItem(hwnd,4308),qr?SW_SHOW:SW_HIDE);ShowWindow(GetDlgItem(hwnd,ID_CODE_QR_TEXT_POSITION),qr?SW_SHOW:SW_HIDE);
    MoveWindow(GetDlgItem(hwnd,4308),20,a4?204:140,130,20,TRUE);MoveWindow(GetDlgItem(hwnd,ID_CODE_QR_TEXT_POSITION),155,a4?200:136,240,150,TRUE);
    MoveWindow(GetDlgItem(hwnd,ID_CODE_PREVIEW),20,a4?264:234,560,a4?230:260,TRUE);
    ShowWindow(GetDlgItem(hwnd,4305),a4?SW_SHOW:SW_HIDE);ShowWindow(GetDlgItem(hwnd,ID_CODE_WIDTH),a4?SW_SHOW:SW_HIDE);
    EnableWindow(GetDlgItem(hwnd,ID_CODE_LOGO),qr);
    EnableWindow(GetDlgItem(hwnd,ID_CODE_FIT_TAPE),!a4);
    if(!qr)SendMessageW(GetDlgItem(hwnd,ID_CODE_LOGO),BM_SETCHECK,BST_UNCHECKED,0);
    InvalidateRect(hwnd,nullptr,TRUE);
}

LRESULT CALLBACK CodeGeneratorWndProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
    switch(msg){
    case WM_CREATE:{
        CodeDialogData* dialogData=(CodeDialogData*)((CREATESTRUCTW*)lp)->lpCreateParams;const wchar_t* initialCode=dialogData?dialogData->initialCode.c_str():L"";const wchar_t* fieldName=dialogData?dialogData->fieldName.c_str():L"Lagernummer";SetPropW(hwnd,L"LogSCodeFieldName",(HANDLE)fieldName);
        HWND tab=addControl(WC_TABCONTROLW,L"",WS_TABSTOP|TCS_OWNERDRAWFIXED,ID_CODE_TAB,hwnd);SetWindowSubclass(tab,TabBackgroundProc,1,0);TCITEMW ti{};ti.mask=TCIF_TEXT;ti.pszText=(LPWSTR)L"Etikettendrucker";TabCtrl_InsertItem(tab,0,&ti);ti.pszText=(LPWSTR)L"DIN A4";TabCtrl_InsertItem(tab,1,&ti);
        addControl(L"STATIC",L"Codeart",SS_LEFT,4301,hwnd);HWND kind=addControl(WC_COMBOBOXW,L"",WS_TABSTOP|CBS_DROPDOWNLIST|CBS_OWNERDRAWFIXED|CBS_HASSTRINGS,ID_CODE_KIND,hwnd);SendMessageW(kind,CB_ADDSTRING,0,(LPARAM)L"Code 128");SendMessageW(kind,CB_ADDSTRING,0,(LPARAM)L"QR-Code");SendMessageW(kind,CB_SETCURSEL,0,0);
        addControl(L"STATIC",fieldName,SS_LEFT,4302,hwnd);addControl(L"EDIT",initialCode,WS_TABSTOP|ES_AUTOHSCROLL,ID_CODE_START,hwnd,WS_EX_CLIENTEDGE);
        addControl(L"STATIC",L"Anzahl",SS_LEFT,4303,hwnd);addControl(L"EDIT",L"1",WS_TABSTOP|ES_NUMBER,ID_CODE_COUNT,hwnd,WS_EX_CLIENTEDGE);
        addControl(L"STATIC",L"Zählrichtung",SS_LEFT,4304,hwnd);HWND direction=addControl(WC_COMBOBOXW,L"",WS_TABSTOP|CBS_DROPDOWNLIST|CBS_OWNERDRAWFIXED|CBS_HASSTRINGS,ID_CODE_DIRECTION,hwnd);SendMessageW(direction,CB_ADDSTRING,0,(LPARAM)L"Aufwärts");SendMessageW(direction,CB_ADDSTRING,0,(LPARAM)L"Abwärts");SendMessageW(direction,CB_SETCURSEL,0,0);
        addControl(L"STATIC",L"Breite (mm)",SS_LEFT,4305,hwnd);addControl(L"EDIT",L"50",WS_TABSTOP|ES_AUTOHSCROLL,ID_CODE_WIDTH,hwnd,WS_EX_CLIENTEDGE);
        addControl(L"STATIC",L"Höhe (mm)",SS_LEFT,4306,hwnd);addControl(L"EDIT",L"30",WS_TABSTOP|ES_AUTOHSCROLL,ID_CODE_HEIGHT,hwnd,WS_EX_CLIENTEDGE);
        addControl(L"STATIC",L"Etikettenlänge (mm)",SS_LEFT,4309,hwnd);addControl(L"EDIT",L"50",WS_TABSTOP|ES_AUTOHSCROLL,ID_CODE_MAX_LENGTH,hwnd,WS_EX_CLIENTEDGE);
        addControl(L"STATIC",L"Drucker",SS_LEFT,4310,hwnd);HWND printer=addControl(WC_COMBOBOXW,L"",WS_TABSTOP|CBS_DROPDOWNLIST|CBS_OWNERDRAWFIXED|CBS_HASSTRINGS,ID_CODE_PRINTER,hwnd);SendMessageW(printer,CB_ADDSTRING,0,(LPARAM)L"Brother");SendMessageW(printer,CB_SETCURSEL,0,0);
        bool printerConnected=brotherPrinterConnected();SetPropW(hwnd,L"LogSBrotherConnected",(HANDLE)(INT_PTR)(printerConnected?1:0));addControl(L"STATIC",printerConnected?L"● Etikettendrucker verbunden":L"● Kein Etikettendrucker verbunden",SS_LEFT,ID_CODE_PRINTER_STATUS,hwnd);
        addControl(L"STATIC",L"Bandbreite",SS_LEFT,4307,hwnd);HWND tapeWidth=addControl(WC_COMBOBOXW,L"",WS_TABSTOP|CBS_DROPDOWNLIST|CBS_OWNERDRAWFIXED|CBS_HASSTRINGS,ID_CODE_TAPE_WIDTH,hwnd);for(const wchar_t* width:{L"6 mm",L"9 mm",L"12 mm",L"24 mm"})SendMessageW(tapeWidth,CB_ADDSTRING,0,(LPARAM)width);SendMessageW(tapeWidth,CB_SETCURSEL,2,0);
        addControl(L"STATIC",L"Nummernposition",SS_LEFT,4308,hwnd);HWND textPosition=addControl(WC_COMBOBOXW,L"",WS_TABSTOP|CBS_DROPDOWNLIST|CBS_OWNERDRAWFIXED|CBS_HASSTRINGS,ID_CODE_QR_TEXT_POSITION,hwnd);SendMessageW(textPosition,CB_ADDSTRING,0,(LPARAM)L"Unter dem QR-Code");SendMessageW(textPosition,CB_ADDSTRING,0,(LPARAM)L"Rechts neben dem QR-Code");SendMessageW(textPosition,CB_SETCURSEL,0,0);
        addControl(L"BUTTON",L"An Etikettenbreite anpassen",WS_TABSTOP|BS_AUTOCHECKBOX,ID_CODE_FIT_TAPE,hwnd);
        addControl(L"BUTTON",L"Auf DIN A4 automatisch skalieren",WS_TABSTOP|BS_AUTOCHECKBOX,ID_CODE_AUTOSCALE,hwnd);
        addControl(L"BUTTON",L"Logo in QR-Code einfügen",WS_TABSTOP|BS_AUTOCHECKBOX,ID_CODE_LOGO,hwnd);
        addControl(L"STATIC",L"",SS_LEFT,ID_CODE_FIT,hwnd);
        HWND preview=addControl(L"STATIC",L"",SS_NOTIFY,ID_CODE_PREVIEW,hwnd,WS_EX_CLIENTEDGE);SetWindowSubclass(preview,CodePreviewProc,1,0);
        addControl(L"BUTTON",L"Drucken…",WS_TABSTOP|BS_OWNERDRAW,ID_CODE_PRINT,hwnd);addControl(L"BUTTON",L"Schließen",WS_TABSTOP|BS_OWNERDRAW,IDCANCEL,hwnd);
        MoveWindow(tab,14,14,572,34,TRUE);int x=20,y=62;for(int i=0;i<3;i++){int label=4301+i,id=i==0?ID_CODE_KIND:i==1?ID_CODE_START:ID_CODE_COUNT;MoveWindow(GetDlgItem(hwnd,label),x,y,105,20,TRUE);MoveWindow(GetDlgItem(hwnd,id),130,y-4,170,i==0?180:27,TRUE);y+=39;}y=62;for(int i=0;i<3;i++){int label=4304+i,id=i==0?ID_CODE_DIRECTION:i==1?ID_CODE_WIDTH:ID_CODE_HEIGHT;MoveWindow(GetDlgItem(hwnd,label),320,y,120,20,TRUE);MoveWindow(GetDlgItem(hwnd,id),450,y-4,130,i==0?180:27,TRUE);y+=39;}
        MoveWindow(GetDlgItem(hwnd,4307),400,140,80,20,TRUE);MoveWindow(GetDlgItem(hwnd,ID_CODE_TAPE_WIDTH),485,136,95,150,TRUE);
        MoveWindow(GetDlgItem(hwnd,4309),320,101,145,20,TRUE);MoveWindow(GetDlgItem(hwnd,ID_CODE_MAX_LENGTH),470,97,110,27,TRUE);
        MoveWindow(GetDlgItem(hwnd,ID_CODE_KIND),130,58,140,180,TRUE);MoveWindow(GetDlgItem(hwnd,4310),290,62,55,20,TRUE);MoveWindow(GetDlgItem(hwnd,ID_CODE_PRINTER),350,58,230,150,TRUE);
        MoveWindow(GetDlgItem(hwnd,4308),20,140,130,20,TRUE);MoveWindow(GetDlgItem(hwnd,ID_CODE_QR_TEXT_POSITION),155,136,240,150,TRUE);
        MoveWindow(GetDlgItem(hwnd,ID_CODE_FIT_TAPE),20,174,260,26,TRUE);
        MoveWindow(GetDlgItem(hwnd,ID_CODE_AUTOSCALE),20,174,300,26,TRUE);MoveWindow(GetDlgItem(hwnd,ID_CODE_LOGO),320,174,240,26,TRUE);MoveWindow(GetDlgItem(hwnd,ID_CODE_FIT),20,234,560,22,TRUE);MoveWindow(GetDlgItem(hwnd,ID_CODE_PRINTER_STATUS),20,204,560,22,TRUE);
        MoveWindow(preview,20,234,560,260,TRUE);MoveWindow(GetDlgItem(hwnd,ID_CODE_PRINT),390,508,90,30,TRUE);MoveWindow(GetDlgItem(hwnd,IDCANCEL),490,508,90,30,TRUE);SetTimer(hwnd,20,1000,nullptr);updateCodeFit(hwnd);updateCodeTabControls(hwnd);applyWindowFrameTheme(hwnd);EnumChildWindows(hwnd,ThemeChild,0);return 0;}
    case WM_COMMAND:
        if(LOWORD(wp)==ID_CODE_PRINT){printCodes(hwnd);return 0;}if(LOWORD(wp)==IDCANCEL){DestroyWindow(hwnd);return 0;}
        if(LOWORD(wp)==ID_CODE_KIND&&HIWORD(wp)==CBN_SELCHANGE)updateCodeTabControls(hwnd);
        if(HIWORD(wp)==EN_CHANGE||HIWORD(wp)==CBN_SELCHANGE||LOWORD(wp)==ID_CODE_AUTOSCALE||LOWORD(wp)==ID_CODE_LOGO||LOWORD(wp)==ID_CODE_FIT_TAPE){updateCodeFit(hwnd);InvalidateRect(GetDlgItem(hwnd,ID_CODE_PREVIEW),nullptr,TRUE);}break;
    case WM_NOTIFY:if(((NMHDR*)lp)->idFrom==ID_CODE_TAB&&((NMHDR*)lp)->code==TCN_SELCHANGE){updateCodeTabControls(hwnd);updateCodeFit(hwnd);InvalidateRect(GetDlgItem(hwnd,ID_CODE_PREVIEW),nullptr,TRUE);return 0;}break;
    case WM_TIMER:if(wp==20){bool connected=brotherPrinterConnected(),previous=(INT_PTR)GetPropW(hwnd,L"LogSBrotherConnected")!=0;if(connected!=previous){SetPropW(hwnd,L"LogSBrotherConnected",(HANDLE)(INT_PTR)(connected?1:0));setText(hwnd,ID_CODE_PRINTER_STATUS,connected?L"● Etikettendrucker verbunden":L"● Kein Etikettendrucker verbunden");InvalidateRect(GetDlgItem(hwnd,ID_CODE_PRINTER_STATUS),nullptr,TRUE);}return 0;}break;
    case WM_CTLCOLORSTATIC:if(GetDlgCtrlID((HWND)lp)==ID_CODE_PRINTER_STATUS){SetBkMode((HDC)wp,TRANSPARENT);SetTextColor((HDC)wp,(INT_PTR)GetPropW(hwnd,L"LogSBrotherConnected")?RGB(55,190,90):RGB(210,85,85));return (LRESULT)g_bgBrush;}return themeControlColor(msg,wp);case WM_CTLCOLOREDIT:case WM_CTLCOLORBTN:case WM_CTLCOLORLISTBOX:return themeControlColor(msg,wp);
    case WM_ERASEBKGND:return eraseThemedBackground(hwnd,wp);case WM_MEASUREITEM:if(measureOwnerItem((MEASUREITEMSTRUCT*)lp))return TRUE;break;case WM_DRAWITEM:if(drawOwnerItem((DRAWITEMSTRUCT*)lp))return TRUE;break;
    case WM_APP+10:showNotice(hwnd,L"Drucken",wp?L"Der Druckauftrag wurde an den Drucker übergeben.":L"Der Druckauftrag ist fehlgeschlagen.",!wp);return 0;
    case WM_CLOSE:DestroyWindow(hwnd);return 0;case WM_DESTROY:KillTimer(hwnd,20);RemovePropW(hwnd,L"LogSBrotherConnected");return 0;}
    return DefWindowProcW(hwnd,msg,wp,lp);
}

void codeGeneratorDialog(const wstring& initialCode,const wchar_t* fieldName){
    if(initialCode.empty()){showNotice(g_main,L"Code erzeugen",L"Bitte geben Sie zuerst einen Wert für \""+wstring(fieldName)+L"\" ein.");return;}
    CodeDialogData data{initialCode,fieldName};RECT r;GetWindowRect(g_main,&r);HWND w=CreateWindowExW(WS_EX_DLGMODALFRAME,L"LogSCodeGenerator",L"Barcode / QR-Code erzeugen",WS_CAPTION|WS_SYSMENU,r.left+80,r.top+40,620,625,g_main,nullptr,g_instance,&data);beginModal(g_main,w);ShowWindow(w,SW_SHOW);runModalLoop(w);endModal(g_main,w);SetForegroundWindow(g_main);
}

LRESULT CALLBACK AboutWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch(msg){
    case WM_CREATE:{
        HWND icon=addControl(L"STATIC",L"",SS_ICON,4201,hwnd);
        HICON large=(HICON)LoadImageW(g_instance,MAKEINTRESOURCE(IDI_LOGS),IMAGE_ICON,48,48,LR_DEFAULTCOLOR|LR_SHARED);SendMessageW(icon,STM_SETICON,(WPARAM)large,0);
        HWND title=addControl(L"STATIC",L"LogS – Lagerorganisation Software",SS_LEFT,4202,hwnd);SendMessageW(title,WM_SETFONT,(WPARAM)g_titleFont,TRUE);
        wstring details=L"Version: "+wstring(LOGS_VERSION_TEXT)+L"\nAutor: @pms\n\nBuild: Vorkompilierte Release-Version\nArchitektur: 64-Bit (x64)\nSQLite: "+utf8ToWide(sqlite3_libversion())+L"\nKompiliert: "+utf8ToWide(__DATE__ " " __TIME__);
        addControl(L"STATIC",details.c_str(),SS_LEFT,4203,hwnd);
        addControl(L"BUTTON",L"OK",WS_TABSTOP|BS_OWNERDRAW,IDOK,hwnd);
        MoveWindow(icon,20,20,52,52,TRUE);MoveWindow(title,88,22,350,28,TRUE);MoveWindow(GetDlgItem(hwnd,4203),88,58,350,130,TRUE);MoveWindow(GetDlgItem(hwnd,IDOK),354,198,84,30,TRUE);
        applyWindowFrameTheme(hwnd);EnumChildWindows(hwnd,ThemeChild,0);return 0;}
    case WM_COMMAND:if(LOWORD(wp)==IDOK){DestroyWindow(hwnd);return 0;}break;
    case WM_CTLCOLORSTATIC:case WM_CTLCOLORBTN:return themeControlColor(msg,wp);
    case WM_ERASEBKGND:return eraseThemedBackground(hwnd,wp);
    case WM_MEASUREITEM:if(measureOwnerItem((MEASUREITEMSTRUCT*)lp))return TRUE;break;
    case WM_DRAWITEM:if(drawOwnerItem((DRAWITEMSTRUCT*)lp))return TRUE;break;
    case WM_CLOSE:DestroyWindow(hwnd);return 0;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}

void aboutDialog(){
    RECT r;GetWindowRect(g_main,&r);
    HWND w=CreateWindowExW(WS_EX_DLGMODALFRAME,L"LogSAbout",L"Über LogS",WS_CAPTION|WS_SYSMENU,
      r.left+(r.right-r.left-476)/2,r.top+(r.bottom-r.top-280)/2,476,280,g_main,nullptr,g_instance,nullptr);
    beginModal(g_main,w);ShowWindow(w,SW_SHOW);runModalLoop(w);
    endModal(g_main,w);SetForegroundWindow(g_main);
}

void createMenus(HWND hwnd){
    g_databaseMenu=CreatePopupMenu();
    AppendMenuW(g_databaseMenu,MF_OWNERDRAW,ID_DB_EXPORT,(LPCWSTR)L"Als CSV exportieren…");
    AppendMenuW(g_databaseMenu,MF_OWNERDRAW|MF_DISABLED,0,nullptr);
    AppendMenuW(g_databaseMenu,MF_OWNERDRAW,ID_DB_NEW,(LPCWSTR)L"Neue Datenbank erstellen…");
    AppendMenuW(g_databaseMenu,MF_OWNERDRAW,ID_DB_BACKUP,(LPCWSTR)L"Datenbank speichern unter…");
    AppendMenuW(g_databaseMenu,MF_OWNERDRAW,ID_DB_RESTORE,(LPCWSTR)L"Datenbank öffnen…");
    AppendMenuW(g_databaseMenu,MF_OWNERDRAW|MF_DISABLED,0,nullptr);
    AppendMenuW(g_databaseMenu,MF_OWNERDRAW,ID_EXIT,(LPCWSTR)L"Beenden");
    addControl(L"BUTTON",L"Datenbank",WS_TABSTOP|BS_OWNERDRAW,ID_TOP_DATABASE,hwnd);
    addControl(L"BUTTON",L"Einstellungen",WS_TABSTOP|BS_OWNERDRAW,ID_SETTINGS,hwnd);
    addControl(L"BUTTON",L"Info",WS_TABSTOP|BS_OWNERDRAW,ID_ABOUT,hwnd);
}

void createControls(HWND hwnd){
    addLabel(hwnd,L"Artikelname",3001); addControl(L"EDIT",L"",WS_TABSTOP|ES_AUTOHSCROLL,ID_NAME,hwnd,WS_EX_CLIENTEDGE);
    addLabel(hwnd,L"Lagernummer",3002); addLabel(hwnd,L"EAN / UPC / GTIN",3003);
    addControl(L"EDIT",L"",WS_TABSTOP|ES_AUTOHSCROLL,ID_STORAGE_NO,hwnd,WS_EX_CLIENTEDGE);addControl(L"EDIT",L"",WS_TABSTOP|ES_AUTOHSCROLL,ID_BARCODE,hwnd,WS_EX_CLIENTEDGE);
    addControl(L"BUTTON",L"+",WS_TABSTOP|BS_OWNERDRAW,ID_STORAGE_AUTO_NUMBER,hwnd);
    addControl(L"BUTTON",L"",WS_TABSTOP|BS_OWNERDRAW,ID_CODE_GENERATOR,hwnd);
    addLabel(hwnd,L"Lagerort",3004);addLabel(hwnd,L"Menge",3005);addLabel(hwnd,L"Einheit",3006);
    addControl(L"EDIT",L"",WS_TABSTOP|ES_AUTOHSCROLL,ID_LOCATION,hwnd,WS_EX_CLIENTEDGE);
    addControl(L"EDIT",L"0",WS_TABSTOP|ES_NUMBER|ES_AUTOHSCROLL,ID_AMOUNT,hwnd,WS_EX_CLIENTEDGE);
    HWND unit=addControl(WC_COMBOBOXW,L"",WS_TABSTOP|CBS_DROPDOWNLIST|CBS_OWNERDRAWFIXED|CBS_HASSTRINGS,ID_UNIT,hwnd);
    fillUnitCombo(unit,setting("default_unit",L"Stück"));
    addLabel(hwnd,L"Fach",3007);addLabel(hwnd,L"Preis",3008);addLabel(hwnd,L"Letzte Änderung",3009);
    addControl(L"EDIT",L"",WS_TABSTOP|ES_AUTOHSCROLL,ID_SHELF,hwnd,WS_EX_CLIENTEDGE);addControl(L"EDIT",L"-",WS_TABSTOP|ES_AUTOHSCROLL|ES_CENTER,ID_PRICE,hwnd,WS_EX_CLIENTEDGE);
    addControl(L"BUTTON",L"+",WS_TABSTOP|BS_OWNERDRAW,ID_SHELF_AUTO_NUMBER,hwnd);
    addControl(L"BUTTON",L"",WS_TABSTOP|BS_OWNERDRAW,ID_SHELF_CODE_GENERATOR,hwnd);
    addControl(L"EDIT",L"",ES_READONLY|ES_CENTER,ID_CHANGED,hwnd,WS_EX_CLIENTEDGE);
    addLabel(hwnd,L"Beschreibung / Notizen",3010);addControl(L"EDIT",L"",WS_TABSTOP|ES_MULTILINE|ES_AUTOVSCROLL|ES_WANTRETURN|WS_VSCROLL,ID_NOTES,hwnd,WS_EX_CLIENTEDGE);
    HWND preview=addControl(L"STATIC",L"",SS_NOTIFY,ID_IMAGE_PREVIEW,hwnd,WS_EX_CLIENTEDGE);SetWindowSubclass(preview,ImagePreviewProc,1,0);
    addControl(L"BUTTON",L"Bild hinzufügen",WS_TABSTOP|BS_OWNERDRAW,ID_IMAGE_LOAD,hwnd);
    addControl(L"BUTTON",L"Bild speichern",WS_TABSTOP|BS_OWNERDRAW,ID_IMAGE_SAVE,hwnd);
    addControl(L"BUTTON",L"Bild löschen",WS_TABSTOP|BS_OWNERDRAW,ID_IMAGE_DELETE,hwnd);
    addControl(L"EDIT",L"",WS_TABSTOP|ES_AUTOHSCROLL,ID_SEARCH,hwnd,WS_EX_CLIENTEDGE);
    HWND list=addControl(WC_LISTVIEWW,L"",WS_TABSTOP|LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS,ID_LIST,hwnd,WS_EX_CLIENTEDGE);
    ListView_SetExtendedListViewStyle(list,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER|LVS_EX_GRIDLINES);
    LVCOLUMNW col{};col.mask=LVCF_TEXT|LVCF_WIDTH;
    col.pszText=(LPWSTR)L"Artikel";col.cx=210;ListView_InsertColumn(list,0,&col);
    col.pszText=(LPWSTR)L"Bestand";col.cx=100;ListView_InsertColumn(list,1,&col);
    col.pszText=(LPWSTR)L"Lagerort";col.cx=105;ListView_InsertColumn(list,2,&col);
    col.pszText=(LPWSTR)L"Fach";col.cx=80;ListView_InsertColumn(list,3,&col);
    for(auto b:std::vector<std::pair<int,const wchar_t*>>{{ID_NEW,L"Neuer Eintrag"},{ID_SCAN,L"Barcode scannen"},{ID_DELETE,L"Eintrag löschen"},{ID_SAVE,L"Eintrag speichern"},{ID_OLDEST,L"Ältester Eintrag"},{ID_NEWEST,L"Neuester Eintrag"}})
        addControl(L"BUTTON",b.second,WS_TABSTOP|BS_OWNERDRAW,b.first,hwnd);
    addControl(L"STATIC",L"Bereit",SS_LEFT,ID_STATUS,hwnd);
    HWND grip = addControl(L"STATIC",L"◢",SS_CENTER | SS_NOTIFY,ID_GRIP,hwnd);
    SetWindowSubclass(grip, ResizeGripProc, 1, 0);
}

void layout(HWND hwnd){
    RECT rc;GetClientRect(hwnd,&rc);int W=rc.right,H=rc.bottom,pad=18,gap=14,statusH=26,menuH=30;
    setRect(hwnd,ID_TOP_DATABASE,0,0,100,menuH);setRect(hwnd,ID_SETTINGS,100,0,132,menuH);setRect(hwnd,ID_ABOUT,232,0,66,menuH);
    int left=std::max(330,(W-pad*2-gap)*47/100);int rightX=pad+left+gap,right=W-rightX-pad;
    int y=menuH+18;setRect(hwnd,3001,pad,y,left,18);y+=20;setRect(hwnd,ID_NAME,pad,y,left,28);y+=38;
    int half=(left-8)/2;setRect(hwnd,3002,pad,y,half,18);setRect(hwnd,3003,pad+half+8,y,half,18);y+=19;
    setRect(hwnd,ID_STORAGE_NO,pad,y,half-72,27);setRect(hwnd,ID_STORAGE_AUTO_NUMBER,pad+half-68,y,32,27);setRect(hwnd,ID_CODE_GENERATOR,pad+half-32,y,32,27);setRect(hwnd,ID_BARCODE,pad+half+8,y,half,27);y+=37;
    int c1=left*46/100,c2=left*20/100,c3=left-c1-c2-16;
    setRect(hwnd,3004,pad,y,c1,18);setRect(hwnd,3005,pad+c1+8,y,c2,18);setRect(hwnd,3006,pad+c1+c2+16,y,c3,18);y+=19;
    setRect(hwnd,ID_LOCATION,pad,y,c1,27);setRect(hwnd,ID_AMOUNT,pad+c1+8,y,c2,27);setRect(hwnd,ID_UNIT,pad+c1+c2+16,y,c3,180);y+=37;
    int shelfW=left*30/100,smallButtonW=32,priceW=std::max(58,left*21/100),priceX=pad+shelfW+smallButtonW*2+12,changedX=priceX+priceW+8,changedW=left-(changedX-pad);
    setRect(hwnd,3007,pad,y,shelfW,18);setRect(hwnd,3008,priceX,y,priceW,18);setRect(hwnd,3009,changedX,y,changedW,18);y+=19;
    setRect(hwnd,ID_SHELF,pad,y,shelfW,27);setRect(hwnd,ID_SHELF_AUTO_NUMBER,pad+shelfW+4,y,smallButtonW,27);setRect(hwnd,ID_SHELF_CODE_GENERATOR,pad+shelfW+smallButtonW+8,y,smallButtonW,27);setRect(hwnd,ID_PRICE,priceX,y,priceW,27);setRect(hwnd,ID_CHANGED,changedX,y,changedW,27);y+=38;
    int imageRowH=std::min(150,std::max(105,(H-y-statusH-150)/2)),previewSize=imageRowH;
    int imageButtonX=pad+previewSize+10,imageButtonW=left-previewSize-10,buttonGap=5;
    int imageButtonH=(imageRowH-buttonGap*2)/3;
    setRect(hwnd,ID_IMAGE_PREVIEW,pad,y,previewSize,previewSize);
    setRect(hwnd,ID_IMAGE_LOAD,imageButtonX,y,imageButtonW,imageButtonH);
    setRect(hwnd,ID_IMAGE_SAVE,imageButtonX,y+imageButtonH+buttonGap,imageButtonW,imageButtonH);
    setRect(hwnd,ID_IMAGE_DELETE,imageButtonX,y+(imageButtonH+buttonGap)*2,imageButtonW,imageRowH-imageButtonH*2-buttonGap*2);
    y+=imageRowH+10;setRect(hwnd,3010,pad,y,left,18);y+=20;
    setRect(hwnd,ID_NOTES,pad,y,left,std::max(80,H-y-statusH-16));
    setRect(hwnd,ID_SEARCH,rightX,menuH+18,right,28);
    int buttonsH=106;setRect(hwnd,ID_LIST,rightX,menuH+56,right,std::max(120,H-menuH-56-buttonsH-statusH-18));
    int by=H-buttonsH-statusH-8,bw=(right-10)/2,bh=30;
    setRect(hwnd,ID_NEW,rightX,by,bw,bh);setRect(hwnd,ID_SCAN,rightX+bw+10,by,bw,bh);by+=38;
    setRect(hwnd,ID_DELETE,rightX,by,bw,bh);setRect(hwnd,ID_SAVE,rightX+bw+10,by,bw,bh);by+=38;
    setRect(hwnd,ID_OLDEST,rightX,by,bw,bh);setRect(hwnd,ID_NEWEST,rightX+bw+10,by,bw,bh);
    setRect(hwnd,ID_STATUS,pad,H-statusH,W-pad*2-24,statusH);
    setRect(hwnd,ID_GRIP,W-pad-22,H-statusH+2,22,statusH-2);
}

SIZE minimumWindowSize(HWND hwnd) {
    // All controls remain readable with this much client space.
    RECT minimum{0, 0, 860, 630};
    AdjustWindowRectEx(&minimum, (DWORD)GetWindowLongPtrW(hwnd, GWL_STYLE),
                       GetMenu(hwnd) != nullptr,
                       (DWORD)GetWindowLongPtrW(hwnd, GWL_EXSTYLE));
    return {minimum.right - minimum.left, minimum.bottom - minimum.top};
}

LRESULT customDrawHeader(NMCUSTOMDRAW* draw) {
    if (!g_darkMode) return CDRF_DODEFAULT;
    if (draw->dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
    if (draw->dwDrawStage == CDDS_ITEMPREPAINT) {
        HBRUSH bg=CreateSolidBrush(RGB(52,55,60));FillRect(draw->hdc,&draw->rc,bg);DeleteObject(bg);
        wchar_t text[160]{};HDITEMW item{};item.mask=HDI_TEXT;item.pszText=text;item.cchTextMax=160;
        Header_GetItem((HWND)draw->hdr.hwndFrom,(int)draw->dwItemSpec,&item);
        SetBkMode(draw->hdc,TRANSPARENT);SetTextColor(draw->hdc,RGB(245,245,245));
        HFONT old=(HFONT)SelectObject(draw->hdc,g_font);RECT r=draw->rc;r.left+=7;
        DrawTextW(draw->hdc,text,-1,&r,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);SelectObject(draw->hdc,old);
        HPEN pen=CreatePen(PS_SOLID,1,RGB(90,94,100));HGDIOBJ oldPen=SelectObject(draw->hdc,pen);
        MoveToEx(draw->hdc,draw->rc.right-1,draw->rc.top,nullptr);LineTo(draw->hdc,draw->rc.right-1,draw->rc.bottom);
        MoveToEx(draw->hdc,draw->rc.left,draw->rc.bottom-1,nullptr);LineTo(draw->hdc,draw->rc.right,draw->rc.bottom-1);
        SelectObject(draw->hdc,oldPen);DeleteObject(pen);return CDRF_SKIPDEFAULT;
    }
    return CDRF_DODEFAULT;
}

void paintDarkMenuRemainder(HWND hwnd) {
    if (!g_darkMode || !GetMenu(hwnd)) return;
    MENUBARINFO bar{};bar.cbSize=sizeof(bar);
    if (!GetMenuBarInfo(hwnd,OBJID_MENU,0,&bar)) return;
    int count=GetMenuItemCount(GetMenu(hwnd));if(count<1)return;
    RECT last{},window{};if(!GetMenuItemRect(hwnd,GetMenu(hwnd),(UINT)(count-1),&last))return;
    GetWindowRect(hwnd,&window);
    RECT remainder{last.right-window.left,bar.rcBar.top-window.top,bar.rcBar.right-window.left,bar.rcBar.bottom-window.top+1};
    if(remainder.right>remainder.left){HDC dc=GetWindowDC(hwnd);FillRect(dc,&remainder,g_bgBrush);ReleaseDC(hwnd,dc);}
}

LRESULT CALLBACK MainWndProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
    switch(msg){
    case WM_CREATE:g_main=hwnd;createMenus(hwnd);createControls(hwnd);applyTheme();applyCustomLabels();clearForm();refreshList();if(!g_runningUnderWine)SetTimer(hwnd,20,1000,nullptr);return 0;
    case WM_SIZE:if(wp!=SIZE_MINIMIZED)layout(hwnd);return 0;
    case WM_TIMER:if(wp==20){checkWindowsThemeChange();return 0;}break;
    case WM_SETTINGCHANGE:checkWindowsThemeChange();break;
    case WM_NCPAINT:{LRESULT result=DefWindowProcW(hwnd,msg,wp,lp);paintDarkMenuRemainder(hwnd);return result;}
    case WM_GETMINMAXINFO: {
        SIZE minimum = minimumWindowSize(hwnd);
        ((MINMAXINFO*)lp)->ptMinTrackSize = {minimum.cx, minimum.cy};
        return 0;
    }
    case WM_SIZING: {
        // Wine compositors may treat WM_GETMINMAXINFO only after the drag ends.
        // Clamp the live sizing rectangle so the dragged edge stops immediately.
        RECT* sizing = (RECT*)lp;
        SIZE minimum = minimumWindowSize(hwnd);
        int width = sizing->right - sizing->left;
        int height = sizing->bottom - sizing->top;
        if (width < minimum.cx) {
            if (wp == WMSZ_LEFT || wp == WMSZ_TOPLEFT || wp == WMSZ_BOTTOMLEFT)
                sizing->left = sizing->right - minimum.cx;
            else
                sizing->right = sizing->left + minimum.cx;
        }
        if (height < minimum.cy) {
            if (wp == WMSZ_TOP || wp == WMSZ_TOPLEFT || wp == WMSZ_TOPRIGHT)
                sizing->top = sizing->bottom - minimum.cy;
            else
                sizing->bottom = sizing->top + minimum.cy;
        }
        return TRUE;
    }
    case WM_WINDOWPOSCHANGING: {
        // Cover programmatic, keyboard, and compositor-driven resize paths too.
        WINDOWPOS* pos = (WINDOWPOS*)lp;
        if (!(pos->flags & SWP_NOSIZE)) {
            SIZE minimum = minimumWindowSize(hwnd);
            pos->cx = std::max(pos->cx, (int)minimum.cx);
            pos->cy = std::max(pos->cy, (int)minimum.cy);
        }
        return 0;
    }
    case WM_CTLCOLORSTATIC:case WM_CTLCOLOREDIT:case WM_CTLCOLORBTN:case WM_CTLCOLORLISTBOX:
        return themeControlColor(msg,wp);
    case WM_ERASEBKGND:return eraseThemedBackground(hwnd,wp);
    case WM_MEASUREITEM:if(measureOwnerItem((MEASUREITEMSTRUCT*)lp))return TRUE;break;
    case WM_DRAWITEM:if(drawOwnerItem((DRAWITEMSTRUCT*)lp))return TRUE;break;
    case WM_COMMAND:
        if(HIWORD(wp)==EN_CHANGE && LOWORD(wp)==ID_SEARCH){g_shelfFilter.clear();refreshList();return 0;}
        if(!g_loadingForm&&HIWORD(wp)==EN_CHANGE&&LOWORD(wp)>=ID_NAME&&LOWORD(wp)<=ID_NOTES&&LOWORD(wp)!=ID_CHANGED)g_formDirty=true;
        if(!g_loadingForm&&LOWORD(wp)==ID_UNIT&&HIWORD(wp)==CBN_SELCHANGE)g_formDirty=true;
        if(HIWORD(wp)==EN_CHANGE && LOWORD(wp)==ID_PRICE){
            wstring value=getText(hwnd,ID_PRICE);
            setPriceAlignment(!value.empty() && value!=L"-");return 0;
        }
        switch(LOWORD(wp)){
        case ID_TOP_DATABASE:{RECT button;GetWindowRect(GetDlgItem(hwnd,ID_TOP_DATABASE),&button);TrackPopupMenu(g_databaseMenu,TPM_LEFTALIGN|TPM_TOPALIGN,button.left,button.bottom,0,hwnd,nullptr);break;}
        case ID_NEW:if(resolveUnsavedChanges()){g_shelfFilter.clear();refreshList();newEntry();}break;case ID_SAVE:saveCurrent();break;case ID_DELETE:deleteCurrent();break;
        case ID_OLDEST:if(resolveUnsavedChanges()){g_shelfFilter.clear();refreshList();loadEdge(false);}break;case ID_NEWEST:if(resolveUnsavedChanges()){g_shelfFilter.clear();refreshList();loadEdge(true);}break;case ID_SCAN:if(resolveUnsavedChanges()){g_shelfFilter.clear();refreshList();scanBarcode();}break;
        case ID_IMAGE_LOAD:loadPicture();break;case ID_IMAGE_SAVE:savePicture();break;case ID_IMAGE_DELETE:deletePicture();break;
        case ID_CODE_GENERATOR:codeGeneratorDialog(getText(g_main,ID_STORAGE_NO),L"Lagernummer");break;
        case ID_STORAGE_AUTO_NUMBER:assignNextFreeStorageNumber();break;
        case ID_SHELF_CODE_GENERATOR:codeGeneratorDialog(getText(g_main,ID_SHELF),L"Fach");break;
        case ID_SHELF_AUTO_NUMBER:assignNextFreeShelfNumber();break;
        case ID_DB_EXPORT:exportCsv();break;case ID_DB_NEW:createNewDatabase();break;case ID_DB_BACKUP:saveDatabaseCopy();break;case ID_DB_RESTORE:openDatabaseFromMenu();break;
        case ID_SETTINGS:settingsDialog();break;case ID_ABOUT:aboutDialog();break;
        case ID_EXIT:PostMessageW(hwnd,WM_CLOSE,0,0);break;
        }return 0;
    case WM_NOTIFY:
        if(((NMHDR*)lp)->hwndFrom==ListView_GetHeader(GetDlgItem(hwnd,ID_LIST))&&((NMHDR*)lp)->code==NM_CUSTOMDRAW)
            return customDrawHeader((NMCUSTOMDRAW*)lp);
        if(!g_loadingForm&&((NMHDR*)lp)->idFrom==ID_LIST && (((NMHDR*)lp)->code==NM_DBLCLK || ((NMHDR*)lp)->code==LVN_ITEMCHANGED)){
            NMITEMACTIVATE* a=(NMITEMACTIVATE*)lp;int idx=a->iItem;
            if(idx>=0 && (((NMHDR*)lp)->code==NM_DBLCLK || (ListView_GetItemState(GetDlgItem(hwnd,ID_LIST),idx,LVIS_SELECTED)&LVIS_SELECTED))){LVITEMW li{};li.mask=LVIF_PARAM;li.iItem=idx;if(ListView_GetItem(GetDlgItem(hwnd,ID_LIST),&li)&&li.lParam!=g_currentId){sqlite3_int64 selectedId=(sqlite3_int64)li.lParam;if(resolveUnsavedChanges()){if(loadById(selectedId))selectItemInList(selectedId);}else{HWND list=GetDlgItem(hwnd,ID_LIST);g_loadingForm=true;ListView_SetItemState(list,-1,0,LVIS_SELECTED|LVIS_FOCUSED);g_loadingForm=false;selectItemInList(g_currentId);}}}
        }return 0;
    case WM_APP+10:showNotice(hwnd,L"Drucken",wp?L"Der Druckauftrag wurde an den Drucker übergeben.":L"Der Druckauftrag ist fehlgeschlagen.",!wp);return 0;
    case WM_CLOSE:if(InterlockedCompareExchange(&g_printing,0,0)!=0){showNotice(hwnd,L"Drucken",L"Bitte warten Sie, bis der laufende Druckauftrag abgeschlossen ist.");return 0;}if(resolveUnsavedChanges())DestroyWindow(hwnd);return 0;
    case WM_DESTROY:KillTimer(hwnd,20);if(g_databaseMenu){DestroyMenu(g_databaseMenu);g_databaseMenu=nullptr;}if(g_db){sqlite3_close(g_db);g_db=nullptr;}PostQuitMessage(0);return 0;
    }return DefWindowProcW(hwnd,msg,wp,lp);
}

int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int show){
    g_instance=instance;g_runningUnderWine=runningUnderWine();SetProcessDPIAware();
    Gdiplus::GdiplusStartupInput gdiplusInput;if(Gdiplus::GdiplusStartup(&g_gdiplusToken,&gdiplusInput,nullptr)!=Gdiplus::Ok)return 3;loadQrLogo();
    INITCOMMONCONTROLSEX ic{sizeof(ic),ICC_LISTVIEW_CLASSES|ICC_STANDARD_CLASSES};InitCommonControlsEx(&ic);
    g_font=CreateFontW(-16,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");
    g_titleFont=CreateFontW(-20,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");
    if(!openDatabase()){Gdiplus::GdiplusShutdown(g_gdiplusToken);return 1;}synchronizeWindowsTheme(true);
    g_lightBgBrush=CreateSolidBrush(RGB(246,247,249));g_lightPanelBrush=CreateSolidBrush(RGB(255,255,255));
    g_darkBgBrush=CreateSolidBrush(RGB(31,33,36));g_darkPanelBrush=CreateSolidBrush(RGB(45,47,51));
    g_bgBrush=g_darkMode?g_darkBgBrush:g_lightBgBrush;g_panelBrush=g_darkMode?g_darkPanelBrush:g_lightPanelBrush;
    WNDCLASSEXW promptClass{};promptClass.cbSize=sizeof(promptClass);promptClass.lpfnWndProc=PromptWndProc;promptClass.hInstance=instance;promptClass.hCursor=LoadCursor(nullptr,IDC_ARROW);promptClass.hbrBackground=g_bgBrush;promptClass.lpszClassName=L"LogSPrompt";promptClass.hIcon=LoadIcon(instance,MAKEINTRESOURCE(IDI_LOGS));RegisterClassExW(&promptClass);
    WNDCLASSEXW messageClass{};messageClass.cbSize=sizeof(messageClass);messageClass.lpfnWndProc=ThemedMessageWndProc;messageClass.hInstance=instance;messageClass.hCursor=LoadCursor(nullptr,IDC_ARROW);messageClass.hbrBackground=g_bgBrush;messageClass.lpszClassName=L"LogSMessage";messageClass.hIcon=LoadIcon(instance,MAKEINTRESOURCE(IDI_LOGS));RegisterClassExW(&messageClass);
    WNDCLASSEXW unsavedClass{};unsavedClass.cbSize=sizeof(unsavedClass);unsavedClass.lpfnWndProc=UnsavedWndProc;unsavedClass.hInstance=instance;unsavedClass.hCursor=LoadCursor(nullptr,IDC_ARROW);unsavedClass.hbrBackground=g_bgBrush;unsavedClass.lpszClassName=L"LogSUnsaved";unsavedClass.hIcon=LoadIcon(instance,MAKEINTRESOURCE(IDI_LOGS));RegisterClassExW(&unsavedClass);
    WNDCLASSEXW settingsClass{};settingsClass.cbSize=sizeof(settingsClass);settingsClass.lpfnWndProc=SettingsWndProc;settingsClass.hInstance=instance;settingsClass.hCursor=LoadCursor(nullptr,IDC_ARROW);settingsClass.hbrBackground=g_bgBrush;settingsClass.lpszClassName=L"LogSSettings";settingsClass.hIcon=LoadIcon(instance,MAKEINTRESOURCE(IDI_LOGS));RegisterClassExW(&settingsClass);
    WNDCLASSEXW aboutClass{};aboutClass.cbSize=sizeof(aboutClass);aboutClass.lpfnWndProc=AboutWndProc;aboutClass.hInstance=instance;aboutClass.hCursor=LoadCursor(nullptr,IDC_ARROW);aboutClass.hbrBackground=g_bgBrush;aboutClass.lpszClassName=L"LogSAbout";aboutClass.hIcon=LoadIcon(instance,MAKEINTRESOURCE(IDI_LOGS));RegisterClassExW(&aboutClass);
    WNDCLASSEXW codeClass{};codeClass.cbSize=sizeof(codeClass);codeClass.lpfnWndProc=CodeGeneratorWndProc;codeClass.hInstance=instance;codeClass.hCursor=LoadCursor(nullptr,IDC_ARROW);codeClass.hbrBackground=g_bgBrush;codeClass.lpszClassName=L"LogSCodeGenerator";codeClass.hIcon=LoadIcon(instance,MAKEINTRESOURCE(IDI_LOGS));RegisterClassExW(&codeClass);
    WNDCLASSEXW fileClass{};fileClass.cbSize=sizeof(fileClass);fileClass.lpfnWndProc=FileDialogWndProc;fileClass.hInstance=instance;fileClass.hCursor=LoadCursor(nullptr,IDC_ARROW);fileClass.hbrBackground=g_bgBrush;fileClass.lpszClassName=L"LogSFileDialog";fileClass.hIcon=LoadIcon(instance,MAKEINTRESOURCE(IDI_LOGS));RegisterClassExW(&fileClass);
    WNDCLASSEXW scannerClass{};scannerClass.cbSize=sizeof(scannerClass);scannerClass.lpfnWndProc=ScannerWndProc;scannerClass.hInstance=instance;scannerClass.hCursor=LoadCursor(nullptr,IDC_ARROW);scannerClass.hbrBackground=g_bgBrush;scannerClass.lpszClassName=L"LogSScanner";scannerClass.hIcon=LoadIcon(instance,MAKEINTRESOURCE(IDI_LOGS));RegisterClassExW(&scannerClass);
    WNDCLASSEXW wc{};wc.cbSize=sizeof(wc);wc.style=CS_HREDRAW|CS_VREDRAW;wc.lpfnWndProc=MainWndProc;wc.hInstance=instance;wc.hIcon=LoadIcon(instance,MAKEINTRESOURCE(IDI_LOGS));wc.hIconSm=wc.hIcon;wc.hCursor=LoadCursor(nullptr,IDC_ARROW);wc.hbrBackground=g_bgBrush;wc.lpszClassName=L"LogSMainWindow";RegisterClassExW(&wc);
    // Deliberately omit WS_THICKFRAME. Under Wine/Wayland the compositor can
    // visually shrink a native resize preview below the Win32 minimum before
    // snapping it back. The in-app grip performs live, strictly clamped sizing.
    DWORD windowStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU |
                        WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_CLIPCHILDREN;
    g_main=CreateWindowExW(0,wc.lpszClassName,L"LogS – Lagerorganisation Software",windowStyle,CW_USEDEFAULT,CW_USEDEFAULT,1080,690,nullptr,nullptr,instance,nullptr);
    if(!g_main){Gdiplus::GdiplusShutdown(g_gdiplusToken);return 2;}ShowWindow(g_main,show);UpdateWindow(g_main);
    MSG msg;while(GetMessageW(&msg,nullptr,0,0)>0){if(!IsDialogMessageW(g_main,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}
    DeleteObject(g_font);DeleteObject(g_titleFont);DeleteObject(g_lightBgBrush);DeleteObject(g_lightPanelBrush);DeleteObject(g_darkBgBrush);DeleteObject(g_darkPanelBrush);releaseQrLogo();Gdiplus::GdiplusShutdown(g_gdiplusToken);return (int)msg.wParam;
}
