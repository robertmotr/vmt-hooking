/*
Short explanation because it was very difficult to find this info (chatgpt with some edits sprinkled in)
I spent a long time trying to understand this because resources are all over the place.

Background:
    Virtual functions in C++ allow for runtime polymorphism, where the function to be called is resolved at runtime based on the actual type of the object, 
    not the type of the pointer or reference that is used to call the function. When you declare a function as virtual, the compiler generates a Virtual Method Table (VMT) for the class. 
    This table contains pointers to the actual function implementations for the class. Each object of a class with virtual methods has a vtable pointer, which points to the vtable of that class.
    When you call a virtual function, the program looks up the function address in the vtable and then calls the appropriate function. This allows you to override methods in derived classes.

    In comparison, if you DON'T mark the function as virtual, the function will be resolved at compile time and no vtable is generated.
    
    
    Example:
        Dynamic polymorphism, method overwriting gets resolved at runtime using VMT:
        (More flexible but more overhead + slower)

        class Base {
        public:
            virtual void show() {  // Virtual method
                ...
            }
        };

        class Derived : public Base {
        public:
            void show() {  // Overrides virtual method
                ...
            }
        };

        Static (no vtable generation) polymorphism:
		(Faster but less flexible)

        class Base {
        public:
            void show() {
                ...
            }
        };

        class Derived : public Base {
        public:
            void show() {
                ...
        }

Important notes:
    - Use __stdcall for our hooked endscene since its the calling convention used in DX9.

Credits:
    ChatGPT 4o (my lovely)
    https://www.unknowncheats.me/forum/d3d-programming/66133-midfunction-hook-v2.html (for the pattern scanning and the vmt hook idea)
    https://github.com/stimmy1442/EndSceneHookExample (for the inline asm, avoiding the yucky midfunction hook from the first post)
*/

#include <windows.h>
#include <d3d9.h>
#include <d3dx9.h>
#include <imgui.h>
#include <imgui_impl_dx9.h>
#include <imgui_impl_win32.h>
#include <cstdio>
#include <string>
#include <iostream>
#include <logger.h>

// link dx9 stuff
#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "d3dx9.lib")
#pragma comment(lib, "logging.lib")

#define                                     ENDSCENE_INDEX 42 // index of EndScene() in IDirect3DDevice9 vtable

DWORD*                                      oEndScene = nullptr; // original EndScene function address
DWORD**                                     vtable = nullptr; // IDirect3DDevice9 virtual method table
volatile LPDIRECT3DDEVICE9                  pDevice = nullptr; // IDirect3DDevice9 pointer being used in the target application

ImGuiIO*                                    io = nullptr; // stored globally to reduce overhead inside renderOverlay

/*
* Compares a pattern to a specified memory region.
	*
	* @param data The memory region to search.
	* @param pattern The pattern to search for.
	* @param mask The mask specifying which bytes in the pattern to compare.
	* @return True if the pattern was found, false otherwise.
*/
bool compare(const uint8_t* data, const uint8_t* pattern, const char* mask) {
    for (; *mask; ++mask, ++data, ++pattern) {
        if (*mask == 'x' && *data != *pattern) {
            return false;
        }
    }
    return true;
}

/*
    * Searches for a pattern in a specified memory region.
    *
    * @param startAddress The starting address of the memory region to search.
    * @param regionSize The size of the memory region to search.
    * @param pattern The pattern to search for.
    * @param mask The mask specifying which bytes in the pattern to compare.
    * @return The address of the first occurrence of the pattern, or NULL if not found.
    */
uintptr_t patternScan(uintptr_t startAddress, size_t regionSize, const uint8_t* pattern, const char* mask) {
    size_t patternLength = strlen(mask);

    for (size_t i = 0; i <= regionSize - patternLength; i++) {
        uintptr_t currentAddress = startAddress + i;
        if (compare(reinterpret_cast<const uint8_t*>(currentAddress), pattern, mask)) {
            return currentAddress;  
        }
    }

    return NULL;
}

