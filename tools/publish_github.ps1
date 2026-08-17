# Run after: gh auth login
# Usage: powershell -ExecutionPolicy Bypass -File tools/publish_github.ps1 [-Version 1.1.1]

param(
    [string]$Version = "1.1.1"
)

$ErrorActionPreference = "Stop"
$env:Path = "C:\Program Files\Git\cmd;C:\Program Files\GitHub CLI;" + $env:Path
$tag = "v$Version"

Set-Location $PSScriptRoot\..

gh auth status | Out-Null
if ($LASTEXITCODE -ne 0) {
    Write-Error "Not logged in. Run: gh auth login"
}

$username = gh api user -q .login
Write-Host "GitHub user: $username"

# Update README download link
$readme = Get-Content README.md -Raw
$readme = $readme -replace 'https://github.com/PLACEHOLDER/CurvioEQ', "https://github.com/$username/CurvioEQ"
Set-Content README.md $readme -NoNewline

git add README.md
git -c user.name="Hussein" -c user.email="$username@users.noreply.github.com" commit -m "Update README download link for GitHub" 2>$null

# Create CurvioEQ repo if missing
$repoExists = $false
try {
    gh repo view "$username/CurvioEQ" 2>$null | Out-Null
    if ($LASTEXITCODE -eq 0) { $repoExists = $true }
} catch {
    $repoExists = $false
}
if (-not $repoExists) {
    gh repo create CurvioEQ --public --source=. --remote=origin --description "Per-app equalizer for Windows (Qt/C++, WASAPI loopback)"
} else {
    git remote get-url origin 2>$null
    if ($LASTEXITCODE -ne 0) {
        git remote add origin "https://github.com/$username/CurvioEQ.git"
    }
}

git push -u origin main

# Profile README repo
$profileDir = "$env:TEMP\github-profile-$username"
if (Test-Path $profileDir) { Remove-Item $profileDir -Recurse -Force }
New-Item -ItemType Directory -Path $profileDir | Out-Null

@"
# Hi, I'm Hussein

Software engineer building **Windows desktop apps** with C++, Qt, and audio/DSP.

## Focus

- Per-app audio tools (WASAPI loopback, routing, EQ)
- Qt 6 desktop UI
- Real-time signal processing

## Featured project

### [CurvioEQ](https://github.com/$username/CurvioEQ)

Per-app equalizer for Windows — capture any running app's audio, apply a 10-band EQ, and replay on your headphones while routing the original stream to a virtual device.

## Links

- GitHub: [@$username](https://github.com/$username)

---

![GitHub stats](https://github-readme-stats.vercel.app/api?username=$username&show_icons=true&theme=dark&hide_border=true)
"@ | Set-Content "$profileDir\README.md"

Push-Location $profileDir
git init -b main
git add README.md
git -c user.name="Hussein" -c user.email="$username@users.noreply.github.com" commit -m "Add profile README"

$profileExists = gh repo view "$username/$username" 2>$null
if ($LASTEXITCODE -ne 0) {
    gh repo create $username --public --description "GitHub profile"
    git remote add origin "https://github.com/$username/$username.git"
} else {
    git remote add origin "https://github.com/$username/$username.git"
    git pull origin main --rebase 2>$null
}

git push -u origin main
Pop-Location

# Release
if (-not (Test-Path "dist\CurvioEQ-Setup.exe")) {
    Write-Error "Installer not found. Run build_installer.bat first."
}

$releaseNotesFile = "CHANGELOG.md"
if (-not (Test-Path $releaseNotesFile)) {
    Write-Error "Release notes not found: $releaseNotesFile"
}

gh release view $tag 2>$null | Out-Null
if ($LASTEXITCODE -ne 0) {
    gh release create $tag dist/CurvioEQ-Setup.exe `
        --title "CurvioEQ $tag" `
        --notes-file $releaseNotesFile
} else {
    Write-Host "Release $tag already exists — uploading installer (clobber)..."
    gh release upload $tag dist/CurvioEQ-Setup.exe --clobber
}

Write-Host ""
Write-Host "Done!"
Write-Host "  Repo:     https://github.com/$username/CurvioEQ"
Write-Host "  Release:  https://github.com/$username/CurvioEQ/releases/tag/$tag"
Write-Host "  Profile:  https://github.com/$username"
Write-Host ""
Write-Host "Pin repos: https://github.com/$username?tab=repositories -> Customize your pins -> pin CurvioEQ first"
