# ASTRA Desktop — Qt 6 Widgets Frontend

## Automated Windows Build via GitHub Actions

Push code to `main` (when `frontend/qt/**` changes) and GitHub builds the
Windows `.exe` automatically. No local Qt installation needed.

### How to get the .exe

1. Push to `main` — the workflow triggers automatically
   (or go to **Actions** → **Build Windows .exe** → **Run workflow** for manual)
2. Wait for the build to complete (typically 3-5 minutes)
3. Open the **Actions** tab in GitHub
4. Click the latest **Build Windows .exe** run
5. Scroll to the **Artifacts** section at the bottom
6. Download **ASTRA-windows.zip** (retained 30 days)

### What's in the artifact

```
ASTRA-windows.zip
└── astra_desktop.exe          # main executable
    + Qt libraries (windeployqt)
    + platform plugins
    + style plugins
    + translations
```

The `.exe` is self-contained — double-click to run on Windows 10/11.
No Qt installer, no CMD, no DLLs to copy manually.

### Manual build (if you want to build locally)

Prerequisites: Qt 6.5+ (Widgets, Network), CMake 3.16+, MSVC 2019+.

```bat
cd frontend\qt
cmake -S . -B build -DCMAKE_PREFIX_PATH="C:\Qt\6.5.0\msvc2019_64"
cmake --build build --config Release
windeployqt build\bin\Release\astra_desktop.exe --release --no-compiler-runtime
```

See `frontend/qt/build.bat` for the full script.

## Dev with mock API

```bash
python3 scripts/mock_api.py &
# Then build and run the frontend
```

## Target hardware

Intel HD 3000, i5 2nd gen, Windows 10. QPainter only.
No QML, no OpenGL, no QGraphicsView.
