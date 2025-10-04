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

- **🎨 Clean Interface** - Minimalist design with borderless text area and modern UI
- **⌨️ Keyboard Shortcuts** - Full keyboard support for quick file operations
- **🔍 Text Zoom** - Scale text size with Ctrl+Scroll or keyboard shortcuts
- **📊 Real-time Statistics** - Track lines, words, and character count as you type
- **📍 Cursor Tracking** - Always know your current line and column position
- **💾 Quick Save** - Fast file operations with native GTK dialogs
- **🎯 Monospace Font** - Perfect for coding and plain text editing

## 🚀 Installation

### From Source

#### Requirements

- GTK4 >= 4.0
- libadwaita >= 1.4
- Meson >= 0.59
- GCC or Clang
- Python 3

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
| `Ctrl+N` | Create new document |
| `Ctrl+O` | Open file |
| `Ctrl+S` | Save file |
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

- [ ] Syntax highlighting
- [ ] Find and replace
- [ ] Multiple tabs support
- [ ] Dark mode toggle
- [ ] Custom themes
- [ ] Auto-save functionality
- [ ] Recent files menu

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
