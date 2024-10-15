# VMT Hooking: a minimal example
Simple example of VMT hooking to display a simple ImGui render on a minimal DX9 application.

1. Ensure that DirectX 9 is installed, and that the $(DXSDK_DIR) path var works.
2. Open the .sln file in Visual Studio and build the project as usual.
3. Using the injector of your choice (tested on Xenos injector), inject the built DLL using the `New` option in Xenos, selecting dx9_app.exe as the file.

### Note:
This currently doesn't work if you try injecting the DLL mid-executing into a DX9 process. Not sure why exactly, but eventually I might fix it.
