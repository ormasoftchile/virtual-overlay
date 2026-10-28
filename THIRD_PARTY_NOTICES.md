# Third-Party Notices

## TalkPrompter (Speech-to-Script Alignment & SmoothDamp Algorithms)

Portions of `src/presenter/ScriptAlignmentEngine.h` and `src/presenter/ScriptAlignmentEngine.cpp` are adapted into native C++ from [TalkPrompter](https://github.com/Sven-Bo/talkprompter):

```
MIT License

Copyright (c) 2026 Sven Bosau (Bosau Digital LLC)

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
```

## Vosk Speech Recognition API

`lib/vosk/vosk_api.h` and the Vosk runtime (`libvosk.dll`) are licensed under the Apache License, Version 2.0:

```
Copyright 2020-2021 Alpha Cephei Inc.
Licensed under the Apache License, Version 2.0
http://www.apache.org/licenses/LICENSE-2.0
```

The four unmodified Windows x64 runtime DLLs are bundled from
`vosk-win64-0.3.45.zip` in the official
[Vosk 0.3.45 release](https://github.com/alphacep/vosk-api/releases/tag/v0.3.45).
Archive SHA-256:
`f1dcc9cca460630f81ea8f71794f69c80bed6556d2a4e6237b5785e1d2dff34b`.
The full Apache license is included in `LICENSE.vosk.txt`. Our copy of
`vosk_api.h` is a reduced declaration-only adaptation; its original copyright
and license header are retained and our modifications are identified there.

Vosk's Windows runtime also contains the following components, identified from
the release's [Windows build recipe](https://github.com/alphacep/vosk-api/blob/v0.3.45/travis/Dockerfile.win)
and [link recipe](https://github.com/alphacep/vosk-api/blob/v0.3.45/src/Makefile).
Their licenses are not replaced by Vosk's Apache license or this project's MIT
license. All the named license files and this notice ship beside the EXE in
build outputs and in the MSI installation.

| Component | Copyright / attribution | License and distributed notice |
|-----------|-------------------------|--------------------------------|
| Kaldi | Kaldi contributors; the complete upstream contributor and source-material notices are retained | Apache-2.0; `LICENSE.kaldi.txt` |
| OpenFST | Copyright 2005-2020 Google LLC | Apache-2.0; `LICENSE.openfst.txt` and `LICENSE.vosk.txt` |
| OpenBLAS 0.3.20 | Copyright (c) 2011-2014, The OpenBLAS Project | BSD-3-Clause; `LICENSE.openblas.txt` |
| GotoBLAS portions of OpenBLAS | Copyright 2009, 2010 The University of Texas at Austin | BSD-2-Clause; `LICENSE.gotoblas.txt` |
| CLAPACK 3.2.1 / BLAS | Copyright (c) 1992-2008 The University of Tennessee | BSD-3-Clause; `LICENSE.clapack.txt` |
| libf2c | Copyright 1990-1997 AT&T, Lucent Technologies and Bellcore | Permission notice and disclaimer in `LICENSE.f2c.txt` |
| MinGW-w64 runtime portions | MinGW-w64 and the additional copyright holders listed in its runtime notice | The complete upstream multi-license notice is retained in `LICENSE.mingw-runtime.txt` |

### SimpleJSON inside Vosk

Vosk's `src/json.h` is adapted from [nbsdx/SimpleJSON](https://github.com/nbsdx/SimpleJSON).
Its upstream license notice is retained here verbatim:

```
// From https://github.com/nbsdx/SimpleJSON
// SimpleJSON
// Simple C++ JSON library
//
// License
// Do what the fuck you want public license
```

This is the [WTFPL](https://www.wtfpl.net/), not the MIT-licensed
nlohmann/json library used separately by Virtual Overlay.

### GNU Runtime Libraries

The Vosk Windows distribution also includes `libgcc_s_seh-1.dll` and
`libstdc++-6.dll`, copyright Free Software Foundation, Inc. These GCC runtime
libraries are licensed under GPL version 3 or later with the GCC Runtime Library
Exception, version 3.1 (`GPL-3.0-or-later WITH GCC-exception-3.1`).
The license and exception are included in `LICENSE.gcc.txt` and
`LICENSE.gcc-runtime.txt`. Both DLLs match, byte-for-byte, Debian's
`gcc-mingw-w64-x86-64-posix-runtime` version `10.2.1-6+24.2` (amd64).
`SOURCES.vosk.txt` identifies the exact binary hashes, corresponding GCC source,
Debian patches, and cross-build scripts, with direct download URLs.

The exception permits eligible independent applications to retain their own
licenses; it does not relicense these runtime DLLs as MIT or Apache.
Redistributors must preserve these notices and maintain access to the
corresponding GNU runtime source. Publish `SOURCES.vosk.txt` alongside every
installer or portable download, not only inside the installer. See the
[FSF's explanation](https://www.gnu.org/licenses/gcc-exception-3.1-faq.html)
of independent libstdc++ redistribution and GPLv3 section 6.

The included `libwinpthread-1.dll` is from the
[mingw-w64 winpthreads project](https://www.mingw-w64.org/). Its MIT and BSD
license notices are included in `LICENSE.winpthreads.txt`.

### Speech Models

No speech model is bundled in the EXE or MSI. The optional
`scripts/Get-VoskModel.ps1` downloader installs a model separately and preserves
the contents of its archive. Its default, `vosk-model-small-en-us-0.15`, is listed
as Apache-2.0 by the [official Vosk model catalogue](https://alphacephei.com/vosk/models).
Other models can have different licenses; check the selected model's catalogue
entry and retain its included notices before use or redistribution. Do not
assume that Vosk API's license applies to every speech model.

## nlohmann/json

`lib/json.hpp` is licensed under the MIT License (`Copyright (c) 2013-2022 Niels Lohmann`).
