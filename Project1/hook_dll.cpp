#include <windows.h>
#include <d3d9.h>
#include <d3dx9.h>
#include <detours.h>
#include <imgui.h>
#include <imgui_impl_dx9.h>
#include <imgui_impl_win32.h>

// link required DX9 library (todo: find out exactly why? i hate development on windows)
#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "d3dx9.lib")

// function pointer for original EndScene function
typedef HRESULT(__stdcall* EndScene_t)(IDirect3DDevice9*);
// original EndScene function
EndScene_t oEndScene = nullptr;

// hook function for EndScene
HRESULT __stdcall hkEndScene(IDirect3DDevice9* device) {

	// start new frame 
	ImGui_ImplDX9_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	// render 
	ImGui::Begin("Hello, world!");
	ImGui::Text("This is some useful text.");
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