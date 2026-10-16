# Cerberus Sentinel Watch: third-party notices

This notice accompanies source and binary distributions containing the Watch signature catalog. Retain it in the firmware download package. Original sources and matching limitations are documented in WATCH_SIGNATURES.md. No third-party project endorses Cerberus Sentinel.

## Buttplug protocol configuration catalog

Source: https://github.com/buttplugio/buttplug/tree/179b0d9306f519912c10bdddd111686622b7f63a

Revision: `179b0d9306f519912c10bdddd111686622b7f63a`. License: BSD 3-Clause.

```text
Copyright (c) 2016-2023, Nonpolynomial Labs, LLC
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice, this
  list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

* Neither the name of buttplug nor the names of its
  contributors may be used to endorse or promote products derived from
  this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

## Flock original signature research

Source: https://github.com/colonelpanichacks/flock-you/tree/6704f011f3e6c9cfe94fe18c593f863032c9c954

Revision: `6704f011f3e6c9cfe94fe18c593f863032c9c954`. License: MIT.

```text
MIT License

Copyright (c) 2026 colonelpanichacks

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

## Raven service catalog

Source: https://github.com/NSM-Barii/flock-back/tree/011e024e0fc0c06567f44ababfa7c957ac8b6b4e

Revision: `011e024e0fc0c06567f44ababfa7c957ac8b6b4e`. License: MIT.

```text
MIT License

Copyright (c) 2025 Jabari Lucien

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

## OpenHaystack Zephyr frame reference

Source: https://github.com/koenvervloesem/openhaystack-zephyr/tree/86c4ea5219602eac8a7a800eccf4164ba865aa6e

Revision: `86c4ea5219602eac8a7a800eccf4164ba865aa6e`. License: MIT.

```text
MIT License

Copyright (c) 2021 Koen Vervloesem

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
```

## Additional protocol and registry references

Google Find Hub protocol facts were checked against https://developers.google.com/nearby/fast-pair/specifications/extensions/fmdn (retrieved 2026-10-13). Google Developers documentation is generally CC BY 4.0, except where otherwise noted: https://developers.google.com/terms/site-policies and https://creativecommons.org/licenses/by/4.0/. Cerberus Sentinel contributors wrote the receive-only matching logic and condensed protocol description. No Google sample code is included.

GoPro protocol facts were checked against https://gopro.github.io/OpenGoPro/docs/ble/protocol/ble_setup/ and revision 0f963572611c4410a15678531e9681a6ff874edb of GoPro/OpenGoPro. No GoPro SDK or its third-party dependencies are included in this classifier.

Individual vendor identifier assignments were checked in the Bluetooth SIG assigned-number registries. No SIG document, logo, or full registry compilation is distributed. Trademarks belong to their respective owners.

Argus was used to discover original sources. No Argus database, code, or documentation is bundled. No SquachWatch/SquatchWatch code, catalog, or assets are bundled.


## Expanded Watch protocol research (2026-10-14)

The new Flipper, Meshtastic, Samsung, Eddystone, AltBeacon, PwnGrid and OpenDroneID parsers are original Cerberus code. Their fixtures are synthetic. Only public interoperable identifiers, packet layouts and bounds were used; no implementation, illustrations, field captures, vendor logos or character art from the following sources are included in the firmware/package. Detailed pinned source links and matching limits are in `WATCH_SIGNATURES.md`.

| Research source | Reviewed revision / publication | Source licensing information |
| --- | --- | --- |
| Flipper Devices firmware | `3fb1d51727733d2007abc5ec636a89328857803e` | GPL-3.0; code not imported |
| Meshtastic Python client | `0a1835760c2437314a2d3544bbdd8b6fead559bc` | GPL-3.0; code not imported |
| Yu, Henderson, Tiu and Haines, Samsung protocol research | USENIX Security 2024, Figure 3 / Table 1 | Authors retain their publication rights; paper/figures not bundled |
| Google Eddystone | `bb8738d7ddac0ddd3dfa70e594d011a0475e763d` | Apache-2.0 source repository; code/specification text not imported |
| AltBeacon specification | `d5c7f6da1192313d28222f9da2e0251074153252` | No separate LICENSE file at reviewed revision; packet facts only |
| evilsocket PwnGrid | `52d5685f2cea1ada86e9c28b16dc912013c68b71` | GPL-3.0; code/assets not imported |
| OpenDroneID core C | `849c03adbe0cdd68997a462fafab397edcfe53f3` | Apache-2.0; library not imported |
| OpenDroneID Linux transmitter | `6852afd0d4bed0d139b9dac3cd3a45eb595e30f0` | Apache-2.0; transmitter code not imported |

These research references do not relicense the cited projects or imply their endorsement. Existing notices above continue to apply to the earlier permissively licensed catalog selections. No SquachWatch code or assets are included.
