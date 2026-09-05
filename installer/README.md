# CurvioEQ installer build

## 1. Build and deploy the app

From the project root:

```bat
build_app.bat
```

Output: `dist/bin/CurvioEQ.exe` plus Qt DLLs in `dist/bin`, `dist/plugins`, and `dist/translations`.

## 2. Compile the installer

Requires [Inno Setup 6](https://jrsoftware.org/isinfo.php).

```bat
build_installer.bat
```

Or manually:

```bat
"C:\Users\Hussa\AppData\Local\Programs\Inno Setup 6\ISCC.exe" installer\CurvioEQ.iss
```

Output: `dist/CurvioEQ-Setup.exe`

## Portable zip

```bat
build_portable.bat
```

Output: `dist/CurvioEQ-1.3.1-portable.zip`. Includes `portable.txt` so settings stay in the unzipped folder.

## 3. After install

Launch CurvioEQ from the Start Menu. Install a virtual audio device (for example VB-Cable or Voicemeeter), then choose routing sink and EQ output under **Settings**.
