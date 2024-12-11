#include <windows.h>
#include <TlHelp32.h>
#include <cstdio>
#include <cwchar> // Für wcscmp

// Funktion zur Konvertierung von char* zu wchar_t*
wchar_t* ConvertCharToWChar(const char* charArray) {
    // Länge des benötigten wchar_t-Arrays berechnen
    int length = MultiByteToWideChar(CP_UTF8, 0, charArray, -1, nullptr, 0);
    // Speicher für wchar_t-Array reservieren
    wchar_t* wCharArray = new wchar_t[length];
    // Konvertierung durchführen
    MultiByteToWideChar(CP_UTF8, 0, charArray, -1, wCharArray, length);
    return wCharArray;
}

// Funktion zur Suche der Prozess-ID anhand des Namens
DWORD find_process_id(const char* process) {
    PROCESSENTRY32 entry;
    entry.dwSize = sizeof(PROCESSENTRY32);

    // Snapshot aller Prozesse erstellen
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, NULL);
    if (snapshot == INVALID_HANDLE_VALUE) {
        printf("Der Handle zum Snapshot ist nicht gegeben\n");
        return 0;
    }

    // Konvertiere den Prozessnamen in wchar_t*
    wchar_t* wProcessName = ConvertCharToWChar(process);
    DWORD P_ID = 0;
    bool found = false;

    // Durch die Prozessliste iterieren
    if (Process32First(snapshot, &entry)) {
        do {
            // Überprüfen, ob der aktuelle Prozessname mit dem gesuchten übereinstimmt
            if (wcscmp(entry.szExeFile, wProcessName) == 0) {
                wprintf(L"Prozessname: %s | Prozess-ID: %lu\n", entry.szExeFile, entry.th32ProcessID);
                P_ID = entry.th32ProcessID;
                found = true;
                break;
            }
        } while (Process32Next(snapshot, &entry));
    }

    // Falls der Prozess nicht gefunden wurde, eine Meldung ausgeben
    if (!found) {
        printf("Der Prozess '%s' wurde nicht gefunden\n", process);
    }

    // Ressourcen freigeben
    delete[] wProcessName; // Speicher des konvertierten Strings freigeben
    CloseHandle(snapshot);  // Handle des Snapshots schließen

    return P_ID;
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        printf("Verwendung: %s <Prozessname> <DLL-Pfad>\n", argv[0]);
        return 1;
    }

    DWORD dllPathLength = static_cast<DWORD>(strlen(argv[2]) + 1); // Länge des DLL-Pfads inklusive Nullterminator
    DWORD P_ID = find_process_id(argv[1]);
    if (P_ID == 0) {
        printf("Kein Prozess gefunden\n");
        return 1;
    }

    printf("Die Prozess-ID ist: %lu\n", P_ID);

    // Öffne ein Handle zum Zielprozess
    HANDLE PROCESS = OpenProcess(PROCESS_ALL_ACCESS, NULL, P_ID);
    if (PROCESS == NULL) {
        printf("Eröffnung zum Zielprozess nicht möglich\n");
        return 1;
    }

    // Reserviere Speicher im Zielprozess für den DLL-Pfad
    LPVOID remoteMemory = VirtualAllocEx(PROCESS, NULL, dllPathLength, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (remoteMemory == NULL) {
        printf("Speicher im Zielprozess konnte nicht reserviert werden\n");
        CloseHandle(PROCESS);
        return 1;
    }

    // Schreibe den DLL-Pfad in den reservierten Speicherbereich des Zielprozesses
    BOOL opWrite = WriteProcessMemory(PROCESS, remoteMemory, argv[2], dllPathLength, NULL);
    if (!opWrite) {
        printf("DLL konnte nicht in den Zielprozess geschrieben werden\n");
        VirtualFreeEx(PROCESS, remoteMemory, 0, MEM_RELEASE);
        CloseHandle(PROCESS);
        return 1;
    }
    else {
        printf("DLL erfolgreich im Zielprozess abgelegt\n");
    }

    // Ermitteln der Adresse von LoadLibraryA
    LPVOID loadLibraryAddr = (LPVOID)GetProcAddress(GetModuleHandleA("kernel32.dll"), "LoadLibraryA");
    if (loadLibraryAddr == NULL) {
        printf("Konnte Adresse von LoadLibraryA nicht ermitteln. Fehlercode: %lu\n", GetLastError());
        VirtualFreeEx(PROCESS, remoteMemory, 0, MEM_RELEASE);
        CloseHandle(PROCESS);
        return 1;
    }

    // Erstelle einen Remote-Thread im Zielprozess, um LoadLibraryA aufzurufen und die DLL zu laden
    HANDLE remoteThread = CreateRemoteThread(
        PROCESS,                // Handle des Zielprozesses
        NULL,                   // Standard-Sicherheitsattribute
        0,                      // Standard-Stackgröße
        (LPTHREAD_START_ROUTINE)loadLibraryAddr, // Adresse von LoadLibraryA
        remoteMemory,           // Parameter für LoadLibraryA (Adresse des DLL-Pfads im Zielprozess)
        0,                      // Standard-Erstellungsflags
        NULL                    // Optionale Thread-ID (NULL, wenn nicht benötigt)
    );

    if (remoteThread == NULL) {
        printf("Konnte keinen Remote-Thread erstellen. Fehlercode: %lu\n", GetLastError());
        VirtualFreeEx(PROCESS, remoteMemory, 0, MEM_RELEASE);
        CloseHandle(PROCESS);
        return 1;
    }

    // Warten, bis der Remote-Thread die Ausführung beendet hat
    WaitForSingleObject(remoteThread, INFINITE);
    printf("DLL erfolgreich im Zielprozess geladen\n");

    // Aufräumen: Remote-Thread und reservierter Speicher freigeben
    CloseHandle(remoteThread);
    VirtualFreeEx(PROCESS, remoteMemory, 0, MEM_RELEASE);
    CloseHandle(PROCESS);

    return 0;
}
