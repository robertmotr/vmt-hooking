#include <windows.h>
#include <d3d9.h>
#include <d3dx9.h>
#include <detours.h>
#include <imgui.h>
#include <imgui_impl_dx9.h>
#include <imgui_impl_win32.h>

#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "d3dx9.lib")
#pragma comment(lib, "detours.lib")

// typedefs for d3d fns	
typedef IDirect3D9* (WINAPI* Direct3DCreate9_t)(UINT);
typedef HRESULT(WINAPI* EndScene_t)(IDirect3DDevice9*);

// original fn ptrs
EndScene_t oEndScene = nullptr;
Direct3DCreate9_t oD3DCreate9 = Direct3DCreate9;

// hook function for Direct3DCreate9
IDirect3D9* WINAPI hkD3DCreate9(UINT SDKVersion) {
	IDirect3D9* pD3D = oD3DCreate9(SDKVersion);
	return pD3D;
}
// hook function for EndScene
HRESULT WINAPI hkEndScene(IDirect3DDevice9* device) {

	// start new frame 
	ImGui_ImplDX9_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	// render 
	ImGui::Begin("hook");
	ImGui::Text("Now hooked using imgui.");
	ImGui::End();
	ImGui::Render();
	ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());

	return oEndScene(device);
}

// install hook by changing oEndScene to point to our hook function
void hkInstall(IDirect3DDevice9* device) {
	// get pointer to vtable
	// pointer chain:
	// IDirect3DDevice9 -> IDirect3D9 -> IDirect3D9Vtbl
	// IDirect3D9Vtbl[42] is the EndScene function
	void** pVMT = *reinterpret_cast<void***>(device);

	DetourTransactionBegin();
	DetourUpdateThread(GetCurrentThread());
	DetourAttach(&(PVOID&)pVMT[42], hkEndScene);
	DetourTransactionCommit();
}

void setupImGui(IDirect3DDevice9* pDevice) {
	ImGui::CreateContext();
	ImGui_ImplWin32_Init(GetForegroundWindow());
	ImGui_ImplDX9_Init(pDevice);
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpReserved) {
	switch (fdwReason) {
	case DLL_PROCESS_ATTACH:
		MessageBox(NULL, "injection successful", "test", MB_OK);
		DetourTransactionBegin();
		DetourUpdateThread(GetCurrentThread());
		DetourAttach(&(PVOID&)oD3DCreate9, hkD3DCreate9);
		DetourTransactionCommit();
	case DLL_THREAD_ATTACH:
		DetourTransactionBegin();
		DetourUpdateThread(GetCurrentThread());
		DetourAttach(&(PVOID&)oD3DCreate9, hkD3DCreate9);
		DetourTransactionCommit();
	case DLL_THREAD_DETACH:
		// Cleanup code here
		break;
	case DLL_PROCESS_DETACH:
		// Cleanup code here
		break;
	}
	return TRUE;
}