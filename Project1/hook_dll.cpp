#include <windows.h>
#include <d3d9.h>
#include <d3dx9.h>
#include <detours.h>
#include <imgui.h>
#include <imgui_impl_dx9.h>
#include <imgui_impl_win32.h>
#include <cstdio>
#include <string>
#include <iostream>

// link dx9 libs and detours
#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "d3dx9.lib")
#pragma comment(lib, "detours.lib")

// typedefs for DX9 stuff to clean things up
// 2nd typedef is for the EndScene function so we can eventually use a PVOID given to us (need to cast it)
typedef IDirect3DDevice9				DX9_DEVICE;
typedef HRESULT							(WINAPI* EndScene)(DX9_DEVICE* ptrDevice);

INT										endSceneIndex = 0;
DX9_DEVICE*								device = nullptr;
void*									originalEndScene = nullptr;	
void**									vtable = nullptr;

/*
short explanation because it was very difficult to find this info (chatgpt with some edits sprinkled in)
i spent a long time trying to understand this because resources for this are quite scarce

In C++, you can have polymorphism such that functions marked as virtual get resolved at runtime.
For example, if you have a class Base and a class Derived and Derived inherits from Base, you can have a virtual function in Base (show)
that is overridden in Derived. When you call the function on a Base pointer that points to a Derived object, 
the Derived function will be called. When the function is marked with a virtual keyword, the compiler
generates a virtual table for the object which holds function pointers to functions marked as virtual.

In comparison, if you DON'T mark the function as virtual, the function will be resolved at compile time and no vtable is generated.
This is why you can't hook non-virtual functions using VMT hooking.

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
}

Two side notes:
1. All objects of a given class that share the same virtual methods share the same VMT.
2. Also, when each object is constructed, it holds a vptr that points to the vtable that is shared among all of its relatives.

VMT Hooking in practice:
Find the vtable: Locate the vtable for the object, which is typically at the beginning of the object’s memory layout.
Overwrite the method's function pointer: Replace the function pointer of the target method (e.g., EndScene in Direct3D) in the vtable with a pointer to your custom function (the hook).
Intercept calls: When the method is called, your hook function is executed instead of the original method.
Optionally, call the original method: Inside the hook, you can choose to call the original method by invoking the saved original function pointer.
*/

/*
 * findVMT
 * --------------------
 * 1. Get the foreground window
 * 2. Create a dummy Direct3D device
 * 3. Ptr to IDirect3DDevice9 is stored in global variable device
 * 3. Return S_OK if successful, E_FAIL if failed
 *
 *  returns: S_OK/E_FAIL (HRESULT)
 */
HRESULT WINAPI findVMT() {
	// get hwnd first
	HWND hwnd = GetForegroundWindow();
	if (!hwnd) {
		std::cout << "Failed to get foreground window." << "\n";
		return E_FAIL;
	}

	IDirect3D9* pD3D = Direct3DCreate9(D3D_SDK_VERSION);
	if (!pD3D) {
		std::cout << "Failed to create IDirect3D9 object." << "\n";
		return E_FAIL;
	}

	D3DPRESENT_PARAMETERS d3dpp = {};
	d3dpp.Windowed = TRUE;
	d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
	d3dpp.hDeviceWindow = hwnd;

	// dummy Direct3D device
	// recall that pD3D holds a vtable ptr to the same vtable as the real D3D obj we're interested in
	pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &d3dpp, &device);
	if (!device) {
		std::cout << "Failed to create IDirect3DDevice9 object." << "\n";
		return E_FAIL;
	}

	std::cout << "Successfully created IDirect3DDevice9 object." << "\n";

	// each objects ptr to their shared vtable is located at the start of the memory layout of the object
	vtable = reinterpret_cast<void**>(device);
	return S_OK;
}

/*
* TODO: elaborate here
*/
HRESULT WINAPI installHook() {
	HMODULE dx9Handle = GetModuleHandle(TEXT("d3d9.dll"));
	if (dx9Handle == NULL) {
		std::cout << "Getting module handle for d3d9.dll failed" << "\n";
	}

	originalEndScene = reinterpret_cast<void*>(GetProcAddress(GetModuleHandle(TEXT("d3d9.dll")), "EndScene"));

	// first let us find endScene (it should be 42 but we'll double check)
	for (int i = 0; i < 128; i++) {
		if (vtable[i] == originalEndScene) {
			endSceneIndex = i;
			std::cout << "Found EndScene at index: " << endSceneIndex << "\n";

			// call virtualprotect to change read-only perms on vtable
			DWORD oldProtect;
			if (VirtualProtect(&vtable[i], sizeof(void*), PAGE_READWRITE, &oldProtect)) {
				vtable[i] = &hkEndScene;
				VirtualProtect(&vtable[i], sizeof(void*), oldProtect, &oldProtect);
				return S_OK;
			}
			else {
				std::cout << "Failed to change permissions on vtable." << "\n";
				return E_FAIL;
			}
		}
	}
	setupImGui();
	std::cout << "EndScene not found" << "\n";
	return E_FAIL;
}

/*
 * hkEndScene (hooked version of EndScene)
 * --------------------
 * 1. Begin a new frame
 * 2. Render ImGui
 * 3. Return the original EndScene function
 *  returns: S_OK/E_FAIL (HRESULT)
*/
HRESULT WINAPI hkEndScene(DX9_DEVICE *ptrDevice) {
	ImGui_ImplDX9_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	ImGui::Begin("hook");
	ImGui::Text("Now hooked using imgui.");
	ImGui::End();
	ImGui::Render();
	ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());

	// at this point in time, realize that calling ptrDevice->EndScene() will cause an infinite loop
	// because we've replaced the original pointer with hkEndScene. so we need to call the original function
    return reinterpret_cast<EndScene>(originalEndScene)(ptrDevice);
}

void setupImGui() {
	ImGui::CreateContext();
	ImGui_ImplWin32_Init(GetForegroundWindow());
	ImGui_ImplDX9_Init(device);
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpReserved) {
	LONG beginStatus, updateStatus, attachStatus, commitStatus = 0;

	AllocConsole();
	freopen("CONOUT$", "w", stdout);

	switch (fdwReason) {
	case DLL_PROCESS_ATTACH || DLL_THREAD_ATTACH: {
		// get pointer to IDirect3DDevice9
		// do this by creating a dummy device
		if (findVMT() != S_OK) {
			std::cout << "Failed to find VMT." << "\n";
			return FALSE;
		} // findVMT() will internally set the global variable device and the global variable vtablePtr
		installHook();
		break;
	}
	case DLL_THREAD_DETACH:
		// Cleanup code here
		break;
	case DLL_PROCESS_DETACH:
		// Cleanup code here
		break;
	}
	return TRUE; 
}