# DeskTick
### Multi-Face Clock Widget for the Windows Desktop

A clock for your Windows desktop - sixteen faces and no clutter. Analog, digital, word clock,
terminal, banner. Frameless and transparent, it sits on your wallpaper, stays put through Show
Desktop, and remembers its place. Around 1 MB, under 10 MB of RAM, twenty languages, no telemetry.

---

## 🎬 Preview

<!-- Drop a screen recording or screenshot here -->
<img width="1832" height="2104" alt="minclock-faces" src="https://github.com/user-attachments/assets/1f3a94cb-e5ae-411d-b344-797fcc88d4e3" />



## ✨ Features

- **Sixteen Clock Faces** - Analog, digital, word clock, terminal, banner and more; pick one from the right-click menu or double-click the clock to cycle
- **Per-Face Customization** - Colours, numerals, ticks, the date line, 12- or 24-hour time. Each face offers only the options it actually has
- **Frameless & Transparent** - No window, no background. It sits directly on your wallpaper
- **Drag Anywhere** - Grab it and move it; position and size are remembered
- **Scale From Widget to Wall** - *Resize* from the right-click menu puts a grip in the corner; drag it to any size
- **Show Seconds** - Second hand on or off, your choice
- **Light & Dark** - The Customize and About dialogs follow your Windows theme
- **Survives Win+D** - Stays where you put it, even after Show Desktop
- **Always on Top** *(optional)* - Or let it live quietly behind your windows
- **Start with Windows** *(optional)*
- **System Tray Icon** *(optional)* - Same menu, from the notification area
- **High-DPI Sharp** - Rescales cleanly across monitors and DPI boundaries
- **20 Languages** - Matched automatically to your Windows display language
- **Native & Tiny** - Software Direct2D. No frameworks, no background services, no runtime to install
- **No Telemetry** - Nothing collected, nothing sent, no account, no internet needed

---

## 📥 Download

<a href="https://apps.microsoft.com/detail/9NQGFVNBX4WJ?referrer=appbadge&mode=full&cid=from_github" target="_blank" rel="noopener noreferrer">
  <img src="https://get.microsoft.com/images/en-us%20light.svg" width="200"/>
</a>

- Purchasing from the Microsoft Store helps support ongoing development ❤️
- You can also support via GitHub Sponsors: [![Sponsor](https://img.shields.io/badge/Sponsor-%E2%9D%A4-fe8e86?logo=github)](https://github.com/sponsors/riyasy)

### 📊 Store vs GitHub Version

|  | Microsoft Store | GitHub Release |
|--|--|--|
| **Price** | 🪙 Paid | 🆓 Free |
| **Updates** | ✅ Automatic | ❌ Manual |
| **Security** | ✅ Signed & verified | ❌ Not signed |

---

## 🚀 Installation

- Option 1 : [**Install from Microsoft Store**](https://apps.microsoft.com/detail/9NQGFVNBX4WJ?launch=true&cid=from_github&mode=full)
- Option 2 : Download the portable exe from the Github [**Releases Page**](https://github.com/riyasy/DeskTick/releases)
- Option 3 : Build it yourself (see below)

---

## 🕰️ The Faces

| | |
|--|--|
| **Simple** | Clean analog dial, the default |
| **Classic dark** / **Classic light** | Traditional dial with numerals, two themes |
| **Minimal** | Ticks only, no numerals |
| **Icon** | The app's own mark as a dial |
| **Bold** | Heavy markers, high contrast |
| **Flat** | Solid disc, flat colour |
| **Glass** | Frosted-glass dial |
| **Dots** | Dot markers instead of ticks |
| **Digital** / **Digital bold** | Panel readout with optional date and AM/PM |
| **Word clock** | Spells the time out in English on a letter grid |
| **Stack** | Hour over minute |
| **Terminal** | Monospaced console readout with a blinking cursor |
| **Banner** | Wide condensed type |
| **Split** | Hour and minute split apart |

Pick one from the right-click menu, or double-click the clock to cycle through them.

---

## ⚙️ Settings

The right-click menu is the whole interface:

- **Face** - The sixteen faces, in a submenu
- **Customize...** - The options the current face actually has: colours, numerals, tick marks, the date line, 12- or 24-hour time, drop shadows, frosted glass. Changes apply instantly, and one button resets a face to its original look
- **Show seconds** - Second hand on or off
- **Resize** - Turns on the grip at the bottom-right corner; drag it to scale
- **Always on top**
- **Start with Windows**
- **Show in system tray**
- **About DeskTick...**
- **Exit**

Everything is saved to `%LOCALAPPDATA%\DeskTick.ini`.

---

## 📌 Requirements

- Windows 10 or Windows 11 - x64, ARM64 or x86
- No .NET, no Visual C++ redistributable, no runtime of any kind

---

## 🧰 Building

MSBuild only - no package manager, no test suite.

```powershell
$mb = "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe"
& $mb src\DeskTick\DeskTick.vcxproj -p:Configuration=Release -p:Platform=x64
```

Output lands in `src\DeskTick\x64\Release\DeskTick.exe`. The `lang\` folder ships beside the exe - without it the UI is English.

---

## 🌍 Translations

Twenty languages: Arabic, Chinese (Simplified & Traditional), Dutch, English, Finnish, French, German, Hungarian, Italian, Japanese, Korean, Malayalam, Polish, Portuguese (Brazil & Portugal), Russian, Spanish, Swedish, Ukrainian.

Each is one `lang\<locale>.ini` file keyed by the English string, so adding a language is a file copy and a translation pass - no code, no rebuild. Pull requests welcome.

---

## 🔒 Privacy

Nothing is collected, nothing is sent. There is no account, no telemetry, and no internet connection
required.

---

## 🛠️ Roadmap

- Image faces - drop a PNG in `assets\` and it becomes a dial
- More faces and more languages
- A localizable word clock

---

## 🤝 Contributing

Contributions are welcome! Feel free to fork and submit pull requests.

---

## 💡 Tip

Park it in a corner with **Always on top** off - it stays visible on the desktop, out of the way of
your windows, and Win+D never loses it.

---

(c) 2026 RYF Tools. All rights reserved.
