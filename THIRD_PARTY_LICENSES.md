# Third-party licenses

| Component | Pinned version | License | Use |
|---|---:|---|---|
| X-Plane Plugin SDK | 4.3.0 | Laminar Research SDK license (included in the SDK archive) | XPLM headers and platform stub libraries |
| Dear ImGui | 1.91.8 | MIT | DCDU widgets and OpenGL 2 renderer backend |
| nlohmann/json | 3.11.3 | MIT | Settings, VATSIM and SimBrief JSON parsing |
| Noto Sans CJK SC | Sans 2.004 | SIL Open Font License 1.1 | Bundled Simplified Chinese UI font |

The build downloads each dependency from its upstream project using an immutable version and verifies the SHA-256 recorded in `CMakeLists.txt`. Upstream notices and license files remain in the CMake dependency source directories during a source build.

Dear ImGui copyright © 2014–2025 Omar Cornut and contributors.  
nlohmann/json copyright © 2013–2025 Niels Lohmann.  
Noto fonts copyright © Google LLC and their respective contributors.

EasyCPDLC is not bundled. It is a GPLv3 reference implementation credited for workflow and protocol research: <https://github.com/quassbutreally/EasyCPDLC>.

## Dear ImGui notice

The GL2 draw-list adapter in `src/ui.cpp` also follows Dear ImGui's GL2 renderer implementation.

The MIT License (MIT)

Copyright (c) 2014-2025 Omar Cornut

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