/*
* Called every frame to render the ImGui overlay from inside our hook.
	*
*/
void __stdcall renderOverlay() {
    static bool initialized = false;

    if (!initialized) {
        LOG("Initializing ImGui...");
        initialized = true;

        LOG("Creating ImGui context...");
        ImGui::CreateContext();
		io = &ImGui::GetIO();
        io->ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io->WantCaptureKeyboard = true;
        io->WantCaptureMouse = true;

        HRESULT hr;
        D3DDEVICE_CREATION_PARAMETERS cparams;
        hr = pDevice->GetCreationParameters(&cparams);
        if (FAILED(hr)) {
            LOG("Failed to get device creation parameters.");
            return;
        }

        // init backend with correct window handle
        if (!ImGui_ImplWin32_Init(cparams.hFocusWindow)) {
            LOG("ImGui_ImplWin32_Init failed.");
            return;
        }
        if (!ImGui_ImplDX9_Init(pDevice)) {
            LOG("ImGui_ImplDX9_Init failed.");
            return;
        }
        LOG("ImGui initialized successfully.");
    }

	// this needs to be done every frame in order to have responsive input (e.g. buttons)
    io->MouseDown[0] = GetAsyncKeyState(VK_LBUTTON) & 0x8000;  
    io->MouseDown[1] = GetAsyncKeyState(VK_RBUTTON) & 0x8000;  
    io->MouseDown[2] = GetAsyncKeyState(VK_MBUTTON) & 0x8000;  

    ImGui_ImplDX9_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    ImGui::Begin("example imgui overlay after hooking");
    ImGui::Text("Now hooked using ImGui.");
    if (ImGui::Button("Close")) {
        exit(0);
    }

    ImGui::End();
    ImGui::EndFrame();
    ImGui::Render();
    ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
}

/*
    Hooked EndScene function for DX9 rendering pipeline.
    This function intercepts the call to the original EndScene method in the
    IDirect3DDevice9 vtable, allowing us to insert custom rendering logic (e.g.,
    ImGui overlay) before handing control back to DirectX for normal rendering.
    
    How this works:
        1. Inline assembly is used to get the CPU's state (registers and flags).
        2. The device pointer (LPDIRECT3DDEVICE9) is retrieved from the stack.
        In the case of DX9, the device pointer is passed to EndScene and
        is located at an offset on the stack. We access it manually.
        3. `renderOverlay(pDevice)` is called to execute our custom rendering logic.
        4. We restore the CPU's state using inline assembly, making sure to return
        control back to the original EndScene function(`oEndScene`), which ensures
        DirectX continues rendering as expected.
    
    Notes:
        - The function is marked as `__declspec(naked)` to avoid the compiler generating
        prologue and epilogue code (such as setting up the stack frame). This is essential
        because we're manually manipulating the stack and registers.
        - The 0x2C offset is specific to the calling convention and the layout of arguments
        on the stack when EndScene is called. This offset points to the IDirect3DDevice9
        pointer passed to EndScene. I didn't come up with this asm code as well as finding the offset,
		credits to the UC posts I've researched and the original author.
*/
__declspec(naked) void hkEndScene() {
    LOG("Hooked EndScene called");
    __asm {
		pushad // push all general purpose registers onto stack
		pushfd // push all flags onto stack
		push esi // push esi onto stack

		mov esi, dword ptr ss : [esp + 0x2c] // gets the IDirect3DDevice9 pointer from the stack
		mov pDevice, esi // store the pointer in a global variable
    }

	renderOverlay();

    __asm {
		pop esi // pop esi off stack
		popfd // pop flags off stack
		popad // pop all general purpose registers off stack
		jmp oEndScene // jump back to the address stored in the oEndScene variable
    }
}

