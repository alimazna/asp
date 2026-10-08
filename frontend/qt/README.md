# ASTRA Desktop — Qt 6 Widgets Frontend

## Build (Windows)

### Prerequisites
- Qt 6.5+ (Widgets, Network) — MSVC 2019+ or MinGW
- CMake 3.16+

### Configure
```bat
cd frontend\qt
cmake -S . -B build -DCMAKE_PREFIX_PATH="C:\Qt\6.5.0\msvc2019_64"
```

### Build
```bat
cmake --build build --config Release
```

### Deploy (single .exe with Qt libs)
```bat
windeployqt build\bin\astra_desktop.exe --release --no-compiler-runtime
```

### Run
Double-click `build\bin\astra_desktop.exe`. No CMD required.

## Dev with mock API
```bash
python3 scripts/mock_api.py &
# Then build and run the frontend
```

## Target hardware
Intel HD 3000, i5 2nd gen, Windows 10. QPainter only.
