# Gemini Project: Flow

## Project Overview

This is a GTK application written in C. It uses the Meson build system and Adwaita for the UI. The application is a simple window with a header bar and a main content area. The UI is defined in `src/flow-window.ui` and loaded as a GResource.

The application ID is `ink.coda.Flow`.

## Building and Running

To build the project, you need to have `meson` and `ninja` installed, as well as the GTK4 development libraries.

**1. Set up the build directory:**

```bash
meson setup build
```

**2. Compile the project:**

```bash
meson compile -C build
```

**3. Run the application:**

```bash
./build/src/flow
```

## Development Conventions

*   The code follows the GObject coding style.
*   The UI is defined in `.ui` files and loaded as GResources.
*   The project uses Meson for building.
*   Compiler warnings are treated as errors.
