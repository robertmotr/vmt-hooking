#include <windows.h>
#include <d3d9.h>
#include <d3dx9.h>
#include <imgui.h>
#include <imgui_impl_dx9.h>
#include <imgui_impl_win32.h>
#include <cstdio>
#include <string>
#include <iostream>
#include <DxErr.h>
#include <logger.h>

// link dx9 libs and detours
#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "d3dx9.lib")
#pragma comment(lib, "logging.lib")

#define ENDSCENE_INDEX 42
#define VMT_SIZE 119

// typedefs for DX9 stuff to clean things up
// 2nd typedef is for the EndScene function so we can eventually use a PVOID given to us (need to cast it)
typedef IDirect3DDevice9				DX9_DEVICE;
typedef HRESULT							(WINAPI* EndScene)(DX9_DEVICE* ptrDevice);

DX9_DEVICE*								device = nullptr;				// ptr to IDirect3DDevice9 
void*									originalEndScene = nullptr;	    // fn ptr to original DX9::EndScene function
void*									vtable[VMT_SIZE];  // ptr to IDirect3DDevice9 vtable

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
This vptr is usually the first member of the object's memory layout.

VMT Hooking in practice:
Find the vtable: Locate the vtable for the object, which is typically at the beginning of the object’s memory layout.
Overwrite the method's function pointer: Replace the function pointer of the target method (e.g., EndScene in Direct3D) in the vtable with a pointer to your custom function (the hook).
Intercept calls: When the method is called, your hook function is executed instead of the original method.
Optionally, call the original method: Inside the hook, you can choose to call the original method by invoking the saved original function pointer.

 * findVMT
 * --------------------
 * 1. Get the foreground window
 * 2. Create a dummy Direct3D device
 * 3. Ptr to IDirect3DDevice9 is stored in global variable device
 * 3. Return S_OK if successful, E_FAIL if failed
 *
 *  returns: S_OK/E_FAIL (HRESULT)
 */

 /*
  * hkEndScene (hooked version of EndScene)
  * --------------------
  * 1. Begin a new frame
  * 2. Render ImGui
  * 3. Return the original EndScene function
  *  returns: S_OK/E_FAIL (HRESULT)
 */
HRESULT WINAPI hkEndScene(DX9_DEVICE* ptrDevice) {
	LOG("Hooked EndScene called.");
	static bool initialized = false;

	if (!initialized) {
		initialized = true;

		ImGui::CreateContext();

		// retrieve window handle from device
		HRESULT hr;
		D3DDEVICE_CREATION_PARAMETERS cparams;
		hr = ptrDevice->GetCreationParameters(&cparams);
		if (FAILED(hr)) {
			LOG("Failed to get device creation parameters.");
			LOG("Got: ", HResultToString(hr));
			return reinterpret_cast<EndScene>(originalEndScene)(ptrDevice);
		}
		// init backend with correct window handle
		if (!ImGui_ImplWin32_Init(cparams.hFocusWindow)) {
			LOG("ImGui_ImplWin32_Init failed.");
			return reinterpret_cast<EndScene>(originalEndScene)(ptrDevice);
		}
		if (!ImGui_ImplDX9_Init(ptrDevice)) {
			LOG("ImGui_ImplDX9_Init failed.");
			return reinterpret_cast<EndScene>(originalEndScene)(ptrDevice);
		}
	}

	ImGui_ImplDX9_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	ImGui::Begin("hook");
	ImGui::Text("Now hooked using ImGui.");
	ImGui::End();

	ImGui::EndFrame();
	ImGui::Render();
	ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());

	// call original EndScene fn
	return reinterpret_cast<EndScene>(originalEndScene)(ptrDevice);
}

