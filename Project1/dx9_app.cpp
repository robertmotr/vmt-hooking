#include <windows.h>
#include <d3d9.h>
#include <d3dx9.h>

#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "d3dx9.lib")

LPDIRECT3D9 d3d = nullptr;
LPDIRECT3DDEVICE9 d3dDevice = nullptr;
LPD3DXFONT font = nullptr;

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

bool InitD3D(HWND hwnd) {
    d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!d3d) return false;

    D3DPRESENT_PARAMETERS d3dpp = {};
    d3dpp.Windowed = TRUE;
    d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    d3dpp.BackBufferFormat = D3DFMT_UNKNOWN;

    if (FAILED(d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd,
        D3DCREATE_SOFTWARE_VERTEXPROCESSING,
        &d3dpp, &d3dDevice))) {
        return false;
    }

	D3DXCreateFont(d3dDevice, 30, 0, FW_BOLD, 1, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial", &font);    

    return true;
}

void Render() {
    if (d3dDevice) {
        d3dDevice->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0);
        d3dDevice->BeginScene();

        RECT rect;
        SetRect(&rect, 0, 100, 0, 0); // Positioning the text
        font->DrawText(NULL, L"Original DX9 app, nothing has been injected and hooked yet.", -1, &rect, DT_NOCLIP, D3DCOLOR_XRGB(255, 255, 255));

        d3dDevice->EndScene();
        d3dDevice->Present(nullptr, nullptr, nullptr, nullptr);
    }
}

// entry
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    const wchar_t CLASS_NAME[] = L"DX9WindowClass"; // Changed to wide character string

    WNDCLASS wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;

    RegisterClass(&wc);

    HWND hwnd = CreateWindowExW(0, CLASS_NAME, L"DirectX 9 Example", // Changed to CreateWindowExW for wide character string
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        800, 600, nullptr, nullptr, hInstance, nullptr);

    ShowWindow(hwnd, nCmdShow);

    if (!InitD3D(hwnd)) {
        return -1;
    }

    // message loop
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
        Render();
    }

    if (font) font->Release();
    if (d3dDevice) d3dDevice->Release();
    if (d3d) d3d->Release();

    return 0;
}

int main() {
    WinMain(GetModuleHandle(NULL), NULL, GetCommandLineA(), SW_SHOWDEFAULT);
    return 0;
}