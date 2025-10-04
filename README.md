<div align="center">

# 📝 Flow Notepad

**A simple, fast, and elegant text editor for Linux**

Built with GTK4 and libadwaita

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)
[![GTK 4](https://img.shields.io/badge/GTK-4-green.svg)](https://www.gtk.org/)

[Features](#features) • [Installation](#installation) • [Building](#building) • [Keyboard Shortcuts](#keyboard-shortcuts)

</div>

---

## ✨ Features

- **📂 File Explorer** - Always-visible sidebar for easy folder navigation and file management
- **📑 Multiple Tabs** - Open and edit multiple files simultaneously with tab interface
- **🎨 Menu System** - Intuitive File/View/Search menus instead of toolbar buttons
- **🎨 Light & Dark Themes** - Toggle between themes with Ctrl+T, automatically applies to all tabs
- **🔍 Syntax Highlighting** - Automatic language detection with GtkSourceView for 200+ languages
- **🔍 Find and Replace** - Advanced search with forward/backward navigation and replace all
- **📏 Text Zoom** - Scale text size with Ctrl+Scroll or keyboard shortcuts (Ctrl+Plus/Minus)
- **📊 Real-time Statistics** - Track lines, words, and character count for active file
- **📍 Cursor Tracking** - Always know your current line and column position
- **💾 Multi-file Support** - Open entire folders and switch between files instantly
- **⌨️ Keyboard Shortcuts** - Full keyboard support for all operations

## 🚀 Installation

### From Source

#### Requirements

- GTK4 >= 4.0
- libadwaita >= 1.4
- GtkSourceView >= 5.0
- Meson >= 0.59
- GCC or Clang
- Python 3

On Debian/Ubuntu:
```bash
sudo apt install libgtk-4-dev libadwaita-1-dev libgtksourceview-5-dev meson
```

#### Building

```bash
git clone https://github.com/aasm3535/Flow.git
cd Flow
meson setup build
ninja -C build
```

#### Running

```bash
./build/src/flow
```

#### Installing

```bash
sudo ninja -C build install
```

## ⌨️ Keyboard Shortcuts

| Shortcut | Action |
|----------|--------|
| `Ctrl+N` | Create new file in new tab |
| `Ctrl+O` | Open file |
| `Ctrl+Shift+O` | Open folder in File Explorer |
| `Ctrl+S` | Save current file |
| `Ctrl+Shift+S` | Save As |
| `Ctrl+W` | Close current tab |
| `Ctrl+F` | Find text |
| `Ctrl+H` | Find and replace |
| `Ctrl+T` | Toggle light/dark theme |
| `Ctrl++` | Zoom in (increase text size) |
| `Ctrl+-` | Zoom out (decrease text size) |
| `Ctrl+0` | Reset zoom to default |
| `Ctrl+Scroll` | Zoom in/out with mouse wheel |

## 🖼️ Screenshots

<!-- Add screenshots here when available -->
*Coming soon*

## 🛠️ Technology Stack

- **Language**: C (C90 compliant)
- **UI Framework**: GTK 4
- **Design**: libadwaita
- **Build System**: Meson
- **File Operations**: GIO (async)

## 📋 Roadmap

- [x] Find and replace
- [ ] Syntax highlighting
- [ ] Multiple tabs support
- [ ] Dark mode toggle
- [ ] Custom themes
- [ ] Auto-save functionality
- [ ] Recent files menu
- [ ] Line numbers
- [ ] Case-sensitive search option

## 🤝 Contributing

Contributions are welcome! Feel free to open issues or submit pull requests.

## 📄 License

This project is licensed under the GNU General Public License v3.0 - see the [COPYING](COPYING) file for details.

## 🙏 Acknowledgments

- Built with [GTK](https://www.gtk.org/)
- Designed with [libadwaita](https://gnome.pages.gitlab.gnome.org/libadwaita/)

---

<div align="center">
Made with ❤️ for the Linux community
</div>