HRESULT WINAPI findVMT() {
	HWND hwnd = FindWindow(NULL, L"DirectX 9 Example");
	if (!hwnd) {
		LOG("Failed to get foreground window.");
		return E_FAIL;
	}

	IDirect3D9* pD3D = Direct3DCreate9(D3D_SDK_VERSION);
	if (!pD3D) {
		LOG("Failed to create IDirect3D9 object.");
		return E_FAIL;
	}

	D3DDISPLAYMODE displayMode;
	HRESULT result = pD3D->GetAdapterDisplayMode(D3DADAPTER_DEFAULT, &displayMode);
	if (FAILED(result)) {
		LOG("Failed to get adapter display mode. HRESULT: ", HResultToString(result));
		pD3D->Release();
		return E_FAIL;
	}

	D3DPRESENT_PARAMETERS d3dpp = {};
	d3dpp.Windowed = TRUE;
	d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
	d3dpp.hDeviceWindow = hwnd;
	d3dpp.BackBufferFormat = displayMode.Format; 
	d3dpp.BackBufferCount = 1;
	d3dpp.BackBufferWidth = 0;
	d3dpp.BackBufferHeight = 0;
	d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
	d3dpp.MultiSampleType = D3DMULTISAMPLE_NONE;
	d3dpp.MultiSampleQuality = 0;

	result = pD3D->CheckDeviceType(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL,
		displayMode.Format, displayMode.Format, TRUE);
	if (FAILED(result)) {
		LOG("Failed to check device type in windowed mode. HRESULT: ", HResultToString(result));
		pD3D->Release();
		return E_FAIL;
	}

	result = pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd,
		D3DCREATE_HARDWARE_VERTEXPROCESSING, &d3dpp, &device);

	if (FAILED(result)) {
		LOG("Failed to create IDirect3DDevice9 object with hardware vertex processing. Trying software vertex processing. HRESULT: ", HResultToString(result));
		result = pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd,
			D3DCREATE_SOFTWARE_VERTEXPROCESSING, &d3dpp, &device);

		if (FAILED(result)) {
			LOG("Failed to create IDirect3DDevice9 object with software vertex processing. HRESULT: ", HResultToString(result));
			pD3D->Release();
			return E_FAIL;
		}
	}

	LOG("Successfully created IDirect3DDevice9 object and found VMT.");

	// obtain the vtable
	memcpy(vtable, *(void***)device, sizeof(vtable));

	pD3D->Release();
	device->Release();
	return S_OK;
}

/*
* 1. find dx9 HMODULE handle through GetModuleHandle, looking for the fn handle through the dx9 DLL
* 2. find the original EndScene address by calling GetProcAddress on EndScene
* 3. find the index of the original EndScene in the vtable by comparing every fn pointer to the original EndScene
* 4. change the permissions on the vtable to allow writing using VirtualProtect
* 5. replace the original EndScene pointer with the hooked EndScene pointer
* returns: S_OK/E_FAIL (HRESULT)
*/
HRESULT WINAPI installHook() {
	HMODULE dx9Handle = GetModuleHandle(TEXT("d3d9.dll"));
	if (dx9Handle == NULL) {
		LOG("Getting module handle for d3d9.dll failed");
	}

	originalEndScene = vtable[ENDSCENE_INDEX]; // 42 should be the index of EndScene in the vtable

	// call virtualprotect to change read-only perms on vtable
	DWORD oldProtect;
	LOG("Before hook: VMT[42] = ", vtable[ENDSCENE_INDEX]);
	if (VirtualProtect(&vtable[ENDSCENE_INDEX], sizeof(void*), PAGE_READWRITE, &oldProtect)) {
		vtable[ENDSCENE_INDEX] = &hkEndScene;
		VirtualProtect(&vtable[ENDSCENE_INDEX], sizeof(void*), oldProtect, &oldProtect);
		LOG("After hook: VMT[42] = ", vtable[ENDSCENE_INDEX]);
	}
	else {
		LOG("VirtualProtect failed to change vtable permissions.");
		return E_FAIL;
	}

	LOG("Successfully installed hook.");
	return S_OK;
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpReserved) {
	switch (fdwReason) {
	case DLL_PROCESS_ATTACH:
		DisableThreadLibraryCalls(hinstDLL);
		AllocConsole();
		freopen_s(reinterpret_cast<FILE**>(stdout), "CONOUT$", "w", stdout);
		LOG("test?");

		if (findVMT() != S_OK) {
			LOG("Failed to find VMT.");
			return 1;
		}
		installHook();
		break;
	}
	return TRUE;
}