/*
* Finds the IDirect3DDevice9 pointer and the VMT. Address to VMT is then stored globally in vtable.
	*
	* @return S_OK if successful, E_FAIL otherwise.
*/
HRESULT __stdcall findVMT() {
    LOG("Attempting to find VMT...");
	DWORD hD3D = NULL;
    while(!hD3D) hD3D = (DWORD)GetModuleHandle(L"d3d9.dll");
    LOG("d3d9.dll found.");
    // pointer chain:
	// d3d9.dll -> IDirect3D9 -> IDirect3DDevice9 -> IDirect3DDevice9 VMT
	// honestly i have no idea how this pattern was found, but it works, credits to the original author
    DWORD PPPDevice = patternScan(hD3D, 0x128000, (PBYTE)"\xC7\x06\x00\x00\x00\x00\x89\x86\x00\x00\x00\x00\x89\x86", "xx????xx????xx");
	if (PPPDevice == NULL) {
		LOG("Failed to find IDirect3DDevice9 pointer.");
		return E_FAIL;
	}
	LOG("IDirect3DDevice9 pointer found.");
    memcpy(&vtable, (void*)(PPPDevice + 2), 4);
    return S_OK;
}

/*
* Installs the hook by overwriting the EndScene function pointer in the VMT with our hook.
	*
	* @return S_OK if successful, E_FAIL otherwise.
*/
HRESULT __stdcall installHook() {
    if (!vtable) {
        LOG("VMT not found.");
        return E_FAIL;
    }

    LOG("Attempting to install hook...");

	// overwrite vtable entry with our hook
    DWORD oldProtect;
    if (VirtualProtect(&vtable[ENDSCENE_INDEX], sizeof(DWORD), PAGE_EXECUTE_READWRITE, &oldProtect)) {
        // save original EndScene function pointer
        oEndScene = (DWORD*)vtable[ENDSCENE_INDEX];
        vtable[ENDSCENE_INDEX] = (DWORD*)&hkEndScene;
        VirtualProtect(&vtable[ENDSCENE_INDEX], sizeof(DWORD), oldProtect, &oldProtect);
        LOG("Successfully installed hook.");
		// dx9 endscene calls should now be redirected to preHkEndScene
    }
	else {
		LOG("Failed to install hook.");
		return E_FAIL;
	}
    return S_OK;
}

/*
* Starts a debug console and installs the hook. Waits for VK_END to close. This thread entry point is used to avoid blocking the main thread. 
* (Not sure if this is necessary though)
	*
	* @param hModule The handle to the DLL module.
*/
void __stdcall hookThread(HMODULE hModule) {
    AllocConsole();
    freopen_s(reinterpret_cast<FILE**>(stdout), "CONOUT$", "w", stdout);
    LOG("Hook thread started.");

    if (FAILED(findVMT())) {
        LOG("Failed to find VMT.");
        return;
    }
    if (FAILED(installHook())) {
        LOG("Failed to install hook.");
        return;
    }

    LOG("Hook installed successfully. Waiting for VK_END to unhook...");
    while (!GetAsyncKeyState(VK_END)) {
        Sleep(500);
		LOG("Hooked EndScene addr:", (void*)&hkEndScene);
        LOG("VMT address: ", (void*)vtable);
		LOG("VMT[42]: ", (void*)vtable[42]);
		LOG("EndScene address: ", (void*)oEndScene);
    }

    FreeLibraryAndExitThread(hModule, 0);
    return;
}

BOOL __stdcall DllMain(HMODULE hModule, DWORD fdwReason, LPVOID lpReserved) {
    if (fdwReason == DLL_PROCESS_ATTACH) {
        LOG("DLL_PROCESS_ATTACH called.");
		DisableThreadLibraryCalls(hModule); 
        CreateThread(NULL, NULL, (LPTHREAD_START_ROUTINE)hookThread, NULL, NULL, NULL);
    }
    return TRUE;
}